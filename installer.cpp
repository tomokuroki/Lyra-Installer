#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <gdiplus.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class Component { Core, Stdlib, Docs, Examples, Source };
struct PayloadItem { int resourceId; const wchar_t* relativePath; Component component; };

static const PayloadItem kPayload[] = {
#include "generated/payload_manifest.inc"
};

constexpr wchar_t kAppName[] = L"Lyra";
constexpr wchar_t kLyraVersion[] = L"3.0.0";
constexpr wchar_t kInstallerVersion[] = L"1.0.0";
constexpr UINT WM_INSTALL_COMPLETE = WM_APP + 1;

enum ControlId {
    ID_INSTALL_NOW = 100,
    ID_CUSTOMIZE,
    ID_ADD_PATH_HOME,
    ID_BACK,
    ID_BROWSE,
    ID_START_INSTALL,
    ID_INSTALL_DIR,
    ID_STDLIB,
    ID_DOCS,
    ID_EXAMPLES,
    ID_SOURCE,
    ID_SHORTCUT,
    ID_ASSOC,
    ID_ADD_PATH,
    ID_CLOSE,
    ID_OPEN_EXAMPLES,
    ID_PROGRESS,
    ID_STATUS
};

struct Options {
    bool stdlib = true;
    bool docs = true;
    bool examples = true;
    bool source = false;
    bool shortcut = true;
    bool association = true;
    bool addPath = true;
    std::wstring installDir;
};

static HINSTANCE g_instance = nullptr;
static HWND g_window = nullptr;
static HFONT g_font = nullptr;
static HFONT g_titleFont = nullptr;
static HFONT g_buttonFont = nullptr;
static HFONT g_monoFont = nullptr;
static HBRUSH g_whiteBrush = nullptr;
static HBRUSH g_blueBrush = nullptr;
static HBRUSH g_pageBrush = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static Gdiplus::Bitmap* g_logo = nullptr;
static Options g_options;
static std::vector<HWND> g_controls;
static int g_page = 0;
static std::wstring g_installError;

static std::wstring GetKnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &value))) result = value;
    CoTaskMemFree(value);
    return result;
}

static std::wstring DefaultInstallDir() {
    std::wstring local = GetKnownFolder(FOLDERID_LocalAppData);
    return (fs::path(local) / L"Programs" / L"Lyra").wstring();
}

static std::wstring ModulePath() {
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::wstring(buffer.data(), size);
}

static void ClearPage() {
    for (HWND control : g_controls) DestroyWindow(control);
    g_controls.clear();
}

static HWND AddControl(const wchar_t* cls, const wchar_t* text, DWORD style,
                       int x, int y, int width, int height, int id = 0,
                       DWORD exStyle = 0, HFONT font = nullptr) {
    HWND hwnd = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, g_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_instance, nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : g_font), TRUE);
    g_controls.push_back(hwnd);
    return hwnd;
}

static HWND Label(const wchar_t* text, int x, int y, int width, int height,
                  HFONT font = nullptr, DWORD extraStyle = 0) {
    return AddControl(L"STATIC", text, SS_LEFT | extraStyle, x, y, width, height, 0, 0, font);
}

static HWND Button(const wchar_t* text, int x, int y, int width, int height, int id, bool primary = false) {
    DWORD style = WS_TABSTOP | BS_OWNERDRAW;
    if (primary) style |= BS_DEFPUSHBUTTON;
    return AddControl(L"BUTTON", text, style, x, y, width, height, id, 0, g_buttonFont);
}

static HWND CheckBox(const wchar_t* text, int x, int y, int width, int height, int id, bool checked) {
    HWND hwnd = AddControl(L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, x, y, width, height, id);
    SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return hwnd;
}

