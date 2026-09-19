#include "bifrostdiffengine.h"
#include "bifrostfeatures.h"

#include <memory>

using namespace BinaryNinja;

namespace bifrost {

std::string bvDisplayName(Ref<BinaryView> bv)
{
    if (!bv) return "(none)";
    if (auto file = bv->GetFile())
    {
        if (auto pf = file->GetProjectFile())
        {
            auto n = pf->GetName();
            if (!n.empty()) return n;
        }
        auto p = file->GetFilename();
        auto s = p.rfind('/');
        return (s != std::string::npos) ? p.substr(s + 1) : p;
    }
    return "(none)";
}

const char* LegacyStatusString(MatchStatus s)
{
    return (s == MatchStatus::Added)   ? "added" :
           (s == MatchStatus::Removed) ? "removed" : "matched";
}

std::vector<DiffFuncEntry> toStoreEntries(const DiffResult& diff)
{
    std::vector<DiffFuncEntry> out;
    out.reserve(diff.functions.size());
    for (auto& fm : diff.functions)
    {
        DiffFuncEntry e;
        e.name        = !fm.leftName.empty() ? fm.leftName : fm.rightName;
        e.status      = LegacyStatusString(fm.status);
        e.leftAddr    = fm.leftAddr;
        e.rightAddr   = fm.rightAddr;
        e.similarity  = fm.similarity;
        e.confidence  = fm.confidence;
        e.algorithm   = fm.algorithm;
        e.bbIdentical = fm.bbIdentical;
        e.bbChanged   = fm.bbChanged;
        e.bbAdded     = fm.bbAdded;
        e.bbRemoved   = fm.bbRemoved;
        for (auto& b : fm.blocks)
            e.blocks.push_back({b.leftAddr, b.rightAddr, MatchStatusString(b.status)});
        out.push_back(std::move(e));
    }
    return out;
}

DiffResult DiffEngine::Compute(Ref<BinaryView> left, Ref<BinaryView> right,
                               const ProgressFn& progress)
{
    DiffResult result;
    if (!left || !right)
        return result;

    if (progress) progress("Extracting features (left)", 0, 3);
    FeatureExtractor leftExtractor(left);
    auto leftFeatures = leftExtractor.ExtractAll();

    if (progress) progress("Extracting features (right)", 1, 3);
    FeatureExtractor rightExtractor(right);
    auto rightFeatures = rightExtractor.ExtractAll();

    if (progress) progress("Matching functions", 2, 3);
    Matcher matcher;
    result = matcher.Run(std::move(leftFeatures), std::move(rightFeatures), progress);

    result.leftBinary = bvDisplayName(left);
    result.rightBinary = bvDisplayName(right);
    return result;
}

void ComputeAsync(Ref<BinaryView> left, Ref<BinaryView> right,
                  std::function<void(DiffResult)> onDone)
{
    if (!left || !right)
    {
        if (onDone) onDone(DiffResult{});
        return;
    }

    std::string title = "Bifrost: diff " + bvDisplayName(left) + " vs " + bvDisplayName(right);
    Ref<BackgroundTask> task = new BackgroundTask(title, true);

    WorkerEnqueue([left, right, onDone, task]() {
        DiffEngine engine;
        DiffResult diff = engine.Compute(
            left, right,
            [task](const std::string& phase, size_t, size_t) {
                task->SetProgressText("Bifrost: " + phase);
                return !task->IsCancelled();
            });

        bool cancelled = task->IsCancelled();
        auto shared = std::make_shared<DiffResult>(std::move(diff));

        // Hand the result back on the main (UI) thread.
        ExecuteOnMainThread([onDone, shared, task, cancelled]() {
            task->Finish();
            if (onDone)
                onDone(cancelled ? DiffResult{} : std::move(*shared));
        });
    }, "Bifrost Diff");
}

} // namespace bifrost
