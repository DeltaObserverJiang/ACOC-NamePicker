#include "generator.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace gen {
namespace {

std::string JsString(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += static_cast<char>(c);
        } else if (c == '\n' || c == '\r') {
            o += ' ';
        } else {
            o += static_cast<char>(c);
        }
    }
    return o + "\"";
}

// 裁剪：标记行整行删掉，被关闭功能之间的内容一并删掉
std::string Strip(const std::string& text,
                  const std::map<std::string, bool>& enabled) {
    std::string out;
    out.reserve(text.size());
    std::vector<std::string> stack;
    int muted = 0;
    size_t pos = 0;

    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        size_t next = (nl < text.size()) ? nl + 1 : text.size() + 1;

        const std::string kOpen = "@MOD:";
        size_t m = line.find(kOpen);
        if (m != std::string::npos) {
            // 键名只取标识符字符，好让 HTML 的 "-->" 和 CSS 的 "*/" 都能收尾
            size_t a = m + kOpen.size(), b = a;
            while (b < line.size() &&
                   (isalnum((unsigned char)line[b]) || line[b] == '_'))
                b++;
            std::string key = line.substr(a, b - a);
            stack.push_back(key);
            auto it = enabled.find(key);
            if (it == enabled.end() || !it->second) muted++;
        } else if (line.find("@/MOD") != std::string::npos) {
            if (!stack.empty()) {
                std::string key = stack.back();
                stack.pop_back();
                auto it = enabled.find(key);
                if (it == enabled.end() || !it->second) muted--;
            }
        } else if (muted <= 0) {
            out += line;
            if (nl < text.size()) out += '\n';
        }
        if (next > text.size()) break;
        pos = next;
    }
    return out;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
}

// 每行摊 4 个 { id, name }，缩进对齐
void StudentArray(std::string& s, const std::vector<roster::Student>& v,
                  const std::string& indent) {
    if (v.empty()) {
        s += "[]";
        return;
    }
    s += "[\n";
    for (size_t i = 0; i < v.size(); i++) {
        if (i % 4 == 0) s += indent + "    ";
        s += "{ id: " + std::to_string(v[i].id) + ", name: " + JsString(v[i].name) +
             " }";
        if (i + 1 < v.size()) s += ",";
        if (i % 4 == 3 || i + 1 == v.size()) s += "\n";
        else s += " ";
    }
    s += indent + "]";
}

std::string RosterConst(const std::vector<Klass>& cs) {
    std::string s = "        /* 班级名单：由配置器写入 */\n";
    s += "        const CLASS_NAME = " + JsString(cs[0].name) + ";\n";
    s += "        const CLASS_NAMES = ";
    StudentArray(s, cs[0].students, "        ");
    s += ";\n";
    s += "        const CLASS_PRESETS = [\n";
    for (size_t i = 0; i < cs.size(); i++) {
        s += "            {\n";
        s += "                name: " + JsString(cs[i].name) + ",\n";
        s += "                students: ";
        StudentArray(s, cs[i].students, "                ");
        s += "\n            }";
        s += (i + 1 < cs.size()) ? ",\n" : "\n";
    }
    s += "        ];";
    return s;
}

std::string Pad(int n) { return std::string(static_cast<size_t>(n), ' '); }

}  // namespace

bool Enabled(const Config& c, const char* key) {
    for (const auto& kv : c.features)
        if (kv.first == key) return kv.second;
    return false;
}

