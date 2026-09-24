// A.C.O.C. 点名系统配置器 —— 安装程序
// C++17 / Win32，安装到当前用户目录，不需要管理员权限。
// 同一个 exe 既是安装程序也是卸载程序（带 /uninstall 或名字为 uninstall.exe）。
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cwctype>
#include <string>
#include <vector>

namespace {

// 资源里的载荷：配置器本体
constexpr int kResApp = 3;

const wchar_t* kAppName = L"A.C.O.C. 点名系统配置器";
const wchar_t* kVersion = L"0.2.1";
const wchar_t* kPublisher = L"A.C.O.C.";
const wchar_t* kExeName = L"ACOCConfigurator.exe";
const wchar_t* kUninstName = L"uninstall.exe";
const wchar_t* kRegKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ACOCConfigurator";
const wchar_t* kShortcutName = L"A.C.O.C. 点名系统配置器";

// 控件 id
enum {
    kPathEdit = 100,
    kBrowse,
    kDesktopChk,
    kStartChk,
    kInstallBtn,
    kCancelBtn,
    kRunChk,
    kProgress,
    kStatus,
    kInfoText,
    kPathLabel,
};

HINSTANCE g_inst;
HWND g_main, g_pathEdit, g_browse, g_deskChk, g_startChk, g_installBtn;
HWND g_cancelBtn, g_runChk, g_progress, g_status, g_info;
HFONT g_fTitle, g_fBody, g_fBold, g_fSmall;
HBRUSH g_bg, g_band;
std::wstring g_installDir;
bool g_uninstallMode = false;

// 页面编号：0 选项、1 进行中、2 完成、3 确认覆盖
enum { kPageOpt = 0, kPageBusy = 1, kPageDone = 2, kPageConfirm = 3 };
int g_page = kPageOpt;

// 客户区尺寸。比原来宽一圈，字小了以后留出余量，不至于挤在一起
constexpr int kWinW = 600, kWinH = 364;
constexpr int kBandH = 76;      // 顶部标题带高度
constexpr int kPadX = 32;       // 左右留白

// ---------- 动效状态 ----------

int g_hoverBtn = 0;        // 鼠标停在哪个按钮上（控件 id）
int g_progressPos = 0;     // 进度条显示值，用计时器缓动到这个数
int g_progressTarget = 0;
double g_fadeT = 1.0;      // 完成页文字滑入的进度 0→1

// 切页时把内容从右边推进来
struct SlideItem {
    HWND w;
    RECT home;   // 原位置
};
std::vector<SlideItem> g_slide;
double g_slideT = 1.0;

// 确认覆盖页要显示的文案，由 OnInstallClicked 填好
std::wstring g_confirmHead, g_confirmBody;

constexpr UINT_PTR kTimerProgress = 1;
constexpr UINT_PTR kTimerFade = 2;
constexpr UINT_PTR kTimerSlide = 3;
constexpr UINT kProgressMs = 350;
constexpr UINT kFadeMs = 200;
constexpr UINT kSlideMs = 170;
constexpr int kSlideDx = 16;    // 滑入的起始偏移
constexpr ULONGLONG kMinInstallMs = 2000;  // 安装过程至少走这么久

const COLORREF kInk = RGB(0x22, 0x25, 0x2e);
const COLORREF kMuted = RGB(0x74, 0x7a, 0x89);
const COLORREF kAccent = RGB(0x18, 0x24, 0xfc);
const COLORREF kLine = RGB(0xdd, 0xe1, 0xec);
const COLORREF kBand = RGB(0xf6, 0xf7, 0xfb);

std::wstring W(const char* s) {
    if (!s || !*s) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}

int S(int v) {
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return MulDiv(v, dpi, 96);
}

HFONT MakeFont(int pt, bool bold) {
    return CreateFontW(-MulDiv(pt, S(96) / 96, 72), 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}

void Fill(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void Text(HDC dc, const std::wstring& s, RECT r, COLORREF c, HFONT f, UINT fmt) {
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt);
    SelectObject(dc, of);
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\') return a + b;
    return a + L"\\" + b;
}

bool Exists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void SetStatus(const char* utf8) { SetWindowTextW(g_status, W(utf8).c_str()); }

// ---------- 文件操作 ----------

bool WritePayload(const std::wstring& dest, std::string& err) {
    HRSRC hr = FindResourceW(g_inst, MAKEINTRESOURCEW(kResApp), RT_RCDATA);
    if (!hr) {
        err = "安装包里没有找到程序数据，文件可能已损坏。";
        return false;
    }
    DWORD size = SizeofResource(g_inst, hr);
    HGLOBAL hg = LoadResource(g_inst, hr);
    const void* data = hg ? LockResource(hg) : nullptr;
    if (!data || !size) {
        err = "安装包里的程序数据读不出来。";
        return false;
    }
    HANDLE f = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = "写不进目标目录。如果点名器正开着，请先关掉再装。";
        return false;
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, data, size, &wrote, nullptr);
    CloseHandle(f);
    if (!ok || wrote != size) {
        err = "写入过程中出错了。";
        return false;
    }
    return true;
}

void RemoveDirRecursive(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring p = Join(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirRecursive(p);
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

bool MakeDirTree(const std::wstring& dir) {
    if (Exists(dir)) return true;
    std::wstring cur;
    for (size_t i = 0; i <= dir.size(); i++) {
        if (i == dir.size() || dir[i] == L'\\') {
            if (!cur.empty() && cur.back() != L':') {
                if (!Exists(cur) && !CreateDirectoryW(cur.c_str(), nullptr) &&
                    GetLastError() != ERROR_ALREADY_EXISTS)
                    return false;
            }
        }
        if (i < dir.size()) cur += dir[i];
    }
    return true;
}

// ---------- 快捷方式 ----------

bool MakeShortcut(const std::wstring& lnkPath, const std::wstring& target,
                  const std::wstring& workDir) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&link))))
        return false;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(workDir.c_str());
    link->SetDescription(kAppName);
    link->SetIconLocation(target.c_str(), 0);

    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                       reinterpret_cast<void**>(&pf)))) {
        ok = SUCCEEDED(pf->Save(lnkPath.c_str(), TRUE));
        pf->Release();
    }
    link->Release();
    return ok;
}

