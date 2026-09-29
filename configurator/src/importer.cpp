#include "importer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "roster.h"

namespace imp {
namespace {

constexpr size_t kNpos = std::string::npos;

void AppendUtf8(std::string& o, unsigned cp) {
    if (cp < 0x80) {
        o += static_cast<char>(cp);
    } else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18));
        o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// 只认生成器写出来的那几种字面量：对象、数组、字符串、数字。
// 手改过的文件里若有注释、引号包起来的键名、十六进制数字，也一并认下。
struct Cursor {
    const std::string& s;
    size_t i;
};

void SkipWs(Cursor& c) {
    while (c.i < c.s.size()) {
        char ch = c.s[c.i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            c.i++;
        } else if (ch == '/' && c.i + 1 < c.s.size() && c.s[c.i + 1] == '/') {
            while (c.i < c.s.size() && c.s[c.i] != '\n') c.i++;
        } else if (ch == '/' && c.i + 1 < c.s.size() && c.s[c.i + 1] == '*') {
            size_t e = c.s.find("*/", c.i + 2);
            c.i = (e == kNpos) ? c.s.size() : e + 2;
        } else {
            break;
        }
    }
}

bool ReadString(Cursor& c, std::string& out) {
    SkipWs(c);
    if (c.i >= c.s.size() || (c.s[c.i] != '"' && c.s[c.i] != '\'')) return false;
    char quote = c.s[c.i++];
    while (c.i < c.s.size()) {
        char ch = c.s[c.i++];
        if (ch == quote) return true;
        if (ch != '\\') {
            out += ch;
            continue;
        }
        if (c.i >= c.s.size()) break;
        char e = c.s[c.i++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'v': out += '\v'; break;
            case '0': out += '\0'; break;
            case 'x': {
                unsigned cp = 0;
                for (int k = 0; k < 2 && c.i < c.s.size(); k++) {
                    char h = c.s[c.i];
                    int d = (h >= '0' && h <= '9') ? h - '0'
                            : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                            : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                    if (d < 0) break;
                    cp = cp * 16 + static_cast<unsigned>(d);
                    c.i++;
                }
                AppendUtf8(out, cp);
                break;
            }
            case 'u': {
                unsigned cp = 0;
                for (int k = 0; k < 4 && c.i < c.s.size(); k++) {
                    char h = c.s[c.i];
                    int d = (h >= '0' && h <= '9') ? h - '0'
                            : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                            : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                    if (d < 0) break;
                    cp = cp * 16 + static_cast<unsigned>(d);
                    c.i++;
                }
                // 代理对：高位后面还跟着一个 \uXXXX
                if (cp >= 0xD800 && cp <= 0xDBFF && c.i + 1 < c.s.size() &&
                    c.s[c.i] == '\\' && c.s[c.i + 1] == 'u') {
                    size_t save = c.i;
                    c.i += 2;
                    unsigned lo = 0;
                    for (int k = 0; k < 4 && c.i < c.s.size(); k++) {
                        char h = c.s[c.i];
                        int d = (h >= '0' && h <= '9') ? h - '0'
                                : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                                : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                        if (d < 0) break;
                        lo = lo * 16 + static_cast<unsigned>(d);
                        c.i++;
                    }
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        c.i = save;
                    }
                }
                AppendUtf8(out, cp);
                break;
            }
            default: out += e; break;
        }
    }
    return false;
}

bool ReadNumber(Cursor& c, long& out) {
    SkipWs(c);
    size_t a = c.i;
    if (c.i < c.s.size() && (c.s[c.i] == '-' || c.s[c.i] == '+')) c.i++;
    if (c.i + 1 < c.s.size() && c.s[c.i] == '0' &&
        (c.s[c.i + 1] == 'x' || c.s[c.i + 1] == 'X')) {
        c.i += 2;
        size_t b = c.i;
        while (c.i < c.s.size() && isxdigit((unsigned char)c.s[c.i])) c.i++;
        if (c.i == b) return false;
        out = std::strtol(c.s.substr(a, c.i - a).c_str(), nullptr, 16);
        return true;
    }
    while (c.i < c.s.size() &&
           (isdigit((unsigned char)c.s[c.i]) || c.s[c.i] == '.' ||
            c.s[c.i] == 'e' || c.s[c.i] == 'E' || c.s[c.i] == '+' ||
            c.s[c.i] == '-')) {
        c.i++;
    }
    if (c.i == a) return false;
    out = std::strtol(c.s.substr(a, c.i - a).c_str(), nullptr, 10);
    return true;
}

