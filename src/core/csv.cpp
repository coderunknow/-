#include "core/csv.h"

namespace wrg {

std::string csvEscape(const std::string& field) {
    bool needQuotes = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needQuotes = true;
            break;
        }
    }
    if (!needQuotes) return field;
    std::string out;
    out.reserve(field.size() + 2);
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string csvRow(const std::vector<std::string>& fields) {
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out.push_back(',');
        out += csvEscape(fields[i]);
    }
    return out;
}

}  // namespace wrg