bool FolderPath(int csidl, std::wstring& out) {
    wchar_t buf[MAX_PATH] = L"";
    if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf)))
        return false;
    out = buf;
    return !out.empty();
}

// ---------- 注册表 ----------

void WriteUninstallEntry(const std::wstring& dir, DWORD sizeKb) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    auto str = [&](const wchar_t* name, const std::wstring& v) {
        RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                       static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    };
    auto num = [&](const wchar_t* name, DWORD v) {
        RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v),
                       sizeof(v));
    };
    str(L"DisplayName", kAppName);
    str(L"DisplayVersion", kVersion);
    str(L"Publisher", kPublisher);
    str(L"InstallLocation", dir);
    str(L"DisplayIcon", Join(dir, kExeName));
    str(L"UninstallString", L"\"" + Join(dir, kUninstName) + L"\" /uninstall");
    str(L"QuietUninstallString",
        L"\"" + Join(dir, kUninstName) + L"\" /uninstall /quiet");
    num(L"NoModify", 1);
    num(L"NoRepair", 1);
    num(L"EstimatedSize", sizeKb);
    RegCloseKey(k);
}

// 读回已安装的版本与位置。返回 false 表示注册表里没有这台机器的安装记录。
bool InstalledVersion(std::wstring& ver, std::wstring& loc) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &k) !=
        ERROR_SUCCESS)
        return false;
    auto rd = [&](const wchar_t* name, std::wstring& out) {
        wchar_t buf[MAX_PATH] = L"";
        DWORD cb = sizeof(buf), type = 0;
        if (RegQueryValueExW(k, name, nullptr, &type,
                             reinterpret_cast<BYTE*>(buf), &cb) == ERROR_SUCCESS &&
            type == REG_SZ)
            out = buf;
    };
    rd(L"DisplayVersion", ver);
    rd(L"InstallLocation", loc);
    RegCloseKey(k);
    // v0.1 出厂时资源里的版本号写的是占位的 1.0.0，读回来直接显示会
    // 和“初版是 v0.1”对不上，这里归一化一下
    if (ver == L"1.0.0" || ver == L"1.0.0.0") ver = L"0.1";
    return true;
}

// ---------- 安装 / 卸载 ----------

