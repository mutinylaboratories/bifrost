#include "bifrostmatch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace bifrost {

const char* MatchStatusString(MatchStatus s)
{
    switch (s)
    {
    case MatchStatus::Identical: return "identical";
    case MatchStatus::Changed:   return "changed";
    case MatchStatus::Added:     return "added";
    case MatchStatus::Removed:   return "removed";
    }
    return "changed";
}

// ── Key helpers ─────────────────────────────────────────────────────────────

static inline uint64_t hashCombine(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t quantize(double d)
{
    return static_cast<uint64_t>(std::llround(d * 1e6));
}

static uint64_t hashConstantsStrings(const FuncFeatures& f)
{
    uint64_t h = 0x243f6a8885a308d3ULL;
    for (int64_t c : f.constants)
        h = hashCombine(h, static_cast<uint64_t>(c));
    for (uint64_t s : f.stringHashes)
        h = hashCombine(h, s);
    return h;
}

// A matching pass: derives an optional key for a still-unmatched function.
// nullopt means the function is not eligible for this pass.
struct MatchPass
{
    std::string name;
    double      confidence;
    std::function<std::optional<uint64_t>(const FuncFeatures&)> key;
};

// ── Basic-block matching (§5) ───────────────────────────────────────────────

void Matcher::MatchBlocks(const FuncFeatures& l, const FuncFeatures& r, FuncMatch& out)
{
    out.blocks.clear();
    out.bbIdentical = out.bbChanged = out.bbAdded = out.bbRemoved = 0;

    const size_t nL = l.blocks.size();
    const size_t nR = r.blocks.size();
    std::vector<bool> lUsed(nL, false), rUsed(nR, false);

    auto addMatch = [&](size_t li, size_t ri, MatchStatus st) {
        lUsed[li] = rUsed[ri] = true;
        out.blocks.push_back({l.blocks[li].addr, r.blocks[ri].addr, st});
        if (st == MatchStatus::Identical) ++out.bbIdentical; else ++out.bbChanged;
    };

    // Pass A — exact byte hash → Identical.
    {
        std::unordered_map<uint64_t, std::vector<size_t>> rByHash;
        for (size_t ri = 0; ri < nR; ++ri)
            rByHash[r.blocks[ri].byteHash].push_back(ri);
        for (size_t li = 0; li < nL; ++li)
        {
            auto it = rByHash.find(l.blocks[li].byteHash);
            if (it == rByHash.end()) continue;
            for (size_t ri : it->second)
                if (!rUsed[ri]) { addMatch(li, ri, MatchStatus::Identical); break; }
        }
    }

    // Pass B — normalized mnemonic skeleton → Changed.
    {
        std::unordered_map<std::string, std::vector<size_t>> rBySig;
        for (size_t ri = 0; ri < nR; ++ri)
            if (!rUsed[ri]) rBySig[r.blocks[ri].mnemonicSig].push_back(ri);
        for (size_t li = 0; li < nL; ++li)
        {
            if (lUsed[li]) continue;
            auto it = rBySig.find(l.blocks[li].mnemonicSig);
            if (it == rBySig.end()) continue;
            for (size_t ri : it->second)
                if (!rUsed[ri]) { addMatch(li, ri, MatchStatus::Changed); break; }
        }
    }

    // Pass C — greedy nearest by (in,out,instrCount) for the leftovers.
    // Bounded to avoid quadratic blow-up on pathologically large functions.
    if (nL <= 2048 && nR <= 2048)
    {
        for (size_t li = 0; li < nL; ++li)
        {
            if (lUsed[li]) continue;
            const auto& lb = l.blocks[li];
            size_t best = SIZE_MAX;
            long   bestScore = LONG_MAX;
            for (size_t ri = 0; ri < nR; ++ri)
            {
                if (rUsed[ri]) continue;
                const auto& rb = r.blocks[ri];
                long degPenalty = std::llabs((long)lb.inDegree - (long)rb.inDegree)
                                + std::llabs((long)lb.outDegree - (long)rb.outDegree);
                long score = degPenalty * 1000
                           + std::llabs((long)lb.instrCount - (long)rb.instrCount);
                if (score < bestScore) { bestScore = score; best = ri; }
            }
            if (best != SIZE_MAX)
                addMatch(li, best, MatchStatus::Changed);
        }
    }

    for (size_t li = 0; li < nL; ++li)
        if (!lUsed[li]) { out.blocks.push_back({l.blocks[li].addr, 0, MatchStatus::Removed}); ++out.bbRemoved; }
    for (size_t ri = 0; ri < nR; ++ri)
        if (!rUsed[ri]) { out.blocks.push_back({0, r.blocks[ri].addr, MatchStatus::Added}); ++out.bbAdded; }

    // Similarity: block match ratio blended with edge-count agreement.
    double bbSim = 1.0;
    if (nL + nR > 0)
        bbSim = (2.0 * out.bbIdentical + 1.0 * out.bbChanged) / static_cast<double>(nL + nR);
    double maxEdges = std::max<double>({(double)l.edges, (double)r.edges, 1.0});
    double edgeSim = 1.0 - std::fabs((double)l.edges - (double)r.edges) / maxEdges;
    out.similarity = 0.8 * bbSim + 0.2 * edgeSim;

    if (l.byteHash == r.byteHash)
    {
        out.status = MatchStatus::Identical;
        out.similarity = 1.0;
    }
    else
    {
        out.status = MatchStatus::Changed;
        if (out.similarity > 0.999) out.similarity = 0.999; // reserve 1.0 for identical
    }
}

// ── Fuzzy similarity (last-resort weighted vector) ──────────────────────────

static double jaccardI64(const std::vector<int64_t>& a, const std::vector<int64_t>& b)
{
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    size_t i = 0, j = 0, inter = 0;
    while (i < a.size() && j < b.size())
    {
        if (a[i] == b[j]) { ++inter; ++i; ++j; }
        else if (a[i] < b[j]) ++i;
        else ++j;
    }
    size_t uni = a.size() + b.size() - inter;
    return uni ? static_cast<double>(inter) / static_cast<double>(uni) : 1.0;
}

static double fuzzySim(const FuncFeatures& l, const FuncFeatures& r)
{
    auto rel = [](double a, double b) {
        double m = std::max({a, b, 1.0});
        return 1.0 - std::fabs(a - b) / m;
    };
    // Generic size/shape features are weak discriminators (all small loops look
    // alike), so they carry low weight. The semantic features — mnemonic
    // product, constants, and the decompiler AST — carry the most weight.
    double s = 0.0, w = 0.0;
    s += 1.0 * rel(l.nodes, r.nodes);           w += 1.0;
    s += 1.0 * rel(l.edges, r.edges);           w += 1.0;
    s += 1.0 * rel(l.calls, r.calls);           w += 1.0;
    s += 1.0 * rel(l.instrCount, r.instrCount); w += 1.0;

    double mdDenom = std::max(l.mdIndex, r.mdIndex) + 1e-9;
    double md = 1.0 - std::min(1.0, std::fabs(l.mdIndex - r.mdIndex) / mdDenom);
    s += 1.0 * md; w += 1.0;

    s += (l.spp == r.spp ? 2.0 : 0.0); w += 2.0;
    s += 2.0 * jaccardI64(l.constants, r.constants); w += 2.0;

    if (l.pseudoPrimes && r.pseudoPrimes)
    {
        s += (l.pseudoPrimes == r.pseudoPrimes ? 2.0 : 0.0);
        w += 2.0;
    }
    return w > 0.0 ? s / w : 0.0;
}

// ── Function matching ───────────────────────────────────────────────────────

DiffResult Matcher::Run(std::vector<FuncFeatures> left,
                        std::vector<FuncFeatures> right,
                        const ProgressFn& progress)
{
    DiffResult result;
    const size_t nL = left.size(), nR = right.size();

    std::vector<bool> lMatched(nL, false);
    std::vector<bool> rMatched(nR, false);

    // A matched pair, recorded before the (deferred) per-pair block matching.
    struct PairRec { size_t li, rj; double conf; std::string algo; };
    std::vector<PairRec> pairs;

    // Match still-unmatched functions from the given candidate index sets by a
    // key that is unique on both sides. Reused by the global passes and by the
    // call-graph propagation (over local neighbor sets).
    using KeyFn = std::function<std::optional<uint64_t>(const FuncFeatures&)>;
    auto matchUniqueOver = [&](const std::vector<size_t>& lc, const std::vector<size_t>& rc,
                               const KeyFn& keyFn, double conf, const std::string& algo) -> int {
        std::unordered_map<uint64_t, std::vector<size_t>> lm, rm;
        for (size_t i : lc) if (!lMatched[i]) { if (auto k = keyFn(left[i]))  lm[*k].push_back(i); }
        for (size_t j : rc) if (!rMatched[j]) { if (auto k = keyFn(right[j])) rm[*k].push_back(j); }
        int made = 0;
        for (auto& [key, li] : lm)
        {
            if (li.size() != 1) continue;
            auto it = rm.find(key);
            if (it == rm.end() || it->second.size() != 1) continue;
            size_t a = li[0], b = it->second[0];
            if (lMatched[a] || rMatched[b]) continue;
            lMatched[a] = rMatched[b] = true;
            pairs.push_back({a, b, conf, algo});
            ++made;
        }
        return made;
    };

    std::vector<size_t> allL(nL), allR(nR);
    std::iota(allL.begin(), allL.end(), size_t{0});
    std::iota(allR.begin(), allR.end(), size_t{0});

    // Ordered passes (most reliable first).
    std::vector<MatchPass> passes = {
        {"bytehash", 1.00, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             return f.byteHash; }},
        {"normhash", 0.99, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             return f.normHash; }},
        {"name+shape", 0.98, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             if (f.name.empty()) return std::nullopt;
             uint64_t h = Fnv1aStr(f.name);
             h = hashCombine(h, f.nodes);
             h = hashCombine(h, f.edges);
             h = hashCombine(h, f.calls);
             return h; }},
        // Same symbol name is a strong signal for symbolized binaries, so it is
        // matched before the (weaker, collision-prone) structural passes. Falls
        // through harmlessly for stripped/renamed functions.
        {"name", 0.92, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             if (f.name.empty()) return std::nullopt;
             return Fnv1aStr(f.name); }},
        {"mdindex", 0.95, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             // MD-index alone collides badly for small functions (any two simple
             // loops share a shape), so require a non-trivial graph.
             if (f.nodes < 5) return std::nullopt;
             return quantize(f.mdIndex); }},
        {"mdindex+spp", 0.90, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             if (f.nodes < 2) return std::nullopt;
             return hashCombine(quantize(f.mdIndex), f.spp); }},
        {"pseudocode", 0.88, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             if (f.pseudoHash == 0) return std::nullopt; // HLIL unavailable
             return f.pseudoHash; }},
        {"pseudoprimes", 0.85, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             if (f.pseudoPrimes == 0 || f.pseudoPrimes == 1) return std::nullopt;
             return hashCombine(f.pseudoPrimes, f.nodes); }},
        {"nec", 0.82, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             // (nodes,edges,calls) is very collision-prone for small functions.
             if (f.nodes < 5) return std::nullopt;
             uint64_t h = hashCombine(f.nodes, f.edges);
             return hashCombine(h, f.calls); }},
        {"spp", 0.80, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             if (f.instrCount < 8) return std::nullopt;
             return f.spp; }},
        // Requires at least two referenced constants/strings — a single shared
        // constant is too weak and produces false positives.
        {"constants+strings", 0.65, [](const FuncFeatures& f) -> std::optional<uint64_t> {
             if (f.constants.size() + f.stringHashes.size() < 2) return std::nullopt;
             return hashConstantsStrings(f); }},
    };

    const size_t totalPhases = passes.size() + 2; // + propagation + fuzzy
    size_t passIdx = 0;
    for (auto& pass : passes)
    {
        if (progress && !progress("Matching: " + pass.name, passIdx, totalPhases))
            break;
        ++passIdx;
        matchUniqueOver(allL, allR, pass.key, pass.confidence, pass.name);
    }

    // ── Call-graph propagation ──────────────────────────────────────────────
    // Neighbors of matched anchors are matched with weaker keys that become
    // unique within the local neighbor set. Iterated to a fixpoint.
    if (!progress || progress("Call-graph propagation", passIdx, totalPhases))
    {
        std::unordered_map<uint64_t, size_t> lIdx, rIdx;
        for (size_t i = 0; i < nL; ++i) lIdx[left[i].addr]  = i;
        for (size_t j = 0; j < nR; ++j) rIdx[right[j].addr] = j;

        std::vector<std::vector<size_t>> lCallees(nL), rCallees(nR), lCallers(nL), rCallers(nR);
        for (size_t i = 0; i < nL; ++i)
            for (uint64_t a : left[i].calleeAddrs)
                if (auto it = lIdx.find(a); it != lIdx.end())
                { lCallees[i].push_back(it->second); lCallers[it->second].push_back(i); }
        for (size_t j = 0; j < nR; ++j)
            for (uint64_t a : right[j].calleeAddrs)
                if (auto it = rIdx.find(a); it != rIdx.end())
                { rCallees[j].push_back(it->second); rCallers[it->second].push_back(j); }

        std::vector<KeyFn> propKeys = {
            [](const FuncFeatures& f) -> std::optional<uint64_t> {
                if (f.name.empty()) return std::nullopt; return Fnv1aStr(f.name); },
            [](const FuncFeatures& f) -> std::optional<uint64_t> {
                if (f.pseudoHash == 0) return std::nullopt; return f.pseudoHash; },
            [](const FuncFeatures& f) -> std::optional<uint64_t> {
                if (f.nodes < 2) return std::nullopt; return quantize(f.mdIndex); },
            [](const FuncFeatures& f) -> std::optional<uint64_t> {
                if (f.instrCount < 2) return std::nullopt; return f.spp; },
            [](const FuncFeatures& f) -> std::optional<uint64_t> {
                uint64_t h = hashCombine(f.nodes, f.edges); return hashCombine(h, f.calls); },
        };

        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 16)
        {
            changed = false;
            size_t snap = pairs.size();
            for (size_t m = 0; m < snap; ++m)
            {
                PairRec pr = pairs[m];
                double conf = 0.70 * pr.conf;
                auto propSet = [&](const std::vector<size_t>& ls, const std::vector<size_t>& rs) {
                    std::vector<size_t> lc, rc;
                    for (size_t i : ls) if (!lMatched[i]) lc.push_back(i);
                    for (size_t j : rs) if (!rMatched[j]) rc.push_back(j);
                    if (lc.empty() || rc.empty()) return;
                    for (auto& kf : propKeys)
                        if (matchUniqueOver(lc, rc, kf, conf, "callgraph") > 0) changed = true;
                };
                propSet(lCallees[pr.li], rCallees[pr.rj]);
                propSet(lCallers[pr.li], rCallers[pr.rj]);
            }
        }
    }

    // ── Fuzzy matching (weighted-vector, last resort) ───────────────────────
    if (!progress || progress("Fuzzy matching", passIdx + 1, totalPhases))
    {
        std::vector<size_t> remL, remR;
        for (size_t i = 0; i < nL; ++i) if (!lMatched[i]) remL.push_back(i);
        for (size_t j = 0; j < nR; ++j) if (!rMatched[j]) remR.push_back(j);

        // Bounded to keep the all-pairs scan tractable.
        if (!remL.empty() && !remR.empty() && remL.size() <= 1500 && remR.size() <= 1500)
        {
            // Conservative threshold: fuzzy is a last resort, and a false pair
            // (matching a removed function to an unrelated added one) is worse
            // than leaving both unmatched.
            constexpr double kFuzzyThreshold = 0.75;
            struct Cand { double sim; size_t li, rj; };
            std::vector<Cand> cands;
            for (size_t i : remL)
                for (size_t j : remR)
                {
                    double s = fuzzySim(left[i], right[j]);
                    if (s >= kFuzzyThreshold) cands.push_back({s, i, j});
                }
            std::sort(cands.begin(), cands.end(),
                      [](const Cand& a, const Cand& b) { return a.sim > b.sim; });
            for (auto& c : cands)
            {
                if (lMatched[c.li] || rMatched[c.rj]) continue;
                lMatched[c.li] = rMatched[c.rj] = true;
                pairs.push_back({c.li, c.rj, 0.40 + 0.25 * c.sim, "fuzzy"});
            }
        }
    }

    // ── Assemble results ────────────────────────────────────────────────────
    result.functions.reserve(pairs.size() + nL + nR);
    for (auto& pr : pairs)
    {
        FuncMatch m;
        m.leftName   = left[pr.li].name;
        m.rightName  = right[pr.rj].name;
        m.leftAddr   = left[pr.li].addr;
        m.rightAddr  = right[pr.rj].addr;
        m.confidence = pr.conf;
        m.algorithm  = pr.algo;
        MatchBlocks(left[pr.li], right[pr.rj], m); // sets status + similarity + blocks
        result.functions.push_back(std::move(m));
    }

    for (size_t i = 0; i < nL; ++i)
    {
        if (lMatched[i]) continue;
        FuncMatch m;
        m.leftName = left[i].name;
        m.leftAddr = left[i].addr;
        m.status = MatchStatus::Removed;
        m.algorithm = "unmatched";
        for (auto& b : left[i].blocks)
            m.blocks.push_back({b.addr, 0, MatchStatus::Removed});
        m.bbRemoved = static_cast<int>(left[i].blocks.size());
        result.functions.push_back(std::move(m));
    }
    for (size_t j = 0; j < nR; ++j)
    {
        if (rMatched[j]) continue;
        FuncMatch m;
        m.rightName = right[j].name;
        m.rightAddr = right[j].addr;
        m.status = MatchStatus::Added;
        m.algorithm = "unmatched";
        for (auto& b : right[j].blocks)
            m.blocks.push_back({0, b.addr, MatchStatus::Added});
        m.bbAdded = static_cast<int>(right[j].blocks.size());
        result.functions.push_back(std::move(m));
    }

    for (auto& m : result.functions)
    {
        switch (m.status)
        {
        case MatchStatus::Identical: ++result.identical; break;
        case MatchStatus::Changed:   ++result.changed;   break;
        case MatchStatus::Added:     ++result.added;     break;
        case MatchStatus::Removed:   ++result.removed;   break;
        }
    }

    return result;
}

} // namespace bifrost
