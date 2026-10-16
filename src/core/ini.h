// Minimal INI parser/serializer: [section] + key = value, ';'/'#' comments.
// Tolerant and deterministic; duplicate keys resolve to the last occurrence.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace wrg {

class IniFile {
public:
    // Parses text. Never throws. Malformed lines are skipped and counted.
    void parse(const std::string& text);

    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& key) const;

    // Raw access (keys/sections are stored lower-case).
    std::string get(const std::string& section, const std::string& key,
                    const std::string& fallback = std::string()) const;
    void set(const std::string& section, const std::string& key, const std::string& value);

    const std::map<std::string, std::map<std::string, std::string>>& sections() const {
        return sections_;
    }

    // Number of malformed (skipped) lines during parse.
    int malformedLines() const { return malformed_; }

    std::string serialize() const;

    static std::string toLower(const std::string& s);
    static std::string trim(const std::string& s);

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;
    std::vector<std::string> sectionOrder_;  // for stable serialization
    int malformed_ = 0;
};

}  // namespace wrg