bool ReadKey(Cursor& c, std::string& key) {
    SkipWs(c);
    if (c.i < c.s.size() && (c.s[c.i] == '"' || c.s[c.i] == '\''))
        return ReadString(c, key);
    size_t a = c.i;
    while (c.i < c.s.size() &&
           (isalnum((unsigned char)c.s[c.i]) || c.s[c.i] == '_' ||
            c.s[c.i] == '$')) {
        c.i++;
    }
    if (c.i == a) return false;
    key = c.s.substr(a, c.i - a);
    return true;
}

bool SkipValue(Cursor& c);

bool SkipBracketed(Cursor& c, char open, char close) {
    SkipWs(c);
    if (c.i >= c.s.size() || c.s[c.i] != open) return false;
    int depth = 0;
    while (c.i < c.s.size()) {
        char ch = c.s[c.i];
        if (ch == '"' || ch == '\'') {
            std::string dummy;
            if (!ReadString(c, dummy)) return false;
            continue;
        }
        if (ch == '/' && c.i + 1 < c.s.size() &&
            (c.s[c.i + 1] == '/' || c.s[c.i + 1] == '*')) {
            SkipWs(c);
            continue;
        }
        if (ch == open) depth++;
        else if (ch == close) {
            depth--;
            if (depth == 0) {
                c.i++;
                return true;
            }
        }
        c.i++;
    }
    return false;
}

bool SkipValue(Cursor& c) {
    SkipWs(c);
    if (c.i >= c.s.size()) return false;
    char ch = c.s[c.i];
    if (ch == '{') return SkipBracketed(c, '{', '}');
    if (ch == '[') return SkipBracketed(c, '[', ']');
    if (ch == '"' || ch == '\'') {
        std::string dummy;
        return ReadString(c, dummy);
    }
    long n = 0;
    return ReadNumber(c, n);
}

// { id: 1, name: "张三" }：键序随意，不认识的键跳过
bool ReadStudent(Cursor& c, roster::Student& out) {
    SkipWs(c);
    if (c.i >= c.s.size() || c.s[c.i] != '{') return false;
    c.i++;
    while (true) {
        SkipWs(c);
        if (c.i >= c.s.size()) return false;
        if (c.s[c.i] == '}') {
            c.i++;
            return true;
        }
        if (c.s[c.i] == ',') {
            c.i++;
            continue;
        }
        std::string key;
        if (!ReadKey(c, key)) return false;
        SkipWs(c);
        if (c.i >= c.s.size() || c.s[c.i] != ':') return false;
        c.i++;
        SkipWs(c);
        if (key == "id") {
            long n = 0;
            if (!ReadNumber(c, n)) return false;
            out.id = static_cast<int>(n);
        } else if (key == "name") {
            if (!ReadString(c, out.name)) return false;
        } else if (!SkipValue(c)) {
            return false;
        }
    }
}

bool ReadStudentArray(Cursor& c, std::vector<roster::Student>& out) {
    SkipWs(c);
    if (c.i >= c.s.size() || c.s[c.i] != '[') return false;
    c.i++;
    while (true) {
        SkipWs(c);
        if (c.i >= c.s.size()) return false;
        if (c.s[c.i] == ']') {
            c.i++;
            return true;
        }
        if (c.s[c.i] == ',') {
            c.i++;
            continue;
        }
        roster::Student s;
        if (!ReadStudent(c, s)) return false;
        s.name = roster::Trim(s.name);
        if (!s.name.empty()) out.push_back(s);
    }
}

// ["张三", "李四", ...]
bool ReadStringArray(Cursor& c, std::vector<roster::Student>& out) {
    SkipWs(c);
    if (c.i >= c.s.size() || c.s[c.i] != '[') return false;
    c.i++;
    while (true) {
        SkipWs(c);
        if (c.i >= c.s.size()) return false;
        if (c.s[c.i] == ']') {
            c.i++;
            return true;
        }
        if (c.s[c.i] == ',') {
            c.i++;
            continue;
        }
        std::string v;
        if (!ReadString(c, v)) return false;
        v = roster::Trim(v);
        if (v.empty()) continue;
        roster::Student s;
        s.id = static_cast<int>(out.size()) + 1;
        s.name = v;
        out.push_back(s);
    }
}

