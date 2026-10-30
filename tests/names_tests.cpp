#include "names.h"
#include <iostream>
#include <stdexcept>
static void check(bool success, const char* name) { if (!success) throw std::runtime_error(name); }
int main() {
    using namespace names;
    const std::vector<Item> items = {{L"C:\\资料\\PCB V1.2.xlsx"}, {L"D:\\a\\firmware.v2.bin"}, {L"D:\\docs.v1", true}};
    check(lines(items, Mode::filename) == L"PCB V1.2.xlsx\r\nfirmware.v2.bin\r\ndocs.v1", "Unicode multiple selection / CRLF");
    check(lines(items, Mode::stem) == L"PCB V1.2\r\nfirmware.v2\r\ndocs.v1", "last extension / dotted directory");
    check(value({L"C:\\a\\.gitignore"}, Mode::stem) == L".gitignore", "dotfile");
    check(value({L"C:\\a\\README"}, Mode::stem) == L"README", "no extension");
    check(value({L"C:\\a\\folder\\", true}, Mode::filename) == L"folder", "trailing slash");
    check(value({L"C:\\a\\你好.txt"}, Mode::path) == L"C:\\a\\你好.txt", "full path");
    check(lines({}, Mode::filename).empty(), "empty selection");
    std::vector<Item> many;
    for (int i = 0; i < 10000; ++i) many.push_back({L"C:\\" + std::to_wstring(i) + L".txt"});
    const auto large = lines(many, Mode::filename);
    check(large.substr(large.size() - 8) == L"9999.txt", "10000 files");
    auto h = html({"001.txt", "=1+1.txt", "a&b<test>.txt", u8"中文.txt"});
    auto offset = [&](const std::string& key) { return std::stoull(h.substr(h.find(key) + key.size(), 10)); };
    check(h.substr(offset("StartHTML:"), 6) == "<html>", "HTML start offset");
    check(offset("EndHTML:") == h.size(), "HTML byte length");
    check(h.substr(offset("StartFragment:"), 7) == "<table>", "fragment start");
    check(h.substr(offset("EndFragment:"), 18) == "<!--EndFragment-->", "fragment end");
    check(h.find("a&amp;b&lt;test&gt;.txt") != std::string::npos, "HTML escaping");
    check(h.find("mso-number-format") != std::string::npos, "Excel text cells");
    std::cout << "All filename / multi-selection / CF_HTML tests passed\n";
}
