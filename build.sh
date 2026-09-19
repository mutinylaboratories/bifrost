#!/usr/bin/env bash
# build.sh  —  Build (and optionally install) the Bifrost plugin (Linux / macOS)
#
# Usage:  ./build.sh [clean] [install] [--copy]
#                    [--channel stable|dev] [--bn-api <ref>] [--no-match-api]
#
#   (no args)   Configure + build the plugin -> build/out/bin/libbifrost.<so|dylib>
#   clean       Delete the build directory before building
#   install     Symlink (or --copy) the built plugin into Binary Ninja's user
#               plugins folder
#   --copy      With 'install', copy the plugin instead of symlinking it
#
# Binary Ninja API matching (the api checkout MUST match your installed BN, or
# the link fails with undefined _BN* symbols):
#   (default)          Detect the installed BN build and check out the matching
#                      stable/<version> tag in the binaryninja-api checkout
#   --channel dev      Check out the dev branch head instead
#   --bn-api <ref>     Check out an explicit tag/branch/commit
#   --no-match-api     Leave the binaryninja-api checkout exactly as it is
#
# Examples:
#   ./build.sh                    — build against the api tag matching installed BN
#   ./build.sh clean install      — clean rebuild + symlink into BN
#   ./build.sh install --copy     — build + copy the plugin into BN
#   ./build.sh --no-match-api      — build against the api checkout as-is
#
# Environment variables (override the auto-detected defaults):
#   BN_INSTALL         Binary Ninja install dir
#                      (macOS: /Applications/Binary Ninja.app/Contents/MacOS)
#   BN_API_PATH        Path to a binaryninja-api checkout (default: ../binaryninja-api)
#   CMAKE_PREFIX_PATH  Qt 6 prefix (default: /usr/local/Qt-6.10.1 or newest /usr/local/Qt-6*)
#   BN_PLUGINS_DIR     BN user plugins dir (default: platform-specific — see below)

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# Colour helpers (disabled when not writing to a terminal)
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BOLD=''; RESET=''
fi
info()    { echo -e "${BOLD}$*${RESET}"; }
success() { echo -e "${GREEN}$*${RESET}"; }
warn()    { echo -e "${YELLOW}$*${RESET}"; }
fail()    { echo -e "${RED}$*${RESET}"; }
die()     { fail "ERROR: $*"; exit 1; }

# ---------------------------------------------------------------------------
# Platform-specific defaults
# ---------------------------------------------------------------------------
BUILD_DIR="$SCRIPT_DIR/build"
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

if [[ "$(uname)" == "Darwin" ]]; then
    BN_INSTALL="${BN_INSTALL:-/Applications/Binary Ninja.app/Contents/MacOS}"
    BN_PLUGINS_DIR="${BN_PLUGINS_DIR:-$HOME/Library/Application Support/Binary Ninja/plugins}"
    PLUGIN_LIB="libbifrost.dylib"
else
    BN_INSTALL="${BN_INSTALL:-/opt/binaryninja}"
    BN_PLUGINS_DIR="${BN_PLUGINS_DIR:-$HOME/.binaryninja/plugins}"
    PLUGIN_LIB="libbifrost.so"
fi
PLUGIN_PATH="$BUILD_DIR/out/bin/$PLUGIN_LIB"

# binaryninja-api checkout: honour $BN_API_PATH, else default to a sibling dir.
if [[ -z "${BN_API_PATH:-}" ]]; then
    if [[ -f "$SCRIPT_DIR/../binaryninja-api/binaryninjaapi.h" ]]; then
        BN_API_PATH="$(cd "$SCRIPT_DIR/../binaryninja-api" && pwd)"
    fi
fi

# Qt 6 prefix: honour $CMAKE_PREFIX_PATH, else pick a /usr/local/Qt-6* install.
if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    if [[ -d /usr/local/Qt-6.10.1 ]]; then
        CMAKE_PREFIX_PATH=/usr/local/Qt-6.10.1
    else
        CMAKE_PREFIX_PATH="$(ls -d /usr/local/Qt-6* 2>/dev/null | sort -V | tail -1 || true)"
    fi
fi

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
DO_CLEAN=0
DO_INSTALL=0
INSTALL_COPY=0
MATCH_API=1
BN_CHANNEL=""
BN_API_REF=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        clean)          DO_CLEAN=1 ;;
        install)        DO_INSTALL=1 ;;
        --copy)         INSTALL_COPY=1 ;;
        --no-match-api) MATCH_API=0 ;;
        --channel)      BN_CHANNEL="${2:-}"; shift ;;
        --channel=*)    BN_CHANNEL="${1#*=}" ;;
        --bn-api)       BN_API_REF="${2:-}"; shift ;;
        --bn-api=*)     BN_API_REF="${1#*=}" ;;
        -h|--help)      sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)              warn "Ignoring unknown argument '$1'" ;;
    esac
    shift
done

