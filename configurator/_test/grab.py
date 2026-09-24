# 抓安装程序某个瞬间的画面。用法：grab.py <输出名> <点第几个按钮前等待秒数>
# PrintWindow 出来的位图是自下而上的，这里翻回来。
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time
from PIL import Image

u32 = ctypes.windll.user32
gdi = ctypes.windll.gdi32
SETUP = r"R:\NamePicker\configurator\installer\build\bin\ACOCConfiguratorSetup.exe"


class RECT(ctypes.Structure):
    _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long),
                ("r", ctypes.c_long), ("b", ctypes.c_long)]


class BMIH(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long),
                ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


def find_setup(pid):
    out = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(h, l):
        if not u32.IsWindowVisible(h):
            return True
        p = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value != pid:
            return True
        b = ctypes.create_unicode_buffer(64)
        u32.GetClassNameW(h, b, 64)
        if b.value == "AcocSetup":
            out.append(h)
            return False
        return True

    u32.EnumWindows(cb, 0)
    return out[0] if out else None


def grab(hwnd, path):
    r = RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.r - r.l, r.b - r.t
    hdc = u32.GetWindowDC(hwnd)
    mdc = gdi.CreateCompatibleDC(hdc)
    bmp = gdi.CreateCompatibleBitmap(hdc, w, h)
    gdi.SelectObject(mdc, bmp)
    u32.PrintWindow(hwnd, mdc, 2)
    bi = BMIH()
    bi.biSize = ctypes.sizeof(bi)
    bi.biWidth = w
    bi.biHeight = -h          # 让 GetDIBits 按自上而下给我
    bi.biPlanes = 1
    bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    img = Image.frombuffer("RGBA", (w, h), buf.raw, "raw", "BGRA", 0, 1)
    img.convert("RGB").save(path)
    gdi.DeleteObject(bmp)
    gdi.DeleteDC(mdc)
    u32.ReleaseDC(hwnd, hdc)
    print("saved", path, w, h)


out = sys.argv[1]
steps = [float(x) for x in sys.argv[2:]]    # 依次点击后各等几秒
subprocess.run(["taskkill", "/F", "/IM", "ACOCConfiguratorSetup.exe"],
               capture_output=True)
time.sleep(0.8)
p = subprocess.Popen([SETUP], cwd=os.path.dirname(SETUP))
time.sleep(2.5)
main = find_setup(p.pid)
for s in steps:
    # PostMessage：SendMessage 会一直阻塞到安装结束
    u32.PostMessageW(u32.GetDlgItem(main, 104), 0x00F5, 0, 0)
    time.sleep(s)
grab(main, out)
