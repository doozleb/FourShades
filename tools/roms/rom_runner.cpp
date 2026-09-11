// Runs the Emulator Shootout's 167 DMG test ROMs against FourShades and writes
// build/rom-results.json for tools/scoreboard.py. Run from the repository root.
// The 2 the Shootout treats as informational (no pass condition) are listed in
// the results but never run or counted, so a full run scores N / 165.
//
//   rom_runner                all tests
//   rom_runner --only timer   tests whose name contains "timer" (marked partial)

#include "roms/RomRun.h"
#include "roms/RomTests.h"
#include "sst/Manifest.h"
#include "sst/Sha256.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kExpectedTests = 167;  // the Shootout's DMG list
constexpr std::size_t kExpectedScored = 165; // those with a pass condition
constexpr const char* kInformationalReason = "informational in the Shootout: no pass condition";

struct Options {
    std::filesystem::path data = "tools/roms/data";
    std::filesystem::path manifest = "tools/roms/manifest.sha256";
    std::filesystem::path tests = "tools/roms/tests.json";
    std::filesystem::path out = "build/rom-results.json";
    std::string only;
};

int usage() {
    std::cerr << "usage: rom_runner [--data DIR] [--manifest FILE] [--tests FILE] [--out FILE] [--only TEXT]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) return usage();
        if (arg == "--data") options.data = argv[++i];
        else if (arg == "--manifest") options.manifest = argv[++i];
        else if (arg == "--tests") options.tests = argv[++i];
        else if (arg == "--out") options.out = argv[++i];
        else if (arg == "--only") options.only = argv[++i];
        else return usage();
    }

    try {
        const roms::TestList list = roms::parseTestList(sst::readBinaryFile(options.tests));
        if (list.tests.size() != kExpectedTests) {
            throw std::runtime_error("tests.json lists " + std::to_string(list.tests.size()) + " tests, expected 167");
        }
        std::size_t scored = 0;
        for (const auto& t : list.tests) scored += t.informational ? 0 : 1;
        if (scored != kExpectedScored) {
            throw std::runtime_error("tests.json scores " + std::to_string(scored) + " tests, expected 165");
        }
        if (list.shootoutCommit != "38b926bdbc26993d1b4c43e97979ecc66287bf02") {
            throw std::runtime_error("tests.json is not pinned to the Shootout commit the scoreboard claims");
        }

        // Every file the list names must match the committed manifest exactly.
        std::set<std::string> named;
        for (const auto& t : list.tests) {
            named.insert(t.rom);
            named.insert(t.references.begin(), t.references.end());
        }
        const auto entries = sst::parseManifest(sst::readBinaryFile(options.manifest));
        std::set<std::string> listed;
        std::vector<std::string> problems;
        for (const auto& entry : entries) {
            listed.insert(entry.path);
            const std::filesystem::path file = options.data / std::filesystem::path(entry.path);
            if (!std::filesystem::exists(file)) {
                problems.push_back(entry.path + ": missing");
            } else if (sst::sha256Hex(sst::readBinaryFile(file)) != entry.sha256) {
                problems.push_back(entry.path + ": hash does not match the manifest");
            }
        }
        if (listed != named) {
            problems.push_back("manifest and tests.json name different files");
        }
        if (!problems.empty()) {
            for (const auto& p : problems) std::cerr << "data check: " << p << '\n';
            std::cerr << "refusing to run: run python tools/roms/fetch_roms.py\n";
            return 2;
        }

        const auto start = std::chrono::steady_clock::now();
        const bool partial = !options.only.empty();
        nlohmann::json results = nlohmann::json::array();
        std::size_t passing = 0;
        std::size_t total = 0;
        std::vector<std::string> groupOrder;
        std::map<std::string, std::pair<int, int>> groups; // group -> (passing, total)
        std::map<std::string, std::string> firstFailure;

        for (const roms::RomTest& test : list.tests) {
            if (partial && test.name.find(options.only) == std::string::npos) continue;
            const char* method = test.method == roms::Method::Blargg ? "blargg"
                               : test.method == roms::Method::Mooneye ? "mooneye" : "screenshot";
            if (test.informational) {
                // Listed so the results cover the Shootout's whole list, but not
                // run and not counted: the Shootout has no pass condition for it.
                results.push_back({{"name", test.name}, {"group", test.group}, {"method", method},
                                   {"status", "informational"}, {"reason", kInformationalReason},
                                   {"emulated_seconds", 0.0}, {"serial", ""}});
                continue;
            }
            ++total;
            const std::string bytes = sst::readBinaryFile(options.data / std::filesystem::path(test.rom));
            const auto outcome = roms::runRomTest(test, std::vector<fourshades::u8>(bytes.begin(), bytes.end()));
            const bool pass = outcome.status == roms::Verdict::Pass;
            if (!groups.count(test.group)) groupOrder.push_back(test.group);
            auto& g = groups[test.group];
            g.second += 1;
            if (pass) {
                ++passing;
                g.first += 1;
            } else if (!firstFailure.count(test.group)) {
                firstFailure[test.group] = test.name + ": " + outcome.reason;
            }
            results.push_back({{"name", test.name}, {"group", test.group}, {"method", method},
                               {"status", pass ? "pass" : "fail"}, {"reason", outcome.reason},
                               {"emulated_seconds", outcome.emulatedSeconds}, {"serial", outcome.serial}});
        }
        if (total == 0) throw std::runtime_error("--only matched no tests");

        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const nlohmann::json doc = {
            {"suite", "GBEmulatorShootout (DMG)"}, {"shootout_commit", list.shootoutCommit},
            {"partial", partial}, {"total", total}, {"passing", passing},
            {"elapsed_seconds", seconds}, {"tests", results},
        };
        if (options.out.has_parent_path()) std::filesystem::create_directories(options.out.parent_path());
        std::ofstream out(options.out);
        out << doc.dump(1) << '\n';
        out.close();
        if (!out.good()) {
            std::cerr << "error: could not write " << options.out.string() << '\n';
            return 2;
        }

        for (const auto& name : groupOrder) {
            const auto& g = groups[name];
            std::printf("  %-18s %3d / %-3d  %s\n", name.c_str(), g.first, g.second,
                        firstFailure.count(name) ? firstFailure[name].c_str() : "");
        }
        std::printf("%s%zu / %zu passing  (%.1f s)\nresults: %s\n", partial ? "selected: " : "test roms: ",
                    passing, total, seconds, options.out.string().c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