// { name: "A2班", students: [...] }
bool ReadKlass(Cursor& c, gen::Klass& out) {
    SkipWs(c);
    if (c.i >= c.s.size() || c.s[c.i] != '{') return false;
    c.i++;
    while (true) {
        SkipWs(c);
        if (c.i >= c.s.size()) return false;
        if (c.s[c.i] == '}') {
            c.i++;
            return true;
        }
        if (c.s[c.i] == ',') {
            c.i++;
            continue;
        }
        std::string key;
        if (!ReadKey(c, key)) return false;
        SkipWs(c);
        if (c.i >= c.s.size() || c.s[c.i] != ':') return false;
        c.i++;
        SkipWs(c);
        if (key == "name") {
            if (!ReadString(c, out.name)) return false;
            out.name = roster::Trim(out.name);
        } else if (key == "students" || key == "roster" || key == "names") {
            if (!ReadStudentArray(c, out.students)) return false;
        } else if (!SkipValue(c)) {
            return false;
        }
    }
}

bool ReadKlassArray(Cursor& c, std::vector<gen::Klass>& out) {
    SkipWs(c);
    if (c.i >= c.s.size() || c.s[c.i] != '[') return false;
    c.i++;
    while (true) {
        SkipWs(c);
        if (c.i >= c.s.size()) return false;
        if (c.s[c.i] == ']') {
            c.i++;
            return true;
        }
        if (c.s[c.i] == ',') {
            c.i++;
            continue;
        }
        gen::Klass k;
        if (!ReadKlass(c, k)) return false;
        out.push_back(k);
    }
}

// 找 `NAME` 后面跟着 `=` 的位置，返回等号之后（跳过空白）的起点。
// 只认赋值，这样 `CLASS_PRESETS.forEach(...)` 这类引用不会被当成定义。
size_t FindAssignment(const std::string& s, const std::string& name) {
    size_t p = 0;
    while ((p = s.find(name, p)) != kNpos) {
        size_t q = p + name.size();
        if (q < s.size() &&
            (isalnum((unsigned char)s[q]) || s[q] == '_' || s[q] == '$')) {
            p = q;
            continue;
        }
        Cursor c{s, q};
        SkipWs(c);
        if (c.i < s.size() && s[c.i] == '=') {
            c.i++;
            SkipWs(c);
            return c.i;
        }
        p = q;
    }
    return kNpos;
}

// 学号缺了或重了，按当前位置补一个
void FixIds(std::vector<roster::Student>& v) {
    for (size_t i = 0; i < v.size(); i++) {
        bool dup = false;
        for (size_t j = 0; j < i; j++)
            if (v[j].id == v[i].id) {
                dup = true;
                break;
            }
        if (v[i].id <= 0 || dup) v[i].id = static_cast<int>(i) + 1;
    }
}

void Normalize(std::vector<gen::Klass>& classes) {
    for (size_t i = 0; i < classes.size(); i++) {
        if (classes[i].name.empty())
            classes[i].name = "班级 " + std::to_string(i + 1);
        FixIds(classes[i].students);
    }
}

// 名字完全一样、人数也一样的班级，只留一个。空班级原样保留：
// 它没有学生可丢，读回工作区时保持原样更忠实。
void Dedupe(std::vector<gen::Klass>& v) {
    std::vector<gen::Klass> out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].students.empty()) {
            out.push_back(v[i]);
            continue;
        }
        bool dup = false;
        for (size_t j = 0; j < out.size(); j++) {
            if (out[j].name != v[i].name ||
                out[j].students.size() != v[i].students.size())
                continue;
            bool same = true;
            for (size_t k = 0; k < out[j].students.size(); k++)
                if (out[j].students[k].name != v[i].students[k].name) {
                    same = false;
                    break;
                }
            if (same) {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(v[i]);
    }
    v.swap(out);
}

// ---------- 判断这是不是点名器 ----------

std::string Lower(const std::string& s) {
    std::string o = s;
    for (size_t i = 0; i < o.size(); i++)
        o[i] = static_cast<char>(tolower((unsigned char)o[i]));
    return o;
}

// needle 必须是小写；这里不复制整份 haystack，页面动辄几百 KB
size_t LowerFind(const std::string& hay, const std::string& needle, size_t from = 0) {
    size_t n = needle.size();
    if (n == 0 || hay.size() < n) return kNpos;
    for (size_t i = from; i + n <= hay.size(); i++) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)hay[i + k]) == needle[k]) k++;
        if (k == n) return i;
    }
    return kNpos;
}

