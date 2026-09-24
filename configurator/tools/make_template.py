# -*- coding: utf-8 -*-
"""把 index.html 转换成带功能标记的模板 res/template.html。

只需跑一次；之后模板就是配置器的输入。
标记行形如：
    <!--@MOD:multi-->      (HTML)
    /*@MOD:multi*/         (CSS / JS)
    ...同类标记 @/MOD 收尾
配置器按标记裁剪被关闭的功能，并替换 @@XXX@@ 占位符。
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "index.html")
DST = os.path.join(ROOT, "configurator", "res", "template.html")

# (起始行, 结束行, 功能名)，行号从 1 开始且含两端
WRAPS = [
    # ---- CSS ----
    (20, 20, "theme_classic"),        # :root 中的经典主题背景图
    (280, 439, "theme_at"),           # AT 主题全部样式
    (441, 469, "theme_switch"),       # 主题切换动画与切换控件
    # ---- HTML ----
    (507, 517, "roster"),             # 启动装载界面
    (538, 549, "multi"),              # 连续抽取界面
    (551, 562, "char"),               # 轮字抽取界面
    (570, 570, "menu"),               # 设置按钮
    (583, 585, "deduct"),             # 模式选择：不重复
    (586, 588, "multi"),              # 模式选择：连续抽取
    (589, 591, "char"),               # 模式选择：轮字抽取
    (600, 606, "modeswitch"),         # 当前模式信息 + 重载模式
    (608, 608, "prob"),
    (609, 609, "fixed"),
    (610, 610, "stats"),
    (611, 611, "other"),
    (613, 615, "prob"),
    (616, 619, "fixed"),
    (620, 622, "stats"),
    (623, 650, "other"),
    (624, 633, "theme_switch"),       # 其他页：界面主题
    (634, 641, "data"),               # 其他页：高级数据管理
    (642, 642, "exp"),                # 其他页：实验性功能按钮
    (645, 648, "glitch"),             # 其他页：故障跳跃概率
    (658, 658, "theme_switch"),       # 主题擦除层
    # ---- JS ----
    (949, 970, "roster"),             # 预设装载监听 + Excel 读取
    (1132, 1368, "multi"),
    (1370, 1483, "char"),
    (1581, 1609, "menu"),             # 设置菜单开关与页签
    (1611, 1626, "prob"),
    (1628, 1636, "fixed"),
    (1638, 1645, "stats"),
    (1647, 1660, "exp"),
    (1662, 1667, "glitch"),
    (1669, 1704, "data"),
    (1706, 1755, "theme_switch"),
]

# 整段替换：(起始行, 结束行, 替换文本)
REPLACES = [
    (510, 512, "@@ROSTER_BTN@@"),
    (810, 815, "@@BOOT_TAIL@@"),
    (820, 859, "@@ROSTER_CONST@@"),
    (949, 951, "@@ROSTER_LISTENER@@"),
]

CSS_END, BODY_END = 472, 659   # 1..472 样式、473..659 结构、660.. 脚本


def kind(n):
    if n <= CSS_END:
        return "css"
    return "html" if n <= BODY_END else "js"


def main():
    with io.open(SRC, "rb") as f:
        lines = f.read().decode("utf-8").replace("\r\n", "\n").split("\n")

    ops = {}

    def add(n, item):
        ops.setdefault(n, []).append(item)

    for a, b, feat in WRAPS:
        add(a, ("open", feat))
        add(b + 1, ("close", feat))
    for a, b, rep in REPLACES:
        add(a, ("replace", rep))
        for k in range(a + 1, b + 1):
            add(k, ("skip",))

    out = []
    for n, line in enumerate(lines, 1):
        items = ops.get(n)
        if items is None:
            out.append(line)
            continue
        kinds = [it[0] for it in items]
        reps = [it[1] for it in items if it[0] == "replace"]
        if "skip" in kinds and not reps:
            continue
        k = kind(n)
        # 同一行既有收尾又有起始时，先收尾再起始，保证结构是并列而非嵌套
        for it in items:
            if it[0] == "close":
                out.append("<!--@/MOD-->" if k == "html" else "/*@/MOD*/")
        for it in items:
            if it[0] == "open":
                out.append("<!--@MOD:%s-->" % it[1] if k == "html"
                           else "/*@MOD:%s*/" % it[1])
        if reps:
            out.append(reps[0])
            continue
        out.append(line)

    result = "\n".join(out)

    subs = [
        ('data-theme="at">', 'data-theme="@@THEME@@">'),
        ("    <script>\n        setInterval(",
         "    <script>\n        /* 功能开关，由配置器写入 */\n        const FEATURES = @@FEATURES@@;\n\n        setInterval("),
        ("const startup = document.getElementById('startupScreen');\n"
         "            startup.classList.add('hidden');\n"
         "            setTimeout(() => { startup.style.display = 'none'; }, 500);",
         "const startup = document.getElementById('startupScreen');\n"
         "            if (startup) { startup.classList.add('hidden'); setTimeout(() => { startup.style.display = 'none'; }, 500); }"),
        ("document.getElementById('currentClassBadge').textContent = classNameStr;",
         "{ const badge = document.getElementById('currentClassBadge'); if (badge) badge.textContent = classNameStr; }"),
        ("setTimeout(() => { setListTransition('none'); }, 600); updateStatsTable();",
         "setTimeout(() => { setListTransition('none'); }, 600); if (FEATURES.stats) updateStatsTable();"),
        ("                    updateStatsTable();\n                }, 400); ",
         "                    if (FEATURES.stats) updateStatsTable();\n                }, 400); "),
        ("renderProbTable(); renderFixedTable(); updateStatsTable(); ",
         "if (FEATURES.prob) renderProbTable(); if (FEATURES.fixed) renderFixedTable(); if (FEATURES.stats) updateStatsTable(); "),
        ("            document.getElementById('menuModeTitle').textContent = MODE_INFO[currentMode].title;\n"
         "            document.getElementById('menuModeDesc').textContent = MODE_INFO[currentMode].desc;",
         "            const mtEl = document.getElementById('menuModeTitle'); if (mtEl) mtEl.textContent = MODE_INFO[currentMode].title;\n"
         "            const mdEl = document.getElementById('menuModeDesc'); if (mdEl) mdEl.textContent = MODE_INFO[currentMode].desc;"),
        ("            document.getElementById('multiModeUI').style.display = 'none';\n"
         "            document.getElementById('charModeUI').style.display = 'none';",
         "            const muEl = document.getElementById('multiModeUI'); if (muEl) muEl.style.display = 'none';\n"
         "            const chEl = document.getElementById('charModeUI'); if (chEl) chEl.style.display = 'none';"),
        ("if(currentMode === 'multi') {", "if(currentMode === 'multi' && FEATURES.multi) {"),
        ("} else if (currentMode === 'char') {", "} else if (currentMode === 'char' && FEATURES.char) {"),
    ]
    for a, b in subs:
        if a not in result:
            print("!! 未命中:", a[:70].replace("\n", "\\n"))
        result = result.replace(a, b)

    result = result.replace("\n", "\r\n")
    with io.open(DST, "wb") as f:
        f.write(("" + result).encode("utf-8"))
    print("written", DST, len(result), "chars,",
          result.count("\r\n") + 1, "lines")


if __name__ == "__main__":
    main()
