#include "bifrostdiffdb.h"

#include <filesystem>
#include <system_error>

using namespace BinaryNinja;

Ref<Metadata> bifrostDiffFromBinaryView(Ref<BinaryView> bv)
{
    if (!bv)
        return nullptr;

    // QueryMetadata throws when the key is absent, which is the common case for
    // every ordinary binary the view type is asked about.
    try
    {
        Ref<Metadata> md = bv->QueryMetadata(kDiffDbKey);
        if (md && md->IsKeyValueStore())
            return md;
    }
    catch (const std::exception&)
    {
    }
    return nullptr;
}

bool bifrostSaveDiffToProject(const std::string& diffName,
                              Ref<Metadata> diffData,
                              Ref<Project> project,
                              std::string& errorOut)
{
    if (!diffData || !diffData->IsKeyValueStore())
    {
        errorOut = "no diff data to save";
        return false;
    }
    if (!project || !project->IsOpen())
    {
        errorOut = "no open project";
        return false;
    }

    // Adding the same name twice would silently pile up duplicate project
    // entries (nothing keys project files by name), so refuse instead.
    const std::string fileName = diffName + ".bndb";
    for (auto& pf : project->GetFiles())
        if (pf && pf->GetName() == fileName)
        {
            errorOut = "\"" + fileName + "\" is already in the project; "
                       "delete it there first to replace it";
            return false;
        }

    // Use the diff's JSON as the database's raw contents so the file still says
    // something useful if it's inspected outside Binary Ninja. The diff itself
    // is carried as metadata below — that's what actually round-trips.
    const std::string json = diffData->GetJsonString();
    if (json.empty())
    {
        errorOut = "failed to serialize diff";
        return false;
    }

    Ref<FileMetadata> fm = new FileMetadata();
    fm->SetFilename(diffName + ".bndiff");

    DataBuffer buf(json.data(), json.size());
    Ref<BinaryView> bv = new BinaryData(fm, buf);
    if (!bv)
    {
        errorOut = "failed to create the diff view";
        return false;
    }

    // BinaryView metadata is serialized into the .bndb, so this is what makes
    // the diff survive a save/open round trip.
    bv->StoreMetadata(kDiffDbKey, diffData);

    // Write the database to a temp path first; the project copies it into its
    // own store, after which the temp file is no longer needed.
    std::error_code ec;
    std::filesystem::path tmp =
        std::filesystem::temp_directory_path(ec) / (diffName + ".bndb");
    if (ec)
    {
        errorOut = "no writable temp directory: " + ec.message();
        return false;
    }

    Ref<SaveSettings> settings = new SaveSettings();
    // Keep the database self-contained — it must not chase the path of the
    // scratch buffer it was built from.
    settings->SetOption(PurgeOriginalFilenamePath, true);

    if (!bv->CreateDatabase(tmp.string(), settings))
    {
        errorOut = "failed to write the diff database";
        return false;
    }

    Ref<ProjectFile> pf = project->CreateFileFromPath(
        tmp.string(), nullptr, fileName,
        "Bifrost diff: " + diffName);

    std::filesystem::remove(tmp, ec);   // best effort

    if (!pf)
    {
        errorOut = "failed to add the diff to the project";
        return false;
    }
    return true;
}
