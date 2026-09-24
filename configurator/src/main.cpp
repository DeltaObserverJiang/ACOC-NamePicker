// A.C.O.C. 点名系统配置器
// C++17 / Win32，无第三方依赖，兼容 Windows 7 及以后。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "features.h"
#include "generator.h"
#include "roster.h"

namespace {

constexpr UINT kMsgCommit = WM_APP + 1;

// ---------- 文本与颜色 ----------

std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(static_cast<size_t>(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}
std::wstring W(const char* s) { return W(std::string(s)); }

std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n,
                        nullptr, nullptr);
    return s;
}

const COLORREF kInk = RGB(0x22, 0x25, 0x2e);
const COLORREF kMuted = RGB(0x74, 0x7a, 0x89);
const COLORREF kLine = RGB(0xdd, 0xe1, 0xec);
const COLORREF kAccent = RGB(0x18, 0x24, 0xfc);
const COLORREF kSide = RGB(0xef, 0xf1, 0xf7);
const COLORREF kPage = RGB(0xff, 0xff, 0xff);
const COLORREF kCard = RGB(0xfa, 0xfb, 0xfe);
const COLORREF kSoft = RGB(0xf3, 0xf5, 0xfb);

// ---------- 全局状态 ----------

HINSTANCE g_inst;
HWND g_main, g_page[2], g_nav[2], g_export, g_list, g_className, g_rosterInfo,
    g_rosterNote;
HFONT g_fBody, g_fSmall, g_fTitle, g_fBold, g_fNav;
HBRUSH g_bPage, g_bSide, g_bCard;
int g_curPage = 0;
double g_scale = 1.0;
std::wstring g_status = L"选好功能、填好名单，就可以导出。";

std::string g_template;
std::vector<roster::Student> g_students;
std::vector<std::pair<std::string, bool>> g_features;

HWND g_cellEdit = nullptr;
WNDPROC g_cellOld = nullptr;
int g_editItem = -1, g_editSub = -1;

struct Card {
    RECT rc;
    std::wstring title;
};
std::vector<Card> g_cards;

int S(int v) { return static_cast<int>(v * g_scale + 0.5); }

void SetStatus(const std::string& utf8) {
    g_status = W(utf8);
    RECT r = {S(24), S(626), S(790), S(656)};
    InvalidateRect(g_main, &r, FALSE);
}

// ---------- 字体 ----------

bool FontExists(const wchar_t* name) {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, name, LF_FACESIZE - 1);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf,
                        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
                            *reinterpret_cast<bool*>(p) = true;
                            return 0;
                        },
                        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

HFONT MakeFont(int pt, bool bold) {
    HDC dc = GetDC(nullptr);
    int h = -MulDiv(pt, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, dc);
    static const wchar_t* face = nullptr;
    if (!face)
        face = FontExists(L"Microsoft YaHei UI") ? L"Microsoft YaHei UI"
                                                : L"Microsoft YaHei";
    return CreateFontW(h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

void ApplyFont(HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); }

HWND Mk(const wchar_t* cls, const char* text, DWORD style, int x, int y, int w,
        int h, int id, HWND parent, bool keepFont = false) {
    HWND c = CreateWindowExW(0, cls, text ? W(text).c_str() : L"",
                             WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                             parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    if (g_fBody && !keepFont) ApplyFont(c, g_fBody);
    return c;
}

// ---------- 绘制 ----------

void FillRounded(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void DrawTextC(HDC dc, const std::wstring& s, RECT r, COLORREF color, HFONT f,
               UINT fmt) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), -1, &r, fmt);
    SelectObject(dc, of);
}

// ---------- 名单 ----------

int FeatureIndex(const std::string& key) {
    for (size_t i = 0; i < g_features.size(); i++)
        if (g_features[i].first == key) return static_cast<int>(i);
    return -1;
}

void RefreshList() {
    ListView_DeleteAllItems(g_list);
    for (size_t i = 0; i < g_students.size(); i++) {
        std::wstring id = std::to_wstring(g_students[i].id);
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = static_cast<int>(i);
        it.pszText = const_cast<wchar_t*>(id.c_str());
        ListView_InsertItem(g_list, &it);
        std::wstring nm = W(g_students[i].name);
        ListView_SetItemText(g_list, static_cast<int>(i), 1,
                             const_cast<wchar_t*>(nm.c_str()));
    }
    SetWindowTextW(g_rosterInfo,
                   W(g_students.empty()
                         ? std::string("名单还是空的。可以从 Excel 或 CSV 导入，也可以直接点“添加”。")
                         : "共 " + std::to_string(g_students.size()) +
                               " 人。双击单元格可以直接改学号或姓名。").c_str());
}

