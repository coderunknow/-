#include "core/ini.h"

#include <cctype>
#include <sstream>

namespace wrg {

std::string IniFile::toLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string IniFile::trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

void IniFile::parse(const std::string& text) {
    sections_.clear();
    sectionOrder_.clear();
    malformed_ = 0;

    std::string section;  // empty section = global
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == ';' || t[0] == '#') continue;
        if (t[0] == '[') {
            const size_t close = t.find(']');
            if (close == std::string::npos) {
                ++malformed_;
                continue;
            }
            section = toLower(trim(t.substr(1, close - 1)));
            if (sections_.find(section) == sections_.end()) sectionOrder_.push_back(section);
            sections_[section];  // ensure exists
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) {
            ++malformed_;
            continue;
        }
        const std::string key = toLower(trim(t.substr(0, eq)));
        const std::string value = trim(t.substr(eq + 1));
        if (key.empty()) {
            ++malformed_;
            continue;
        }
        if (sections_.find(section) == sections_.end()) sectionOrder_.push_back(section);
        sections_[section][key] = value;  // last one wins
    }
}

bool IniFile::hasSection(const std::string& section) const {
    return sections_.find(toLower(trim(section))) != sections_.end();
}

bool IniFile::hasKey(const std::string& section, const std::string& key) const {
    auto sit = sections_.find(toLower(trim(section)));
    if (sit == sections_.end()) return false;
    return sit->second.find(toLower(trim(key))) != sit->second.end();
}

std::string IniFile::get(const std::string& section, const std::string& key,
                         const std::string& fallback) const {
    auto sit = sections_.find(toLower(trim(section)));
    if (sit == sections_.end()) return fallback;
    auto kit = sit->second.find(toLower(trim(key)));
    if (kit == sit->second.end()) return fallback;
    return kit->second;
}

void IniFile::set(const std::string& section, const std::string& key, const std::string& value) {
    const std::string sec = toLower(trim(section));
    const std::string k = toLower(trim(key));
    if (sections_.find(sec) == sections_.end()) sectionOrder_.push_back(sec);
    sections_[sec][k] = value;
}

std::string IniFile::serialize() const {
    std::ostringstream out;
    out << "; Windows Resource Governor configuration\n";
    out << "; Generated file - values are validated on load; safe defaults apply if missing.\n";
    for (const auto& sec : sectionOrder_) {
        auto sit = sections_.find(sec);
        if (sit == sections_.end()) continue;
        out << "\n[" << sec << "]\n";
        for (const auto& kv : sit->second) {
            out << kv.first << " = " << kv.second << "\n";
        }
    }
    return out.str();
}

}  // namespace wrg
