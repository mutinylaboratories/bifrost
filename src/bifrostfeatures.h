#pragma once

#include "binaryninjaapi.h"

#include <cstdint>
#include <string>
#include <vector>

// Structural feature extraction for binary diffing.
//
// FeatureExtractor pulls a compact, architecture-agnostic feature vector out of
// each analysis function and its basic blocks. The features are the raw material
// for the matching passes in bifrostmatch: exact/normalized byte hashes, the
// BinDiff MD-index graph invariant, a small-prime product of mnemonics, and the
// call-graph / constant / string references used for propagation and fuzzy
// matching.
//
// Everything here is read-only on the BinaryView and free of Qt, so it is safe
// to run on a worker thread (see bifrostdiffengine).

namespace bifrost {

// Per basic block features.
struct BbFeatures
{
    uint64_t    addr = 0;
    size_t      inDegree = 0;
    size_t      outDegree = 0;
    size_t      instrCount = 0;
    uint64_t    byteHash = 0;     // exact bytes of the block
    uint64_t    spp = 0;          // small-prime product of the block's mnemonics
    std::string mnemonicSig;      // ordered normalized mnemonic skeleton
};

// Per function features.
struct FuncFeatures
{
    uint64_t    addr = 0;
    std::string name;

    size_t      nodes = 0;        // basic block count
    size_t      edges = 0;        // CFG edge count
    size_t      calls = 0;        // out-call site count
    size_t      instrCount = 0;
    size_t      cyclomatic = 0;   // E - N + 2

    uint64_t    byteHash = 0;     // exact instruction bytes, block order
    uint64_t    normHash = 0;     // immediates/addresses masked
    uint64_t    spp = 0;          // whole-function mnemonic product
    double      mdIndex = 0.0;    // BinDiff flow-graph invariant

    // HLIL (decompiler) pseudocode fingerprints. 0 if HLIL is unavailable.
    uint64_t    pseudoHash = 0;   // FNV over the ordered HLIL AST operation sequence
    uint64_t    pseudoPrimes = 0; // small-prime product of HLIL AST operations

    std::vector<int64_t>  constants;    // sorted, deduped
    std::vector<uint64_t> stringHashes; // sorted, deduped hashes of referenced strings
    std::vector<uint64_t> calleeAddrs;  // sorted, deduped raw callee addresses

    std::vector<BbFeatures> blocks;      // in ascending address order
};

class FeatureExtractor
{
public:
    explicit FeatureExtractor(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);

    // Extract every analysis function. Functions whose analysis is not yet
    // available are skipped.
    std::vector<FuncFeatures> ExtractAll();

    // Extract a single function. Returns a zero-initialized record with addr set
    // if the function has no usable analysis.
    FuncFeatures ExtractFunction(BinaryNinja::Ref<BinaryNinja::Function> f);

    // Individually testable primitives.
    static double   ComputeMdIndex(BinaryNinja::Ref<BinaryNinja::Function> f);
    BbFeatures      ExtractBlock(BinaryNinja::Ref<BinaryNinja::BasicBlock> bb);

private:
    BinaryNinja::Ref<BinaryNinja::BinaryView> m_bv;
};

// --- Hash / prime helpers (exposed for reuse by the block matcher) ---

// 64-bit FNV-1a over a byte range.
uint64_t Fnv1a(const void* data, size_t len, uint64_t seed = 0xcbf29ce484222325ULL);
uint64_t Fnv1aStr(const std::string& s, uint64_t seed = 0xcbf29ce484222325ULL);

// Map a mnemonic string to a small prime (stable within a process run).
uint64_t MnemonicPrime(const std::string& mnemonic);

// Fold a prime into a running small-prime product held modulo a large prime.
void SppAccumulate(uint64_t& product, uint64_t prime);

} // namespace bifrost