static bool IsChecked(int id) {
    HWND hwnd = GetDlgItem(g_window, id);
    return hwnd && SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void AddRoundedRectangle(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

static void DrawInstallerButton(const DRAWITEMSTRUCT* item) {
    wchar_t text[256]{};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(std::size(text)));
    bool primary = item->CtlID == ID_INSTALL_NOW || item->CtlID == ID_START_INSTALL || item->CtlID == ID_CLOSE;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;

    Gdiplus::Graphics graphics(item->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::RectF rect(1.0f, 1.0f,
        static_cast<float>(item->rcItem.right - item->rcItem.left - 2),
        static_cast<float>(item->rcItem.bottom - item->rcItem.top - 2));
    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path, rect, 5.0f);

    Gdiplus::Color start;
    Gdiplus::Color end;
    Gdiplus::Color border;
    COLORREF textColor;
    if (disabled) {
        start = Gdiplus::Color(255, 226, 230, 233);
        end = Gdiplus::Color(255, 207, 213, 218);
        border = Gdiplus::Color(255, 180, 188, 195);
        textColor = RGB(120, 127, 133);
    } else if (primary) {
        start = pressed ? Gdiplus::Color(255, 234, 191, 53) : Gdiplus::Color(255, 255, 220, 89);
        end = pressed ? Gdiplus::Color(255, 255, 211, 67) : Gdiplus::Color(255, 239, 198, 62);
        border = Gdiplus::Color(255, 218, 172, 25);
        textColor = RGB(103, 79, 0);
    } else if (g_page == 0) {
        start = pressed ? Gdiplus::Color(255, 51, 113, 173) : Gdiplus::Color(255, 70, 133, 191);
        end = pressed ? Gdiplus::Color(255, 62, 125, 182) : Gdiplus::Color(255, 51, 115, 173);
        border = Gdiplus::Color(255, 92, 157, 211);
        textColor = RGB(255, 255, 255);
    } else {
        start = pressed ? Gdiplus::Color(255, 227, 231, 235) : Gdiplus::Color(255, 255, 255, 255);
        end = pressed ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 227, 231, 235);
        border = Gdiplus::Color(255, 182, 192, 201);
        textColor = RGB(62, 72, 81);
    }
    Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(0, 0), Gdiplus::PointF(0, rect.Height), start, end);
    Gdiplus::Pen outline(border, 1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&outline, &path);

    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, textColor);
    HFONT old = static_cast<HFONT>(SelectObject(item->hDC, g_buttonFont));
    RECT label{2, pressed ? 3 : 2, item->rcItem.right - item->rcItem.left - 2,
               item->rcItem.bottom - item->rcItem.top - 2};
    DrawTextW(item->hDC, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(item->hDC, old);
    if (item->itemState & ODS_FOCUS) {
        RECT focus{5, 5, item->rcItem.right - item->rcItem.left - 5,
                   item->rcItem.bottom - item->rcItem.top - 5};
        DrawFocusRect(item->hDC, &focus);
    }
}

static Gdiplus::Bitmap* LoadLogoBitmap() {
    HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(900), RT_RCDATA);
    if (!resource) return nullptr;
    HGLOBAL loaded = LoadResource(g_instance, resource);
    DWORD size = SizeofResource(g_instance, resource);
    const void* source = LockResource(loaded);
    if (!source || size == 0) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    void* target = GlobalLock(memory);
    memcpy(target, source, size);
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return nullptr;
    }
    Gdiplus::Bitmap* decoded = Gdiplus::Bitmap::FromStream(stream);
    Gdiplus::Bitmap* copy = nullptr;
    if (decoded && decoded->GetLastStatus() == Gdiplus::Ok) {
        copy = new Gdiplus::Bitmap(decoded->GetWidth(), decoded->GetHeight(), PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(copy);
        graphics.DrawImage(decoded, 0, 0, decoded->GetWidth(), decoded->GetHeight());
    }
    delete decoded;
    stream->Release();
    return copy;
}