void LoadSheet(const roster::Sheet& sheet, const std::wstring& from) {
    roster::Result r = roster::Detect(sheet);
    if (!r.ok) {
        MessageBoxW(g_main, W(r.message).c_str(), L"名单识别失败",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    g_students = r.students;
    int n = GetWindowTextLengthW(g_className);
    if (n == 0) {
        std::wstring base = from;
        size_t slash = base.find_last_of(L"\\/");
        if (slash != std::wstring::npos) base = base.substr(slash + 1);
        size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) base = base.substr(0, dot);
        if (!base.empty()) SetWindowTextW(g_className, base.c_str());
    }
    RefreshList();
    SetStatus("名单已载入：" + r.message);
}

void Import(bool csv) {
    wchar_t file[MAX_PATH] = L"";
    std::wstring filter =
        csv ? W("表格与文本 (*.csv;*.txt)\0*.csv;*.txt\0所有文件 (*.*)\0*.*\0\0")
            : W("Excel 工作簿 (*.xlsx;*.xlsm)\0*.xlsx;*.xlsm\0所有文件 (*.*)\0*.*\0\0");
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = csv ? L"csv" : L"xlsx";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = csv ? L"选择 CSV 文件" : L"选择 Excel 文件";
    if (!GetOpenFileNameW(&ofn)) return;

    roster::Sheet sheet;
    std::string err;
    bool ok = csv ? roster::ReadCsv(file, sheet, err)
                  : roster::ReadXlsx(file, sheet, err);
    if (!ok) {
        MessageBoxW(g_main, W(err).c_str(), L"读取失败", MB_OK | MB_ICONWARNING);
        return;
    }
    LoadSheet(sheet, file);
}

// ---------- 导出 ----------

bool ReadTemplate() {
    HRSRC r = FindResourceW(g_inst, MAKEINTRESOURCEW(2), RT_RCDATA);
    if (!r) return false;
    DWORD sz = SizeofResource(g_inst, r);
    HGLOBAL g = LoadResource(g_inst, r);
    const char* p = static_cast<const char*>(LockResource(g));
    if (!p || !sz) return false;
    g_template.assign(p, sz);
    return true;
}

std::wstring EditText(HWND e) {
    int n = GetWindowTextLengthW(e);
    if (n <= 0) return {};
    std::wstring s(static_cast<size_t>(n) + 1, 0);
    GetWindowTextW(e, &s[0], n + 1);
    s.resize(static_cast<size_t>(n));
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

void Export() {
    gen::Config cfg;
    cfg.features = g_features;
    cfg.students = g_students;
    cfg.className = U8(EditText(g_className));

    std::string html, err;
    if (!gen::Build(g_template, cfg, html, err)) {
        MessageBoxW(g_main, W(err).c_str(), L"还不能导出", MB_OK | MB_ICONINFORMATION);
        if (cfg.className.empty()) SetFocus(g_className);
        return;
    }

    std::wstring suggest = EditText(g_className) + L"_点名系统.html";
    std::wstring filter = W("网页文件 (*.html)\0*.html\0所有文件 (*.*)\0*.*\0\0");
    wchar_t file[MAX_PATH];
    wcsncpy(file, suggest.c_str(), MAX_PATH - 1);
    file[MAX_PATH - 1] = 0;

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"html";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"导出点名器";
    if (!GetSaveFileNameW(&ofn)) return;

    HANDLE fh = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_main, L"文件写入失败，换个位置再试一次。", L"导出失败",
                    MB_OK | MB_ICONERROR);
        return;
    }
    DWORD written = 0;
    WriteFile(fh, html.data(), static_cast<DWORD>(html.size()), &written, nullptr);
    CloseHandle(fh);

    std::wstring msg = std::wstring(L"已导出到\n") + file +
                       L"\n\n双击这个文件就能打开点名器。";
    SetStatus("导出完成");
    MessageBoxW(g_main, msg.c_str(), L"导出完成", MB_OK | MB_ICONINFORMATION);
}

// ---------- 单元格就地编辑 ----------

