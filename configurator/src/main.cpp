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

// 取编辑框内容并去掉首尾空白
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
    g_rosterNote, g_classList;
HFONT g_fBody, g_fSmall, g_fTitle, g_fBold, g_fNav;
HBRUSH g_bPage, g_bSide, g_bCard;
int g_curPage = 0;
double g_scale = 1.0;
std::wstring g_status = L"选好功能、填好名单，就可以导出。";

std::string g_template;
std::vector<gen::Klass> g_classes;
int g_curClass = 0;
bool g_syncing = false;  // 程序性改写编辑框时，挡住随之而来的 EN_CHANGE

std::vector<std::pair<std::string, bool>> g_features;

HWND g_cellEdit = nullptr;
WNDPROC g_cellOld = nullptr;
int g_editItem = -1, g_editSub = -1;

// ---------- 动效状态 ----------

int g_hoverCard = -1;  // 鼠标停在第一页的哪张卡片上
int g_hoverBtn = 0;    // 鼠标停在哪个控件上（控件 id）
std::vector<int> g_featCard;  // 功能序号 -> 它所在的卡片序号
double g_barY = 0;      // 侧边栏强调条的当前位置与目标位置（逻辑像素）
double g_barFrom = 0, g_barTo = 0, g_barT = 1.0;
DWORD g_statusTick = 0;  // 状态文字变色的起始时刻

constexpr UINT_PTR kTimerBar = 1;
constexpr UINT_PTR kTimerStatus = 2;
constexpr UINT kBarMs = 140;      // 强调条滑动时长
constexpr UINT kStatusMs = 900;   // 状态文字颜色回落时长

// 侧边栏底色只铺中间这一段，标题区和导出区保持干净
constexpr int kSideTop = 72, kSideBottom = 612;
constexpr int kNavX = 12, kNavY = 88, kNavW = 166, kNavH = 42, kNavGap = 48;

struct Card {
    RECT rc;
    std::wstring title;
};
std::vector<Card> g_cards;

int S(int v) { return static_cast<int>(v * g_scale + 0.5); }

