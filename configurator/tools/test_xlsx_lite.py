# 用 _test/roster 里的真实表格跑一遍内嵌的 xlsx 读取器。
# file:// 下 fetch 会被 CORS 挡掉，所以把表格转成 base64 直接写进测试页。
import base64
import io
import json
import os
import shutil
import subprocess

ROOT = r"R:\NamePicker\configurator"
PAGE = r"R:\NamePicker\index.html"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
FIXTURES = os.path.join(ROOT, r"_test\roster")
TMP = os.path.join(ROOT, "_test", "_xlsx_check.html")

# 读取器内联在 index.html 里，这里直接从成品页中截取来测，
# 免得另存一份源码、两边逐渐走样。
page = io.open(PAGE, encoding="utf-8", newline="").read()
head = page.find("/* 精简的 xlsx 读取器。")
tail = page.find("    </script>", head)
assert head > 0 and tail > head, "index.html 里没有找到内联的 xlsx 读取器"
reader = page[head:tail]
print("自 index.html 取到读取器 %d 字符" % len(reader))

cases = []
for f in sorted(os.listdir(FIXTURES)):
    if f.lower().endswith((".xlsx", ".xlsm")):
        p = os.path.join(FIXTURES, f)
        cases.append((f, base64.b64encode(open(p, "rb").read()).decode()))

payload = json.dumps([[n, b] for n, b in cases])

html = """<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>t</title></head><body>
<div id="OUT">pending</div>
<script>
%s
</script>
<script>
var CASES = %s;
function b64(s) {
    var bin = atob(s), a = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i);
    return a;
}
var out = {};
CASES.forEach(function (c) {
    try {
        var wb = XLSX.read(b64(c[1]), {type: 'array'});
        var rows = XLSX.utils.sheet_to_json(wb.Sheets[wb.SheetNames[0]], {header: 1});
        out[c[0]] = { ok: true, sheets: wb.SheetNames, rows: rows.slice(0, 4).map(
            function (r) { return r.map(function (v) { return v === undefined ? null : v; }); }) };
    } catch (e) {
        out[c[0]] = { ok: false, err: String(e && e.message) };
    }
});
document.getElementById('OUT').textContent = JSON.stringify(out);
</script></body></html>
""" % (reader, payload)

io.open(TMP, "w", encoding="utf-8").write(html)

prof = os.path.join(ROOT, "_test", "_xlsx_prof")
shutil.rmtree(prof, ignore_errors=True)
cmd = [CHROME, "--headless=new", "--disable-gpu", "--virtual-time-budget=8000",
       "--user-data-dir=" + prof, "--dump-dom",
       "file:///" + TMP.replace("\\", "/")]
p = subprocess.run(cmd, capture_output=True)
dom = p.stdout.decode("utf-8", "replace")
k = dom.find('id="OUT"')
if k < 0:
    print("没有拿到结果：", dom[-600:])
    raise SystemExit(1)
seg = dom[k:]
a = seg.find(">") + 1
b = seg.find("</div>", a)
result = json.loads(seg[a:b])

ok = True
for name in sorted(result):
    r = result[name]
    if not r.get("ok"):
        print("失败 %-24s %s" % (name, r.get("err")))
        ok = False
        continue
    head = r["rows"][0] if r["rows"] else []
    print("通过 %-24s 首行 %s" % (name, json.dumps(head, ensure_ascii=False)))
    for row in r["rows"][1:4]:
        print("       %s" % json.dumps(row, ensure_ascii=False))

print()
print("全部解析成功" if ok else "有解析失败的表格")
