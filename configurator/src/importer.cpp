#include "importer.h"

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
        } else if (key == "students" || key == "roster") {
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

}  // namespace

bool ParseHtml(const std::string& utf8, std::vector<gen::Klass>& classes,
               std::string& err) {
    // ---- 新格式：CLASS_PRESETS 里就是全部班级 ----
    size_t p = FindAssignment(utf8, "CLASS_PRESETS");
    if (p != kNpos) {
        Cursor c{utf8, p};
        std::vector<gen::Klass> v;
        if (ReadKlassArray(c, v) && !v.empty()) {
            Normalize(v);
            classes.swap(v);
            return true;
        }
        // 认得出变量却读不动内容，多半是文件被改坏了，退回旧格式再试一次
    }

    // ---- 旧格式：CLASS_NAME（班级名）+ CLASS_NAMES（该班学生） ----
    std::string name;
    std::vector<roster::Student> students;
    bool any = false;

    size_t np = FindAssignment(utf8, "CLASS_NAME");
    if (np != kNpos) {
        Cursor c{utf8, np};
        if (ReadString(c, name)) {
            name = roster::Trim(name);
            any = true;
        }
    }
    size_t sp = FindAssignment(utf8, "CLASS_NAMES");
    if (sp != kNpos) {
        Cursor c{utf8, sp};
        if (ReadStudentArray(c, students)) any = true;
    }

    if (!any) {
        err = "这个文件里没有点名器的班级名单。\n\n"
              "请选择由本配置器导出的 HTML（文件名通常以「_点名系统.html」结尾）。";
        return false;
    }

    classes.clear();
    classes.resize(1);
    classes[0].name = name.empty() ? "导入的班级" : name;
    classes[0].students.swap(students);
    Normalize(classes);
    return true;
}

bool ImportFile(const std::wstring& path, std::vector<gen::Klass>& classes,
                std::string& err) {
    std::string text;
    if (!roster::ReadTextFile(path, text, err)) return false;
    return ParseHtml(text, classes, err);
}

}  // namespace imp