LRESULT CALLBACK CellEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN) {
            PostMessageW(g_main, kMsgCommit, 0, 0);
            return 0;
        }
        if (w == VK_ESCAPE) {
            PostMessageW(g_main, kMsgCommit, 1, 0);
            return 0;
        }
    } else if (m == WM_KILLFOCUS) {
        PostMessageW(g_main, kMsgCommit, 0, 0);
    }
    return CallWindowProcW(g_cellOld, h, m, w, l);
}

void CommitCellEdit(bool cancel) {
    if (!g_cellEdit) return;
    HWND e = g_cellEdit;
    g_cellEdit = nullptr;
    std::wstring v;
    if (!cancel) {
        int n = GetWindowTextLengthW(e);
        v.resize(static_cast<size_t>(n) + 1);
        GetWindowTextW(e, &v[0], n + 1);
        v.resize(static_cast<size_t>(n));
    }
    DestroyWindow(e);
    g_cellOld = nullptr;

    if (cancel) return;
    if (g_editItem < 0 || g_editItem >= static_cast<int>(g_students.size())) return;
    roster::Student& s = g_students[g_editItem];
    if (g_editSub == 0) {
        int id = _wtoi(v.c_str());
        if (id > 0) s.id = id;
    } else {
        std::string name = roster::Trim(U8(v));
        if (!name.empty()) s.name = name;
    }
    std::wstring id = std::to_wstring(s.id);
    std::wstring nm = W(s.name);
    ListView_SetItemText(g_list, g_editItem, 0, const_cast<wchar_t*>(id.c_str()));
    ListView_SetItemText(g_list, g_editItem, 1, const_cast<wchar_t*>(nm.c_str()));
}

void BeginCellEdit(int item, int sub) {
    if (item < 0 || item >= static_cast<int>(g_students.size())) return;
    if (sub < 0 || sub > 1) return;
    if (g_cellEdit) CommitCellEdit(false);

    RECT rc;
    if (sub == 0) {
        ListView_GetItemRect(g_list, item, &rc, LVIR_LABEL);
    } else {
        rc.left = LVIR_BOUNDS;
        ListView_GetSubItemRect(g_list, item, sub, LVIR_BOUNDS, &rc);
    }
    rc.bottom -= 1;

    wchar_t buf[512] = L"";
    ListView_GetItemText(g_list, item, sub, buf, 512);
    g_editItem = item;
    g_editSub = sub;
    g_cellEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", buf,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, rc.left, rc.top,
        rc.right - rc.left, rc.bottom - rc.top, g_list, nullptr, g_inst, nullptr);
    ApplyFont(g_cellEdit, g_fBody);
    g_cellOld = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_cellEdit, GWLP_WNDPROC, (LONG_PTR)CellEditProc));
    SendMessageW(g_cellEdit, EM_SETSEL, 0, -1);
    SetFocus(g_cellEdit);
}

// ---------- 页面一 ----------

void BuildPage1() {
    HWND p = g_page[0];
    const auto& groups = feat::Groups();
    const int colX[2] = {0, 392};
    const int colW = 364;
    int colY[2] = {0, 0};
    const int assign[5] = {0, 1, 0, 1, 1};

    int id = 3000;
    for (size_t gi = 0; gi < groups.size(); gi++) {
        int col = (gi < 5) ? assign[gi] : 0;
        const auto& g = groups[gi];
        int n = static_cast<int>(g.items.size());
        // 34 给标题，每项 36（复选框 + 说明各 18），末尾再留出分组说明的位置
        int h = 62 + n * 36;

        Card c;
        c.rc = {S(colX[col]), S(colY[col]), S(colX[col] + colW), S(colY[col] + h)};
        c.title = W(g.title);
        g_cards.push_back(c);

        Mk(L"STATIC", g.note, SS_LEFT, colX[col] + 16, colY[col] + h - 22, colW - 32,
           18, id++, p);

        int y = colY[col] + 34;
        for (const auto& it : g.items) {
            int idx = FeatureIndex(it.key);
            HWND cb = Mk(L"BUTTON", it.label, BS_AUTOCHECKBOX | WS_TABSTOP,
                         colX[col] + 16, y, colW - 32, 18, 4000 + idx, p);
            if (idx >= 0 && g_features[idx].second)
                SendMessageW(cb, BM_SETCHECK, BST_CHECKED, 0);
            Mk(L"STATIC", it.note, SS_LEFT | SS_ENDELLIPSIS, colX[col] + 34, y + 18,
               colW - 50, 16, 5000 + idx, p);
            y += 36;
        }
        colY[col] += h + 16;
    }
}

