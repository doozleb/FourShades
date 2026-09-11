#include <doctest/doctest.h>

#include "sst/Manifest.h"
#include "sst/Sha256.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fsys = std::filesystem;

namespace {
const std::string kAbcHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

fsys::path freshDir() {
    const fsys::path dir = fsys::temp_directory_path() / "fourshades-manifest-test";
    fsys::remove_all(dir);
    fsys::create_directories(dir / "v1");
    return dir;
}

void writeFile(const fsys::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary) << content;
}
} // namespace

TEST_CASE("parseManifest reads sha256sum lines, including names with spaces and CRLF") {
    const std::string text = kAbcHash + "  v1/00.json\r\n" + kAbcHash + "  v1/cb 00.json\n\n";
    const auto entries = sst::parseManifest(text);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].sha256 == kAbcHash);
    CHECK(entries[0].path == "v1/00.json");
    CHECK(entries[1].path == "v1/cb 00.json");
    CHECK(sst::stemOf(entries[1].path) == "cb 00");
}

TEST_CASE("parseManifest rejects malformed lines") {
    CHECK_THROWS_AS(sst::parseManifest("not a hash  v1/00.json\n"), std::runtime_error);
    CHECK_THROWS_AS(sst::parseManifest(kAbcHash + " v1/00.json\n"), std::runtime_error);
}

TEST_CASE("verifyFiles accepts matching files and reports changed or missing ones") {
    const fsys::path dir = freshDir();
    writeFile(dir / "v1" / "00.json", "abc");
    const std::vector<sst::ManifestEntry> entries{{kAbcHash, "v1/00.json"}, {kAbcHash, "v1/01.json"}};

    CHECK(sst::verifyFiles(entries, dir, {"00"}).empty());

    const auto missing = sst::verifyFiles(entries, dir, {"00", "01"});
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].find("01.json") != std::string::npos);

    writeFile(dir / "v1" / "00.json", "abd");
    CHECK(sst::verifyFiles(entries, dir, {"00"}).size() == 1);
}

TEST_CASE("unexpectedFiles reports files the manifest doesn't list") {
    const fsys::path dir = freshDir();
    writeFile(dir / "v1" / "00.json", "abc");
    writeFile(dir / "v1" / "zz.json", "extra");
    const std::vector<sst::ManifestEntry> entries{{kAbcHash, "v1/00.json"}};
    const auto extra = sst::unexpectedFiles(entries, dir);
    REQUIRE(extra.size() == 1);
    CHECK(extra[0].find("zz.json") != std::string::npos);
}
