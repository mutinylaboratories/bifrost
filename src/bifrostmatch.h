#pragma once

#include "bifrostfeatures.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Function and basic-block matching.
//
// The Matcher takes the feature vectors of two binaries and produces a set of
// function matches using an ordered sequence of passes, from most reliable to
// least. Each pass matches only functions still unmatched on both sides, and
// only where its key is unique on both sides (guaranteeing a 1:1 pairing). For
// every matched pair the basic-block matcher then computes a structural
// similarity and refines the match into Identical vs Changed.

namespace bifrost {

enum class MatchStatus
{
    Identical,  // byte-for-byte identical
    Changed,    // matched, but the body differs
    Added,      // only in the right binary
    Removed     // only in the left binary
};

const char* MatchStatusString(MatchStatus s);

struct BbMatch
{
    uint64_t    leftAddr = 0;
    uint64_t    rightAddr = 0;
    MatchStatus status = MatchStatus::Changed;
};

struct FuncMatch
{
    std::string leftName, rightName;
    uint64_t    leftAddr = 0, rightAddr = 0;
    MatchStatus status = MatchStatus::Changed;
    double      similarity = 0.0;   // 0..1 structural similarity
    double      confidence = 0.0;   // 0..1, from the pass that made the match
    std::string algorithm;          // e.g. "mdindex+spp"

    int         bbIdentical = 0, bbChanged = 0, bbAdded = 0, bbRemoved = 0;
    std::vector<BbMatch> blocks;
};

struct DiffResult
{
    std::string leftBinary, rightBinary;
    int identical = 0, changed = 0, added = 0, removed = 0;
    std::vector<FuncMatch> functions;
};

class Matcher
{
public:
    // phase text, current, total; return false to request cancellation.
    using ProgressFn = std::function<bool(const std::string&, size_t, size_t)>;

    DiffResult Run(std::vector<FuncFeatures> left,
                   std::vector<FuncFeatures> right,
                   const ProgressFn& progress = {});

    // Basic-block matcher for a single function pair. Fills out.blocks, the
    // bb* counts, out.similarity and refines out.status (Identical/Changed).
    static void MatchBlocks(const FuncFeatures& l, const FuncFeatures& r,
                            FuncMatch& out);
};

} // namespace bifrost