static void PaintHeader(HDC dc) {
    RECT client{};
    GetClientRect(g_window, &client);
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    Gdiplus::SolidBrush page(Gdiplus::Color(255, 236, 239, 241));
    graphics.FillRectangle(&page, 0, 0, client.right, client.bottom);

    Gdiplus::LinearGradientBrush topGradient(Gdiplus::Point(0, 0), Gdiplus::Point(0, 46),
        Gdiplus::Color(255, 37, 46, 55), Gdiplus::Color(255, 23, 29, 35));
    graphics.FillRectangle(&topGradient, 0, 0, client.right, 46);
    Gdiplus::LinearGradientBrush blueGradient(Gdiplus::Point(0, 46),
        Gdiplus::Point(0, g_page == 0 ? client.bottom : 124),
        Gdiplus::Color(255, 43, 90, 135), Gdiplus::Color(255, 35, 75, 114));
    graphics.FillRectangle(&blueGradient, 0, 46, client.right, g_page == 0 ? client.bottom - 46 : 78);
    if (g_logo) graphics.DrawImage(g_logo, 30, 58, 64, 64);

    SetBkMode(dc, TRANSPARENT);
    HFONT old = static_cast<HFONT>(SelectObject(dc, g_font));
    SetTextColor(dc, RGB(191, 197, 202));
    RECT navLeft{28, 0, 260, 46};
    DrawTextW(dc, L"LYRA INSTALLER", -1, &navLeft, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT navRight{500, 0, client.right - 28, 46};
    DrawTextW(dc, L"INSTALLER 1.0.0   •   LYRA 3.0.0", -1, &navRight, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, g_titleFont);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT brand{106, 55, 310, 102};
    DrawTextW(dc, L"Lyra", -1, &brand, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, g_font);
    SetTextColor(dc, RGB(214, 228, 239));
    RECT tagline{108, 95, 380, 122};
    DrawTextW(dc, L"Music programming language", -1, &tagline, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (g_page == 0) {
        SelectObject(dc, g_titleFont);
        SetTextColor(dc, RGB(255, 211, 67));
        RECT heading{38, 160, 450, 208};
        DrawTextW(dc, L"Music, written as code.", -1, &heading, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, g_font);
        SetTextColor(dc, RGB(237, 244, 250));
        RECT copy{40, 211, 435, 285};
        DrawTextW(dc, L"Install Lyra for notes, tracks, chords, loops and drums\nwith WAV and MIDI output.",
                  -1, &copy, DT_LEFT | DT_WORDBREAK);

        Gdiplus::SolidBrush code(Gdiplus::Color(255, 27, 41, 57));
        Gdiplus::SolidBrush codeTitle(Gdiplus::Color(255, 32, 56, 78));
        graphics.FillRectangle(&code, 500, 162, 280, 194);
        graphics.FillRectangle(&codeTitle, 500, 162, 280, 34);
        SelectObject(dc, g_monoFont);
        SetTextColor(dc, RGB(196, 208, 218));
        RECT codeCaption{514, 162, 765, 196};
        DrawTextW(dc, L"quick-start.cmd", -1, &codeCaption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, RGB(246, 248, 251));
        RECT codeBody{516, 211, 765, 345};
        DrawTextW(dc, L"> lyra --version\nLyra 3.0.0\n\n> lyra init\nCreated main.lyra\n\n> lyra run main.lyra", -1,
                  &codeBody, DT_LEFT | DT_WORDBREAK);
    } else {
        SelectObject(dc, g_buttonFont);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT pageTitle{400, 65, client.right - 30, 111};
        const wchar_t* caption = g_page == 1 ? L"Customize installation" :
            (g_page == 2 ? L"Installing Lyra" : L"Setup complete");
        DrawTextW(dc, caption, -1, &pageTitle, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, old);
}

static void ShowHomePage() {
    ClearPage();
    g_page = 0;
    InvalidateRect(g_window, nullptr, TRUE);
    Button(L"Install now", 40, 314, 250, 50, ID_INSTALL_NOW, true);
    Button(L"Customize", 305, 314, 155, 50, ID_CUSTOMIZE);
    CheckBox(L"Add Lyra to PATH", 42, 386, 300, 30, ID_ADD_PATH_HOME, g_options.addPath);
    Label(g_options.installDir.c_str(), 42, 422, 410, 28, nullptr, SS_PATHELLIPSIS);
    Label(L"Self-contained  •  Per-user  •  No administrator access required",
          42, 486, 600, 26);
}

static void ShowCustomizePage() {
    ClearPage();
    g_page = 1;
    InvalidateRect(g_window, nullptr, TRUE);
    Label(L"Optional features", 34, 150, 320, 30, g_buttonFont);
    HWND stdlib = CheckBox(L"Standard library (recommended)", 42, 187, 335, 27, ID_STDLIB, g_options.stdlib);
    EnableWindow(stdlib, FALSE);
    CheckBox(L"English documentation", 42, 221, 250, 27, ID_DOCS, g_options.docs);
    CheckBox(L"Example projects", 42, 255, 250, 27, ID_EXAMPLES, g_options.examples);
    CheckBox(L"C++ source code", 42, 289, 250, 27, ID_SOURCE, g_options.source);

    Label(L"Windows integration", 422, 150, 310, 30, g_buttonFont);
    CheckBox(L"Add Lyra to PATH", 429, 187, 300, 27, ID_ADD_PATH, g_options.addPath);
    CheckBox(L"Associate .lyra files with Lyra", 429, 221, 320, 27, ID_ASSOC, g_options.association);
    CheckBox(L"Create an examples desktop shortcut", 429, 255, 335, 27, ID_SHORTCUT, g_options.shortcut);

    Label(L"Install location", 34, 336, 250, 27, g_buttonFont);
    AddControl(L"EDIT", g_options.installDir.c_str(), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
               38, 370, 618, 32, ID_INSTALL_DIR, WS_EX_CLIENTEDGE);
    Button(L"Browse…", 669, 369, 105, 34, ID_BROWSE);
    Label(L"Installs for the current user. Administrator access is not required.",
          40, 418, 700, 30);
    Button(L"Back", 38, 474, 120, 42, ID_BACK);
    Button(L"Install", 608, 474, 166, 42, ID_START_INSTALL, true);
}

static bool BrowseForFolder(std::wstring& result) {
    BROWSEINFOW info{};
    info.hwndOwner = g_window;
    info.lpszTitle = L"Choose the Lyra installation folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (!item) return false;
    wchar_t path[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    if (ok) result = path;
    return ok;
}

static bool ComponentEnabled(Component component) {
    switch (component) {
        case Component::Core: return true;
        case Component::Stdlib: return g_options.stdlib;
        case Component::Docs: return g_options.docs;
        case Component::Examples: return g_options.examples;
        case Component::Source: return g_options.source;
    }
    return false;
}

static bool WriteRegistryString(HKEY root, const std::wstring& subkey,
                                const wchar_t* name, const std::wstring& value,
                                DWORD type = REG_SZ) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG status = RegSetValueExW(key, name, 0, type,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

static std::wstring ReadRegistryString(HKEY root, const std::wstring& subkey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || bytes == 0) {
        RegCloseKey(key);
        return {};
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return {};
    }
    RegCloseKey(key);
    return buffer.data();
}

static std::vector<std::wstring> SplitPath(const std::wstring& value) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(L';', start);
        std::wstring part = value.substr(start, end == std::wstring::npos ? end : end - start);
        if (!part.empty()) parts.push_back(part);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return parts;
}

static std::wstring NormalizePath(std::wstring path) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    std::transform(path.begin(), path.end(), path.begin(), towlower);
    return path;
}

static void SetUserPathEntry(const std::wstring& installDir, bool add) {
    const std::wstring envKey = L"Environment";
    std::wstring current = ReadRegistryString(HKEY_CURRENT_USER, envKey, L"Path");
    std::vector<std::wstring> parts = SplitPath(current);
    std::wstring wanted = NormalizePath(installDir);
    parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const std::wstring& item) {
        return NormalizePath(item) == wanted;
    }), parts.end());
    if (add) parts.push_back(installDir);
    std::wstring updated;
    for (const auto& part : parts) {
        if (!updated.empty()) updated += L';';
        updated += part;
    }
    WriteRegistryString(HKEY_CURRENT_USER, envKey, L"Path", updated, REG_EXPAND_SZ);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 3000, nullptr);
}

