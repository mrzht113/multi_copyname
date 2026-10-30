#include "win_common.h"
#include <iostream>
#include <atomic>
#include <stdexcept>
static void check(bool success, const char* name) { if (!success) throw std::runtime_error(name); }
// Explorer supplies a single CF_HDROP data object for the entire selection.
class DropData final : public IDataObject {
    std::atomic<ULONG> refs{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (format->cfFormat != CF_HDROP || !(format->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
        const wchar_t paths[] = L"C:\\测试\\001.txt\0C:\\测试\\程序 V1.2.bin\0C:\\测试\\=1+1.txt\0";
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + sizeof(paths));
        if (!mem) return E_OUTOFMEMORY;
        auto* data = static_cast<DROPFILES*>(GlobalLock(mem));
        if (!data) { GlobalFree(mem); return E_OUTOFMEMORY; }
        data->pFiles = sizeof(DROPFILES); data->fWide = TRUE;
        std::memcpy(reinterpret_cast<BYTE*>(data) + sizeof(DROPFILES), paths, sizeof(paths));
        GlobalUnlock(mem);
        medium->tymed = TYMED_HGLOBAL; medium->hGlobal = mem; medium->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override { return f->cfFormat == CF_HDROP ? S_OK : DV_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};
int main() {
    try {
        check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
        HMODULE library = LoadLibraryW((parentPath(modulePath()) + L"\\multi_copyname_shell.dll").c_str());
        check(library != nullptr, "load DLL");
        auto getClass = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(library, "DllGetClassObject"));
        auto unload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(library, "DllCanUnloadNow"));
        check(getClass && unload && unload() == S_OK, "exports and initial refcount");
        CLSID id; CLSIDFromString(L"{E8C82EC2-9808-42C9-9929-A8FF52BD76C1}", &id);
        IClassFactory* factory = nullptr;
        check(SUCCEEDED(getClass(id, IID_PPV_ARGS(&factory))), "class factory");
        IShellExtInit* init = nullptr;
        check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&init))), "create shell extension");
        factory->Release();
        auto* data = new DropData;
        check(SUCCEEDED(init->Initialize(nullptr, data, nullptr)), "initialize three selected files"); data->Release();
        IContextMenu* menu = nullptr;
        check(SUCCEEDED(init->QueryInterface(IID_PPV_ARGS(&menu))), "query context menu"); init->Release();
        HMENU nativeMenu = CreatePopupMenu();
        auto result = menu->QueryContextMenu(nativeMenu, 0, 10, 12, CMF_NORMAL);
        check(SUCCEEDED(result) && HRESULT_CODE(result) == 3, "three command IDs");
        check(GetMenuItemCount(GetSubMenu(nativeMenu, 0)) == 3, "submenu entries");
        const wchar_t* expected[] = { L"001.txt\r\n程序 V1.2.bin\r\n=1+1.txt", L"001\r\n程序 V1.2\r\n=1+1", L"C:\\测试\\001.txt\r\nC:\\测试\\程序 V1.2.bin\r\nC:\\测试\\=1+1.txt" };
        for (unsigned i = 0; i < 3; ++i) {
            CMINVOKECOMMANDINFO invocation{}; invocation.cbSize = sizeof(invocation);
            invocation.lpVerb = MAKEINTRESOURCEA(i); invocation.fMask = CMIC_MASK_FLAG_NO_UI;
            check(SUCCEEDED(menu->InvokeCommand(&invocation)), "invoke clipboard command");
            check(OpenClipboard(nullptr) != FALSE, "read clipboard");
            auto mem = GetClipboardData(CF_UNICODETEXT);
            auto text = static_cast<const wchar_t*>(GlobalLock(mem));
            bool matches = text && std::wstring(text) == expected[i];
            if (text) GlobalUnlock(mem);
            bool hasHTML = IsClipboardFormatAvailable(RegisterClipboardFormatW(L"HTML Format")) != FALSE;
            CloseClipboard();
            check(matches, "clipboard preserves all three names"); check(hasHTML, "Excel HTML format present");
        }
        CMINVOKECOMMANDINFO canonical{}; canonical.cbSize = sizeof(canonical); canonical.lpVerb = "multi_copyname.names";
        check(SUCCEEDED(menu->InvokeCommand(&canonical)), "canonical verb");
        CMINVOKECOMMANDINFOEX unicode{}; unicode.cbSize = sizeof(unicode);
        unicode.fMask = CMIC_MASK_UNICODE; unicode.lpVerbW = L"multi_copyname.stems";
        check(SUCCEEDED(menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&unicode))), "Unicode canonical verb");
        canonical.lpVerb = "unknown.command";
        check(menu->InvokeCommand(&canonical) == E_INVALIDARG, "reject unknown command");
        DestroyMenu(nativeMenu); menu->Release();
        check(unload() == S_OK, "COM objects released");
        FreeLibrary(library); CoUninitialize();
        std::cout << "Windows COM / multi-selection / clipboard integration passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
