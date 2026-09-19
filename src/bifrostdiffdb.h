#pragma once

#include "binaryninjaapi.h"

#include <string>

// A saved diff can live in the project two ways:
//   1. project metadata under "bifrost.diff.<name>" (see bifrostdiffstore.h) —
//      the original store, reachable only from the Manage Diffs dialog;
//   2. a standalone .bndb project file (this module) — shows up in the project
//      browser next to the binaries and opens with a double-click.
//
// The .bndb route leans on machinery Binary Ninja already has: BinaryView
// metadata is serialized into the database, so a Ref<Metadata> round-trips
// natively (no JSON parser needed), and BN already knows how to open .bndb, so
// no custom BinaryViewType is required to recognize the file.
//
// Note the diff database holds NO binary image — its raw contents are just the
// diff's JSON (a few KB). The two binaries stay as their own project entries and
// are referenced by the "left"/"right" names inside the diff.

// Metadata key under which the diff tree is stored inside a diff database.
// BifrostDiffViewType keys off the presence of this key.
static constexpr const char* kDiffDbKey = "bifrost.diff";

// Write `diffData` into the project as "<diffName>.bndb". Returns false and
// fills `errorOut` with a human-readable reason on failure.
bool bifrostSaveDiffToProject(const std::string& diffName,
                              BinaryNinja::Ref<BinaryNinja::Metadata> diffData,
                              BinaryNinja::Ref<BinaryNinja::Project> project,
                              std::string& errorOut);

// Read a diff back out of an opened database. Returns nullptr when `bv` is not
// a Bifrost diff database — which is exactly the test the view type uses.
BinaryNinja::Ref<BinaryNinja::Metadata> bifrostDiffFromBinaryView(
    BinaryNinja::Ref<BinaryNinja::BinaryView> bv);
