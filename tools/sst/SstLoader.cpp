#include "sst/SstLoader.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <set>
#include <stdexcept>

namespace sst {

namespace {

using nlohmann::json;

const std::set<std::string> kTestKeys{"name", "initial", "final", "cycles"};
const std::set<std::string> kInitialKeys{"pc", "sp", "a", "b", "c", "d", "e", "f", "h", "l", "ime", "ie", "ram"};
const std::set<std::string> kFinalKeys{"pc", "sp", "a", "b", "c", "d", "e", "f", "h", "l", "ime", "ei", "ram"};

[[noreturn]] void fail(const std::string& test, const std::string& what) {
    throw std::runtime_error("test '" + test + "': " + what);
}

void checkKeys(const json& object, const std::set<std::string>& allowed, const std::string& test,
               const std::string& where) {
    if (!object.is_object()) {
        fail(test, where + " is not an object");
    }
    for (const auto& item : object.items()) {
        if (!allowed.contains(item.key())) {
            fail(test, "unknown key '" + item.key() + "' in " + where);
        }
    }
}

unsigned number(const json& value, unsigned max, const std::string& test, const std::string& what) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > max) {
        fail(test, "bad value for " + what);
    }
    return value.get<unsigned>();
}

unsigned field(const json& object, const char* key, unsigned max, const std::string& test) {
    if (!object.contains(key)) {
        fail(test, std::string("missing key '") + key + "'");
    }
    return number(object.at(key), max, test, key);
}

CpuSnapshot snapshot(const json& object, bool isFinal, const std::string& test) {
    checkKeys(object, isFinal ? kFinalKeys : kInitialKeys, test, isFinal ? "final" : "initial");
    CpuSnapshot s;
    s.pc = static_cast<u16>(field(object, "pc", 0xFFFF, test));
    s.sp = static_cast<u16>(field(object, "sp", 0xFFFF, test));
    s.a = static_cast<u8>(field(object, "a", 0xFF, test));
    s.b = static_cast<u8>(field(object, "b", 0xFF, test));
    s.c = static_cast<u8>(field(object, "c", 0xFF, test));
    s.d = static_cast<u8>(field(object, "d", 0xFF, test));
    s.e = static_cast<u8>(field(object, "e", 0xFF, test));
    s.f = static_cast<u8>(field(object, "f", 0xFF, test));
    s.h = static_cast<u8>(field(object, "h", 0xFF, test));
    s.l = static_cast<u8>(field(object, "l", 0xFF, test));
    s.ime = field(object, "ime", 1, test) == 1;
    if (isFinal) {
        if (object.contains("ei")) {
            s.imePending = field(object, "ei", 1, test) == 1;
        }
    } else {
        // "ie" is in every initial state and no final one: the generator's own
        // interrupt-enable latch (0 or 1, not the 0xFFFF register). Interrupt
        // delivery is piece 2, so it is validated here and otherwise unused.
        field(object, "ie", 1, test);
    }
    if (!object.contains("ram") || !object.at("ram").is_array()) {
        fail(test, "missing ram");
    }
    for (const json& entry : object.at("ram")) {
        if (!entry.is_array() || entry.size() != 2) {
            fail(test, "bad ram entry");
        }
        s.ram.emplace_back(static_cast<u16>(number(entry[0], 0xFFFF, test, "ram address")),
                           static_cast<u8>(number(entry[1], 0xFF, test, "ram value")));
    }
    return s;
}

Cycle cycle(const json& entry, const std::string& test) {
    if (!entry.is_array() || entry.size() != 3 || !entry[2].is_string()) {
        fail(test, "bad cycle entry");
    }
    const std::string kind = entry[2].get<std::string>();
    Cycle c;
    c.address = static_cast<u16>(number(entry[0], 0xFFFF, test, "cycle address"));
    c.value = static_cast<u8>(number(entry[1], 0xFF, test, "cycle value"));
    if (kind == "r-m") {
        c.kind = CycleKind::Read;
    } else if (kind == "-wm") {
        c.kind = CycleKind::Write;
    } else if (kind == "---") {
        c.kind = CycleKind::Idle;
    } else {
        fail(test, "unknown cycle kind '" + kind + "'");
    }
    return c;
}

} // namespace

std::vector<SstTest> parseTests(std::string_view jsonText) {
    const json document = json::parse(jsonText);
    if (!document.is_array()) {
        throw std::runtime_error("test file is not a JSON array");
    }
    std::vector<SstTest> tests;
    tests.reserve(document.size());
    for (const json& item : document) {
        if (!item.is_object() || !item.contains("name") || !item.at("name").is_string()) {
            throw std::runtime_error("test without a string 'name'");
        }
        const std::string name = item.at("name").get<std::string>();
        checkKeys(item, kTestKeys, name, "test");
        if (!item.contains("initial") || !item.contains("final") || !item.contains("cycles") ||
            !item.at("cycles").is_array()) {
            fail(name, "missing initial, final or cycles");
        }
        SstTest test;
        test.name = name;
        test.initial = snapshot(item.at("initial"), false, name);
        test.final = snapshot(item.at("final"), true, name);
        for (const json& entry : item.at("cycles")) {
            test.cycles.push_back(cycle(entry, name));
        }
        tests.push_back(std::move(test));
    }
    return tests;
}

} // namespace sst
