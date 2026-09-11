#include "sst/Manifest.h"

#include "sst/Sha256.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace sst {

std::vector<ManifestEntry> parseManifest(std::string_view text) {
    std::vector<ManifestEntry> entries;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        const bool hexDigest =
            line.size() > 66 && std::all_of(line.begin(), line.begin() + 64, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        if (!hexDigest || line.substr(64, 2) != "  ") {
            throw std::runtime_error("manifest line " + std::to_string(lineNumber) + " is malformed");
        }
        entries.push_back({std::string(line.substr(0, 64)), std::string(line.substr(66))});
    }
    return entries;
}

std::string readBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::string data(size, '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

std::string stemOf(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

std::vector<std::string> verifyFiles(const std::vector<ManifestEntry>& entries,
                                     const std::filesystem::path& dataDir,
                                     const std::vector<std::string>& stems) {
    std::vector<std::string> problems;
    for (const std::string& stem : stems) {
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [&](const ManifestEntry& e) { return stemOf(e.path) == stem; });
        if (entry == entries.end()) {
            problems.push_back(stem + ": not in the manifest");
            continue;
        }
        const std::filesystem::path file = dataDir / std::filesystem::path(entry->path);
        if (!std::filesystem::exists(file)) {
            problems.push_back(entry->path + ": missing");
            continue;
        }
        if (sha256Hex(readBinaryFile(file)) != entry->sha256) {
            problems.push_back(entry->path + ": hash does not match the manifest");
        }
    }
    return problems;
}

std::vector<std::string> unexpectedFiles(const std::vector<ManifestEntry>& entries,
                                         const std::filesystem::path& dataDir) {
    std::set<std::string> known;
    for (const ManifestEntry& entry : entries) {
        known.insert(std::filesystem::path(entry.path).filename().string());
    }
    const std::filesystem::path v1 = dataDir / "v1";
    if (!std::filesystem::exists(v1)) {
        return {"v1: missing"};
    }
    std::vector<std::string> problems;
    for (const auto& item : std::filesystem::directory_iterator(v1)) {
        const std::string name = item.path().filename().string();
        if (!known.contains(name)) {
            problems.push_back("v1/" + name + ": not in the manifest");
        }
    }
    return problems;
}

} // namespace sst