std::string ExtractTitle(const std::string& s) {
    std::string low = Lower(s);
    size_t a = low.find("<title");
    if (a == kNpos) return {};
    size_t gt = low.find('>', a);
    if (gt == kNpos) return {};
    size_t b = low.find("</title", gt);
    if (b == kNpos) return {};
    return roster::Trim(s.substr(gt + 1, b - gt - 1));
}

const char* const kMarks[] = {
    "initSystemData", "studentsData",     "big-name",       "MODE_INFO",
    "currentClassBadge", "acoc.namepicker", "startupScreen", "loadPresetBtn",
    "zone-random",    "glitchProbability", "classNameStr",   "装载预设",
};

int PickerSignals(const std::string& s, std::string& hits) {
    int n = 0;
    for (const char* mk : kMarks) {
        if (s.find(mk) == kNpos) continue;
        n++;
        if (!hits.empty()) hits += "、";
        hits += mk;
    }
    return n;
}

// ---------- 姓名与班级名 ----------

size_t Utf8Len(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) n++;
    return n;
}

bool HasCjk(const std::string& s) {
    for (size_t i = 0; i + 2 < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xF0) != 0xE0) continue;
        unsigned cp = ((c & 0x0F) << 12) |
                      ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF))
            return true;
    }
    return false;
}

// 界面上的固定说法，免得把按钮文字当成学生
const char* const kUiWords[] = {
    "概率修改", "顺位设定", "统计数据", "界面主题", "高级数据管理", "其他",
    "开启实验性功能", "装载预设", "数据装载节点", "外部数据源导入", "继续上次",
    "全随机模式", "不重复模式", "连续抽取", "轮字抽取", "当前模式", "暗境节点",
    "经典节点", "确定", "取消", "关闭", "设置", "导出", "导入", "重置", "返回",
    "姓名", "学号", "序号", "班级", "学生", "名单", "概率", "权重", "统计",
    "锁定", "解锁", "上移", "下移", "删除", "添加", "清空", "保存", "加载",
    "刷新", "开始", "停止", "抽取", "结果", "模式", "主题", "数据", "实验性",
};

bool LooksLikeName(const std::string& s, bool requireCjk) {
    if (s.empty() || s.size() > 60) return false;
    size_t n = Utf8Len(s);
    if (n < 2 || n > 16) return false;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            // 西文姓名里允许的字符；数字和标点一律不算
            if (isalpha(c) || c == '-' || c == '\'' || c == '.') continue;
            return false;
        }
        if (c == 0xE2 || c == 0xEF) return false;  // 零宽字符、各种符号
    }
    if (requireCjk && !HasCjk(s)) return false;
    for (const char* w : kUiWords)
        if (s == w) return false;
    return true;
}

std::string ClassNameFromVar(const std::string& var) {
    std::string v = var;
    const char* suffix[] = {"_NAMES", "_NAME", "_STUDENTS", "_STUDENT",
                            "_ROSTER", "NAMES", "STUDENTS", "ROSTER", "LIST"};
    for (const char* suf : suffix) {
        size_t n = std::string(suf).size();
        if (v.size() > n && v.compare(v.size() - n, n, suf) == 0) {
            v = v.substr(0, v.size() - n);
            break;
        }
    }
    while (!v.empty() && v[0] == '_') v.erase(v.begin());
    if (v.empty()) return "导入的班级";
    if (isdigit((unsigned char)v[v.size() - 1])) return v + "班";
    return v;
}

// ---------- 各种取名单的办法 ----------

bool Used(const std::vector<std::string>& used, const std::string& var) {
    for (size_t i = 0; i < used.size(); i++)
        if (used[i] == var) return true;
    return false;
}

// 一个数组里可能是 {id, name} 对象，也可能直接是姓名
bool ReadAnyNameArray(Cursor& c, std::vector<roster::Student>& out,
                      bool requireCjkForStrings) {
    {
        Cursor probe = c;
        std::vector<roster::Student> v;
        if (ReadStudentArray(probe, v) && v.size() >= 2) {
            out.swap(v);
            c.i = probe.i;
            return true;
        }
    }
    Cursor probe = c;
    std::vector<roster::Student> v;
    if (ReadStringArray(probe, v) && v.size() >= 2) {
        for (size_t i = 0; i < v.size(); i++)
            if (!LooksLikeName(v[i].name, requireCjkForStrings)) return false;
        out.swap(v);
        c.i = probe.i;
        return true;
    }
    return false;
}

