# -*- coding: utf-8 -*-
"""手搓几个最小但合法的 xlsx / csv，用来试名单识别的宽容度。"""
import io
import os
import zipfile

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "_test", "roster")
CT = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>
</Types>"""
RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>"""
WB = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>
</workbook>"""
WB_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>
</Relationships>"""


def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def build(path, rows, shared=True):
    """rows: list of list of (value, is_number)"""
    if shared:
        pool, index = [], {}
        for r in rows:
            for v, num in r:
                if not num and v not in index:
                    index[v] = len(pool)
                    pool.append(v)
        ss = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
              '<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
              'count="%d" uniqueCount="%d">' % (len(pool), len(pool)))
        for s in pool:
            ss += "<si><t>%s</t></si>" % esc(s)
        ss += "</sst>"

    body = ""
    for ri, r in enumerate(rows, 1):
        body += '<row r="%d">' % ri
        for ci, (v, num) in enumerate(r):
            ref = chr(ord("A") + ci) + str(ri)
            if num:
                body += '<c r="%s"><v>%s</v></c>' % (ref, esc(v))
            elif shared:
                body += '<c r="%s" t="s"><v>%d</v></c>' % (ref, index[v])
            else:
                body += '<c r="%s" t="inlineStr"><is><t>%s</t></is></c>' % (ref, esc(v))
        body += "</row>"
    sheet = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
             '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
             '<sheetData>%s</sheetData></worksheet>' % body)

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CT)
        z.writestr("_rels/.rels", RELS)
        z.writestr("xl/workbook.xml", WB)
        z.writestr("xl/_rels/workbook.xml.rels", WB_RELS)
        if shared:
            z.writestr("xl/sharedStrings.xml", ss)
        z.writestr("xl/worksheets/sheet1.xml", sheet)


NAMES = ["林知遥", "陈慕白", "周砚清", "沈聿", "顾南枝",
         "苏行止", "叶怀瑾", "许砚书", "秦望舒", "何以安"]


def main():
    os.makedirs(OUT, exist_ok=True)
    T = lambda s: (s, False)
    N = lambda s: (s, True)

    # 1. 只有一列人名
    build(os.path.join(OUT, "only_names.xlsx"), [[T(n)] for n in NAMES])

    # 2. 学号 + 人名
    build(os.path.join(OUT, "id_name.xlsx"),
          [[N(str(i + 1)), T(n)] for i, n in enumerate(NAMES)])

    # 3. 学号 + 姓名，首行是说明文字
    build(os.path.join(OUT, "header_id_name.xlsx"),
          [[T("学号"), T("姓名")]] +
          [[N(str(i + 1)), T(n)] for i, n in enumerate(NAMES)])

    # 4. 只有姓名，但首行是说明文字
    build(os.path.join(OUT, "header_name.xlsx"),
          [[T("姓名")]] + [[T(n)] for n in NAMES])

    # 5. inlineStr 写法（WPS / 部分导出工具）
    build(os.path.join(OUT, "inline_id_name.xlsx"),
          [[T("序号"), T("学生姓名")]] +
          [[N(str(100 + i)), T(n)] for i, n in enumerate(NAMES)],
          shared=False)

    # 6. 前面有空白行的
    build(os.path.join(OUT, "leading_blank.xlsx"),
          [[T(""), T("")], [T(""), T("")]] +
          [[T("学籍号"), T("名字")]] +
          [[N(str(i + 1)), T(n)] for i, n in enumerate(NAMES)])

    # 7. 只有学号一列（没有人名的坏表）
    build(os.path.join(OUT, "bad_only_ids.xlsx"),
          [[N(str(i + 1))] for i in range(6)])

    # csv 两种
    for name, enc, extra in [("csv_header.csv", "utf-8-sig", True),
                             ("csv_gbk.csv", "gbk", False)]:
        lines = []
        if extra:
            lines.append("学号,姓名")
        for i, n in enumerate(NAMES):
            lines.append("%d,%s" % (i + 1, n))
        io.open(os.path.join(OUT, name), "w", encoding=enc, newline="").write(
            "\r\n".join(lines))
    print("written ->", OUT)


if __name__ == "__main__":
    main()
