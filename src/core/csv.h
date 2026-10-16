// RFC-4180-style CSV writer used for benchmark export.
#pragma once

#include <string>
#include <vector>

namespace wrg {

// Escapes one field: quotes when it contains comma/quote/newline/CR.
std::string csvEscape(const std::string& field);

// Joins fields into one CSV line (no trailing newline).
std::string csvRow(const std::vector<std::string>& fields);

}  // namespace wrg
