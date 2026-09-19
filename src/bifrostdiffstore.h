#pragma once

#include "binaryninjaapi.h"

#include <string>
#include <vector>

// Keys used in the project metadata database.
// Each diff is stored at "bifrost.diff.<name>".
// The index listing all names is stored at "bifrost.diff._index".
static constexpr const char* kDiffIndexKey = "bifrost.diff._index";

inline std::string diffMetaKey(const std::string& name)
{
    return std::string("bifrost.diff.") + name;
}

// Serialize a diff into a Metadata key-value tree and store it in the
// project database. Also updates the index of diff names.
//
// The Metadata tree has the shape (schema v2; v1 lacked "schema" and every
// field below the first four):
//   {
//     "schema":    2,
//     "left":      <string>,
//     "right":     <string>,
//     "timestamp": <string>,
//     "functions": [ { "name": <string>, "status": <string>,
//                      "leftAddr":  <uint64>, "rightAddr": <uint64>,
//                      "similarity": <double>, "confidence": <double>,
//                      "algorithm": <string>,
//                      "bbIdentical": <uint64>, "bbChanged": <uint64>,
//                      "bbAdded": <uint64>, "bbRemoved": <uint64>,
//                      "blocks": [ { "l": <uint64>, "r": <uint64>,
//                                    "s": <string> }, ... ] }, ... ]
//   }
//
// Readers must guard every field beyond the original four so that v1 diffs
// still load.

// One matched/added/removed/identical basic block within a function pair.
struct DiffBbEntry
{
    uint64_t    leftAddr = 0;
    uint64_t    rightAddr = 0;
    std::string status;   // "identical" | "changed" | "added" | "removed"
};

struct DiffFuncEntry
{
    std::string name;
    std::string status;   // "matched" | "added" | "removed"  (v1 compatible)
    uint64_t    leftAddr = 0;
    uint64_t    rightAddr = 0;

    // Schema v2 additions.
    double      similarity = 1.0;
    double      confidence = 1.0;
    std::string algorithm;
    int         bbIdentical = 0;
    int         bbChanged = 0;
    int         bbAdded = 0;
    int         bbRemoved = 0;
    std::vector<DiffBbEntry> blocks;
};

// Build the Metadata tree (schema v2) for a diff, without storing it. Shared by
// bifrostSaveDiff and by tests/tools that need the on-disk shape.
BinaryNinja::Ref<BinaryNinja::Metadata> bifrostBuildDiffMetadata(
    const std::string& leftBinaryName,
    const std::string& rightBinaryName,
    const std::string& timestamp,
    const std::vector<DiffFuncEntry>& entries);

// Save diff to project metadata. Returns false if no project is open.
bool bifrostSaveDiff(const std::string& diffName,
                     const std::string& leftBinaryName,
                     const std::string& rightBinaryName,
                     const std::string& timestamp,
                     const std::vector<DiffFuncEntry>& entries,
                     BinaryNinja::Ref<BinaryNinja::Project> project);

// Load a diff from project metadata. Returns nullptr if not found.
BinaryNinja::Ref<BinaryNinja::Metadata> bifrostLoadDiff(
    const std::string& diffName,
    BinaryNinja::Ref<BinaryNinja::Project> project);

// Return the list of stored diff names from the index.
std::vector<std::string> bifrostListDiffs(
    BinaryNinja::Ref<BinaryNinja::Project> project);

// Delete a diff and remove it from the index.
void bifrostDeleteDiff(const std::string& diffName,
                       BinaryNinja::Ref<BinaryNinja::Project> project);
