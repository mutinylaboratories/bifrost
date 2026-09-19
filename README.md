# Bifrost

> *In Norse mythology, Bifrost is the rainbow bridge connecting Midgard (the world of humans) to Asgard (the realm of the gods). Here, it bridges two binary views side by side.*

Bifrost is a [Binary Ninja](https://binary.ninja) UI plugin for **structural binary diffing** — a BinDiff/Diaphora-style differ built natively into Binary Ninja. It matches functions between two versions of a binary using control-flow structure (not just symbol names), scores how similar each match is, and lets you walk the differences side by side with live block- and instruction-level highlighting.

## Motivation

Binary diffing tools like [BinDiff](https://github.com/google/bindiff) and [Diaphora](https://github.com/joxeankoret/diaphora) are invaluable for patch analysis, malware triage, and vulnerability research — but they operate largely outside the disassembler's UI. Bifrost brings that comparison experience natively into Binary Ninja, using multiple versions of a binary held inside a single Binary Ninja **project**.

## Features

- **Structural function matching** that works on stripped and renamed binaries: exact and normalized byte hashes, the BinDiff **MD-index** graph invariant, small-prime-product of mnemonics, `(nodes, edges, calls)`, referenced constants/strings, HLIL (decompiler) pseudocode fingerprints, call-graph propagation, and a fuzzy last resort — applied most-reliable-first
- **Similarity and confidence scoring** per matched function, classifying each as *identical*, *changed*, *added*, or *removed*
- **Basic-block matching** and **instruction-level** highlighting of what actually changed, rendered in Binary Ninja's own Linear and Graph views
- **Side-by-side diff view** with a Linear/Graph toggle for a control-flow-graph comparison, driven from a results sidebar
- **Guided walkthrough**: filter to changed/added/removed, step through changes with the *Prev*/*Next* buttons (F8 / Shift+F8)
- Diffs are computed on a **background thread** and **saved into the Binary Ninja project** metadata, so they persist across sessions
- **Manage saved diffs** (Plugins ▸ Bifrost ▸ Manage Diffs…): open, delete, or **export** a diff to **JSON** (for tooling) or a self-contained **HTML** report
- Also includes the original side-by-side **split view** (`ViewType` "Bifrost", priority 25) for keeping two panes in sync

## Diffing workflow

1. Create a Binary Ninja **project** and add two (or more) versions of a binary to it.
2. Run **Plugins ▸ Bifrost ▸ Run Diff…**, pick the left and right binaries, and give the diff a name. (You can also run a diff from the Bifrost sidebar's *Diff* tab.)
3. The diff opens a side-by-side view; the sidebar lists every function with its status, similarity, confidence, and the matching heuristic used.
4. Click a row to jump both panes to that function with the changed blocks/instructions highlighted; use *Prev*/*Next* to walk only the changes, and the **Graph view** toggle to compare control-flow graphs.

## Architecture

The matching engine is UI-independent (no Qt) and lives in `src/`:

| File | Responsibility |
|------|----------------|
| `bifrostfeatures.{h,cpp}` | `FeatureExtractor` — per-function/per-block features (hashes, MD-index, SPP, CFG metrics, constants, strings, callees, HLIL pseudocode) |
| `bifrostmatch.{h,cpp}` | `Matcher` — ordered matching passes, call-graph propagation, fuzzy matching, and the basic-block matcher + similarity |
| `bifrostdiffengine.{h,cpp}` | `DiffEngine::Compute` / `ComputeAsync` orchestration and project-metadata serialization |
| `bifrostdiffstore.{h,cpp}` | diff persistence to Binary Ninja project metadata (schema v2, back-compatible) |
| `bifrostdiffview.*`, `bifrostsidebar.*`, `bifrostdiffdialog.*` | the diff view, results sidebar, and Run Diff dialog |

## Requirements

- [Binary Ninja](https://binary.ninja) with UI plugin support
- [Qt 6](https://www.qt.io/product/qt6) (tested with Qt 6.10.1)
- [binaryninja-api](https://github.com/Vector35/binaryninja-api)
- CMake 3.15+

## Building

You need a [`binaryninja-api`](https://github.com/Vector35/binaryninja-api) checkout (cloned next to this repo by default) and Qt 6.

```bash
git clone https://github.com/mutinylaboratories/bifrost
git clone https://github.com/Vector35/binaryninja-api   # sibling directory
cd bifrost

./build.sh install     # build and symlink the plugin into Binary Ninja
```

`build.sh` detects your installed Binary Ninja build, checks out the **matching** `binaryninja-api` tag (critical — a mismatch fails the link with undefined `_BN*` symbols), configures, builds, and installs. Common invocations:

```bash
./build.sh                 # build only  -> build/out/bin/libbifrost.dylib
./build.sh clean install   # clean rebuild + install
./build.sh install --copy  # copy instead of symlink
./build.sh --channel dev   # build against the api dev branch (for a dev BN)
./build.sh --no-match-api  # build against the api checkout as-is
./build.sh --help          # all options and env-var overrides
```

Override the auto-detected paths with the `BN_INSTALL`, `BN_API_PATH`, `CMAKE_PREFIX_PATH`, and `BN_PLUGINS_DIR` environment variables. After installing, restart Binary Ninja to load the plugin.

<details>
<summary>Manual CMake build (without the script)</summary>

```bash
# Match the api checkout to your installed BN first, e.g. for stable 5.3.9757:
git -C ../binaryninja-api fetch --tags && git -C ../binaryninja-api checkout stable/5.3.9757

cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/usr/local/Qt-6.10.1 \
  -DBN_API_PATH=../binaryninja-api \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target bifrost
# plugin -> build/out/bin/libbifrost.dylib
```
</details>

## Testing

`tests/` contains two small C fixtures (`target_v1.c`, `target_v2.c`) built as shared libraries, each function a deliberate diff case (identical, changed, renamed, block-reordered, added, removed). A headless harness runs the real engine over them and asserts the expected result:

```bash
./test.sh              # build the fixtures + harness, run the diff, check results
./test.sh --verbose    # also print the full per-function diff
./test.sh --no-build   # run against already-built binaries
```

The harness links the Binary Ninja API/core and the (Qt-free) engine sources, so it exercises feature extraction on real analysis plus the full matching pipeline without the UI. (It needs a Binary Ninja core that can initialise headlessly.)

## Branches

| Branch | Purpose |
|---|---|
| `dev` | Active development — the default branch. Work lands here. |
| `main` | Releases only. Updated by merging `dev` when a version is cut. |

## Related Projects

| Project | Description |
|---------|-------------|
| [google/bindiff](https://github.com/google/bindiff) | Industry-standard binary differ from Google; supports IDA Pro, Ghidra, and Binary Ninja |
| [joxeankoret/diaphora](https://github.com/joxeankoret/diaphora) | Open-source IDA Pro diffing plugin with function matching and scoring |
| [Vector35/binaryninja-api](https://github.com/Vector35/binaryninja-api) | Binary Ninja C++/Python plugin API |

## Icon

![Bifrost icon](images/icon.svg)

The icon is a custom rune drawn in Binary Ninja's teal (`#4ec9b0`), built from parts that each carry meaning:

| Element | Visual | Meaning |
|---------|--------|---------|
| Crown bar | Horizontal bar at the top | Unity — the realm above |
| Arc | Quadratic curve spanning the two pillars | The rainbow bridge itself — the core motif |
| Twin pillars | Vertical lines descending from the arc | The dual pane views (Linear + Disassembly) |
| Mid-bar | Horizontal connector between the pillars | The sync connection between the two views |
| Foot bars | Three horizontal groundings at the base | Runic triple-grounding, echoing a trident base |

Two faint accent dots sit at the inner base of the arc, marking where the bridge meets its towers.

## Credits

- Plugin and icon designed with [Claude](https://claude.ai) (Anthropic)

## License

See [LICENSE](LICENSE).