// ---------- 页面二 ----------

void BuildPage2() {
    HWND p = g_page[1];
    Mk(L"STATIC", "班级名称", SS_LEFT, 0, 5, 74, 18, 6000, p);
    g_className = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  S(80), S(0), S(300), S(24), p, (HMENU)6001, g_inst,
                                  nullptr);
    ApplyFont(g_className, g_fBody);
    SendMessageW(g_className, EM_SETLIMITTEXT, 60, 0);
    SendMessageW(g_className, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"我的班级"));

    Mk(L"STATIC",
       "这个名字会出现在点名器设置菜单的标题上，也会作为预设名出现在装载节点。例如：A2班、高二(3)班。",
       SS_LEFT, 80, 28, 620, 34, 6002, p);

    struct Btn {
        const char* text;
        int w;
        int id;
    } btns[] = {
        {"从 Excel 导入", 124, 6100}, {"从 CSV 导入", 112, 6101},
        {"添加", 66, 6102},           {"删除", 66, 6103},
        {"上移", 66, 6104},           {"下移", 66, 6105},
        {"重新编号", 92, 6106},       {"清空", 66, 6107},
    };
    int x = 0;
    for (const auto& b : btns) {
        Mk(L"BUTTON", b.text, BS_OWNERDRAW | WS_TABSTOP, x, 74, b.w, 26, b.id, p);
        x += b.w + 8;
    }

    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                                 LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                             S(0), S(112), S(758), S(330), p, (HMENU)6200, g_inst,
                             nullptr);
    ListView_SetExtendedListViewStyle(
        g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    ApplyFont(g_list, g_fBody);
    LVCOLUMNW c = {};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = const_cast<wchar_t*>(L"学号");
    c.cx = S(96);
    ListView_InsertColumn(g_list, 0, &c);
    c.pszText = const_cast<wchar_t*>(L"姓名");
    c.cx = S(636);
    ListView_InsertColumn(g_list, 1, &c);

    g_rosterInfo = Mk(L"STATIC", "", SS_LEFT, 0, 450, 758, 20, 6300, p);
    g_rosterNote = Mk(L"STATIC", "", SS_LEFT | SS_ENDELLIPSIS, 0, 474, 758, 20, 6301, p);
    SetWindowTextW(
        g_rosterNote,
        L"表格可以是只有姓名一列、学号加姓名两列，或是第一行写着“学号 / 姓名”的表头。");

    RefreshList();
}

void SetPage(int page) {
    g_curPage = page;
    for (int i = 0; i < 2; i++) {
        ShowWindow(g_page[i], page == i ? SW_SHOW : SW_HIDE);
        InvalidateRect(g_nav[i], nullptr, TRUE);
    }
}

// ---------- 主窗口 ----------

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l);

LRESULT CALLBACK PageProc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hw, &ps);
            RECT rc;
            GetClientRect(hw, &rc);
            FillRect(dc, &rc, g_bPage);
            if (GetWindowLongPtrW(hw, GWLP_ID) == 900) {
                for (const auto& c : g_cards) {
                    FillRounded(dc, c.rc, S(8), kCard, kLine);
                    RECT t = {c.rc.left + S(16), c.rc.top + S(10),
                              c.rc.right - S(16), c.rc.top + S(30)};
                    DrawTextC(dc, c.title, t, kInk, g_fBold,
                              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                }
            }
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HBRUSH b = (GetWindowLongPtrW(hw, GWLP_ID) == 900) ? g_bCard : g_bPage;
            HDC dc = reinterpret_cast<HDC>(w);
            SetBkMode(dc, TRANSPARENT);
            SetBkColor(dc, (GetWindowLongPtrW(hw, GWLP_ID) == 900) ? kCard : kPage);
            return reinterpret_cast<LRESULT>(b);
        }
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_DRAWITEM:
            return SendMessageW(g_main, m, w, l);
    }
    return DefWindowProcW(hw, m, w, l);
}

void RegisterClasses() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = PageProc;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"AcocPage";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
}

