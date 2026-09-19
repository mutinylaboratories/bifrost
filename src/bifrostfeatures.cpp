#include "bifrostfeatures.h"

#include "highlevelilinstruction.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace BinaryNinja;

namespace bifrost {

// ── Hash helpers ────────────────────────────────────────────────────────────

uint64_t Fnv1a(const void* data, size_t len, uint64_t seed)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t Fnv1aStr(const std::string& s, uint64_t seed)
{
    return Fnv1a(s.data(), s.size(), seed);
}

// ── Small-prime product (mnemonic multiset fingerprint) ─────────────────────

// Largest prime below 2^64; keeps the running product in a well-defined field
// while staying a 64-bit value.
static constexpr uint64_t kSppModulus = 0xFFFFFFFFFFFFFFC5ULL;

void SppAccumulate(uint64_t& product, uint64_t prime)
{
    if (product == 0)
        product = 1;
    product = static_cast<uint64_t>(
        (static_cast<unsigned __int128>(product) * prime) % kSppModulus);
}

static bool isPrime(uint64_t n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (uint64_t i = 3; i * i <= n; i += 2)
        if (n % i == 0) return false;
    return true;
}

uint64_t MnemonicPrime(const std::string& mnemonic)
{
    // A shared, process-wide table so the same mnemonic maps to the same prime
    // in both binaries being compared. Guarded because extraction may run off
    // the main thread.
    static std::mutex mtx;
    static std::unordered_map<std::string, uint64_t> table;
    static uint64_t nextCandidate = 2;

    std::lock_guard<std::mutex> lock(mtx);
    auto it = table.find(mnemonic);
    if (it != table.end())
        return it->second;

    while (!isPrime(nextCandidate))
        ++nextCandidate;
    uint64_t prime = nextCandidate++;
    table.emplace(mnemonic, prime);
    return prime;
}

// ── Token classification ────────────────────────────────────────────────────

// Immediate / address tokens are masked out of the normalized skeleton so that
// only mnemonics, registers and structure survive.
static bool isImmediateToken(BNInstructionTextTokenType t)
{
    switch (t)
    {
    case IntegerToken:
    case PossibleAddressToken:
    case CodeRelativeAddressToken:
    case FloatingPointToken:
        return true;
    default:
        return false;
    }
}

// ── FeatureExtractor ────────────────────────────────────────────────────────

FeatureExtractor::FeatureExtractor(Ref<BinaryView> bv)
    : m_bv(bv)
{
}

double FeatureExtractor::ComputeMdIndex(Ref<Function> f)
{
    // Edge-based BinDiff flow-graph invariant. Depends only on the in/out
    // degrees of each edge's endpoints, so it is stable under block renumbering
    // and needs no topological sort.
    static const double kS2 = std::sqrt(2.0);
    static const double kS3 = std::sqrt(3.0);
    static const double kS5 = std::sqrt(5.0);
    static const double kS7 = std::sqrt(7.0);

    double md = 0.0;
    for (auto& b : f->GetBasicBlocks())
    {
        const double iu = static_cast<double>(b->GetIncomingEdges().size());
        const double ou = static_cast<double>(b->GetOutgoingEdges().size());
        for (auto& e : b->GetOutgoingEdges())
        {
            if (!e.target) continue;
            const double iv = static_cast<double>(e.target->GetIncomingEdges().size());
            const double ov = static_cast<double>(e.target->GetOutgoingEdges().size());
            const double denom = kS2 * iu + kS3 * ou + kS5 * iv + kS7 * ov;
            if (denom > 0.0)
                md += 1.0 / std::sqrt(denom);
        }
    }
    return md;
}

// Compute block-local features from a pre-fetched disassembly (single pass).
static BbFeatures extractBlockFromLines(Ref<BinaryView> bv, Ref<BasicBlock> bb,
                                        const std::vector<DisassemblyTextLine>& lines)
{
    BbFeatures f;
    f.addr = bb->GetStart();
    f.inDegree = bb->GetIncomingEdges().size();
    f.outDegree = bb->GetOutgoingEdges().size();

    // Exact bytes of the block.
    uint64_t len = bb->GetLength();
    if (len > 0)
    {
        std::vector<uint8_t> buf(len);
        size_t got = bv->Read(buf.data(), bb->GetStart(), len);
        f.byteHash = Fnv1a(buf.data(), got);
    }

    uint64_t spp = 1;
    std::string sig;
    for (auto& line : lines)
    {
        ++f.instrCount;
        for (auto& tok : line.tokens)
        {
            if (tok.type == InstructionToken)
                SppAccumulate(spp, MnemonicPrime(tok.text));
            if (!isImmediateToken(tok.type))
            {
                sig += tok.text;
                sig += ' ';
            }
        }
        sig += '\n';
    }
    f.spp = spp;
    f.mnemonicSig = std::move(sig);
    return f;
}

BbFeatures FeatureExtractor::ExtractBlock(Ref<BasicBlock> bb)
{
    Ref<DisassemblySettings> settings = new DisassemblySettings();
    return extractBlockFromLines(m_bv, bb, bb->GetDisassemblyText(settings));
}

FuncFeatures FeatureExtractor::ExtractFunction(Ref<Function> f)
{
    FuncFeatures out;
    out.addr = f->GetStart();
    if (auto sym = f->GetSymbol())
        out.name = sym->GetFullName();

    Ref<Architecture> arch = f->GetArchitecture();
    Ref<DisassemblySettings> settings = new DisassemblySettings();

    auto blocks = f->GetBasicBlocks();
    std::sort(blocks.begin(), blocks.end(),
              [](const Ref<BasicBlock>& a, const Ref<BasicBlock>& b) {
                  return a->GetStart() < b->GetStart();
              });

    out.nodes = blocks.size();
    out.mdIndex = ComputeMdIndex(f);

    uint64_t byteHash = 0xcbf29ce484222325ULL;
    uint64_t normHash = 0xcbf29ce484222325ULL;
    uint64_t funcSpp = 1;

    std::vector<int64_t>  constants;
    std::vector<uint64_t> stringHashes;

    for (auto& bb : blocks)
    {
        auto lines = bb->GetDisassemblyText(settings);
        BbFeatures bf = extractBlockFromLines(m_bv, bb, lines);

        out.edges += bf.outDegree;
        out.instrCount += bf.instrCount;
        SppAccumulate(funcSpp, bf.spp);

        // Chain the exact bytes and the normalized skeleton across blocks.
        uint64_t blen = bb->GetLength();
        if (blen > 0)
        {
            std::vector<uint8_t> buf(blen);
            size_t got = m_bv->Read(buf.data(), bb->GetStart(), blen);
            byteHash = Fnv1a(buf.data(), got, byteHash);
        }
        normHash = Fnv1aStr(bf.mnemonicSig, normHash);

        // Constants and string references, gathered from the same lines.
        for (auto& line : lines)
        {
            for (auto& c : f->GetConstantsReferencedByInstruction(arch, line.addr))
                constants.push_back(c.value);

            auto testString = [&](uint64_t addr) {
                BNStringReference strRef;
                if (m_bv->GetStringAtAddress(addr, strRef) && strRef.length > 0)
                {
                    size_t rlen = std::min<size_t>(strRef.length, 4096);
                    std::vector<uint8_t> sbuf(rlen);
                    size_t got = m_bv->Read(sbuf.data(), strRef.start, rlen);
                    stringHashes.push_back(Fnv1a(sbuf.data(), got));
                }
            };
            for (auto& c : f->GetConstantsReferencedByInstruction(arch, line.addr))
                if (c.pointer) testString(static_cast<uint64_t>(c.value));
            for (auto ref : m_bv->GetCodeReferencesFrom({f, arch, line.addr}))
                testString(ref);
        }

        out.blocks.push_back(std::move(bf));
    }

    out.byteHash = byteHash;
    out.normHash = normHash;
    out.spp = funcSpp;
    out.cyclomatic = (out.edges >= out.nodes) ? (out.edges - out.nodes + 2) : 2;

    // HLIL pseudocode fingerprints from the decompiler AST operation stream.
    // pseudoHash is order-sensitive (control-flow shape); pseudoPrimes is an
    // order-independent multiset of operations.
    if (Ref<HighLevelILFunction> hlil = f->GetHighLevelILIfAvailable())
    {
        uint64_t seq = 0xcbf29ce484222325ULL;
        uint64_t primes = 1;
        size_t count = hlil->GetInstructionCount();
        for (size_t i = 0; i < count; ++i)
        {
            HighLevelILInstruction instr = hlil->GetInstruction(i);
            instr.VisitExprs([&](const HighLevelILInstruction& e) -> bool {
                uint32_t op = static_cast<uint32_t>(e.operation);
                seq = Fnv1a(&op, sizeof(op), seq);
                SppAccumulate(primes, MnemonicPrime("hlil:" + std::to_string(op)));
                return true; // descend into children
            });
        }
        out.pseudoHash = seq;
        out.pseudoPrimes = primes;
    }

    // Call graph.
    auto callSites = f->GetCallSites();
    out.calls = callSites.size();
    std::vector<uint64_t> callees;
    for (auto& cs : callSites)
        for (auto callee : m_bv->GetCallees(cs))
            callees.push_back(callee);

    auto dedupeSort = [](auto& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    dedupeSort(callees);
    dedupeSort(constants);
    dedupeSort(stringHashes);

    out.calleeAddrs = std::move(callees);
    out.constants = std::move(constants);
    out.stringHashes = std::move(stringHashes);

    return out;
}

std::vector<FuncFeatures> FeatureExtractor::ExtractAll()
{
    std::vector<FuncFeatures> result;
    if (!m_bv) return result;

    auto funcs = m_bv->GetAnalysisFunctionList();
    result.reserve(funcs.size());
    for (auto& f : funcs)
    {
        if (!f) continue;
        result.push_back(ExtractFunction(f));
    }
    return result;
}

} // namespace bifrost