static bool CreateShortcut(const fs::path& shortcutPath, const fs::path& target,
                           const std::wstring& arguments, const fs::path& workingDir) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&link)))) return false;
    link->SetPath(target.c_str());
    link->SetArguments(arguments.c_str());
    link->SetWorkingDirectory(workingDir.c_str());
    link->SetDescription(L"Lyra example projects");
    IPersistFile* persist = nullptr;
    HRESULT result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (SUCCEEDED(result)) {
        result = persist->Save(shortcutPath.c_str(), TRUE);
        persist->Release();
    }
    link->Release();
    return SUCCEEDED(result);
}

static bool ExtractResourceFile(const PayloadItem& item, const fs::path& destination) {
    HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(item.resourceId), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(g_instance, resource);
    const void* data = LockResource(loaded);
    DWORD size = SizeofResource(g_instance, resource);
    if (!data || size == 0) return false;
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    output.write(static_cast<const char*>(data), size);
    return output.good();
}

static void UpdateProgress(int value, const std::wstring& status) {
    SendDlgItemMessageW(g_window, ID_PROGRESS, PBM_SETPOS, value, 0);
    SetDlgItemTextW(g_window, ID_STATUS, status.c_str());
}

static void RegisterInstallation() {
    const std::wstring dir = g_options.installDir;
    const std::wstring exe = (fs::path(dir) / L"lyra.exe").wstring();
    const std::wstring uninstall = (fs::path(dir) / L"uninstall.exe").wstring();
    WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Lyra", L"InstallPath", dir);
    WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Lyra", L"Version", kLyraVersion);
    WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Lyra", L"InstallerVersion", kInstallerVersion);

    const std::wstring uninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Lyra";
    WriteRegistryString(HKEY_CURRENT_USER, uninstallKey, L"DisplayName", L"Lyra 3.0.0");
    WriteRegistryString(HKEY_CURRENT_USER, uninstallKey, L"DisplayVersion", kLyraVersion);
    WriteRegistryString(HKEY_CURRENT_USER, uninstallKey, L"Publisher", L"Lyra Community");
    WriteRegistryString(HKEY_CURRENT_USER, uninstallKey, L"DisplayIcon", exe);
    WriteRegistryString(HKEY_CURRENT_USER, uninstallKey, L"UninstallString", L"\"" + uninstall + L"\" --uninstall");
    WriteRegistryString(HKEY_CURRENT_USER, uninstallKey, L"InstallLocation", dir);
    WriteRegistryString(HKEY_CURRENT_USER, uninstallKey, L"URLInfoAbout", L"https://github.com/tomokuroki/lyra");

    if (g_options.addPath) SetUserPathEntry(dir, true);
    if (g_options.stdlib) {
        WriteRegistryString(HKEY_CURRENT_USER, L"Environment", L"LYRA_STDLIB",
                            (fs::path(dir) / L"stdlib").wstring(), REG_EXPAND_SZ);
    }

    if (g_options.association) {
        WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\.lyra", nullptr, L"Lyra.Source");
        WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\Lyra.Source", nullptr, L"Lyra source file");
        WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\Lyra.Source\\DefaultIcon", nullptr, exe + L",0");
        WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\Lyra.Source\\shell\\open\\command", nullptr,
                            L"\"" + exe + L"\" \"%1\"");
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    if (g_options.shortcut && g_options.examples) {
        fs::path desktop = GetKnownFolder(FOLDERID_Desktop);
        CreateShortcut(desktop / L"Lyra Examples.lnk", fs::path(L"explorer.exe"),
                       L"\"" + (fs::path(dir) / L"examples").wstring() + L"\"", fs::path(dir));
    }
}