// initSystemData(A2_NAMES, "A2班 (预设)")：班级名照抄，它就是存档的键
struct Preset {
    std::string var;
    std::string name;
};

void PresetListeners(const std::string& s, std::vector<Preset>& out) {
    const std::string kCall = "initSystemData";
    size_t p = 0;
    while ((p = s.find(kCall, p)) != kNpos) {
        size_t q = p + kCall.size();
        Cursor c{s, q};
        SkipWs(c);
        if (c.i >= s.size() || s[c.i] != '(') {
            p = q;
            continue;
        }
        c.i++;
        std::string var;
        if (ReadKey(c, var)) {
            SkipWs(c);
            if (c.i < s.size() && s[c.i] == ',') c.i++;
            std::string name;
            if (ReadString(c, name)) {
                Preset pr;
                pr.var = var;
                pr.name = roster::Trim(name);
                out.push_back(pr);
            }
        }
        p = q;
    }
}

// 脚本里任何 `标识符 = [ ... ]`：班名从标识符推（A2_NAMES -> A2班）
void GenericArrays(const std::string& s, std::vector<std::string>& used,
                   std::vector<gen::Klass>& out) {
    size_t p = 0;
    while ((p = s.find('=', p)) != kNpos) {
        size_t eq = p;
        p = eq + 1;
        // 标识符：等号前紧邻的 _ $ 字母数字
        size_t e = eq;
        while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
        size_t b = e;
        while (b > 0 && (isalnum((unsigned char)s[b - 1]) || s[b - 1] == '_' ||
                         s[b - 1] == '$'))
            b--;
        if (b == e) continue;
        std::string var = s.substr(b, e - b);
        if (isdigit((unsigned char)var[0])) continue;
        if (Used(used, var)) continue;
        Cursor c{s, eq + 1};
        SkipWs(c);
        if (c.i >= s.size() || s[c.i] != '[') continue;
        bool nameish = Lower(var).find("name") != kNpos ||
                       Lower(var).find("student") != kNpos ||
                       Lower(var).find("roster") != kNpos;
        std::vector<roster::Student> v;
        if (!ReadAnyNameArray(c, v, !nameish)) continue;
        gen::Klass k;
        k.name = ClassNameFromVar(var);
        k.students.swap(v);
        out.push_back(k);
        used.push_back(var);
        p = c.i;
    }
}

std::string StripTags(const std::string& s) {
    std::string o;
    bool in = false;
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch == '<') {
            in = true;
            continue;
        }
        if (ch == '>') {
            in = false;
            continue;
        }
        if (!in) o += ch;
    }
    struct Ent {
        const char* from;
        const char* to;
    } ents[] = {{"&lt;", "<"},   {"&gt;", ">"},    {"&quot;", "\""},
                {"&#39;", "'"},  {"&nbsp;", " "},  {"&amp;", "&"}};
    for (const auto& e : ents) {
        size_t q = 0;
        while ((q = o.find(e.from, q)) != kNpos) {
            o.replace(q, std::string(e.from).size(), e.to);
            q += std::string(e.to).size();
        }
    }
    return roster::Trim(o);
}

bool IsNameHeader(const std::string& s) {
    return s == "姓名" || s == "名字" || s == "学生" || s == "学生姓名" ||
           Lower(s) == "name";
}

bool IsIdHeader(const std::string& s) {
    std::string l = Lower(s);
    return s == "学号" || s == "编号" || s == "序号" || s == "座号" ||
           l == "id" || l == "no" || l == "no.";
}

std::vector<std::string> Cells(const std::string& row) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < row.size()) {
        size_t lt = row.find('<', i);
        if (lt == kNpos) break;
        size_t gt = row.find('>', lt);
        if (gt == kNpos) break;
        std::string tag = Lower(row.substr(lt + 1, (std::min)(gt - lt - 1, size_t(4))));
        bool cell = tag.rfind("td", 0) == 0 || tag.rfind("th", 0) == 0;
        i = gt + 1;
        if (!cell) continue;
        size_t close = LowerFind(row, "</t", i);
        if (close == kNpos) close = row.size();
        out.push_back(StripTags(row.substr(i, close - i)));
        i = close;
    }
    return out;
}