bool DoInstall(std::string& err) {
    SetStatus("正在创建目录…");
    if (!MakeDirTree(g_installDir)) {
        err = "创建安装目录失败，换一个位置试试。";
        return false;
    }

    SetStatus("正在写入程序…");
    const std::wstring exe = Join(g_installDir, kExeName);
    if (!WritePayload(exe, err)) return false;

    // 把安装程序自己复制成卸载程序
    SetStatus("正在准备卸载程序…");
    wchar_t self[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    CopyFileW(self, Join(g_installDir, kUninstName).c_str(), FALSE);

    SetStatus("正在创建快捷方式…");
    if (SendMessageW(g_deskChk, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        std::wstring desk;
        if (FolderPath(CSIDL_DESKTOPDIRECTORY, desk))
            MakeShortcut(Join(desk, std::wstring(kShortcutName) + L".lnk"), exe,
                         g_installDir);
    }
    if (SendMessageW(g_startChk, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        std::wstring prog;
        if (FolderPath(CSIDL_PROGRAMS, prog)) {
            std::wstring group = Join(prog, kShortcutName);
            MakeDirTree(group);
            MakeShortcut(Join(group, std::wstring(kShortcutName) + L".lnk"), exe,
                         g_installDir);
        }
    }

    SetStatus("正在登记卸载信息…");
    WIN32_FILE_ATTRIBUTE_DATA fad;
    DWORD kb = 0;
    if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fad))
        kb = (fad.nFileSizeHigh * (MAXDWORD / 1024) + fad.nFileSizeLow / 1024) + 1;
    WriteUninstallEntry(g_installDir, kb);

    return true;
}

bool g_pendingCleanup = false;

// 卸载要连正在运行的自己一起删掉，所以退出前把这件事交给一个临时副本
void SpawnCleanup() {
    wchar_t self[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t tmp[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dst = std::wstring(tmp) + L"acoc_uninst_" +
                       std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (!CopyFileW(self, dst.c_str(), FALSE)) return;
    std::wstring cmd = L"\"" + dst + L"\" /cleanup \"" + g_installDir + L"\" " +
                       std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    // 工作目录必须离开安装目录：Windows 不允许删掉仍是某进程当前目录的文件夹
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, tmp, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// 临时代理：等原卸载程序退出后清空安装目录，最后再删掉自己
void RunSelfCleanup(const std::wstring& dir, DWORD parentPid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (h) {
        WaitForSingleObject(h, 30000);
        CloseHandle(h);
    }
    // 文件可能刚被父进程放开，重试几轮再放弃
    for (int i = 0; i < 20 && Exists(dir); i++) {
        RemoveDirRecursive(dir);
        if (Exists(dir)) Sleep(300);
    }
    wchar_t self[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    // 计划在重启时删除，管理员权限下立刻生效
    MoveFileExW(self, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    // 普通用户下上面那条会被拒，所以再挂一个隐藏的 cmd：等本进程退出后删掉自己
    std::wstring del = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"";
    del += self;
    del += L"\"";
    std::vector<wchar_t> db(del.begin(), del.end());
    db.push_back(0);
    STARTUPINFOW dsi = {};
    dsi.cb = sizeof(dsi);
    PROCESS_INFORMATION dpi = {};
    if (CreateProcessW(nullptr, db.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &dsi, &dpi)) {
        CloseHandle(dpi.hProcess);
        CloseHandle(dpi.hThread);
    }
}

bool DoUninstall(std::string& err) {
    wchar_t self[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring& dir = g_installDir;
    if (dir.empty()) {
        err = "找不到安装目录。";
        return false;
    }

    SetStatus("正在删除快捷方式…");
    std::wstring desk, prog;
    if (FolderPath(CSIDL_DESKTOPDIRECTORY, desk))
        DeleteFileW(Join(desk, std::wstring(kShortcutName) + L".lnk").c_str());
    if (FolderPath(CSIDL_PROGRAMS, prog)) {
        std::wstring group = Join(prog, kShortcutName);
        DeleteFileW(Join(group, std::wstring(kShortcutName) + L".lnk").c_str());
        RemoveDirectoryW(group.c_str());
    }

    SetStatus("正在清理注册表…");
    RegDeleteKeyW(HKEY_CURRENT_USER, kRegKey);

    SetStatus("正在删除程序文件…");
    // 先删掉除自己以外的文件，剩下的交给临时副本
    WIN32_FIND_DATAW fd;
    HANDLE fh = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
    if (fh != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring f = Join(dir, fd.cFileName);
            if (_wcsicmp(f.c_str(), self) == 0) continue;
            SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(f.c_str());
        } while (FindNextFileW(fh, &fd));
        FindClose(fh);
    }

    // 自己还占着 uninstall.exe，真正的清理由退出前的临时副本完成
    g_pendingCleanup = true;
    return true;
}

// ---------- 界面 ----------

HWND Mk(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
        int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x),
                             S(y), S(w), S(h), g_main, (HMENU)(INT_PTR)id, g_inst,
                             nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    return c;
}

// 进度条不直接跳，用计时器缓动过去
void SetProgress(int pos) {
    g_progressTarget = pos;
    if (g_progressPos == pos) return;
    if (g_main) SetTimer(g_main, kTimerProgress, 15, nullptr);
}

// 让当前可见的子控件从右边 kSlideDx 处滑回原位。
// 两个按钮不动——它们每页都在，跟着滑反而像界面整体在抖。
void BeginSlide() {
    g_slide.clear();
    if (!g_main) return;
    for (HWND c = GetWindow(g_main, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c)) continue;
        const int id = GetDlgCtrlID(c);
        if (id == kInstallBtn || id == kCancelBtn) continue;
        RECT r;
        GetWindowRect(c, &r);
        POINT p = {r.left, r.top};
        ScreenToClient(g_main, &p);
        SlideItem it;
        it.w = c;
        it.home = {p.x, p.y, p.x + (r.right - r.left), p.y + (r.bottom - r.top)};
        g_slide.push_back(it);
    }
    if (g_slide.empty()) return;
    g_slideT = 0.0;
    for (const auto& it : g_slide)
        MoveWindow(it.w, it.home.left + S(kSlideDx), it.home.top,
                   it.home.right - it.home.left, it.home.bottom - it.home.top, TRUE);
    SetTimer(g_main, kTimerSlide, 15, nullptr);
}

void SetPage(int page) {
    g_page = page;
    // 完成页和确认页的文字从下方滑入
    const bool sliding = (page == kPageDone || page == kPageConfirm);
    g_fadeT = sliding ? 0.0 : 1.0;
    if (sliding && g_main) SetTimer(g_main, kTimerFade, 15, nullptr);

    const bool opt = (page == kPageOpt);
    // 卸载时没有安装位置和快捷方式可挑，所以这些控件只在安装流程里露脸
    const bool installOpts = opt && !g_uninstallMode;
    auto show = [&](int id, bool v) {
        HWND c = GetDlgItem(g_main, id);
        if (c) ShowWindow(c, v ? SW_SHOW : SW_HIDE);
    };
    show(kPathLabel, installOpts);
    show(kPathEdit, installOpts);
    show(kBrowse, installOpts);
    show(kDesktopChk, installOpts);
    show(kStartChk, installOpts);
    show(kInfoText, opt);
    // 确认页和进行中/完成页的文字都画在背景上，这几组控件都不该露脸
    show(kProgress, page == kPageBusy);
    show(kStatus, page == kPageBusy);
    show(kRunChk, page == kPageDone);

    SetWindowTextW(g_installBtn, page == kPageDone   ? L"完成"
                                : page == kPageConfirm ? L"覆盖安装"
                                : g_uninstallMode      ? L"卸载"
                                                       : L"安装");
    SetWindowTextW(g_cancelBtn, page == kPageConfirm ? L"返回" : L"取消");
    InvalidateRect(g_main, nullptr, FALSE);
    BeginSlide();
}

// 给按钮挂悬停跟踪，原窗口过程存在 GWLP_USERDATA 里
LRESULT CALLBACK HoverProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    WNDPROC old = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(h, GWLP_USERDATA));
    int id = GetDlgCtrlID(h);
    if (m == WM_MOUSEMOVE) {
        if (g_hoverBtn != id) {
            g_hoverBtn = id;
            InvalidateRect(h, nullptr, TRUE);
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
    } else if (m == WM_MOUSELEAVE) {
        if (g_hoverBtn == id) {
            g_hoverBtn = 0;
            InvalidateRect(h, nullptr, TRUE);
        }
    }
    return CallWindowProcW(old, h, m, w, l);
}

HWND Hoverable(HWND h) {
    WNDPROC old = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HoverProc)));
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(old));
    return h;
}

