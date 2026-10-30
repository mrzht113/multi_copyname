#include "win_common.h"
#include <commctrl.h>
#include <algorithm>

static HINSTANCE instance;
static HWND list, status;
static HFONT font;
static std::vector<names::Item> selected;
enum { Add = 101, Clear, Copy, CopyStem, CopyPath, Install, Uninstall };
static void message(const std::wstring& text) { SetWindowTextW(status, text.c_str()); }
static void refresh() {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& item : selected) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.path.c_str()));
    message(L"已选择 " + std::to_wstring(selected.size()) + L" 项。复制后，在 Excel 单元格中按 Ctrl+V（不要进入单元格编辑状态）。");
}
static void addPath(std::wstring path) {
    if (std::none_of(selected.begin(), selected.end(), [&](const auto& item) { return item.path == path; })) selected.push_back(fileItem(std::move(path)));
}
static void selectFiles(HWND window) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) { showError(window, L"无法打开文件选择窗口。", hr); return; }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    dialog->SetTitle(L"选择要复制名称的文件（支持多选）");
    if (SUCCEEDED(dialog->Show(window))) {
        IShellItemArray* results = nullptr;
        if (SUCCEEDED(dialog->GetResults(&results))) {
            DWORD count = 0; results->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(results->GetItemAt(i, &item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { addPath(path); CoTaskMemFree(path); }
                    item->Release();
                }
            }
            results->Release(); refresh();
        }
    }
    dialog->Release();
}
static std::wstring installDir() {
    PWSTR appData = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &appData);
    if (FAILED(hr)) throw std::runtime_error("LocalAppData");
    std::wstring directory = std::wstring(appData) + L"\\Programs\\multi_copyname\\1.0.0";
    CoTaskMemFree(appData);
    return directory;
}
static HRESULT registerAt(const std::wstring& directory, bool enable) {
    // An absolute path and restricted dependency search avoid loading a DLL from CWD.
    HMODULE dll = LoadLibraryExW((directory + L"\\multi_copyname_shell.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) return HRESULT_FROM_WIN32(GetLastError());
    auto function = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(dll, enable ? "DllRegisterServer" : "DllUnregisterServer"));
    HRESULT hr = function ? function() : E_NOINTERFACE;
    FreeLibrary(dll); return hr;
}
static HRESULT installMenu(bool enable) {
    const auto destination = installDir();
    if (!enable) {
        // Unregister through the distributed DLL, so uninstall works even if the
        // installed copy was removed or locked by Explorer.
        HRESULT hr = registerAt(parentPath(modulePath()), false);
        if (FAILED(hr)) return hr;
        // Loaded Explorer extensions and the running exe may remain locked.
        // Keep the small version directory; users can remove it after signing out.
        return S_OK;
    }
    int err = SHCreateDirectoryExW(nullptr, destination.c_str(), nullptr);
    if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS) return HRESULT_FROM_WIN32(err);
    const auto source = parentPath(modulePath());
    const wchar_t* files[] = {L"multi_copyname.exe", L"multi_copyname_shell.dll"};
    if (_wcsicmp(source.c_str(), destination.c_str()) != 0) {
        for (auto file : files) {
            auto from = source + L"\\" + file;
            auto to = destination + L"\\" + file;
            // A repeated installation of identical binaries should succeed even
            // while Explorer has the DLL loaded.
            bool same = false;
            HANDLE a = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            HANDLE b = CreateFileW(to.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sa{}, sb{};
                if (GetFileSizeEx(a, &sa) && GetFileSizeEx(b, &sb) && sa.QuadPart == sb.QuadPart) {
                    same = true;
                    char ba[16384], bb[16384]; DWORD na, nb;
                    for (;;) {
                        if (!ReadFile(a, ba, sizeof(ba), &na, nullptr) || !ReadFile(b, bb, sizeof(bb), &nb, nullptr) || na != nb || std::memcmp(ba, bb, na)) { same = false; break; }
                        if (!na) break;
                    }
                }
            }
            if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
            if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
            if (!same && !CopyFileW(from.c_str(), to.c_str(), FALSE)) return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    return registerAt(destination, true);
}
static void operation(HWND window, bool enable) {
    HRESULT hr = installMenu(enable);
    if (FAILED(hr)) { showError(window, enable ? L"安装失败。请检查 DLL 是否与程序放在同一文件夹；如已安装旧版，请注销 Windows 后重试。" : L"卸载右键菜单失败。请检查 DLL 是否与程序放在同一文件夹。", hr); return; }
    MessageBoxW(window, enable
        ? L"右键菜单已安装到当前用户。\n\n在资源管理器中选中一个或多个文件，右键 → multi_copyname · 复制名称。\nWindows 11：先点“显示更多选项”。\n若菜单尚未刷新，请重新打开资源管理器窗口或注销后登录。"
        : L"右键菜单已卸载。若旧菜单仍显示，请注销后登录。\n\n安装文件保留在 %LOCALAPPDATA%\\Programs\\multi_copyname，注销后可手动删除该文件夹。",
        L"multi_copyname", MB_OK | MB_ICONINFORMATION);
}
static HWND control(HWND window, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND result = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return result;
}
static LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    try {
        switch (msg) {
        case WM_CREATE:
            font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
            control(window, L"STATIC", L"multi_copyname  1.0.0", 0, 24, 20, 700, 28, 0);
            control(window, L"STATIC", L"复制文件名，每个名称一行。支持右键多选，也可添加或拖入文件 / 文件夹。", 0, 24, 56, 740, 28, 0);
            control(window, L"BUTTON", L"添加文件…", WS_TABSTOP, 24, 98, 125, 36, Add);
            control(window, L"BUTTON", L"清空列表", WS_TABSTOP, 161, 98, 125, 36, Clear);
            list = control(window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOSEL, 24, 148, 740, 235, 0);
            SendMessageW(list, LB_SETHORIZONTALEXTENT, 2400, 0);
            control(window, L"BUTTON", L"复制文件名", WS_TABSTOP | BS_DEFPUSHBUTTON, 24, 400, 170, 40, Copy);
            control(window, L"BUTTON", L"复制（不含扩展名）", WS_TABSTOP, 208, 400, 220, 40, CopyStem);
            control(window, L"BUTTON", L"复制完整路径", WS_TABSTOP, 442, 400, 170, 40, CopyPath);
            status = control(window, L"STATIC", L"", 0, 24, 456, 740, 55, 0);
            control(window, L"BUTTON", L"安装右键菜单", WS_TABSTOP, 24, 520, 190, 38, Install);
            control(window, L"BUTTON", L"卸载右键菜单", WS_TABSTOP, 228, 520, 190, 38, Uninstall);
            control(window, L"STATIC", L"仅当前用户 · 无需管理员权限 · Windows 11 请点“显示更多选项”", 0, 24, 576, 740, 28, 0);
            DragAcceptFiles(window, TRUE); refresh(); return 0;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(w);
            try {
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i) {
                    UINT n = DragQueryFileW(drop, i, nullptr, 0);
                    std::vector<wchar_t> path(static_cast<size_t>(n) + 1);
                    if (DragQueryFileW(drop, i, path.data(), n + 1)) addPath(path.data());
                }
            } catch (...) { DragFinish(drop); throw; }
            DragFinish(drop); refresh(); return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(w)) {
            case Add: selectFiles(window); break;
            case Clear: selected.clear(); refresh(); break;
            case Install: operation(window, true); break;
            case Uninstall: operation(window, false); break;
            case Copy: case CopyStem: case CopyPath: {
                if (selected.empty()) { message(L"请先添加或拖入文件。"); break; }
                auto mode = static_cast<names::Mode>(LOWORD(w) - Copy);
                HRESULT hr = copyNames(selected, mode, window);
                if (FAILED(hr)) showError(window, L"复制失败，请稍后重试。", hr);
                else message(L"已复制 " + std::to_wstring(selected.size()) + L" 项。现在可粘贴到 Excel、记事本或其他程序。");
                break;
            }
            }
            return 0;
        case WM_DESTROY: DeleteObject(font); PostQuitMessage(0); return 0;
        }
    } catch (...) { showError(window, L"操作失败，无法读取文件或内存不足。", E_FAIL); }
    return DefWindowProcW(window, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int show) {
    instance = app;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    int result = 0;
    try {
        int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        bool install = count == 2 && _wcsicmp(args[1], L"/install") == 0;
        bool uninstall = count == 2 && _wcsicmp(args[1], L"/uninstall") == 0;
        LocalFree(args);
        if (install || uninstall) result = FAILED(installMenu(install)) ? 1 : 0;
        else {
            WNDCLASSW cls{};
            cls.lpfnWndProc = windowProc; cls.hInstance = app; cls.lpszClassName = L"MultiCopyNameManager";
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (!RegisterClassW(&cls)) throw std::runtime_error("RegisterClass");
            RECT rect{0, 0, 790, 622};
            DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            AdjustWindowRect(&rect, style, FALSE);
            HWND window = CreateWindowExW(0, cls.lpszClassName, L"multi_copyname · 多文件名称复制", style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, app, nullptr);
            if (!window) throw std::runtime_error("CreateWindow");
            ShowWindow(window, show);
            MSG message;
            int state;
            while ((state = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            result = state == -1 ? 1 : static_cast<int>(message.wParam);
        }
    } catch (...) { showError(nullptr, L"程序运行失败。", E_FAIL); result = 1; }
    CoUninitialize(); return result;
}
