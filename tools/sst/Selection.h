#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sst {

// Expands a --only list such as "40-7f,06,cb00-cbff" into test-file stems
// ("40" ... "7f", "06", "cb 00" ... "cb ff"). Ranges skip opcodes with no
// test file (illegal opcodes); a single item with no file is an error.
std::vector<std::string> expandSelection(std::string_view spec, const std::vector<std::string>& available);

} // namespace sst