void BrowseForFolder() {
    BROWSEINFOW bi = {};
    bi.hwndOwner = g_main;
    bi.lpszTitle = L"选择安装位置";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (!idl) return;
    wchar_t buf[MAX_PATH] = L"";
    if (SHGetPathFromIDListW(idl, buf)) {
        SetWindowTextW(g_pathEdit, buf);
        g_installDir = buf;
    }
    CoTaskMemFree(idl);
}

// 安装动作本身几十毫秒就完了，进度条一闪而过，看着像什么都没干。
// 这里把消息泵着，边等边推进度，保证整个过程至少 kMinInstallMs。
void RunInstallWithFloor(bool& ok, std::string& err) {
    const ULONGLONG t0 = GetTickCount64();
    ok = g_uninstallMode ? DoUninstall(err) : DoInstall(err);

    // 这段完全由时间驱动，缓动计时器会跟这里抢进度条，先停掉
    KillTimer(g_main, kTimerProgress);
    MSG msg;
    for (;;) {
        const ULONGLONG el = GetTickCount64() - t0;
        if (el >= kMinInstallMs) break;
        // 不泵消息的话窗口会假死，切页滑动之类的动画也都停了
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            if (!IsDialogMessageW(g_main, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        int pct = 8 + static_cast<int>(84 * el / kMinInstallMs);
        if (pct > 92) pct = 92;
        g_progressPos = pct;
        g_progressTarget = pct;
        SendMessageW(g_progress, PBM_SETPOS, pct, 0);
        Sleep(15);
    }
    if (ok)
        SetStatus(g_uninstallMode ? "卸载完成。" : "安装完成。");
}

void StartInstall() {
    EnableWindow(g_installBtn, FALSE);
    EnableWindow(g_cancelBtn, FALSE);
    SetPage(kPageBusy);
    g_progressPos = 0;
    g_progressTarget = 0;
    KillTimer(g_main, kTimerProgress);
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    UpdateWindow(g_main);

    std::string err;
    bool ok = false;
    RunInstallWithFloor(ok, err);
    SetProgress(ok ? 100 : 20);
    // 让进度条把最后一段走完，别在 85 上戛然而止
    for (int i = 0; i < 12; i++) {
        MSG m2;
        while (PeekMessageW(&m2, nullptr, 0, 0, PM_REMOVE)) {
            if (m2.message == WM_QUIT) return;
            if (!IsDialogMessageW(g_main, &m2)) {
                TranslateMessage(&m2);
                DispatchMessageW(&m2);
            }
        }
        Sleep(15);
    }

    if (!ok) {
        MessageBoxW(g_main, W(err.c_str()).c_str(),
                    g_uninstallMode ? L"卸载未完成" : L"安装未完成",
                    MB_OK | MB_ICONWARNING);
        EnableWindow(g_installBtn, TRUE);
        EnableWindow(g_cancelBtn, TRUE);
        SetPage(kPageOpt);
        return;
    }

    if (g_uninstallMode) {
        MessageBoxW(g_main, L"A.C.O.C. 点名系统配置器已从这台电脑上移除。", L"卸载完成",
                    MB_OK | MB_ICONINFORMATION);
        DestroyWindow(g_main);
        return;
    }
    EnableWindow(g_installBtn, TRUE);
    EnableWindow(g_cancelBtn, TRUE);
    SetPage(kPageDone);
}

void OnInstallClicked() {
    if (g_page == kPageDone) {
        if (SendMessageW(g_runChk, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            std::wstring exe = Join(g_installDir, kExeName);
            ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, g_installDir.c_str(),
                          SW_SHOWNORMAL);
        }
        DestroyWindow(g_main);
        return;
    }
    // 确认页上点“覆盖安装”，这时才真正动手
    if (g_page == kPageConfirm) {
        StartInstall();
        return;
    }
    if (g_page != kPageOpt) return;

    wchar_t buf[MAX_PATH] = L"";
    GetWindowTextW(g_pathEdit, buf, MAX_PATH);
    g_installDir = buf;
    while (!g_installDir.empty() && g_installDir.back() == L' ') g_installDir.pop_back();
    if (g_installDir.empty()) {
        MessageBoxW(g_main, L"请先选择安装位置。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 检测到旧版本时不再弹窗打断，改成切到确认页，把话说完再让用户决定
    if (!g_uninstallMode) {
        std::wstring oldVer, oldLoc;
        const bool hasReg = InstalledVersion(oldVer, oldLoc);
        const std::wstring exe = Join(g_installDir, kExeName);
        const bool hasFiles = Exists(exe);

        if (hasReg || hasFiles) {
            if (hasReg) {
                g_confirmHead = L"检测到已安装的版本";
                g_confirmBody =
                    L"这台电脑上已经装了 " + std::wstring(kAppName) + L" v" +
                    (oldVer.empty() ? L"?" : oldVer) +
                    L"。\n\n"
                    L"覆盖安装会替换掉原来的程序文件，桌面和开始菜单的快捷方式会"
                    L"指向新版本。";
            } else {
                g_confirmHead = L"目标位置已有同名文件";
                g_confirmBody = L"这个目录里已经有一个同名的程序文件：\n" + exe +
                                L"\n\n继续安装会覆盖它。";
            }
            // 旧装位置和这次不一样的话，旧的不会被清掉，得说一声
            if (!oldLoc.empty() &&
                _wcsicmp(oldLoc.c_str(), g_installDir.c_str()) != 0)
                g_confirmBody += L"\n\n注意：原来装在\n" + oldLoc + L"\n这次装到\n" +
                                 g_installDir + L"\n旧的那一份不会被自动删除。";
            SetPage(kPageConfirm);
            return;
        }
    }

    StartInstall();
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            Fill(dc, rc, RGB(255, 255, 255));

            // 顶部标题带
            RECT band = {0, 0, rc.right, S(kBandH)};
            Fill(dc, band, kBand);
            RECT line = {0, S(kBandH), rc.right, S(kBandH + 1)};
            Fill(dc, line, kLine);

            // 图标
            HICON ic = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                         S(36), S(36), LR_DEFAULTCOLOR);
            if (ic) {
                DrawIconEx(dc, S(kPadX - 4), S(20), ic, S(36), S(36), 0, nullptr,
                           DI_NORMAL);
                DestroyIcon(ic);
            }

            RECT t = {S(kPadX + 44), S(18), rc.right - S(kPadX), S(44)};
            Text(dc, kAppName, t, kInk, g_fTitle, DT_LEFT | DT_SINGLELINE);
            RECT s = {S(kPadX + 45), S(46), rc.right - S(kPadX), S(64)};
            Text(dc, g_uninstallMode ? L"卸载向导" : L"安装向导", s, kMuted, g_fSmall,
                 DT_LEFT | DT_SINGLELINE);
            RECT v = {rc.right - S(140), S(24), rc.right - S(kPadX), S(46)};
            Text(dc, std::wstring(L"v") + kVersion, v, kMuted, g_fSmall,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

            if (g_page == kPageBusy) {
                RECT p = {S(kPadX), S(136), rc.right - S(kPadX), S(160)};
                Text(dc, g_uninstallMode ? L"正在卸载，请稍候…" : L"正在安装，请稍候…",
                     p, kInk, g_fBold, DT_LEFT | DT_SINGLELINE);
            } else if (g_page == kPageOpt && g_uninstallMode) {
                RECT a = {S(kPadX), S(104), rc.right - S(kPadX), S(126)};
                Text(dc, L"将从下面这个位置移除程序：", a, kInk, g_fBold,
                     DT_LEFT | DT_SINGLELINE);
                RECT b = {S(kPadX), S(134), rc.right - S(kPadX), S(156)};
                Text(dc, g_installDir, b, kMuted, g_fBody,
                     DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS);
            } else if (g_page == kPageConfirm) {
                double e = 1.0 - (1.0 - g_fadeT) * (1.0 - g_fadeT);
                int dy = S(static_cast<int>(8 * (1.0 - e) + 0.5));
                RECT hd = {S(kPadX), S(108) + dy, rc.right - S(kPadX), S(132) + dy};
                Text(dc, g_confirmHead, hd, kInk, g_fBold,
                     DT_LEFT | DT_SINGLELINE);
                RECT bd = {S(kPadX), S(142) + dy, rc.right - S(kPadX), S(272) + dy};
                Text(dc, g_confirmBody, bd, kMuted, g_fBody, DT_LEFT | DT_WORDBREAK);
            } else if (g_page == kPageDone) {
                // 完成页整块从下方 8px 滑上来
                double e = 1.0 - (1.0 - g_fadeT) * (1.0 - g_fadeT);
                int dy = S(static_cast<int>(8 * (1.0 - e) + 0.5));
                RECT p = {S(kPadX), S(108) + dy, rc.right - S(kPadX), S(132) + dy};
                Text(dc, L"安装完成。", p, kInk, g_fBold, DT_LEFT | DT_SINGLELINE);
                RECT q = {S(kPadX), S(140) + dy, rc.right - S(kPadX), S(190) + dy};
                Text(dc,
                     L"开始菜单和桌面上都能找到它。双击打开，"
                     L"挑好功能、装好名单，就能导出一份点名器。",
                     q, kMuted, g_fBody, DT_LEFT | DT_WORDBREAK);
            }

            RECT sep = {0, S(kWinH - 78), rc.right, S(kWinH - 77)};
            Fill(dc, sep, kLine);
            EndPaint(h, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(w);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMuted);
            return reinterpret_cast<LRESULT>(g_bg);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(w);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kInk);
            return reinterpret_cast<LRESULT>(g_bg);
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            HDC dc = d->hDC;
            RECT r = d->rcItem;
            wchar_t buf[160] = L"";
            GetWindowTextW(d->hwndItem, buf, 160);
            bool pressed = (d->itemState & ODS_SELECTED) != 0;
            bool hover = (g_hoverBtn == static_cast<int>(d->CtlID));
            const int id = static_cast<int>(d->CtlID);

            if (id == kInstallBtn) {
                COLORREF fill = pressed   ? RGB(0x10, 0x18, 0xc8)
                                : hover   ? RGB(0x2c, 0x38, 0xff)
                                          : kAccent;
                Fill(dc, r, fill);
                Text(dc, buf, r, RGB(255, 255, 255), g_fBold,
                     DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                return TRUE;
            }
            if (id == kCancelBtn || id == kBrowse) {
                Fill(dc, r, pressed ? RGB(0xe6, 0xea, 0xf6)
                                    : (hover ? RGB(0xf3, 0xf5, 0xfb) : RGB(255, 255, 255)));
                HBRUSH b = CreateSolidBrush(hover ? kAccent : kLine);
                FrameRect(dc, &r, b);
                DeleteObject(b);
                Text(dc, buf, r, hover && !pressed ? kAccent : RGB(0x4a, 0x51, 0x60),
                     g_fBody, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                return TRUE;
            }
            return TRUE;
        }

        case WM_TIMER:
            if (w == kTimerProgress) {
                int step = (kProgressMs / 15);
                int diff = g_progressTarget - g_progressPos;
                if (diff == 0) {
                    KillTimer(h, kTimerProgress);
                } else {
                    int move = diff / (step > 0 ? step : 1);
                    if (move == 0) move = (diff > 0) ? 1 : -1;
                    if ((diff > 0 && move > diff) || (diff < 0 && move < diff))
                        move = diff;
                    g_progressPos += move;
                    SendMessageW(g_progress, PBM_SETPOS, g_progressPos, 0);
                }
            } else if (w == kTimerFade) {
                g_fadeT += 15.0 / kFadeMs;
                if (g_fadeT >= 1.0) {
                    g_fadeT = 1.0;
                    KillTimer(h, kTimerFade);
                }
                RECT fr;
                GetClientRect(h, &fr);
                fr.top = S(kBandH);
                fr.bottom = S(kWinH - 78);
                InvalidateRect(h, &fr, FALSE);
            } else if (w == kTimerSlide) {
                g_slideT += 15.0 / kSlideMs;
                if (g_slideT >= 1.0) {
                    g_slideT = 1.0;
                    KillTimer(h, kTimerSlide);
                }
                double e = 1.0 - (1.0 - g_slideT) * (1.0 - g_slideT);  // 缓出
                int dx = static_cast<int>(kSlideDx * (1.0 - e) + 0.5);
                for (const auto& it : g_slide) {
                    if (!IsWindow(it.w)) continue;
                    MoveWindow(it.w, it.home.left + S(dx), it.home.top,
                               it.home.right - it.home.left,
                               it.home.bottom - it.home.top, TRUE);
                }
                if (g_slideT >= 1.0) g_slide.clear();
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(w)) {
                case kBrowse:
                    BrowseForFolder();
                    return 0;
                case kInstallBtn:
                    OnInstallClicked();
                    return 0;
                case kCancelBtn:
                    // 确认页上的“返回”是退回选项页，不是退出安装
                    if (g_page == kPageConfirm)
                        SetPage(kPageOpt);
                    else
                        DestroyWindow(h);
                    return 0;
                // 自绘按钮没有 BS_DEFPUSHBUTTON，回车由对话框管理器
                // 转成 IDOK 发过来，得自己接住
                case IDOK:
                    OnInstallClicked();
                    return 0;
                case IDCANCEL:
                    DestroyWindow(h);
                    return 0;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void CreateChildren() {
    const int R = kWinW - kPadX;   // 右边界

    Mk(L"STATIC", L"安装位置", SS_LEFT, kPadX, 96, 200, 18, kPathLabel);
    g_pathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 S(kPadX), S(118), S(R - kPadX - 132), S(28), g_main,
                                 (HMENU)(INT_PTR)kPathEdit, g_inst, nullptr);
    SendMessageW(g_pathEdit, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    SetWindowTextW(g_pathEdit, g_installDir.c_str());

    g_browse = Hoverable(Mk(L"BUTTON", L"浏览…", BS_OWNERDRAW | WS_TABSTOP,
                            R - 120, 118, 120, 28, kBrowse));

    g_deskChk = Mk(L"BUTTON", L"在桌面创建快捷方式", BS_AUTOCHECKBOX | WS_TABSTOP,
                   kPadX, 170, 420, 22, kDesktopChk);
    SendMessageW(g_deskChk, BM_SETCHECK, BST_CHECKED, 0);
    g_startChk = Mk(L"BUTTON", L"在开始菜单创建快捷方式", BS_AUTOCHECKBOX | WS_TABSTOP,
                    kPadX, 200, 420, 22, kStartChk);
    SendMessageW(g_startChk, BM_SETCHECK, BST_CHECKED, 0);

    g_info = Mk(L"STATIC",
                g_uninstallMode
                    ? L"程序文件、以及安装时创建的快捷方式都会被删掉。"
                    : L"装到当前用户目录，不需要管理员权限，也不会动系统里的其它东西。",
                SS_LEFT, kPadX, 238, R - kPadX, 20, kInfoText);

    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                 WS_CHILD | PBS_SMOOTH, S(kPadX), S(176),
                                 S(R - kPadX), S(10), g_main,
                                 (HMENU)(INT_PTR)kProgress, g_inst, nullptr);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(g_progress, PBM_SETBARCOLOR, 0, kAccent);

    g_status = Mk(L"STATIC", L"", SS_LEFT, kPadX, 198, R - kPadX, 20, kStatus);

    g_runChk = Mk(L"BUTTON",
                  L"立即运行 A.C.O.C. 点名系统配置器", BS_AUTOCHECKBOX | WS_TABSTOP,
                  kPadX, 206, 380, 22, kRunChk);
    SendMessageW(g_runChk, BM_SETCHECK, BST_CHECKED, 0);

    // 自绘按钮要自己画默认按钮的强调边框，回车提交仍在 WM_COMMAND 里处理
    g_installBtn = Hoverable(Mk(L"BUTTON", L"安装", BS_OWNERDRAW | WS_TABSTOP,
                                R - 100, kWinH - 60, 100, 34, kInstallBtn));
    SendMessageW(g_installBtn, WM_SETFONT, (WPARAM)g_fBold, TRUE);
    g_cancelBtn = Hoverable(Mk(L"BUTTON", L"取消", BS_OWNERDRAW | WS_TABSTOP,
                               R - 212, kWinH - 60, 100, 34, kCancelBtn));
}

bool HasArg(const wchar_t* needle) {
    std::wstring cmd = GetCommandLineW();
    for (auto& c : cmd) c = static_cast<wchar_t>(towlower(c));
    std::wstring n = needle;
    for (auto& c : n) c = static_cast<wchar_t>(towlower(c));
    return cmd.find(n) != std::wstring::npos;
}

std::wstring ArgValue(int index) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring r;
    if (argv && index < argc) r = argv[index];
    if (argv) LocalFree(argv);
    return r;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g_inst = inst;

    // 临时副本模式：等原卸载程序退出，再清空安装目录
    if (HasArg(L"/cleanup")) {
        std::wstring dir = ArgValue(2);
        int pid = _wtoi(ArgValue(3).c_str());
        if (!dir.empty()) RunSelfCleanup(dir, static_cast<DWORD>(pid));
        return 0;
    }

    {
        wchar_t self[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring base = self;
        size_t p = base.find_last_of(L'\\');
        if (p != std::wstring::npos) base = base.substr(p + 1);
        for (auto& c : base) c = static_cast<wchar_t>(towlower(c));
        if (base.find(L"uninstall") == 0) g_uninstallMode = true;
    }
    if (HasArg(L"/uninstall")) g_uninstallMode = true;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_fTitle = MakeFont(14, true);
    g_fBody = MakeFont(8, false);
    g_fBold = MakeFont(8, true);
    g_fSmall = MakeFont(7, false);
    g_bg = CreateSolidBrush(RGB(255, 255, 255));
    g_band = CreateSolidBrush(kBand);

    if (g_uninstallMode) {
        // 卸载程序就装在待删目录里，它的位置就是安装位置
        wchar_t self[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring p = self;
        size_t k = p.find_last_of(L'\\');
        if (k != std::wstring::npos) g_installDir = p.substr(0, k);
    } else {
        std::wstring local;
        FolderPath(CSIDL_LOCAL_APPDATA, local);
        g_installDir = local.empty()
                           ? L"C:\\ACOCConfigurator"
                           : Join(Join(local, L"Programs"), L"ACOCConfigurator");
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"AcocSetup";
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(32), S(32), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(16), S(16), 0);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_bg;
    RegisterClassExW(&wc);

    // WS_CLIPCHILDREN：父窗口不画子控件占的地方，切页滑动时不闪
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) |
                  WS_CLIPCHILDREN;
    RECT r = {0, 0, S(kWinW), S(kWinH)};
    AdjustWindowRectEx(&r, style, FALSE, 0);
    g_main = CreateWindowExW(
        0, L"AcocSetup", g_uninstallMode ? L"卸载 A.C.O.C. 点名系统配置器"
                                         : L"安装 A.C.O.C. 点名系统配置器",
        style | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
        r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_main) return 1;

    CreateChildren();
    SetPage(0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_main, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // 走到这里说明窗口和消息框都关完了，临时副本可以立刻接管
    if (g_pendingCleanup) SpawnCleanup();
    CoUninitialize();
    return 0;
}