static DWORD WINAPI InstallThread(void*) {
    try {
        fs::path root = g_options.installDir;
        std::error_code ec;
        fs::create_directories(root, ec);
        if (ec) throw std::runtime_error("cannot create install directory");

        size_t total = 1;
        for (const auto& item : kPayload) if (ComponentEnabled(item.component)) ++total;
        size_t current = 0;
        for (const auto& item : kPayload) {
            if (!ComponentEnabled(item.component)) continue;
            std::wstring status = L"Installing: ";
            status += item.relativePath;
            UpdateProgress(static_cast<int>((current * 88) / total), status);
            if (!ExtractResourceFile(item, root / item.relativePath))
                throw std::runtime_error("resource extraction failed");
            ++current;
        }

        UpdateProgress(91, L"Registering with Windows…");
        if (!CopyFileW(ModulePath().c_str(), (root / L"uninstall.exe").c_str(), FALSE))
            throw std::runtime_error("cannot create uninstaller");
        RegisterInstallation();
        UpdateProgress(100, L"Installation complete.");
    } catch (...) {
        g_installError = L"Setup could not complete. Check access to the selected installation folder.";
    }
    PostMessageW(g_window, WM_INSTALL_COMPLETE, 0, 0);
    return 0;
}

static void ShowProgressPage() {
    ClearPage();
    g_page = 2;
    InvalidateRect(g_window, nullptr, TRUE);
    Label(L"Please wait while Setup installs Lyra on your computer.", 42, 158, 720, 36);
    AddControl(PROGRESS_CLASSW, L"", PBS_SMOOTH, 42, 214, 732, 30, ID_PROGRESS);
    SendDlgItemMessageW(g_window, ID_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    AddControl(L"STATIC", L"Preparing…", SS_LEFT, 42, 260, 732, 60, ID_STATUS);
}

static void ShowCompletionPage() {
    ClearPage();
    g_page = 3;
    InvalidateRect(g_window, nullptr, TRUE);
    Label(L"Lyra was installed successfully", 42, 158, 700, 45, g_titleFont);
    Label(L"Open a new terminal and run  lyra --version.\n"
          L"Create your first project with  lyra init.", 44, 220, 700, 75);
    if (g_options.examples) Button(L"Open examples", 42, 330, 210, 44, ID_OPEN_EXAMPLES);
    Button(L"Close", 604, 474, 170, 44, ID_CLOSE, true);
}

static void SaveCustomOptions() {
    wchar_t dir[32768]{};
    GetDlgItemTextW(g_window, ID_INSTALL_DIR, dir, static_cast<int>(std::size(dir)));
    g_options.installDir = dir;
    g_options.docs = IsChecked(ID_DOCS);
    g_options.examples = IsChecked(ID_EXAMPLES);
    g_options.source = IsChecked(ID_SOURCE);
    g_options.shortcut = IsChecked(ID_SHORTCUT);
    g_options.association = IsChecked(ID_ASSOC);
    g_options.addPath = IsChecked(ID_ADD_PATH);
}

static void BeginInstall() {
    if (g_options.installDir.empty() || !fs::path(g_options.installDir).is_absolute()) {
        MessageBoxW(g_window, L"Enter a complete path to the installation folder.", L"Lyra Setup", MB_OK | MB_ICONWARNING);
        return;
    }
    g_installError.clear();
    ShowProgressPage();
    HANDLE thread = CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

static void RemoveRegistryAndShell(const std::wstring& installDir) {
    SetUserPathEntry(installDir, false);
    std::wstring stdlib = ReadRegistryString(HKEY_CURRENT_USER, L"Environment", L"LYRA_STDLIB");
    HKEY env = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &env) == ERROR_SUCCESS) {
        if (NormalizePath(stdlib) == NormalizePath((fs::path(installDir) / L"stdlib").wstring()))
            RegDeleteValueW(env, L"LYRA_STDLIB");
        RegCloseKey(env);
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Lyra");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Lyra");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Lyra.Source");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.lyra");
    std::error_code ec;
    fs::remove(fs::path(GetKnownFolder(FOLDERID_Desktop)) / L"Lyra Examples.lnk", ec);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

