#include "win_common.h"
#include <atomic>
#include <new>

// {E8C82EC2-9808-42C9-9929-A8FF52BD76C1}
static const CLSID CLSID_MultiCopyName = {0xe8c82ec2,0x9808,0x42c9,{0x99,0x29,0xa8,0xff,0x52,0xbd,0x76,0xc1}};
static constexpr auto classId = L"{E8C82EC2-9808-42C9-9929-A8FF52BD76C1}";
static constexpr auto handlerKey = L"Software\\Classes\\AllFilesystemObjects\\shellex\\ContextMenuHandlers\\multi_copyname";
static HMODULE module;
static std::atomic<long> live{0};
static constexpr const char* verbs[] = {"multi_copyname.names", "multi_copyname.stems", "multi_copyname.paths"};
static constexpr const wchar_t* labels[] = {L"复制文件名（每行一个，含扩展名）", L"复制文件名（每行一个，不含扩展名）", L"复制完整路径（每行一个）"};

class Menu final : public IShellExtInit, public IContextMenu {
    std::atomic<ULONG> refs{1};
    std::vector<names::Item> items;
public:
    Menu() { ++live; }
    ~Menu() { --live; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IShellExtInit) *out = static_cast<IShellExtInit*>(this);
        else if (iid == IID_IContextMenu) *out = static_cast<IContextMenu*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Initialize(PCIDLIST_ABSOLUTE, IDataObject* data, HKEY) override {
        items.clear();
        if (!data) return E_INVALIDARG;
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        HRESULT hr = data->GetData(&format, &medium);
        if (FAILED(hr)) return hr;
        try {
            HDROP drop = static_cast<HDROP>(medium.hGlobal);
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            items.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                UINT n = DragQueryFileW(drop, i, nullptr, 0);
                std::vector<wchar_t> path(static_cast<size_t>(n) + 1);
                if (!n || DragQueryFileW(drop, i, path.data(), n + 1) != n) { hr = E_FAIL; break; }
                items.push_back(fileItem(path.data()));
            }
            if (items.empty()) hr = E_INVALIDARG;
        } catch (...) { hr = E_OUTOFMEMORY; }
        ReleaseStgMedium(&medium);
        if (FAILED(hr)) items.clear();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        if ((flags & CMF_DEFAULTONLY) || items.empty() || last < first || last - first < 2) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        HMENU sub = CreatePopupMenu();
        if (!sub) return E_FAIL;
        for (UINT i = 0; i < 3; ++i) {
            if (!AppendMenuW(sub, MF_STRING, first + i, labels[i])) { DestroyMenu(sub); return E_FAIL; }
        }
        if (!InsertMenuW(menu, index, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"multi_copyname · 复制名称")) { DestroyMenu(sub); return E_FAIL; }
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 3);
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(CMINVOKECOMMANDINFO* info) override {
        if (!info) return E_POINTER;
        unsigned command = 3;
        if (IS_INTRESOURCE(info->lpVerb)) command = LOWORD(info->lpVerb);
        else {
            const bool wide = info->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) && (info->fMask & CMIC_MASK_UNICODE);
            const auto* extended = reinterpret_cast<const CMINVOKECOMMANDINFOEX*>(info);
            for (unsigned i = 0; i < 3; ++i) {
                if (wide && extended->lpVerbW && !IS_INTRESOURCE(extended->lpVerbW)) {
                    std::wstring verb(verbs[i], verbs[i] + std::strlen(verbs[i]));
                    if (_wcsicmp(extended->lpVerbW, verb.c_str()) == 0) command = i;
                } else if (_stricmp(info->lpVerb, verbs[i]) == 0) command = i;
            }
        }
        if (command >= 3) return E_INVALIDARG;
        HRESULT hr;
        try { hr = copyNames(items, static_cast<names::Mode>(command), info->hwnd); }
        catch (...) { hr = E_OUTOFMEMORY; }
        if (FAILED(hr) && !(info->fMask & CMIC_MASK_FLAG_NO_UI)) showError(info->hwnd, L"复制失败。请关闭占用剪贴板的程序后重试。", hr);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR id, UINT flags, UINT*, LPSTR output, UINT capacity) override {
        if (id >= 3) return E_INVALIDARG;
        if (flags == GCS_VALIDATEA || flags == GCS_VALIDATEW) return S_OK;
        if (!output || !capacity) return E_INVALIDARG;
        if (flags == GCS_VERBA) { lstrcpynA(output, verbs[id], capacity); return S_OK; }
        if (flags == GCS_VERBW) {
            return MultiByteToWideChar(CP_ACP, 0, verbs[id], -1, reinterpret_cast<LPWSTR>(output), capacity) ? S_OK : E_FAIL;
        }
        if (flags == GCS_HELPTEXTW) { lstrcpynW(reinterpret_cast<LPWSTR>(output), L"全部选中项一次复制；粘贴到 Excel 时每个名称占一行。", capacity); return S_OK; }
        if (flags == GCS_HELPTEXTA) { lstrcpynA(output, "Copy all selected names, one per row.", capacity); return S_OK; }
        return E_NOTIMPL;
    }
};
class Factory final : public IClassFactory {
    std::atomic<ULONG> refs{1};
public:
    Factory() { ++live; }
    ~Factory() { --live; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* object = new(std::nothrow) Menu();
        if (!object) return E_OUTOFMEMORY;
        auto hr = object->QueryInterface(iid, out); object->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++live; else --live; return S_OK; }
};
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return live == 0 ? S_OK : S_FALSE; }
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != CLSID_MultiCopyName) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new(std::nothrow) Factory();
    if (!factory) return E_OUTOFMEMORY;
    auto hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
static LSTATUS writeValue(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY opened;
    auto err = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr);
    if (err != ERROR_SUCCESS) return err;
    err = RegSetValueExW(opened, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(opened); return err;
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    std::wstring cls = std::wstring(L"Software\\Classes\\CLSID\\") + classId;
    auto first = RegDeleteTreeW(HKEY_CURRENT_USER, handlerKey);
    auto second = RegDeleteTreeW(HKEY_CURRENT_USER, cls.c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (first != ERROR_SUCCESS && first != ERROR_FILE_NOT_FOUND) return HRESULT_FROM_WIN32(first);
    if (second != ERROR_SUCCESS && second != ERROR_FILE_NOT_FOUND) return HRESULT_FROM_WIN32(second);
    return S_OK;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    try {
        std::wstring cls = std::wstring(L"Software\\Classes\\CLSID\\") + classId;
        auto err = writeValue(cls, nullptr, L"multi_copyname context menu");
        if (!err) err = writeValue(cls + L"\\InprocServer32", nullptr, modulePath(module));
        if (!err) err = writeValue(cls + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
        if (!err) err = writeValue(handlerKey, nullptr, classId);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return HRESULT_FROM_WIN32(err);
    } catch (...) { return E_OUTOFMEMORY; }
}
