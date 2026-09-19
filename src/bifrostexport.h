#pragma once

#include "binaryninjaapi.h"

#include <string>

// Serialize a saved diff (as stored in the project metadata, schema v2) to a
// file. Both readers guard every field so v1 diffs export too. Return false on
// a write error or if the metadata is not a diff.

namespace bifrost {

bool exportDiffJson(BinaryNinja::Ref<BinaryNinja::Metadata> diff, const std::string& path);
bool exportDiffHtml(BinaryNinja::Ref<BinaryNinja::Metadata> diff, const std::string& path);

} // namespace bifrost