static int CleanupMode(const std::wstring& target, DWORD parentPid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (process) {
        WaitForSingleObject(process, 30000);
        CloseHandle(process);
    }
    std::error_code ec;
    fs::remove_all(target, ec);
    MoveFileExW(ModulePath().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return ec ? 1 : 0;
}

static int ExtractOnlyMode(const std::wstring& target) {
    g_options.stdlib = true;
    g_options.docs = true;
    g_options.examples = true;
    g_options.source = true;
    for (const auto& item : kPayload) {
        if (!ExtractResourceFile(item, fs::path(target) / item.relativePath)) return 1;
    }
    return 0;
}

static int UninstallMode() {
    std::wstring installDir = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Lyra", L"InstallPath");
    if (installDir.empty()) installDir = fs::path(ModulePath()).parent_path().wstring();
    if (MessageBoxW(nullptr, L"Remove Lyra and all installed components from this computer?", L"Uninstall Lyra",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
    RemoveRegistryAndShell(installDir);

    fs::path helper = fs::path(GetKnownFolder(FOLDERID_LocalAppData)) / L"Temp" /
                      (L"lyra-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe");
    if (!CopyFileW(ModulePath().c_str(), helper.c_str(), FALSE)) {
        MessageBoxW(nullptr, L"Setup could not start the file cleanup process.", L"Lyra", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::wstring args = L"--cleanup \"" + installDir + L"\" " + std::to_wstring(GetCurrentProcessId());
    SHELLEXECUTEINFOW run{sizeof(run)};
    run.lpFile = helper.c_str();
    run.lpParameters = args.c_str();
    run.nShow = SW_HIDE;
    if (!ShellExecuteExW(&run)) return 1;
    MessageBoxW(nullptr, L"Lyra has been removed.", L"Lyra", MB_OK | MB_ICONINFORMATION);
    return 0;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_INSTALL_NOW) {
                g_options.addPath = IsChecked(ID_ADD_PATH_HOME);
                BeginInstall();
            } else if (id == ID_CUSTOMIZE) {
                g_options.addPath = IsChecked(ID_ADD_PATH_HOME);
                ShowCustomizePage();
            } else if (id == ID_BACK) {
                SaveCustomOptions();
                ShowHomePage();
            } else if (id == ID_BROWSE) {
                std::wstring folder;
                if (BrowseForFolder(folder)) SetDlgItemTextW(g_window, ID_INSTALL_DIR, folder.c_str());
            } else if (id == ID_START_INSTALL) {
                SaveCustomOptions();
                BeginInstall();
            } else if (id == ID_OPEN_EXAMPLES) {
                ShellExecuteW(hwnd, L"open", (fs::path(g_options.installDir) / L"examples").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (id == ID_CLOSE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_INSTALL_COMPLETE:
            if (!g_installError.empty()) {
                MessageBoxW(hwnd, g_installError.c_str(), L"Setup error", MB_OK | MB_ICONERROR);
                ShowCustomizePage();
            } else {
                ShowCompletionPage();
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            if (g_page == 0) {
                SetTextColor(dc, RGB(237, 244, 250));
                return reinterpret_cast<LRESULT>(g_blueBrush);
            }
            SetTextColor(dc, RGB(51, 56, 62));
            return reinterpret_cast<LRESULT>(g_pageBrush);
        }
        case WM_DRAWITEM:
            DrawInstallerButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            PaintHeader(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_CLOSE:
            if (g_page == 2) {
                MessageBoxW(hwnd, L"Please wait for the installation to finish.", L"Lyra Setup", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    g_instance = instance;
    std::wstring args = commandLine ? commandLine : L"";
    if (args.rfind(L"--cleanup", 0) == 0) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        int result = argc >= 4 ? CleanupMode(argv[2], wcstoul(argv[3], nullptr, 10)) : 1;
        LocalFree(argv);
        return result;
    }
    if (args.rfind(L"--extract-only", 0) == 0) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        int result = argc >= 3 ? ExtractOnlyMode(argv[2]) : 1;
        LocalFree(argv);
        return result;
    }
    if (args.find(L"--uninstall") != std::wstring::npos) return UninstallMode();

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);
    g_logo = LoadLogoBitmap();
    g_options.installDir = DefaultInstallDir();

    g_font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(-31, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_buttonFont = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_monoFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    g_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    g_blueBrush = CreateSolidBrush(RGB(38, 79, 119));
    g_pageBrush = CreateSolidBrush(RGB(236, 239, 241));

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = g_pageBrush;
    wc.lpszClassName = L"LyraInstallerWindow";
    RegisterClassExW(&wc);

    RECT rect{0, 0, 820, 540};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_window = CreateWindowExW(0, wc.lpszClassName, L"Lyra 3.0.0 Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height, nullptr, nullptr, instance, nullptr);
    ShowHomePage();
    ShowWindow(g_window, show);
    UpdateWindow(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    DeleteObject(g_font);
    DeleteObject(g_titleFont);
    DeleteObject(g_buttonFont);
    DeleteObject(g_monoFont);
    DeleteObject(g_whiteBrush);
    DeleteObject(g_blueBrush);
    DeleteObject(g_pageBrush);
    delete g_logo;
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