bool NameRows(const std::vector<std::vector<std::string>>& rows,
              std::vector<roster::Student>& out) {
    for (size_t r = 0; r < rows.size(); r++) {
        const std::vector<std::string>& cells = rows[r];
        int nameCol = -1;
        for (size_t c = 0; c < cells.size(); c++) {
            if (IsNameHeader(cells[c]) || IsIdHeader(cells[c])) continue;
            if (LooksLikeName(cells[c], true)) {
                nameCol = static_cast<int>(c);
                break;
            }
        }
        if (nameCol < 0) continue;
        roster::Student s;
        s.id = static_cast<int>(out.size()) + 1;
        // 姓名前面若有一列纯数字，当学号用
        if (nameCol > 0) {
            const std::string& prev = cells[nameCol - 1];
            bool digits = !prev.empty();
            for (size_t k = 0; k < prev.size(); k++)
                if (!isdigit((unsigned char)prev[k])) digits = false;
            if (digits) {
                long n = std::strtol(prev.c_str(), nullptr, 10);
                if (n > 0 && n < 100000) s.id = static_cast<int>(n);
            }
        }
        s.name = cells[nameCol];
        out.push_back(s);
    }
    return out.size() >= 3;
}

// 页面里的表格，或 class 名带 student / name 的元素
void HtmlNames(const std::string& s, std::vector<gen::Klass>& out) {
    // 表格
    size_t t = 0;
    while ((t = LowerFind(s, "<table", t)) != kNpos) {
        size_t end = LowerFind(s, "</table", t);
        if (end == kNpos) end = s.size();
        std::string tbl = s.substr(t, end - t);
        std::vector<std::vector<std::string>> rows;
        size_t r = 0;
        while ((r = LowerFind(tbl, "<tr", r)) != kNpos) {
            size_t rend = LowerFind(tbl, "</tr", r);
            if (rend == kNpos) rend = tbl.size();
            rows.push_back(Cells(tbl.substr(r, rend - r)));
            r = rend + 3;
        }
        std::vector<roster::Student> v;
        if (NameRows(rows, v)) {
            gen::Klass k;
            k.name = "导入的班级";
            k.students.swap(v);
            out.push_back(k);
            return;
        }
        t = end + 6;
    }

    // class 名里带 student / name 的元素：模板里写 ${s.name} 的那种不算
    std::vector<roster::Student> v;
    size_t c = 0;
    while ((c = LowerFind(s, "class=\"", c)) != kNpos) {
        size_t a = c + 7;
        size_t bq = s.find('"', a);
        if (bq == kNpos) break;
        std::string cls = Lower(s.substr(a, bq - a));
        c = bq + 1;
        if (cls.find("student") == kNpos && cls.find("name") == kNpos) continue;
        size_t gt = s.find('>', bq);
        if (gt == kNpos) break;
        if (gt + 1 < s.size() && s[gt + 1] == '<') continue;  // 空元素
        size_t close = s.find('<', gt + 1);
        if (close == kNpos) break;
        std::string text = StripTags(s.substr(gt + 1, close - gt - 1));
        if (!LooksLikeName(text, true)) continue;
        bool dup = false;
        for (size_t k = 0; k < v.size(); k++)
            if (v[k].name == text) dup = true;
        if (dup) continue;
        roster::Student st;
        st.id = static_cast<int>(v.size()) + 1;
        st.name = text;
        v.push_back(st);
    }
    if (v.size() >= 3) {
        gen::Klass k;
        k.name = "导入的班级";
        k.students.swap(v);
        out.push_back(k);
    }
}

}  // namespace

// ---------- 对外接口 ----------

bool LooksLikePicker(const std::string& utf8, std::string& title,
                     std::string& why) {
    title = ExtractTitle(utf8);
    std::string hits;
    int n = PickerSignals(utf8, hits);
    std::string lowTitle = Lower(title);
    bool titleOk = title.find("点名") != kNpos ||
                   lowTitle.find("acoc") != kNpos ||
                   lowTitle.find("name picker") != kNpos ||
                   lowTitle.find("namepicker") != kNpos;
    if (titleOk) {
        why = "标题写着「" + title + "」";
        return true;
    }
    if (n >= 2) {
        why = "标题是「" + (title.empty() ? std::string("（无）") : title) +
              "」，但页面里有 " + std::to_string(n) + " 处点名器特征：" + hits;
        return true;
    }
    why = "标题是「" + (title.empty() ? std::string("（无）") : title) +
          "」";
    why += n == 1 ? "，页面里只有 1 处沾边的特征：" + hits
                  : "，页面里也没有点名器的特征。";
    return false;
}

