#pragma once

#include "binaryninjaapi.h"
#include "bifrostdiffstore.h"
#include "bifrostmatch.h"

#include <functional>
#include <string>
#include <vector>

// Top-level diff orchestration: extract features from both BinaryViews and run
// the matcher. UI-independent (no Qt) and read-only on the BinaryViews, so it is
// safe to call from a worker thread.

namespace bifrost {

class DiffEngine
{
public:
    using ProgressFn = Matcher::ProgressFn;

    DiffResult Compute(BinaryNinja::Ref<BinaryNinja::BinaryView> left,
                       BinaryNinja::Ref<BinaryNinja::BinaryView> right,
                       const ProgressFn& progress = {});
};

// Run a diff on a Binary Ninja worker thread behind a cancelable BackgroundTask
// (progress shows in the status bar). `onDone` is invoked on the MAIN thread
// with the result; if the task was cancelled it receives an empty DiffResult
// (functions empty). Returns immediately. Callers must guard their own widget
// lifetime (e.g. QPointer) since `onDone` runs later.
void ComputeAsync(BinaryNinja::Ref<BinaryNinja::BinaryView> left,
                  BinaryNinja::Ref<BinaryNinja::BinaryView> right,
                  std::function<void(DiffResult)> onDone);

// Human-readable name for a BinaryView: the project file name if present,
// otherwise the basename of the backing file. Shared by the diff UI.
std::string bvDisplayName(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);

// Legacy status string ("matched" | "added" | "removed") for a match, used both
// for v1-compatible persistence and for the diff tree's status column.
const char* LegacyStatusString(MatchStatus s);

// Convert an engine DiffResult into the persistence entries stored in the BN
// project metadata (schema v2). Shared by the dialog and the sidebar.
std::vector<DiffFuncEntry> toStoreEntries(const DiffResult& diff);

} // namespace bifrost
