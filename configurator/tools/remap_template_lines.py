# make_template.py 是按行号定位区域的，index.html 一变行号就全错。
# 这里用 difflib 把旧行号映射到新行号，重写 WRAPS / REPLACES / CSS_END / BODY_END。
import difflib
import io
import re
import subprocess

ROOT = r"R:\NamePicker"
TPL_TOOL = ROOT + r"\configurator\tools\make_template.py"

# 重映射只能做一次：它把「原始行号」换算成「当前行号」，
# 再跑一遍就是把偏移叠第二次，标记会插到错误的位置上。
# 所以先确认这份脚本还是仓库里的原始状态，不是就先还原。
_head = subprocess.run(["git", "show", "HEAD:configurator/tools/make_template.py"],
                       cwd=ROOT, capture_output=True).stdout.decode("utf-8")
_cur = io.open(TPL_TOOL, encoding="utf-8", newline="").read()
if _head and _cur != _head:
    io.open(TPL_TOOL, "w", encoding="utf-8", newline="").write(_head)
    print("make_template.py 不是原始状态，已先还原再重映射")

old = subprocess.run(["git", "show", "HEAD:index.html"], cwd=ROOT,
                     capture_output=True).stdout.decode("utf-8")
new = io.open(ROOT + r"\index.html", encoding="utf-8", newline="").read()
ol = old.replace("\r\n", "\n").split("\n")
nl = new.replace("\r\n", "\n").split("\n")
print("旧 %d 行 -> 新 %d 行" % (len(ol), len(nl)))

sm = difflib.SequenceMatcher(None, ol, nl, autojunk=False)
# old 行号（0 基）-> new 行号（0 基）
mapping = {}


def fill(i1, i2, j1):
    for k in range(i2 - i1):
        mapping[i1 + k] = j1 + k


for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == "equal":
        fill(i1, i2, j1)
    elif tag == "replace":
        # 被改写的行统一指向替换块的起点
        for k in range(i1, i2):
            mapping[k] = j1
    elif tag == "delete":
        for k in range(i1, i2):
            mapping[k] = j1
    # insert 不涉及旧行


def m(n):
    """旧行号（1 基）-> 新行号（1 基）"""
    return mapping.get(n - 1, len(nl) - 1) + 1


src = io.open(TPL_TOOL, encoding="utf-8").read()

# ---- WRAPS ----
wraps_block = re.search(r"WRAPS = \[(.*?)\n\]", src, re.S).group(1)
new_wraps = []
changed = 0
for mm in re.finditer(r"\((\d+),\s*(\d+),\s*\"(\w+)\"\),\s*(#.*)?", wraps_block):
    a, b, feat = int(mm.group(1)), int(mm.group(2)), mm.group(3)
    na, nb = m(a), m(b)
    if (na, nb) != (a, b):
        changed += 1
    comment = ("   " + mm.group(4)) if mm.group(4) else ""
    new_wraps.append("    (%d, %d, \"%s\"),%s" % (na, nb, feat, comment))
src = re.sub(r"WRAPS = \[.*?\n\]",
             "WRAPS = [\n" + "\n".join(new_wraps) + "\n]", src, flags=re.S)

# ---- REPLACES ----
rep_block = re.search(r"REPLACES = \[(.*?)\n\]", src, re.S).group(1)
new_rep = []
for mm in re.finditer(r"\((\d+),\s*(\d+),\s*\"([^\"]+)\"\),", rep_block):
    a, b, rep = int(mm.group(1)), int(mm.group(2)), mm.group(3)
    new_rep.append("    (%d, %d, \"%s\")," % (m(a), m(b), rep))
src = re.sub(r"REPLACES = \[.*?\n\]",
             "REPLACES = [\n" + "\n".join(new_rep) + "\n]", src, flags=re.S)

# ---- CSS_END / BODY_END ----
mm = re.search(r"CSS_END, BODY_END = (\d+), (\d+)", src)
ce, be = int(mm.group(1)), int(mm.group(2))
src = src.replace(mm.group(0),
                  "CSS_END, BODY_END = %d, %d" % (m(ce), m(be)))
print("重映射：WRAPS 变动 %d 条，CSS_END %d->%d，BODY_END %d->%d"
      % (changed, ce, m(ce), be, m(be)))

io.open(TPL_TOOL, "w", encoding="utf-8", newline="").write(src)
print("make_template.py 已更新")