[[ -n "$BN_CHANNEL" && -n "$BN_API_REF" ]] && die "pass either --channel or --bn-api, not both."
[[ -n "$BN_API_PATH" ]] || die "binaryninja-api not found. Set BN_API_PATH, or clone it next to this repo:
       git clone https://github.com/Vector35/binaryninja-api ../binaryninja-api"
[[ -n "$CMAKE_PREFIX_PATH" ]] || die "Qt 6 not found. Set CMAKE_PREFIX_PATH to your Qt 6 prefix (e.g. /usr/local/Qt-6.10.1)."

# ---------------------------------------------------------------------------
# Resolve installed Binary Ninja version (macOS reads it from Info.plist)
# ---------------------------------------------------------------------------
detect_bn_version() {
    if [[ "$(uname)" == "Darwin" ]]; then
        local plist="$BN_INSTALL/../Info.plist"
        [[ -f "$plist" ]] || return 1
        local v
        v=$(/usr/libexec/PlistBuddy -c "Print CFBundleVersion" "$plist" 2>/dev/null) || return 1
        # "5.3.9757_commercial" -> "5.3.9757"
        echo "$v" | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+'
    else
        # Linux: best-effort — look for a version marker in the install dir.
        grep -hoE '[0-9]+\.[0-9]+\.[0-9]+' \
            "$BN_INSTALL/CHANGELOG" "$BN_INSTALL/changelog" 2>/dev/null | head -1
    fi
}

# ---------------------------------------------------------------------------
# Match the binaryninja-api checkout to the installed BN build
# ---------------------------------------------------------------------------
if [[ $MATCH_API -eq 1 ]]; then
    echo
    info "====== Matching binaryninja-api to Binary Ninja ======"
    if ! git -C "$BN_API_PATH" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        warn "BN_API_PATH ($BN_API_PATH) is not a git checkout — skipping version match."
    elif [[ -n "$(git -C "$BN_API_PATH" status --porcelain 2>/dev/null)" ]]; then
        warn "binaryninja-api has local changes — skipping version match to avoid clobbering them."
        warn "Re-run with --no-match-api to silence this, or stash/commit changes in $BN_API_PATH."
    else
        # Decide the target ref.
        REF=""
        if [[ -n "$BN_API_REF" ]]; then
            REF="$BN_API_REF"
        elif [[ "$(echo "${BN_CHANNEL:-}" | tr 'A-Z' 'a-z')" == "dev" ]]; then
            REF="dev"
        else
            VER="$(detect_bn_version || true)"
            if [[ -n "$VER" ]]; then
                REF="stable/$VER"
            else
                warn "Could not detect the installed BN version; leaving the api checkout as-is."
                warn "Pass --bn-api <ref> to pin it explicitly."
            fi
        fi
        if [[ -n "$REF" ]]; then
            info "Fetching tags and checking out '$REF' in $BN_API_PATH ..."
            git -C "$BN_API_PATH" fetch --tags --quiet origin || warn "git fetch failed (offline?); trying local refs."
            if git -C "$BN_API_PATH" checkout --quiet "$REF"; then
                git -C "$BN_API_PATH" submodule update --init --quiet vendor/fmt 2>/dev/null || true
                success "binaryninja-api is at $REF ($(git -C "$BN_API_PATH" rev-parse --short HEAD))."
            else
                die "could not check out '$REF' in $BN_API_PATH.
       For stable BN, the tag is stable/<version> (e.g. stable/5.3.9757).
       Run './build.sh --no-match-api' to build against the current checkout."
            fi
        fi
    fi
fi

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
if [[ $DO_CLEAN -eq 1 ]]; then
    echo
    info "Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

# ---------------------------------------------------------------------------
# Configure + build
# ---------------------------------------------------------------------------
echo
info "====== Configuring ======"
echo "  BN_API_PATH      = $BN_API_PATH"
echo "  CMAKE_PREFIX_PATH= $CMAKE_PREFIX_PATH"
echo "  BN_INSTALL       = $BN_INSTALL"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" \
    -DBN_API_PATH="$BN_API_PATH" \
    || die "CMake configure failed."

echo
info "====== Building plugin ======"
cmake --build "$BUILD_DIR" --target bifrost -j "$NPROC" || die "Plugin build failed."
[[ -f "$PLUGIN_PATH" ]] || die "expected plugin at $PLUGIN_PATH but it is missing."
success "Plugin: $PLUGIN_PATH"

# ---------------------------------------------------------------------------
# Install into Binary Ninja's user plugins folder
# ---------------------------------------------------------------------------
if [[ $DO_INSTALL -eq 1 ]]; then
    echo
    info "====== Installing ======"
    mkdir -p "$BN_PLUGINS_DIR"
    DEST="$BN_PLUGINS_DIR/$PLUGIN_LIB"
    rm -f "$DEST"
    if [[ $INSTALL_COPY -eq 1 ]]; then
        cp "$PLUGIN_PATH" "$DEST"
        success "Copied -> $DEST"
    else
        ln -sf "$PLUGIN_PATH" "$DEST"
        success "Symlinked -> $DEST"
        echo "  (rebuilds are picked up automatically; restart Binary Ninja to reload)"
    fi
fi

echo
success "====== Done ======"
