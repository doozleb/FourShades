#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sst {

// One line of tools/sst/manifest.sha256: "<sha256>  v1/<name>.json".
struct ManifestEntry {
    std::string sha256;
    std::string path;
};

std::vector<ManifestEntry> parseManifest(std::string_view text);
std::string readBinaryFile(const std::filesystem::path& path);

// "v1/cb 00.json" -> "cb 00"
std::string stemOf(const std::string& path);

// One message per selected file that is missing or whose hash differs.
// Empty means every selected file matches the manifest.
std::vector<std::string> verifyFiles(const std::vector<ManifestEntry>& entries,
                                     const std::filesystem::path& dataDir,
                                     const std::vector<std::string>& stems);

// One message per file in <dataDir>/v1 that the manifest doesn't list.
std::vector<std::string> unexpectedFiles(const std::vector<ManifestEntry>& entries,
                                         const std::filesystem::path& dataDir);

} // namespace sst
