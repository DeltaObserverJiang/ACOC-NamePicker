# -*- coding: utf-8 -*-
"""裁剪逻辑的 Python 原型，用于快速验证；C++ 端行为与此一致。

用法: python preview.py out.html [feat=0|1 ...]
不带参数时全部开启。
"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TPL = os.path.join(ROOT, "configurator", "res", "template.html")

ALL = ["deduct", "multi", "char", "prob", "fixed", "stats",
       "theme_at", "theme_classic", "roster", "data", "exp", "glitch"]


def derive(f):
    g = dict(f)
    g["theme_switch"] = f["theme_at"] and f["theme_classic"]
    g["other"] = (g["theme_switch"] or f["data"] or f["exp"] or f["glitch"])
    g["menu"] = (f["prob"] or f["fixed"] or f["stats"] or g["other"])
    g["modeswitch"] = f["deduct"] or f["multi"] or f["char"]
    return g


def strip(text, en):
    out = []
    stack = []
    depth = 0
    for line in text.split("\n"):
        s = line.strip()
        if "@MOD:" in s:
            feat = s.split("@MOD:", 1)[1].split("*/")[0].split("-->")[0].strip()
            stack.append(feat)
            if not en.get(feat, True):
                depth += 1
            continue
        if "@/MOD" in s:
            feat = stack.pop()
            if not en.get(feat, True):
                depth -= 1
            continue
        if depth == 0:
            out.append(line)
    assert not stack, stack
    return "\n".join(out)


def rosters(students, cname):
    body = ", ".join('{ id: %d, name: "%s" }' % (s[0], s[1]) for s in students)
    const = ("        /* 本班名单：由配置器写入 */\n"
             "        const CLASS_NAME = %s;\n"
             "        const CLASS_NAMES = [\n            %s\n        ];" % (json_str(cname), body))
    btn = ('            <button class="class-btn highlight" id="loadCustomBtn">'
           '装载预设 %s</button>' % cname)
    listener = ("        document.getElementById('loadCustomBtn').addEventListener('click', "
                "() => { initSystemData(CLASS_NAMES, CLASS_NAME + \" (预设)\"); });")
    tail = ("                if (FEATURES.roster) {\n"
            "                    const startupScreen = document.getElementById('startupScreen');\n"
            "                    startupScreen.style.display = 'flex';\n"
            "                    setTimeout(() => { startupScreen.classList.remove('hidden'); }, 50);\n"
            "                } else {\n"
            "                    initSystemData(CLASS_NAMES, CLASS_NAME);\n"
            "                }")
    return const, btn, listener, tail


def json_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build(en, students, cname):
    text = io.open(TPL, encoding="utf-8").read()
    text = strip(text, derive(en))
    const, btn, listener, tail = rosters(students, cname)
    feats = "{" + ", ".join("%s: %s" % (k, "true" if derive(en)[k] else "false")
                            for k in sorted(derive(en))) + "}"
    theme = "at" if en["theme_at"] else "classic"
    for a, b in [("@@ROSTER_CONST@@", const), ("@@ROSTER_BTN@@", btn),
                 ("@@ROSTER_LISTENER@@", listener), ("@@BOOT_TAIL@@", tail),
                 ("@@FEATURES@@", feats), ("@@THEME@@", theme)]:
        text = text.replace(a, b)
    return text


if __name__ == "__main__":
    out = sys.argv[1]
    en = {k: True for k in ALL}
    for a in sys.argv[2:]:
        k, v = a.split("=")
        en[k] = v == "1"
    students = [(i + 1, "同学%d" % (i + 1)) for i in range(12)]
    io.open(out, "w", encoding="utf-8", newline="").write(build(en, students, "测试班"))
    print("ok", out)
