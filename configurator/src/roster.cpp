#include "roster.h"
#include "zipfile.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace roster {
namespace {

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = out.empty() ||
              (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) &&
               read == out.size());
    CloseHandle(h);
    return ok;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n,
                        nullptr, nullptr);
    return s;
}

std::wstring FromCodePage(const std::vector<uint8_t>& raw, UINT cp) {
    if (raw.empty()) return {};
    int n = MultiByteToWideChar(cp, 0, reinterpret_cast<const char*>(raw.data()),
                                static_cast<int>(raw.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), 0);
    MultiByteToWideChar(cp, 0, reinterpret_cast<const char*>(raw.data()),
                        static_cast<int>(raw.size()), &w[0], n);
    return w;
}

bool LooksLikeUtf8(const std::vector<uint8_t>& b) {
    size_t i = 0;
    while (i < b.size()) {
        uint8_t c = b[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
        if (extra < 0 || i + extra >= b.size()) return false;
        for (int k = 1; k <= extra; k++)
            if ((b[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

// ---- XML 小工具 ----

void DecodeEntities(std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            o += s[i++];
            continue;
        }
        size_t end = s.find(';', i);
        if (end == std::string::npos || end - i > 10) {
            o += s[i++];
            continue;
        }
        std::string ent = s.substr(i + 1, end - i - 1);
        if (ent == "amp") o += '&';
        else if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            long cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                          ? std::strtol(ent.c_str() + 2, nullptr, 16)
                          : std::strtol(ent.c_str() + 1, nullptr, 10);
            if (cp > 0 && cp < 0x80) {
                o += static_cast<char>(cp);
            } else if (cp >= 0x80) {
                wchar_t w = static_cast<wchar_t>(cp);
                o += ToUtf8(std::wstring(1, w));
            }
        } else {
            o += s.substr(i, end - i + 1);
        }
        i = end + 1;
    }
    s.swap(o);
}

void CollectText(const std::string& xml, size_t from, size_t to, std::string& out) {
    size_t i = from;
    while (i < to) {
        size_t t = xml.find("<t", i);
        if (t == std::string::npos || t >= to) break;
        char after = (t + 2 < xml.size()) ? xml[t + 2] : '>';
        if (after != '>' && after != ' ' && after != '/') {
            i = t + 2;
            continue;
        }
        size_t gt = xml.find('>', t);
        if (gt == std::string::npos || gt >= to) break;
        if (xml[gt - 1] == '/') {
            i = gt + 1;
            continue;
        }
        size_t close = xml.find("</t>", gt);
        if (close == std::string::npos || close > to) break;
        out += xml.substr(gt + 1, close - gt - 1);
        i = close + 4;
    }
    DecodeEntities(out);
}

std::string Attr(const std::string& tag, const char* key) {
    std::string pat = std::string(key) + "=\"";
    size_t p = tag.find(pat);
    if (p == std::string::npos) return {};
    p += pat.size();
    size_t e = tag.find('"', p);
    if (e == std::string::npos) return {};
    return tag.substr(p, e - p);
}

int ColumnOfRef(const std::string& ref) {
    int col = 0;
    for (char c : ref) {
        if (c >= 'A' && c <= 'Z') col = col * 26 + (c - 'A' + 1);
        else if (c >= 'a' && c <= 'z') col = col * 26 + (c - 'a' + 1);
        else break;
    }
    return col - 1;
}

bool ParseSharedStrings(const std::string& xml, std::vector<std::string>& out) {
    size_t i = 0;
    while ((i = xml.find("<si", i)) != std::string::npos) {
        char after = (i + 3 < xml.size()) ? xml[i + 3] : '>';
        if (after != '>' && after != ' ' && after != '/') {
            i += 3;
            continue;
        }
        size_t gt = xml.find('>', i);
        if (gt == std::string::npos) return true;
        if (xml[gt - 1] == '/') {
            out.emplace_back();
            i = gt + 1;
            continue;
        }
        size_t close = xml.find("</si>", gt);
        if (close == std::string::npos) return true;
        std::string s;
        CollectText(xml, gt + 1, close, s);
        out.push_back(s);
        i = close + 5;
    }
    return true;
}

void ParseSheet(const std::string& xml, const std::vector<std::string>& shared,
                Sheet& sheet) {
    size_t i = 0;
    while ((i = xml.find("<row", i)) != std::string::npos) {
        char after = (i + 4 < xml.size()) ? xml[i + 4] : '>';
        if (after != '>' && after != ' ' && after != '/') {
            i += 4;
            continue;
        }
        size_t gt = xml.find('>', i);
        if (gt == std::string::npos) break;
        if (xml[gt - 1] == '/') {
            sheet.emplace_back();
            i = gt + 1;
            continue;
        }
        size_t rend = xml.find("</row>", gt);
        if (rend == std::string::npos) rend = xml.size();

        std::vector<std::string> row;
        size_t c = gt + 1;
        while (c < rend && (c = xml.find("<c", c)) != std::string::npos && c < rend) {
            char ca = (c + 2 < xml.size()) ? xml[c + 2] : '>';
            if (ca != '>' && ca != ' ' && ca != '/') {
                c += 2;
                continue;
            }
            size_t cgt = xml.find('>', c);
            if (cgt == std::string::npos) break;
            std::string tag = xml.substr(c, cgt - c);
            int col = ColumnOfRef(Attr(tag, "r"));
            if (col < 0) col = static_cast<int>(row.size());
            std::string type = Attr(tag, "t");
            std::string value;

            if (xml[cgt - 1] == '/') {
                c = cgt + 1;
            } else {
                size_t cend = xml.find("</c>", cgt);
                if (cend == std::string::npos) cend = rend;
                if (type == "s") {
                    size_t v = xml.find("<v>", cgt);
                    if (v != std::string::npos && v < cend) {
                        size_t ve = xml.find("</v>", v);
                        long idx = std::strtol(xml.substr(v + 3, ve - v - 3).c_str(),
                                               nullptr, 10);
                        if (idx >= 0 && idx < static_cast<long>(shared.size()))
                            value = shared[idx];
                    }
                } else if (type == "inlineStr") {
                    CollectText(xml, cgt, cend, value);
                } else {
                    size_t v = xml.find("<v>", cgt);
                    if (v != std::string::npos && v < cend) {
                        size_t ve = xml.find("</v>", v);
                        value = xml.substr(v + 3, ve - v - 3);
                        DecodeEntities(value);
                    }
                }
                c = cend + 4;
            }
            if (static_cast<int>(row.size()) <= col) row.resize(col + 1);
            row[col] = Trim(value);
        }
        sheet.push_back(row);
        i = rend + 6;
    }
}

// ---- 识别 ----

bool Contains(const std::string& hay, const char* needle) {
    std::string h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}

bool IsNameHeader(const std::string& s) {
    if (s.empty() || s.size() > 12) return false;
    return Contains(s, "姓名") || Contains(s, "名字") || Contains(s, "学生") ||
           Contains(s, "名单") || Contains(s, "name");
}

bool IsIdHeader(const std::string& s) {
    if (s.empty() || s.size() > 12) return false;
    return Contains(s, "学号") || Contains(s, "学籍") || Contains(s, "编号") ||
           Contains(s, "序号") || Contains(s, "id") || Contains(s, "no");
}

bool AllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool RowEmpty(const std::vector<std::string>& r) {
    for (const auto& c : r)
        if (!Trim(c).empty()) return false;
    return true;
}

size_t ColCount(const Sheet& sheet) {
    size_t n = 0;
    for (const auto& r : sheet) n = (std::max)(n, r.size());
    return n;
}

std::string Cell(const std::vector<std::string>& r, int c) {
    if (c < 0 || c >= static_cast<int>(r.size())) return {};
    return Trim(r[c]);
}

}  // namespace

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    auto blank = [](const std::string& t, size_t i) {
        unsigned char c = static_cast<unsigned char>(t[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return true;
        // UTF-8 全角空格 U+3000 = E3 80 80，零宽/BOM = EF BB BF
        if (i + 2 < t.size() && c == 0xE3 && static_cast<unsigned char>(t[i + 1]) == 0x80 &&
            static_cast<unsigned char>(t[i + 2]) == 0x80)
            return true;
        if (i + 2 < t.size() && c == 0xEF && static_cast<unsigned char>(t[i + 1]) == 0xBB &&
            static_cast<unsigned char>(t[i + 2]) == 0xBF)
            return true;
        return false;
    };
    while (a < b && blank(s, a)) a += (static_cast<unsigned char>(s[a]) < 0x80 ? 1 : 3);
    while (b > a && blank(s, b - 1)) b--;
    if (b <= a) return {};
    std::string out = s.substr(a, b - a);
    // 去中间的零宽字符
    std::string clean;
    for (size_t i = 0; i < out.size();) {
        if (i + 2 < out.size() && static_cast<unsigned char>(out[i]) == 0xEF &&
            static_cast<unsigned char>(out[i + 1]) == 0xBB &&
            static_cast<unsigned char>(out[i + 2]) == 0xBF) {
            i += 3;
            continue;
        }
        clean += out[i++];
    }
    return clean;
}

bool ReadXlsx(const std::wstring& path, Sheet& sheet, std::string& err) {
    std::vector<uint8_t> raw;
    if (!ReadFileBytes(path, raw)) {
        err = "无法读取文件。";
        return false;
    }
    const uint8_t ole[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    if (raw.size() > 8 && std::memcmp(raw.data(), ole, 8) == 0) {
        err = "这是旧版 .xls 格式。请在 Excel 里另存为 .xlsx 后再导入。";
        return false;
    }
    std::vector<zip::Entry> entries;
    if (!zip::List(raw, entries)) {
        err = "文件不是有效的 xlsx（或已损坏）。";
        return false;
    }

    std::string sharedXml, sheetXml;
    std::string fallback;
    for (const auto& e : entries) {
        if (e.name == "xl/sharedStrings.xml") {
            std::vector<uint8_t> b;
            if (zip::Extract(raw, e, b)) sharedXml.assign(b.begin(), b.end());
        } else if (e.name.rfind("xl/worksheets/", 0) == 0 &&
                   e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".xml") == 0) {
            if (e.name == "xl/worksheets/sheet1.xml") sheetXml = e.name;
            else if (fallback.empty() || e.name < fallback) fallback = e.name;
        }
    }
    if (sheetXml.empty()) sheetXml = fallback;
    if (sheetXml.empty()) {
        err = "文件里没有找到工作表。";
        return false;
    }
    std::vector<uint8_t> sb;
    for (const auto& e : entries)
        if (e.name == sheetXml && zip::Extract(raw, e, sb))
            sheetXml.assign(sb.begin(), sb.end());
    if (sb.empty()) {
        err = "工作表读取失败。";
        return false;
    }

    std::vector<std::string> shared;
    if (!sharedXml.empty()) ParseSharedStrings(sharedXml, shared);
    ParseSheet(sheetXml, shared, sheet);
    while (!sheet.empty() && RowEmpty(sheet.back())) sheet.pop_back();
    if (sheet.empty()) {
        err = "工作表是空的。";
        return false;
    }
    return true;
}

bool ReadCsv(const std::wstring& path, Sheet& sheet, std::string& err) {
    std::vector<uint8_t> raw;
    if (!ReadFileBytes(path, raw)) {
        err = "无法读取文件。";
        return false;
    }
    std::string text;
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        std::wstring w(reinterpret_cast<const wchar_t*>(raw.data() + 2),
                       (raw.size() - 2) / 2);
        text = ToUtf8(w);
    } else {
        size_t start = (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
                           ? 3 : 0;
        std::vector<uint8_t> body(raw.begin() + start, raw.end());
        text = LooksLikeUtf8(body) ? std::string(body.begin(), body.end())
                                   : ToUtf8(FromCodePage(body, 936));
    }

    std::vector<std::string> cells;
    std::string field;
    bool quoted = false;
    auto endField = [&]() { cells.push_back(Trim(field)); field.clear(); };
    auto endRow = [&]() {
        endField();
        sheet.push_back(cells);
        cells.clear();
    };
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    i++;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',' || c == '\t') {
            endField();
        } else if (c == '\r') {
        } else if (c == '\n') {
            endRow();
        } else {
            field += c;
        }
    }
    if (!field.empty() || !cells.empty()) endRow();
    while (!sheet.empty() && RowEmpty(sheet.back())) sheet.pop_back();
    if (sheet.empty()) {
        err = "文件里没有可用的内容。";
        return false;
    }
    return true;
}

Result Detect(const Sheet& sheet) {
    Result r;
    size_t cols = ColCount(sheet);
    if (cols == 0) {
        r.message = "没有读到任何列。";
        return r;
    }

    // 表头：第一行里出现“姓名 / 学号”这类字样
    int headerRow = -1, nameCol = -1, idCol = -1;
    for (size_t i = 0; i < sheet.size() && i < 5; i++) {
        for (size_t c = 0; c < sheet[i].size(); c++) {
            std::string v = Trim(sheet[i][c]);
            if (IsNameHeader(v) && nameCol < 0) nameCol = static_cast<int>(c);
            if (IsIdHeader(v) && idCol < 0) idCol = static_cast<int>(c);
        }
        if (nameCol >= 0 || idCol >= 0) {
            headerRow = static_cast<int>(i);
            break;
        }
    }
    r.headerSkipped = headerRow >= 0;

    if (nameCol < 0 || idCol < 0) {
        // 按内容判断：数字多的列当学号，文字多的列当姓名
        std::vector<int> nonEmpty(cols, 0), numeric(cols, 0);
        size_t first = static_cast<size_t>(headerRow + 1);
        for (size_t i = first; i < sheet.size(); i++)
            for (size_t c = 0; c < sheet[i].size() && c < cols; c++) {
                std::string v = Trim(sheet[i][c]);
                if (v.empty()) continue;
                nonEmpty[c]++;
                if (AllDigits(v)) numeric[c]++;
            }
        int bestText = -1, bestTextCount = 0;
        for (size_t c = 0; c < cols; c++) {
            if (nonEmpty[c] == 0) continue;
            if (numeric[c] * 2 > nonEmpty[c]) continue;  // 大多数是数字，跳过
            if (nonEmpty[c] > bestTextCount) {
                bestTextCount = nonEmpty[c];
                bestText = static_cast<int>(c);
            }
        }
        if (nameCol < 0) nameCol = bestText;
        if (idCol < 0) {
            for (size_t c = 0; c < cols; c++) {
                if (static_cast<int>(c) == nameCol || nonEmpty[c] == 0) continue;
                if (numeric[c] * 2 >= nonEmpty[c]) {
                    idCol = static_cast<int>(c);
                    break;
                }
            }
        }
    }

    if (nameCol < 0) {
        r.message = "没能认出哪一列是姓名。请确认表格里有一列是学生姓名。";
        return r;
    }

    size_t first = static_cast<size_t>(headerRow + 1);
    for (size_t i = first; i < sheet.size(); i++) {
        std::string name = Cell(sheet[i], nameCol);
        if (name.empty()) continue;
        Student s;
        std::string idText = Cell(sheet[i], idCol);
        s.id = AllDigits(idText) ? std::atoi(idText.c_str()) : 0;
        s.name = name;
        r.students.push_back(s);
    }
    if (r.students.empty()) {
        r.message = "姓名列里没有内容。";
        return r;
    }
    for (size_t i = 0; i < r.students.size(); i++)
        if (r.students[i].id <= 0) r.students[i].id = static_cast<int>(i) + 1;

    r.nameColumn = nameCol;
    r.idColumn = idCol;
    r.ok = true;
    r.message = "识别到 " + std::to_string(r.students.size()) + " 人";
    r.message += "，姓名取自第 " + std::to_string(nameCol + 1) + " 列";
    if (idCol >= 0 && idCol != nameCol)
        r.message += "，学号取自第 " + std::to_string(idCol + 1) + " 列";
    else
        r.message += "，没有学号列，按顺序编号";
    if (r.headerSkipped) r.message += "，已跳过表头";
    return r;
}

}  // namespace roster