bool ParseHtml(const std::string& utf8, std::vector<gen::Klass>& classes,
               std::string& err, std::string* method) {
    std::vector<gen::Klass> found;
    std::vector<std::string> used;

    // ---- 1) CLASS_PRESETS：v0.4 起的生成物，一段就含全部班级 ----
    {
        size_t p = FindAssignment(utf8, "CLASS_PRESETS");
        if (p != kNpos) {
            Cursor c{utf8, p};
            std::vector<gen::Klass> v;
            if (ReadKlassArray(c, v)) {
                // 空班级也留着：读回工作区时保持文件原样
                for (size_t i = 0; i < v.size(); i++) found.push_back(v[i]);
                used.push_back("CLASS_PRESETS");
            }
        }
        if (!found.empty()) {
            Dedupe(found);
            Normalize(found);
            classes.swap(found);
            if (method) *method = "内嵌的班级预设（CLASS_PRESETS）";
            return true;
        }
    }

    // ---- 2) CLASS_NAME + CLASS_NAMES ----
    {
        std::string name;
        std::vector<roster::Student> students;
        size_t np = FindAssignment(utf8, "CLASS_NAME");
        if (np != kNpos) {
            Cursor c{utf8, np};
            if (ReadString(c, name)) name = roster::Trim(name);
        }
        size_t sp = FindAssignment(utf8, "CLASS_NAMES");
        if (sp != kNpos) {
            Cursor c{utf8, sp};
            ReadAnyNameArray(c, students, false);
        }
        used.push_back("CLASS_NAMES");
        if (!students.empty()) {
            gen::Klass k;
            k.name = name;
            k.students.swap(students);
            found.push_back(k);
            Dedupe(found);
            Normalize(found);
            classes.swap(found);
            if (method) *method = "班级常量（CLASS_NAME / CLASS_NAMES）";
            return true;
        }
    }

    // ---- 3) initSystemData(VAR, "班级名") + 它引用的数组 ----
    {
        std::vector<Preset> presets;
        PresetListeners(utf8, presets);
        for (size_t i = 0; i < presets.size(); i++) {
            if (Used(used, presets[i].var)) continue;
            size_t p = FindAssignment(utf8, presets[i].var);
            if (p == kNpos) continue;
            Cursor c{utf8, p};
            std::vector<roster::Student> v;
            if (!ReadAnyNameArray(c, v, false)) continue;
            gen::Klass k;
            k.name = presets[i].name;
            k.students.swap(v);
            found.push_back(k);
            used.push_back(presets[i].var);
        }
        if (!found.empty()) {
            Dedupe(found);
            Normalize(found);
            classes.swap(found);
            if (method) *method = "预设装载调用（initSystemData）";
            return true;
        }
    }

    // ---- 4) 脚本里任何名单数组 ----
    {
        std::vector<gen::Klass> v;
        GenericArrays(utf8, used, v);
        for (size_t i = 0; i < v.size(); i++) found.push_back(v[i]);
        if (!found.empty()) {
            Dedupe(found);
            Normalize(found);
            classes.swap(found);
            if (method) *method = "脚本里的名单数组";
            return true;
        }
    }

    // ---- 5) 页面里的表格 / 列表 ----
    {
        std::vector<gen::Klass> v;
        HtmlNames(utf8, v);
        for (size_t i = 0; i < v.size(); i++) found.push_back(v[i]);
        if (!found.empty()) {
            Dedupe(found);
            Normalize(found);
            classes.swap(found);
            if (method) *method = "页面里的表格或列表";
            return true;
        }
    }

    std::string title, why;
    LooksLikePicker(utf8, title, why);
    err = "这个文件里没有找到班级名单。\n\n看过的依据：" + why +
          "\n\n如果它确实是点名器，请把文件发给作者看看名单是怎么存的。";
    return false;
}

bool ImportFile(const std::wstring& path, std::vector<gen::Klass>& classes,
                std::string& err, std::string* method) {
    std::string text;
    if (!roster::ReadTextFile(path, text, err)) return false;
    return ParseHtml(text, classes, err, method);
}

}  // namespace imp
