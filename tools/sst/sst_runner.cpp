// Runs the SingleStepTests SM83 suite against the FourShades CPU and writes
// a results file for tools/scoreboard.py. Run from the repository root.
//
//   sst_runner                      full run: all 500 files, writes build/sst-results.json
//   sst_runner --only 40-7f,cb00-cb0f  a subset, marked partial (the scoreboard refuses it)

#include "sst/Manifest.h"
#include "sst/RecordingBus.h"
#include "sst/Selection.h"
#include "sst/SstLoader.h"
#include "sst/SstRun.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kCommit = "f9c30210245dd691661db39f5ace022c465ecc2f";
constexpr std::size_t kExpectedFiles = 500;

struct Options {
    std::filesystem::path data = "tools/sst/data";
    std::filesystem::path manifest = "tools/sst/manifest.sha256";
    std::filesystem::path out = "build/sst-results.json";
    std::string only;
};

int usage() {
    std::cerr << "usage: sst_runner [--data DIR] [--manifest FILE] [--out FILE] [--only LIST]\n"
                 "  LIST: comma-separated opcodes or ranges, e.g. 40-7f,06,cb00-cbff\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            return usage();
        }
        if (arg == "--data") {
            options.data = argv[++i];
        } else if (arg == "--manifest") {
            options.manifest = argv[++i];
        } else if (arg == "--out") {
            options.out = argv[++i];
        } else if (arg == "--only") {
            options.only = argv[++i];
        } else {
            return usage();
        }
    }

    try {
        const auto entries = sst::parseManifest(sst::readBinaryFile(options.manifest));
        if (entries.size() != kExpectedFiles) {
            throw std::runtime_error("manifest lists " + std::to_string(entries.size()) + " files, expected 500");
        }
        std::vector<std::string> all;
        for (const auto& entry : entries) {
            all.push_back(sst::stemOf(entry.path));
        }
        const bool partial = !options.only.empty();
        const std::vector<std::string> selected = partial ? sst::expandSelection(options.only, all) : all;
        if (selected.empty()) {
            throw std::runtime_error("--only matched no test files");
        }

        auto problems = sst::verifyFiles(entries, options.data, selected);
        if (!partial) {
            const auto extra = sst::unexpectedFiles(entries, options.data);
            problems.insert(problems.end(), extra.begin(), extra.end());
        }
        if (!problems.empty()) {
            for (const auto& problem : problems) {
                std::cerr << "data check: " << problem << '\n';
            }
            std::cerr << "refusing to run: test data does not match tools/sst/manifest.sha256\n"
                         "run: python tools/sst/fetch_sst.py\n";
            return 2;
        }

        const auto start = std::chrono::steady_clock::now();
        sst::RecordingBus bus;
        nlohmann::json files = nlohmann::json::array();
        std::size_t passingFiles = 0;
        std::size_t unimplementedFiles = 0;

        for (const std::string& stem : selected) {
            const auto tests = sst::parseTests(sst::readBinaryFile(options.data / "v1" / (stem + ".json")));
            std::size_t passed = 0;
            bool unimplemented = false;
            nlohmann::json firstFailure = nullptr;
            for (const sst::SstTest& test : tests) {
                const sst::TestOutcome outcome = sst::runTest(test, bus);
                if (outcome.status == sst::Status::Pass) {
                    ++passed;
                } else if (outcome.status == sst::Status::Unimplemented) {
                    unimplemented = true;
                    break;
                } else if (firstFailure.is_null()) {
                    const sst::Mismatch& m = *outcome.mismatch;
                    firstFailure = {{"test", test.name}, {"field", m.field}, {"expected", m.expected},
                                    {"actual", m.actual}, {"cycle", m.cycle}};
                }
            }

            std::string status = "fail";
            if (unimplemented) {
                status = "unimplemented";
                ++unimplementedFiles;
            } else if (passed == tests.size()) {
                status = "pass";
                ++passingFiles;
            } else {
                std::cout << "FAIL " << stem << "  " << passed << "/" << tests.size() << "  first: "
                          << firstFailure["test"].get<std::string>() << "  "
                          << firstFailure["field"].get<std::string>() << " expected "
                          << firstFailure["expected"].get<std::string>() << " got "
                          << firstFailure["actual"].get<std::string>();
                if (firstFailure["cycle"].get<int>() >= 0) {
                    std::cout << " (cycle " << firstFailure["cycle"].get<int>() << ")";
                }
                std::cout << '\n';
            }
            files.push_back({{"name", stem}, {"status", status}, {"passed", passed},
                             {"tests", tests.size()}, {"first_failure", firstFailure}});
        }

        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const nlohmann::json results = {
            {"suite", "SingleStepTests/sm83"}, {"commit", kCommit},        {"partial", partial},
            {"total_files", selected.size()},  {"passing_files", passingFiles},
            {"elapsed_seconds", seconds},      {"files", files},
        };
        if (options.out.has_parent_path()) {
            std::filesystem::create_directories(options.out.parent_path());
        }
        std::ofstream out(options.out);
        out << results.dump(1) << '\n';
        if (!out.good()) {
            std::cerr << "error: could not write " << options.out.string() << '\n';
            return 2;
        }

        std::printf("%s%zu / %zu passing  (%zu unimplemented, %.1f s)\nresults: %s\n",
                    partial ? "selected: " : "CPU instructions: ", passingFiles, selected.size(),
                    unimplementedFiles, seconds, options.out.string().c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