bool Build(const std::string& tpl, const Config& c, std::string& out,
           std::string& err) {
    std::map<std::string, bool> f;
    for (const auto& kv : c.features) f[kv.first] = kv.second;
    f["theme_switch"] = f["theme_at"] && f["theme_classic"];
    f["other"] = f["theme_switch"] || f["data"] || f["exp"] || f["glitch"];
    f["menu"] = f["prob"] || f["fixed"] || f["stats"] || f["other"];
    f["modeswitch"] = f["deduct"] || f["multi"] || f["char"];

    if (c.classes.empty()) {
        err = "还没有建班级。";
        return false;
    }
    for (size_t i = 0; i < c.classes.size(); i++) {
        if (c.classes[i].students.size() < 2) {
            err = c.classes[i].name.empty()
                      ? "第 " + std::to_string(i + 1) + " 个班级里至少要有 2 个人。"
                      : "「" + c.classes[i].name + "」里至少要有 2 个人。";
            return false;
        }
        if (c.classes[i].name.empty()) {
            err = "第 " + std::to_string(i + 1) + " 个班级还没有名字。";
            return false;
        }
    }

    out = Strip(tpl, f);

    std::string theme = f["theme_at"] ? "at" : "classic";
    std::string feats = "{";
    bool first = true;
    for (const auto& kv : f) {
        if (!first) feats += ", ";
        first = false;
        feats += kv.first + ": " + (kv.second ? "true" : "false");
    }
    feats += "}";

    // 每个班级一个按钮。外面这层必须能滚：startup-box 没有 max-height，
    // 而 body 是 overflow:hidden，班级一多按钮会溢出视口且滚不回来。
    std::string btn;
    btn += Pad(12) + "<div style=\"max-height:44vh; overflow-y:auto; "
                     "margin:0 -4px; padding:0 4px;\">\n";
    for (size_t i = 0; i < c.classes.size(); i++) {
        btn += Pad(16) + "<button class=\"class-btn" +
               (i == 0 ? " highlight" : "") + "\" id=\"loadPresetBtn" +
               std::to_string(i) + "\">装载预设 " + c.classes[i].name + "</button>\n";
    }
    btn += Pad(12) + "</div>";

    std::string listener =
        Pad(8) + "CLASS_PRESETS.forEach((p, i) => {\n" +
        Pad(12) + "const b = document.getElementById('loadPresetBtn' + i);\n" +
        Pad(12) + "if (b) b.addEventListener('click', () => { "
                  "initSystemData(p.students, p.name); });\n" +
        Pad(8) + "});";

    std::string tail;
    tail += Pad(12) + "setTimeout(() => {\n";
    tail += Pad(16) + "overlay.remove();\n";
    // 模板里徽标写着原版的 A2班，先按默认班级修正，免得启动瞬间闪一下旧名字
    tail += Pad(16) + "const classBadge = document.getElementById('currentClassBadge');\n";
    tail += Pad(16) + "if (classBadge) classBadge.textContent = CLASS_NAME;\n";
    tail += Pad(16) + "if (FEATURES.roster) {\n";
    tail += Pad(20) + "const startupScreen = document.getElementById('startupScreen');\n";
    tail += Pad(20) + "startupScreen.style.display = 'flex';\n";
    tail += Pad(20) + "setTimeout(() => { startupScreen.classList.remove('hidden'); }, 50);\n";
    tail += Pad(16) + "} else {\n";
    tail += Pad(20) + "initSystemData(CLASS_NAMES, CLASS_NAME);\n";
    tail += Pad(16) + "}\n";
    tail += Pad(12) + "}, 400);";

    ReplaceAll(out, "@@ROSTER_CONST@@", RosterConst(c.classes));
    ReplaceAll(out, "@@ROSTER_BTN@@", btn);
    ReplaceAll(out, "@@ROSTER_LISTENER@@", listener);
    ReplaceAll(out, "@@BOOT_TAIL@@", tail);
    ReplaceAll(out, "@@FEATURES@@", feats);
    ReplaceAll(out, "@@THEME@@", theme);

    const char* tokens[] = {"@@ROSTER_CONST@@", "@@ROSTER_BTN@@",
                            "@@ROSTER_LISTENER@@", "@@BOOT_TAIL@@",
                            "@@FEATURES@@", "@@THEME@@"};
    for (const char* t : tokens) {
        if (out.find(t) != std::string::npos) {
            err = std::string("模板占位符未替换：") + t;
            return false;
        }
    }
    return true;
}

}  // namespace gen