void CreateChildren(HWND h) {
    const char* navText[2] = {"功能模块", "班级名单"};
    for (int i = 0; i < 2; i++) {
        g_nav[i] = CreateWindowExW(0, L"BUTTON", W(navText[i]).c_str(),
                                   WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   S(12), S(88 + i * 48), S(166), S(42), h,
                                   (HMENU)(INT_PTR)(1001 + i), g_inst, nullptr);
        ApplyFont(g_nav[i], g_fNav);
    }

    g_page[0] = CreateWindowExW(0, L"AcocPage", L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, S(206), S(88),
                                S(758), S(506), h, (HMENU)900, g_inst, nullptr);
    g_page[1] = CreateWindowExW(0, L"AcocPage", L"",
                                WS_CHILD | WS_CLIPCHILDREN, S(206), S(88), S(758),
                                S(506), h, (HMENU)901, g_inst, nullptr);
    BuildPage1();
    BuildPage2();

    g_export = CreateWindowExW(0, L"BUTTON", L"导出 index.html",
                               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, S(784), S(620),
                               S(172), S(36), h, (HMENU)2001, g_inst, nullptr);
    ApplyFont(g_export, g_fBold);
    SetPage(0);
}

void OnCommand(HWND h, int id, int code, HWND ctl) {
    if (id == 1001 || id == 1002) {
        if (code == BN_CLICKED) SetPage(id - 1001);
        return;
    }
    if (id == 2001 && code == BN_CLICKED) {
        Export();
        return;
    }
    if (id >= 4000 && id < 4000 + static_cast<int>(g_features.size()) &&
        code == BN_CLICKED) {
        g_features[id - 4000].second =
            SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SetStatus("功能开关已更新。");
        return;
    }
    if (code != BN_CLICKED || id < 6100 || id > 6107) return;

    switch (id) {
        case 6100:
            Import(false);
            break;
        case 6101:
            Import(true);
            break;
        case 6102: {
            roster::Student s;
            int maxId = 0;
            for (const auto& x2 : g_students) maxId = (std::max)(maxId, x2.id);
            s.id = maxId + 1;
            s.name = "新同学";
            g_students.push_back(s);
            RefreshList();
            int row = static_cast<int>(g_students.size()) - 1;
            ListView_EnsureVisible(g_list, row, FALSE);
            ListView_SetItemState(g_list, row, LVIS_SELECTED, LVIS_SELECTED);
            BeginCellEdit(row, 1);
            break;
        }
        case 6103: {
            int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
            if (sel >= 0) {
                g_students.erase(g_students.begin() + sel);
                RefreshList();
                SetStatus("已删除一行。");
            }
            break;
        }
        case 6104:
        case 6105: {
            int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
            int to = sel + (id == 6104 ? -1 : 1);
            if (sel >= 0 && to >= 0 && to < static_cast<int>(g_students.size())) {
                std::swap(g_students[sel], g_students[to]);
                RefreshList();
                ListView_SetItemState(g_list, to, LVIS_SELECTED, LVIS_SELECTED);
            }
            break;
        }
        case 6106:
            for (size_t i = 0; i < g_students.size(); i++)
                g_students[i].id = static_cast<int>(i) + 1;
            RefreshList();
            SetStatus("学号已按当前顺序重排。");
            break;
        case 6107:
            if (!g_students.empty() &&
                MessageBoxW(h, L"清空当前名单？", L"确认",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                g_students.clear();
                RefreshList();
            }
            break;
    }
}

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE:
            CreateChildren(h);
            return 0;

        case kMsgCommit:
            CommitCellEdit(w == 1);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(dc, &rc, g_bPage);

            RECT side = {0, 0, S(190), rc.bottom};
            FillRect(dc, &side, g_bSide);

            HPEN pen = CreatePen(PS_SOLID, 1, kLine);
            HGDIOBJ op = SelectObject(dc, pen);
            MoveToEx(dc, 0, S(72), nullptr);
            LineTo(dc, rc.right, S(72));
            MoveToEx(dc, 0, S(612), nullptr);
            LineTo(dc, rc.right, S(612));
            MoveToEx(dc, S(190), S(72), nullptr);
            LineTo(dc, S(190), S(612));
            SelectObject(dc, op);
            DeleteObject(pen);

            HICON ico = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
            if (ico)
                DrawIconEx(dc, S(26), S(16), ico, S(40), S(40), 0, nullptr, DI_NORMAL);

            RECT t = {S(78), S(14), rc.right - S(20), S(44)};
            DrawTextC(dc, L"A.C.O.C. 点名系统配置器", t, kInk, g_fTitle,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            RECT s2 = {S(80), S(43), rc.right - S(20), S(64)};
            DrawTextC(dc, L"挑好功能、装好名单，导出一份双击就能用的点名器。", s2,
                      kMuted, g_fSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            RECT foot = {S(24), S(626), S(770), S(652)};
            DrawTextC(dc, g_status, foot, kMuted, g_fBody,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            EndPaint(h, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            HDC dc = d->hDC;
            RECT r = d->rcItem;
            wchar_t buf[160] = L"";
            GetWindowTextW(d->hwndItem, buf, 160);
            bool pressed = (d->itemState & ODS_SELECTED) != 0;

            if (d->CtlID == 1001 || d->CtlID == 1002) {
                bool active = (d->CtlID == 1001) == (g_curPage == 0);
                HBRUSH bg = CreateSolidBrush(active ? kPage : kSide);
                FillRect(dc, &r, bg);
                DeleteObject(bg);
                if (active) {
                    RECT bar = {r.left, r.top, r.left + S(3), r.bottom};
                    HBRUSH b = CreateSolidBrush(kAccent);
                    FillRect(dc, &bar, b);
                    DeleteObject(b);
                }
                RECT t = r;
                t.left += S(18);
                DrawTextC(dc, buf, t, active ? kAccent : kInk,
                          active ? g_fBold : g_fNav,
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                return TRUE;
            }

            if (d->CtlID == 2001) {
                COLORREF fill = pressed ? RGB(0x10, 0x18, 0xc8) : kAccent;
                FillRounded(dc, r, S(7), fill, fill);
                DrawTextC(dc, buf, r, RGB(255, 255, 255), g_fBold,
                          DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                return TRUE;
            }

            bool primary = (d->CtlID == 6100);
            FillRounded(dc, r, S(6), pressed ? kSoft : kPage,
                        primary ? kAccent : kLine);
            DrawTextC(dc, buf, r, primary ? kAccent : kInk,
                      primary ? g_fBold : g_fBody,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            return TRUE;
        }

        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(g_bPage);

        case WM_COMMAND:
            OnCommand(h, LOWORD(w), HIWORD(w), reinterpret_cast<HWND>(l));
            return 0;

        case WM_NOTIFY: {
            NMHDR* nh = reinterpret_cast<NMHDR*>(l);
            if (nh->idFrom == 6200) {
                if (nh->code == NM_DBLCLK) {
                    NMITEMACTIVATE* a = reinterpret_cast<NMITEMACTIVATE*>(l);
                    if (a->iItem >= 0) BeginCellEdit(a->iItem, a->iSubItem);
                } else if (nh->code == LVN_KEYDOWN) {
                    NMLVKEYDOWN* k = reinterpret_cast<NMLVKEYDOWN*>(l);
                    if (k->wVKey == VK_F2) {
                        int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
                        if (sel >= 0) BeginCellEdit(sel, 1);
                    }
                }
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(h);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g_inst = inst;
    INITCOMMONCONTROLSEX icc = {sizeof(icc),
                                ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HDC screen = GetDC(nullptr);
    g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, screen);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    double maxS = (work.bottom - work.top) * 0.92 / 668.0;
    if (g_scale > maxS && maxS > 0.6) g_scale = maxS;

    g_fBody = MakeFont(9, false);
    g_fSmall = MakeFont(8, false);
    g_fTitle = MakeFont(16, true);
    g_fBold = MakeFont(9, true);
    g_fNav = MakeFont(10, false);
    g_bPage = CreateSolidBrush(kPage);
    g_bSide = CreateSolidBrush(kSide);
    g_bCard = CreateSolidBrush(kCard);

    if (!ReadTemplate()) {
        MessageBoxW(nullptr,
                    L"内置模板读取失败，程序可能不完整，请重新解压或重新安装。",
                    L"A.C.O.C. 点名系统配置器", MB_OK | MB_ICONERROR);
        return 1;
    }
    for (const auto& g : feat::Groups())
        for (const auto& it : g.items) g_features.emplace_back(it.key, true);

    RegisterClasses();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"AcocConfigurator";
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(32), S(32), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(16), S(16), 0);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE;
    RECT r = {0, 0, S(980), S(668)};
    AdjustWindowRectEx(&r, style, FALSE, 0);
    g_main = CreateWindowExW(0, L"AcocConfigurator", L"A.C.O.C. 点名系统配置器",
                             style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                             r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_main) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_main, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