void SetStatus(const std::string& utf8) {
    g_status = W(utf8);
    g_statusTick = GetTickCount();
    // 建窗口的过程中 g_main 还没赋值，这时不需要动画
    if (g_main) SetTimer(g_main, kTimerStatus, kStatusMs / 12, nullptr);
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

void CommitCellEdit(bool cancel);  // 定义在下面的就地编辑一节

std::vector<roster::Student>& Cur() { return g_classes[g_curClass].students; }

// 新建的班级还没起名，用这个占位；导入名单时会拿文件名替换掉
bool IsPlaceholderName(const std::string& n) {
    return n.rfind("新班级", 0) == 0;
}

// 编辑框是班级名唯一的改名入口，这里把值写回数据并刷新列表项
void SyncClassName() {
    if (g_syncing || g_classes.empty()) return;
    std::string name = U8(EditText(g_className));
    if (name == g_classes[g_curClass].name) return;
    g_classes[g_curClass].name = name;
    if (g_classList && g_curClass < ListView_GetItemCount(g_classList)) {
        std::wstring w = W(name.empty() ? std::string("(未命名)") : name);
        ListView_SetItemText(g_classList, g_curClass, 0,
                             const_cast<wchar_t*>(w.c_str()));
    }
}

void RefreshClassList() {
    if (!g_classList) return;
    ListView_DeleteAllItems(g_classList);
    for (size_t i = 0; i < g_classes.size(); i++) {
        std::wstring nm = W(g_classes[i].name.empty() ? std::string("(未命名)")
                                                      : g_classes[i].name);
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = static_cast<int>(i);
        it.pszText = const_cast<wchar_t*>(nm.c_str());
        ListView_InsertItem(g_classList, &it);
    }
    if (!g_classes.empty() && g_curClass < static_cast<int>(g_classes.size())) {
        ListView_SetItemState(g_classList, g_curClass, LVIS_SELECTED,
                              LVIS_SELECTED);
        ListView_EnsureVisible(g_classList, g_curClass, FALSE);
    }
}

void RefreshList() {
    if (!g_list || g_classes.empty()) return;
    ListView_DeleteAllItems(g_list);
    const auto& v = Cur();
    for (size_t i = 0; i < v.size(); i++) {
        std::wstring id = std::to_wstring(v[i].id);
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = static_cast<int>(i);
        it.pszText = const_cast<wchar_t*>(id.c_str());
        ListView_InsertItem(g_list, &it);
        std::wstring nm = W(v[i].name);
        ListView_SetItemText(g_list, static_cast<int>(i), 1,
                             const_cast<wchar_t*>(nm.c_str()));
    }
    SetWindowTextW(
        g_rosterInfo,
        W(v.empty()
              ? std::string("这个班级还没有人。可以从 Excel 或 CSV 导入，也可以直接点“添加”。")
              : "共 " + std::to_string(v.size()) +
                    " 人。双击单元格可以直接改学号或姓名。").c_str());
}

// 切换班级：先把编辑框里可能改过的名字存回原班级，再载入新班级
void SelectClass(int idx) {
    if (g_classes.empty()) return;
    if (idx < 0 || idx >= static_cast<int>(g_classes.size())) return;
    SyncClassName();
    if (g_cellEdit) CommitCellEdit(false);
    g_curClass = idx;
    g_syncing = true;
    SetWindowTextW(g_className, W(g_classes[idx].name).c_str());
    g_syncing = false;
    RefreshList();
    ListView_SetItemState(g_classList, idx, LVIS_SELECTED, LVIS_SELECTED);
    ListView_EnsureVisible(g_classList, idx, FALSE);
}

void LoadSheet(const roster::Sheet& sheet, const std::wstring& from) {
    roster::Result r = roster::Detect(sheet);
    if (!r.ok) {
        MessageBoxW(g_main, W(r.message).c_str(), L"名单识别失败",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    Cur() = r.students;
    // 新班级还没起名时，拿文件名当班级名
    if (IsPlaceholderName(g_classes[g_curClass].name)) {
        std::wstring base = from;
        size_t slash = base.find_last_of(L"\\/");
        if (slash != std::wstring::npos) base = base.substr(slash + 1);
        size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) base = base.substr(0, dot);
        if (!base.empty()) {
            g_classes[g_curClass].name = U8(base);
            g_syncing = true;
            SetWindowTextW(g_className, base.c_str());
            g_syncing = false;
        }
    }
    RefreshClassList();
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

void Export() {
    SyncClassName();
    gen::Config cfg;
    cfg.features = g_features;
    cfg.classes = g_classes;

    std::string html, err;
    if (!gen::Build(g_template, cfg, html, err)) {
        MessageBoxW(g_main, W(err).c_str(), L"还不能导出", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring suggest =
        (g_classes.empty() ? std::wstring(L"点名器")
                           : W(g_classes[0].name)) +
        L"_点名系统.html";
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
    if (g_editItem < 0 || g_editItem >= static_cast<int>(Cur().size())) return;
    roster::Student& s = Cur()[g_editItem];
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
    if (item < 0 || item >= static_cast<int>(Cur().size())) return;
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

// 给控件挂上悬停跟踪。原窗口过程存在 GWLP_USERDATA 里
// （按钮用不到这个字段，借来放指针是安全的）。
LRESULT CALLBACK HoverProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    WNDPROC old = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(h, GWLP_USERDATA));
    int id = GetDlgCtrlID(h);
    if (m == WM_MOUSEMOVE) {
        if (g_hoverBtn != id) {
            g_hoverBtn = id;
            InvalidateRect(h, nullptr, TRUE);
        }
        // 功能复选框还要顺手点亮它所在的卡片
        if (id >= 4000 && id < 4000 + static_cast<int>(g_featCard.size()) &&
            g_featCard[id - 4000] != g_hoverCard) {
            g_hoverCard = g_featCard[id - 4000];
            if (g_page[0]) InvalidateRect(g_page[0], nullptr, FALSE);
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
    if (!h) return h;
    WNDPROC old = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(h, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(HoverProc)));
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(old));
    return h;
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
        const int cardIdx = static_cast<int>(g_cards.size());
        g_cards.push_back(c);

        Mk(L"STATIC", g.note, SS_LEFT, colX[col] + 16, colY[col] + h - 22, colW - 32,
           18, id++, p);

        int y = colY[col] + 34;
        for (const auto& it : g.items) {
            int idx = FeatureIndex(it.key);
            if (idx >= 0 && idx < static_cast<int>(g_featCard.size()))
                g_featCard[idx] = cardIdx;
            HWND cb = Hoverable(Mk(L"BUTTON", it.label, BS_AUTOCHECKBOX | WS_TABSTOP,
                                   colX[col] + 16, y, colW - 32, 18, 4000 + idx, p));
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

// 班级列表铺满一列，用 ListView 是为了和学生表格长得一致
void MakeClassList(HWND p) {
    g_classList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
        S(0), S(26), S(170), S(300), p, (HMENU)6400, g_inst, nullptr);
    ListView_SetExtendedListViewStyle(g_classList, LVS_EX_FULLROWSELECT |
                                                        LVS_EX_DOUBLEBUFFER);
    ApplyFont(g_classList, g_fBody);
    LVCOLUMNW c = {};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = const_cast<wchar_t*>(L"班级");
    c.cx = S(166);
    ListView_InsertColumn(g_classList, 0, &c);
}

void BuildPage2() {
    HWND p = g_page[1];
    const int rx = 182, rw = 576;  // 右侧一栏

    // ---- 左栏：班级列表 ----
    Mk(L"STATIC", "班级", SS_LEFT, 0, 5, 170, 18, 6420, p);
    MakeClassList(p);
    Hoverable(Mk(L"BUTTON", "新建", BS_OWNERDRAW | WS_TABSTOP, 0, 334, 80, 26, 6410, p));
    Hoverable(Mk(L"BUTTON", "重命名", BS_OWNERDRAW | WS_TABSTOP, 88, 334, 82, 26, 6411, p));
    Hoverable(Mk(L"BUTTON", "上移", BS_OWNERDRAW | WS_TABSTOP, 0, 366, 50, 26, 6413, p));
    Hoverable(Mk(L"BUTTON", "下移", BS_OWNERDRAW | WS_TABSTOP, 58, 366, 50, 26, 6414, p));
    Hoverable(Mk(L"BUTTON", "删除", BS_OWNERDRAW | WS_TABSTOP, 116, 366, 54, 26, 6412, p));
    Mk(L"STATIC",
       "列表里的第一个班级是默认班级，点名器按下直接启动时用它。",
       SS_LEFT, 0, 400, 170, 76, 6421, p);

    // ---- 右栏：所选班级的名称与学生 ----
    Mk(L"STATIC", "班级名称", SS_LEFT, rx, 5, 74, 18, 6000, p);
    g_className = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  S(rx + 80), S(0), S(280), S(24), p, (HMENU)6001,
                                  g_inst, nullptr);
    ApplyFont(g_className, g_fBody);
    SendMessageW(g_className, EM_SETLIMITTEXT, 60, 0);
    SendMessageW(g_className, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"我的班级"));

    Mk(L"STATIC",
       "会出现在设置菜单的标题和装载节点的预设名里。例如：A2班、高二(3)班。",
       SS_LEFT | SS_ENDELLIPSIS, rx + 80, 30, rw - 80, 18, 6002, p);

    struct Btn {
        const char* text;
        int x;
        int w;
        int id;
    } btns[] = {
        {"从 Excel 导入", 0, 112, 6100},   {"从 CSV 导入", 120, 100, 6101},
        {"添加", 228, 60, 6102},            {"删除", 296, 60, 6103},
        {"上移", 364, 60, 6104},            {"下移", 432, 60, 6105},
        {"重新编号", 0, 88, 6106},          {"清空", 96, 60, 6107},
    };
    for (const auto& b : btns) {
        int y = (b.id >= 6106) ? 106 : 74;
        Hoverable(Mk(L"BUTTON", b.text, BS_OWNERDRAW | WS_TABSTOP, rx + b.x, y, b.w,
                     26, b.id, p));
    }

    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                                 LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                             S(rx), S(142), S(rw), S(300), p, (HMENU)6200, g_inst,
                             nullptr);
    ListView_SetExtendedListViewStyle(
        g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    ApplyFont(g_list, g_fBody);
    LVCOLUMNW c = {};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = const_cast<wchar_t*>(L"学号");
    c.cx = S(76);
    ListView_InsertColumn(g_list, 0, &c);
    c.pszText = const_cast<wchar_t*>(L"姓名");
    c.cx = S(492);
    ListView_InsertColumn(g_list, 1, &c);

    g_rosterInfo = Mk(L"STATIC", "", SS_LEFT, rx, 450, rw, 20, 6300, p);
    g_rosterNote =
        Mk(L"STATIC", "", SS_LEFT | SS_ENDELLIPSIS, rx, 474, rw, 20, 6301, p);
    SetWindowTextW(
        g_rosterNote,
        L"表格可以是只有姓名一列、学号加姓名两列，或是第一行写着“学号 / 姓名”的表头。");

    RefreshClassList();
    RefreshList();
}

void SetPage(int page) {
    g_curPage = page;
    for (int i = 0; i < 2; i++) {
        ShowWindow(g_page[i], page == i ? SW_SHOW : SW_HIDE);
        InvalidateRect(g_nav[i], nullptr, TRUE);
    }
    // 让选中条从当前位置滑到新的导航项
    g_barFrom = g_barY;
    g_barTo = kNavY + page * kNavGap;
    g_barT = (g_barFrom == g_barTo) ? 1.0 : 0.0;
    if (g_barT < 1.0 && g_main) SetTimer(g_main, kTimerBar, 15, nullptr);
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
                for (size_t i = 0; i < g_cards.size(); i++) {
                    const Card& c = g_cards[i];
                    bool hov = (static_cast<int>(i) == g_hoverCard);
                    FillRounded(dc, c.rc, S(8), kCard, hov ? kAccent : kLine);
                    RECT t = {c.rc.left + S(16), c.rc.top + S(10),
                              c.rc.right - S(16), c.rc.top + S(30)};
                    DrawTextC(dc, c.title, t, hov ? kAccent : kInk, g_fBold,
                              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                }
            }
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (GetWindowLongPtrW(hw, GWLP_ID) != 900) break;
            POINT pt = {static_cast<short>(LOWORD(l)),
                        static_cast<short>(HIWORD(l))};
            int hover = -1;
            for (size_t i = 0; i < g_cards.size(); i++)
                if (PtInRect(&g_cards[i].rc, pt)) {
                    hover = static_cast<int>(i);
                    break;
                }
            if (hover != g_hoverCard) {
                g_hoverCard = hover;
                InvalidateRect(hw, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hw, 0};
            TrackMouseEvent(&tme);
            break;
        }
        case WM_MOUSELEAVE: {
            // 移到子控件上时也会收到 LEAVE，这时卡片高亮不该灭
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hw, &pt);
            RECT rc;
            GetClientRect(hw, &rc);
            if (!PtInRect(&rc, pt) && g_hoverCard != -1) {
                g_hoverCard = -1;
                InvalidateRect(hw, nullptr, FALSE);
            }
            break;
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
        g_nav[i] = Hoverable(CreateWindowExW(
            0, L"BUTTON", W(navText[i]).c_str(),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, S(kNavX), S(kNavY + i * kNavGap),
            S(kNavW), S(kNavH), h, (HMENU)(INT_PTR)(1001 + i), g_inst, nullptr));
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

    g_export = Hoverable(CreateWindowExW(0, L"BUTTON", L"导出 index.html",
                                         WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                         S(784), S(620), S(172), S(36), h,
                                         (HMENU)2001, g_inst, nullptr));
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
    // 班级名编辑框：随时改随时写回当前班级
    if (id == 6001 && code == EN_CHANGE) {
        SyncClassName();
        return;
    }

    if (code == BN_CLICKED && id >= 6410 && id <= 6414) {
        switch (id) {
            case 6410: {  // 新建
                SyncClassName();
                g_classes.push_back({"新班级 " +
                                         std::to_string(g_classes.size() + 1),
                                     {}});
                g_curClass = static_cast<int>(g_classes.size()) - 1;
                RefreshClassList();
                SelectClass(g_curClass);
                SetFocus(g_className);
                SendMessageW(g_className, EM_SETSEL, 0, -1);
                SetStatus("给新班级起个名字，再加学生。第一个班级是默认班级。");
                break;
            }
            case 6411:  // 重命名就是改右边那个框
                SetFocus(g_className);
                SendMessageW(g_className, EM_SETSEL, 0, -1);
                SetStatus("在“班级名称”里改好后，列表会自动跟着变。");
                break;
            case 6412: {  // 删除
                if (g_classes.size() <= 1) {
                    MessageBoxW(h, L"至少要保留一个班级。", L"不能删除",
                                MB_OK | MB_ICONINFORMATION);
                    break;
                }
                std::wstring nm = W(g_classes[g_curClass].name);
                std::wstring ask = L"删除班级「" + nm + L"」和它的名单？";
                if (MessageBoxW(h, ask.c_str(), L"确认",
                                MB_YESNO | MB_ICONQUESTION) != IDYES)
                    break;
                g_classes.erase(g_classes.begin() + g_curClass);
                if (g_curClass >= static_cast<int>(g_classes.size()))
                    g_curClass = static_cast<int>(g_classes.size()) - 1;
                RefreshClassList();
                SelectClass(g_curClass);
                SetStatus("班级已删除。");
                break;
            }
            case 6413:
            case 6414: {  // 上移 / 下移，第一位就是默认班级
                int to = g_curClass + (id == 6413 ? -1 : 1);
                if (to >= 0 && to < static_cast<int>(g_classes.size())) {
                    SyncClassName();
                    std::swap(g_classes[g_curClass], g_classes[to]);
                    g_curClass = to;
                    RefreshClassList();
                    SelectClass(g_curClass);
                    SetStatus(id == 6413 ? "已上移。" : "已下移。");
                }
                break;
            }
        }
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
            for (const auto& x2 : Cur()) maxId = (std::max)(maxId, x2.id);
            s.id = maxId + 1;
            s.name = "新同学";
            Cur().push_back(s);
            RefreshList();
            int row = static_cast<int>(Cur().size()) - 1;
            ListView_EnsureVisible(g_list, row, FALSE);
            ListView_SetItemState(g_list, row, LVIS_SELECTED, LVIS_SELECTED);
            BeginCellEdit(row, 1);
            break;
        }
        case 6103: {
            int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
            if (sel >= 0) {
                Cur().erase(Cur().begin() + sel);
                RefreshList();
                SetStatus("已删除一行。");
            }
            break;
        }
        case 6104:
        case 6105: {
            int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
            int to = sel + (id == 6104 ? -1 : 1);
            if (sel >= 0 && to >= 0 && to < static_cast<int>(Cur().size())) {
                std::swap(Cur()[sel], Cur()[to]);
                RefreshList();
                ListView_SetItemState(g_list, to, LVIS_SELECTED, LVIS_SELECTED);
            }
            break;
        }
        case 6106:
            for (size_t i = 0; i < Cur().size(); i++)
                Cur()[i].id = static_cast<int>(i) + 1;
            RefreshList();
            SetStatus("学号已按当前顺序重排。");
            break;
        case 6107:
            if (!Cur().empty() &&
                MessageBoxW(h, L"清空当前名单？", L"确认",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                Cur().clear();
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

            // 底色只铺侧边栏中间这一段，顶部标题区和底部导出区保持干净
            RECT side = {0, S(kSideTop), S(190), S(kSideBottom)};
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

            // 选中条由这里统一画，才能在两个导航项之间平滑滑过去
            {
                int barH = S(kNavH);
                RECT bar = {S(kNavX), static_cast<LONG>(g_barY * g_scale + 0.5),
                            S(kNavX + 3),
                            static_cast<LONG>(g_barY * g_scale + 0.5) + barH};
                HBRUSH b = CreateSolidBrush(kAccent);
                FillRect(dc, &bar, b);
                DeleteObject(b);
            }

            // 状态文字刚变过时先亮一下再回落
            COLORREF sc = kMuted;
            if (g_statusTick) {
                DWORD el = GetTickCount() - g_statusTick;
                if (el < kStatusMs) {
                    int k = static_cast<int>(255 - el * 255 / kStatusMs);
                    auto mix = [k](int a, int b) {
                        return a + (b - a) * k / 255;
                    };
                    sc = RGB(mix(GetRValue(kMuted), GetRValue(kAccent)),
                             mix(GetGValue(kMuted), GetGValue(kAccent)),
                             mix(GetBValue(kMuted), GetBValue(kAccent)));
                } else {
                    g_statusTick = 0;
                }
            }
            RECT foot = {S(24), S(626), S(960), S(652)};
            DrawTextC(dc, g_status, foot, sc, g_fBody,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            // 版本号放标题条右端，下面那行留给导出按钮
            RECT ver = {rc.right - S(130), S(20), rc.right - S(24), S(44)};
            DrawTextC(dc, L"v0.2", ver, kMuted, g_fSmall,
                      DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

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
                bool hover = (g_hoverBtn == static_cast<int>(d->CtlID));
                // 强调条改由主窗口统一画（要滑动的），这里只铺底和文字
                HBRUSH bg =
                    CreateSolidBrush(active ? kPage : (hover ? kSoft : kSide));
                FillRect(dc, &r, bg);
                DeleteObject(bg);
                RECT t = r;
                t.left += S(18);
                DrawTextC(dc, buf, t, active ? kAccent : kInk,
                          active || hover ? g_fBold : g_fNav,
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                return TRUE;
            }

            if (d->CtlID == 2001) {
                COLORREF fill = pressed   ? RGB(0x10, 0x18, 0xc8)
                                : (g_hoverBtn == 2001) ? RGB(0x2c, 0x38, 0xff)
                                                       : kAccent;
                FillRounded(dc, r, S(7), fill, fill);
                DrawTextC(dc, buf, r, RGB(255, 255, 255), g_fBold,
                          DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                return TRUE;
            }

            bool primary = (d->CtlID == 6100);
            bool hov = (g_hoverBtn == static_cast<int>(d->CtlID));
            FillRounded(dc, r, S(6), pressed ? kSoft : (hov ? kSoft : kPage),
                        primary || hov ? kAccent : kLine);
            DrawTextC(dc, buf, r, primary || hov ? kAccent : kInk,
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
            if (nh->idFrom == 6400) {
                if (nh->code == LVN_ITEMCHANGED) {
                    NMLISTVIEW* nv = reinterpret_cast<NMLISTVIEW*>(l);
                    // 只看“选中”这一类变化；刷新列表时也会发通知，
                    // 但那时选中的就是 g_curClass 本身，天然被挡掉
                    if ((nv->uNewState & LVIS_SELECTED) &&
                        !(nv->uOldState & LVIS_SELECTED) && nv->iItem >= 0 &&
                        nv->iItem != g_curClass)
                        SelectClass(nv->iItem);
                } else if (nh->code == NM_DBLCLK) {
                    SetFocus(g_className);
                    SendMessageW(g_className, EM_SETSEL, 0, -1);
                }
            } else if (nh->idFrom == 6200) {
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

        case WM_TIMER:
            if (w == kTimerBar) {
                g_barT += 15.0 / kBarMs;
                if (g_barT >= 1.0) {
                    g_barT = 1.0;
                    KillTimer(h, kTimerBar);
                }
                double e = 1.0 - (1.0 - g_barT) * (1.0 - g_barT);  // 缓出
                g_barY = g_barFrom + (g_barTo - g_barFrom) * e;
                RECT r = {0, S(kSideTop), S(20), S(kSideBottom)};
                InvalidateRect(h, &r, FALSE);
            } else if (w == kTimerStatus) {
                RECT r = {S(24), S(626), S(790), S(656)};
                InvalidateRect(h, &r, FALSE);
                if (GetTickCount() - g_statusTick >= kStatusMs)
                    KillTimer(h, kTimerStatus);
            }
            return 0;

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
    g_featCard.assign(g_features.size(), -1);

    // 起手给一个空班级，用户可以直接改名或往里导名单
    g_classes.push_back({"我的班级", {}});
    g_barY = kNavY;

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
