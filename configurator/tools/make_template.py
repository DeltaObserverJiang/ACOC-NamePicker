# -*- coding: utf-8 -*-
"""把 index.html 转换成带功能标记的模板 res/template.html。

标记行形如：
    <!--@MOD:multi-->      (HTML)
    /*@MOD:multi*/         (CSS / JS)
...同类标记 @/MOD 收尾
配置器按标记裁剪被关闭的功能，并替换 @@XXX@@ 占位符。

WRAPS / REPLACES 记的是行号：index.html 一改行号就整体位移。所以这里配了
CHECK —— 每条边界行的原文片段。跑之前先逐条核对，对不上就报错退出，
免得标记插错位置、生成一份看着正常其实少块多块的模板。行号漂了就跑
    python configurator/tools/remap_template_lines.py
"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "index.html")
DST = os.path.join(ROOT, "configurator", "res", "template.html")

# (起始行, 结束行, 功能名)，行号从 1 开始且含两端
WRAPS = [
    (348, 348, "theme_classic"),   # :root 中的经典主题背景图
    (625, 788, "theme_at"),   # AT 主题全部样式
    (790, 818, "theme_switch"),   # 主题切换动画与切换控件
    (856, 869, "roster"),   # 启动装载界面
    (890, 901, "multi"),   # 连续抽取界面
    (903, 914, "char"),   # 轮字抽取界面
    (922, 922, "menu"),   # 设置按钮
    (935, 937, "deduct"),   # 模式选择：不重复
    (938, 940, "multi"),   # 连续抽取界面
    (941, 943, "char"),   # 轮字抽取界面
    (956, 962, "modeswitch"),   # 当前模式信息 + 重载模式
    (964, 964, "prob"),   # 概率修改
    (965, 965, "fixed"),   # 顺位设定
    (966, 966, "stats"),   # 统计数据
    (967, 967, "other"),   # 其他页
    (969, 971, "prob"),   # 概率修改
    (972, 975, "fixed"),   # 顺位设定
    (976, 978, "stats"),   # 统计数据
    (979, 1006, "other"),   # 其他页
    (980, 989, "theme_switch"),   # 主题切换动画与切换控件
    (990, 997, "data"),   # 高级数据管理
    (998, 998, "exp"),   # 实验性功能
    (1001, 1004, "glitch"),   # 故障跳跃
    (1014, 1014, "theme_switch"),   # 主题切换动画与切换控件
    (1509, 1531, "roster"),   # 启动装载界面
    (1693, 1929, "multi"),   # 连续抽取界面
    (1931, 2044, "char"),   # 轮字抽取界面
    (2142, 2170, "menu"),   # 设置按钮
    (2172, 2187, "prob"),   # 概率修改
    (2189, 2197, "fixed"),   # 顺位设定
    (2204, 2234, "stats"),   # 统计数据
    (2236, 2249, "exp"),   # 实验性功能
    (2251, 2256, "glitch"),   # 故障跳跃
    (2258, 2293, "data"),   # 高级数据管理
    (2295, 2344, "theme_switch"),   # 主题切换动画与切换控件
]

# 整段替换：(起始行, 结束行, 替换文本)
REPLACES = [
    (860, 864, "@@ROSTER_BTN@@"),
    (1169, 1180, "@@BOOT_TAIL@@"),
    (1185, 1250, "@@ROSTER_CONST@@"),
    (1509, 1512, "@@ROSTER_LISTENER@@"),
    (1018, 1018, "        const FEATURES = @@FEATURES@@;"),
]

# 边界自检：(行号, 该行必须包含的原文片段)
CHECK = [
    (348, "--bg-image: url(data:image/webp;base64,UklGRmi6AABXRUJQV"),
    (348, "--bg-image: url(data:image/webp;base64,UklGRmi6AABXRUJQV"),
    (625, "/* ====================================================="),
    (788, "}"),
    (790, "/* ====================================================="),
    (818, "html[data-theme=\"at\"] .theme-desc { color: #cdd5e5; }"),
    (856, "<div class=\"startup-screen hidden\" id=\"startupScreen\" st"),
    (869, "</div>"),
    (890, "<div class=\"multi-container fade-component ui-hidden\" id"),
    (901, "</div>"),
    (903, "<div class=\"char-mode-container fade-component ui-hidden"),
    (914, "</div>"),
    (922, "<div class=\"menu-btn fade-component ui-hidden\" id=\"openM"),
    (922, "<div class=\"menu-btn fade-component ui-hidden\" id=\"openM"),
    (935, "<div class=\"mode-option-zone\" id=\"zone-deduct\" data-targ"),
    (937, "</div>"),
    (938, "<div class=\"mode-option-zone\" id=\"zone-multi\" data-targe"),
    (940, "</div>"),
    (941, "<div class=\"mode-option-zone\" id=\"zone-char\" data-target=\"char\">"),
    (943, "</div>"),
    (956, "<div class=\"mode-control-area\">"),
    (962, "</div>"),
    (964, "<button class=\"tab-btn active\" data-tab=\"prob\">概率修改</button>"),
    (964, "<button class=\"tab-btn active\" data-tab=\"prob\">概率修改</button>"),
    (965, "<button class=\"tab-btn\" data-tab=\"fixed\">顺位设定</button>"),
    (965, "<button class=\"tab-btn\" data-tab=\"fixed\">顺位设定</button>"),
    (966, "<button class=\"tab-btn\" data-tab=\"stats\">统计数据</button>"),
    (966, "<button class=\"tab-btn\" data-tab=\"stats\">统计数据</button>"),
    (967, "<button class=\"tab-btn\" data-tab=\"other\">其他</button>"),
    (967, "<button class=\"tab-btn\" data-tab=\"other\">其他</button>"),
    (969, "<div class=\"tab-content active\" id=\"tab-prob\">"),
    (971, "</div>"),
    (972, "<div class=\"tab-content\" id=\"tab-fixed\">"),
    (975, "</div>"),
    (976, "<div class=\"tab-content\" id=\"tab-stats\">"),
    (978, "</div>"),
    (979, "<div class=\"tab-content\" id=\"tab-other\">"),
    (1006, "</div>"),
    (980, "<h3>界面主题</h3>"),
    (989, "<hr class=\"menu-hr\" style=\"border:none; border-top:1px s"),
    (990, "<h3>高级数据管理</h3>"),
    (997, "<hr class=\"menu-hr\" style=\"border:none; border-top:1px s"),
    (998, "<button class=\"small-btn\" id=\"unlockExpBtn\" style=\"color"),
    (998, "<button class=\"small-btn\" id=\"unlockExpBtn\" style=\"color"),
    (1001, "<label style=\"font-size: 0.9rem; color: var(--primary-co"),
    (1004, "</label>"),
    (1014, "<div class=\"theme-wipe\" id=\"themeWipe\"><i></i><i></i><i>"),
    (1014, "<div class=\"theme-wipe\" id=\"themeWipe\"><i></i><i></i><i>"),
    (1509, "CLASS_PRESETS.forEach((p, i) => {"),
    (1531, "});"),
    (1693, "let multiDrawCount = 12; let multiColsData = []; let mul"),
    (1929, "});"),
    (1931, "let charColsData = []; let charWinner = null; let charAn"),
    (2044, "});"),
    (2142, "document.getElementById('openMenuBtn').addEventListener("),
    (2170, "});"),
    (2172, "function renderProbTable() {"),
    (2187, "}"),
    (2189, "function renderFixedTable() {"),
    (2197, "}"),
    (2204, "function updateStatsTable() {"),
    (2234, "}"),
    (2236, "document.getElementById('unlockExpBtn').addEventListener"),
    (2249, "});"),
    (2251, "document.getElementById('glitchProbInput').addEventListe"),
    (2256, "});"),
    (2258, "document.getElementById('exportConfigBtn').addEventListe"),
    (2293, "});"),
    (2295, "const themeWipe = document.getElementById('themeWipe');"),
    (2344, "updateThemePanel();"),
    (860, "<div style=\"max-height:44vh; overflow-y:auto; margin:0 -"),
    (864, "</div>"),
    (1169, "setTimeout(() => {"),
    (1180, "}, 400);"),
    (1185, "/* 班级名单：由配置器写入 */"),
    (1250, "];"),
    (1509, "CLASS_PRESETS.forEach((p, i) => {"),
    (1512, "});"),
    (1018, "const FEATURES = {char: true, data: true, deduct: true, "),
    (1018, "const FEATURES = {char: true, data: true, deduct: true, "),
]

# 这一版表格对应的 index.html 行数
EXPECT_LINES = 2376

CSS_END, BODY_END = 820, 1015

# 全局文本替换：模板里的主题名要留给配置器填
SUBS = [
    ('data-theme="at">', 'data-theme="@@THEME@@">'),
]


def kind(n):
    if n <= CSS_END:
        return "css"
    return "html" if n <= BODY_END else "js"


def verify(lines):
    """行号与原文对不上时，直接说清哪条对不上。"""
    bad = []
    if len(lines) - 1 != EXPECT_LINES:
        print("!! index.html 现在 %d 行，表格对应的是 %d 行"
              % (len(lines) - 1, EXPECT_LINES))
    for n, want in CHECK:
        if n > len(lines) or want not in lines[n - 1]:
            got = lines[n - 1].strip()[:56] if n <= len(lines) else "(越界)"
            bad.append((n, want, got))
    if bad:
        print("!! 有 %d 条边界对不上，模板没有生成：" % len(bad))
        for n, want, got in bad[:20]:
            print("   第 %d 行 期望含 %r\n             实际是 %r" % (n, want, got))
        print("   先跑： python configurator/tools/remap_template_lines.py")
        return False
    return True


def main():
    with io.open(SRC, "rb") as f:
        lines = f.read().decode("utf-8").replace("\r\n", "\n").split("\n")

    if not verify(lines):
        return 1

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

    for a, b in SUBS:
        if a not in result:
            print("!! 未命中:", a[:70].replace("\n", "\\n"))
            return 1
        result = result.replace(a, b)

    result = result.replace("\n", "\r\n")
    with io.open(DST, "wb") as f:
        # 不写 BOM：模板到成品这一段只是替换与裁剪，成品的开头就该和
        # index.html 逐字节一致，多一个 BOM 会白添一处差异
        f.write(result.encode("utf-8"))
    print("written", DST, len(result), "chars,", result.count("\r\n") + 1, "lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
