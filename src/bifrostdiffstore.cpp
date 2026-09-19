#include "bifrostdiffstore.h"

using namespace BinaryNinja;

Ref<Metadata> bifrostBuildDiffMetadata(const std::string& leftBinaryName,
                                       const std::string& rightBinaryName,
                                       const std::string& timestamp,
                                       const std::vector<DiffFuncEntry>& entries)
{
    // Build the per-function array
    std::vector<Ref<Metadata>> funcArray;
    funcArray.reserve(entries.size());
    for (auto& e : entries)
    {
        std::map<std::string, Ref<Metadata>> row;
        row["name"]        = new Metadata(e.name);
        row["status"]      = new Metadata(e.status);
        row["leftAddr"]    = new Metadata(e.leftAddr);
        row["rightAddr"]   = new Metadata(e.rightAddr);
        row["similarity"]  = new Metadata(e.similarity);
        row["confidence"]  = new Metadata(e.confidence);
        row["algorithm"]   = new Metadata(e.algorithm);
        row["bbIdentical"] = new Metadata((uint64_t)e.bbIdentical);
        row["bbChanged"]   = new Metadata((uint64_t)e.bbChanged);
        row["bbAdded"]     = new Metadata((uint64_t)e.bbAdded);
        row["bbRemoved"]   = new Metadata((uint64_t)e.bbRemoved);

        std::vector<Ref<Metadata>> blockArray;
        blockArray.reserve(e.blocks.size());
        for (auto& b : e.blocks)
        {
            std::map<std::string, Ref<Metadata>> brow;
            brow["l"] = new Metadata(b.leftAddr);
            brow["r"] = new Metadata(b.rightAddr);
            brow["s"] = new Metadata(b.status);
            blockArray.push_back(new Metadata(brow));
        }
        row["blocks"] = new Metadata(blockArray);

        funcArray.push_back(new Metadata(row));
    }

    std::map<std::string, Ref<Metadata>> root;
    root["schema"]    = new Metadata((uint64_t)2);
    root["left"]      = new Metadata(leftBinaryName);
    root["right"]     = new Metadata(rightBinaryName);
    root["timestamp"] = new Metadata(timestamp);
    root["functions"] = new Metadata(funcArray);
    return new Metadata(root);
}

bool bifrostSaveDiff(const std::string& diffName,
                     const std::string& leftBinaryName,
                     const std::string& rightBinaryName,
                     const std::string& timestamp,
                     const std::vector<DiffFuncEntry>& entries,
                     Ref<Project> project)
{
    if (!project || !project->IsOpen())
        return false;

    project->StoreMetadata(diffMetaKey(diffName),
        bifrostBuildDiffMetadata(leftBinaryName, rightBinaryName, timestamp, entries));

    // Update index — read existing, add if not present
    std::vector<std::string> names = bifrostListDiffs(project);
    if (std::find(names.begin(), names.end(), diffName) == names.end())
        names.push_back(diffName);
    project->StoreMetadata(kDiffIndexKey, new Metadata(names));

    return true;
}

Ref<Metadata> bifrostLoadDiff(const std::string& diffName,
                               Ref<Project> project)
{
    if (!project || !project->IsOpen())
        return nullptr;
    return project->QueryMetadata(diffMetaKey(diffName));
}

std::vector<std::string> bifrostListDiffs(Ref<Project> project)
{
    if (!project || !project->IsOpen())
        return {};
    Ref<Metadata> idx = project->QueryMetadata(kDiffIndexKey);
    if (!idx || !idx->IsStringList())
        return {};
    return idx->GetStringList();
}

void bifrostDeleteDiff(const std::string& diffName, Ref<Project> project)
{
    if (!project || !project->IsOpen())
        return;

    project->RemoveMetadata(diffMetaKey(diffName));

    // Remove from index
    auto names = bifrostListDiffs(project);
    names.erase(std::remove(names.begin(), names.end(), diffName), names.end());
    project->StoreMetadata(kDiffIndexKey, new Metadata(names));
}
