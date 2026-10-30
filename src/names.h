#pragma once
#include <string>
#include <vector>

namespace names {
enum class Mode { filename, stem, path };
struct Item { std::wstring path; bool directory = false; };
inline std::wstring value(const Item& item, Mode mode) {
    if (mode == Mode::path) return item.path;
    auto end = item.path.find_last_not_of(L"\\/");
    if (end == std::wstring::npos) return item.path;
    auto start = item.path.find_last_of(L"\\/", end);
    auto name = item.path.substr(start == std::wstring::npos ? 0 : start + 1,
                                 end - (start == std::wstring::npos ? 0 : start + 1) + 1);
    if (mode == Mode::stem && !item.directory) {
        auto dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot != 0) name.resize(dot);
    }
    return name;
}
inline std::wstring lines(const std::vector<Item>& items, Mode mode) {
    std::wstring result;
    for (const auto& item : items) {
        if (!result.empty()) result += L"\r\n";
        result += value(item, mode);
    }
    return result;
}
inline std::string escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result += c;
        }
    }
    return result;
}
// CF_HTML offsets are bytes, not Unicode code points.
inline std::string html(const std::vector<std::string>& values) {
    std::string fragment = "<table>";
    for (const auto& v : values)
        fragment += "<tr><td style='mso-number-format:\"\\@\"'>" + escape(v) + "</td></tr>";
    fragment += "</table>";
    const std::string prefix = "<html><head><meta charset=\"utf-8\"></head><body><!--StartFragment-->";
    const std::string suffix = "<!--EndFragment--></body></html>";
    std::string header = "Version:0.9\r\nStartHTML:0000000000\r\nEndHTML:0000000000\r\nStartFragment:0000000000\r\nEndFragment:0000000000\r\n";
    auto set = [&](const std::string& key, size_t offset) {
        std::string digits = std::to_string(offset);
        header.replace(header.find(key) + key.size(), 10, std::string(10 - digits.size(), '0') + digits);
    };
    set("StartHTML:", header.size());
    set("EndHTML:", header.size() + prefix.size() + fragment.size() + suffix.size());
    set("StartFragment:", header.size() + prefix.size());
    set("EndFragment:", header.size() + prefix.size() + fragment.size());
    return header + prefix + fragment + suffix;
}
}
