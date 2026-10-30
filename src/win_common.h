#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>
#include "names.h"

inline std::wstring modulePath(HMODULE module = nullptr) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        DWORD n = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!n) throw std::runtime_error("GetModuleFileName");
        if (n < buffer.size()) return {buffer.data(), n};
        buffer.resize(buffer.size() * 2);
    }
}
inline std::wstring parentPath(const std::wstring& s) { return s.substr(0, s.find_last_of(L"\\/")); }
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("UTF-8 conversion");
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr, nullptr);
    return result;
}
inline names::Item fileItem(std::wstring path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return {std::move(path), attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0};
}
inline HGLOBAL clipMemory(const void* data, size_t size) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    void* destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return nullptr; }
    std::memcpy(destination, data, size);
    GlobalUnlock(memory);
    return memory;
}
inline HRESULT copyNames(const std::vector<names::Item>& items, names::Mode mode, HWND owner) {
    if (items.empty()) return E_INVALIDARG;
    const auto text = names::lines(items, mode);
    std::vector<std::string> values;
    for (const auto& item : items) values.push_back(utf8(names::value(item, mode)));
    const auto html = names::html(values);
    HGLOBAL plain = clipMemory(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    HGLOBAL rich = clipMemory(html.c_str(), html.size() + 1);
    if (!plain || !rich) { if (plain) GlobalFree(plain); if (rich) GlobalFree(rich); return E_OUTOFMEMORY; }
    UINT format = RegisterClipboardFormatW(L"HTML Format");
    HWND hidden = nullptr;
    if (!owner) { hidden = CreateWindowExW(0, L"STATIC", L"multi_copyname", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr); owner = hidden; }
    bool opened = false;
    if (owner) for (int i = 0; i < 10; ++i) { if (OpenClipboard(owner)) { opened = true; break; } Sleep(20); }
    HRESULT result = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    if (opened) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, plain)) {
            plain = nullptr; // Clipboard now owns the allocation.
            result = S_OK;
            if (format && SetClipboardData(format, rich)) rich = nullptr;
        } else result = E_FAIL;
        CloseClipboard();
    }
    if (hidden) DestroyWindow(hidden);
    if (plain) GlobalFree(plain);
    if (rich) GlobalFree(rich);
    return result;
}
inline void showError(HWND owner, const wchar_t* text, HRESULT hr) {
    wchar_t code[40];
    swprintf_s(code, L"\n\n错误代码：0x%08lX", static_cast<unsigned long>(hr));
    MessageBoxW(owner, (std::wstring(text) + code).c_str(), L"multi_copyname", MB_OK | MB_ICONERROR);
}
