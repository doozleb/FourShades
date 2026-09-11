#include "sst/Selection.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace sst {

namespace {

struct Item {
    bool cb = false;
    int value = 0;
};

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && text[begin] == ' ') ++begin;
    while (end > begin && text[end - 1] == ' ') --end;
    return std::string(text.substr(begin, end - begin));
}

Item parseItem(const std::string& original) {
    std::string text = original;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    Item item;
    if (text.size() > 2 && text.rfind("cb", 0) == 0) {
        item.cb = true;
        text = trim(text.substr(2));
    }
    if (text.size() != 2 || !std::isxdigit(static_cast<unsigned char>(text[0])) ||
        !std::isxdigit(static_cast<unsigned char>(text[1]))) {
        throw std::runtime_error("bad opcode in --only: '" + original + "'");
    }
    item.value = std::stoi(text, nullptr, 16);
    return item;
}

std::string stem(Item item) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%s%02x", item.cb ? "cb " : "", item.value);
    return buffer;
}

} // namespace

std::vector<std::string> expandSelection(std::string_view spec, const std::vector<std::string>& available) {
    const auto exists = [&](const std::string& name) {
        return std::find(available.begin(), available.end(), name) != available.end();
    };
    std::vector<std::string> out;
    const auto add = [&](const std::string& name) {
        if (std::find(out.begin(), out.end(), name) == out.end()) {
            out.push_back(name);
        }
    };

    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t comma = spec.find(',', start);
        const std::string token =
            trim(spec.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        start = comma == std::string_view::npos ? spec.size() + 1 : comma + 1;
        if (token.empty()) {
            continue;
        }
        const std::size_t dash = token.find('-');
        if (dash == std::string::npos) {
            const std::string name = stem(parseItem(token));
            if (!exists(name)) {
                throw std::runtime_error("no test file for '" + token + "'");
            }
            add(name);
            continue;
        }
        const Item first = parseItem(trim(token.substr(0, dash)));
        const Item last = parseItem(trim(token.substr(dash + 1)));
        if (first.cb != last.cb || first.value > last.value) {
            throw std::runtime_error("bad range in --only: '" + token + "'");
        }
        for (int value = first.value; value <= last.value; ++value) {
            const std::string name = stem({first.cb, value});
            if (exists(name)) {
                add(name);
            }
        }
    }
    return out;
}

} // namespace sst
