# 从微软官方 download.microsoft.com 抓取 KB2999226（通用 C 运行库）各版本，
# 供 Win7 SP1 / 8.1 机器离线安装。页面 id 来自微软官方下载中心。
import hashlib
import os
import re
import ssl
import urllib.request

ctx = ssl.create_default_context()
OUT = r"R:\NamePicker\configurator\dist\KB2999226"

# details.aspx 页面 id -> 说明
PAGES = {
    "49093": "Windows 7 SP1 x64 / Server 2008 R2 x64",
    "49077": "Windows 7 SP1 x86",
    "49081": "Windows 8.1 x64 / Server 2012 R2 x64",
    "49071": "Windows 8.1 x86",
}

UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}


def page_url(uid):
    u = "https://www.microsoft.com/en-us/download/details.aspx?id=%s" % uid
    h = urllib.request.urlopen(urllib.request.Request(u, headers=UA),
                               timeout=45, context=ctx).read().decode("utf-8", "replace")
    name = re.search(r'"detailsSection_file_name":\["([^"]+)"\]', h)
    size = re.search(r'"detailsSection_file_size":\["([^"]+)"\]', h)
    link = re.search(r'https://download\.microsoft\.com/[^"\'<>\s\\]+\.msu', h)
    if not (name and link):
        raise RuntimeError("页面上找不到下载信息")
    return name.group(1), (size.group(1) if size else "?"), link.group(0)


def download(url, dest):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA),
                                timeout=180, context=ctx) as r, open(dest, "wb") as f:
        while True:
            chunk = r.read(65536)
            if not chunk:
                break
            f.write(chunk)


os.makedirs(OUT, exist_ok=True)
report = []
for uid, desc in PAGES.items():
    name, size, url = page_url(uid)
    dest = os.path.join(OUT, name)
    print("下载 %s  (%s)" % (name, size))
    download(url, dest)
    b = open(dest, "rb").read()
    ok_msu = b[:4] == b"MSCF"          # MSU 内部是 cab
    sha = hashlib.sha256(b).hexdigest()
    report.append((name, desc, len(b), ok_msu, sha, url))
    print("    %d 字节  容器格式%s" % (len(b), "正常" if ok_msu else "异常！"))

print()
for name, desc, n, ok, sha, _ in report:
    print("%-32s %9d  %s\n    %s\n    sha256 %s" % (name, n, desc, "cab 容器 OK" if ok else "!! 格式异常", sha))
