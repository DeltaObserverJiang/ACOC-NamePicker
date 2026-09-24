# Full install -> verify -> uninstall -> verify cycle, driven the way a user
# would (real button clicks, real modal prompts).
import glob
import os
import shutil
import subprocess
import sys
import time
import winreg

CFG = r"R:\NamePicker\configurator"
# 优先验证 dist 里的发布件，没有才退回构建目录
_dist = os.path.join(CFG, r"dist\ACOCConfiguratorSetup.exe")
SETUP = _dist if os.path.exists(_dist) else os.path.join(
    CFG, r"installer\build\bin\ACOCConfiguratorSetup.exe")
INSTALL = os.path.join(os.environ["LOCALAPPDATA"], r"Programs\ACOCConfigurator")
TOOLS = os.path.join(CFG, r"_test\run_setup.ps1")
DISMISS = os.path.join(CFG, r"_test\dismiss.ps1")
REG = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\ACOCConfigurator"

fails = []


def check(label, ok):
    print(("  OK   " if ok else "  FAIL ") + label)
    if not ok:
        fails.append(label)


def ps(script, *args):
    cmd = ["powershell", "-ExecutionPolicy", "Bypass", "-File", script]
    cmd += [str(a) for a in args]
    return subprocess.run(cmd, capture_output=True, text=True,
                          encoding="gbk", errors="replace").stdout


def kill(names):
    for n in names:
        subprocess.run(["taskkill", "/F", "/IM", n], capture_output=True)


def leftovers():
    return sorted(glob.glob(os.path.join(os.environ["TEMP"], "acoc_*")))


def reg_present():
    try:
        winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG)
        return True
    except FileNotFoundError:
        return False


def shortcuts():
    desk = os.path.join(os.path.expanduser("~"), "Desktop")
    dl = [f for f in os.listdir(desk) if "ACOC" in f or f.startswith("A.C")]
    grp = os.path.join(os.environ["APPDATA"],
                       r"Microsoft\Windows\Start Menu\Programs\A.C.O.C. 点名系统配置器")
    sl = os.listdir(grp) if os.path.isdir(grp) else []
    return dl, sl, grp


def plan(expected):
    print("状态：%s" % expected)
    desk, start, grp = shortcuts()
    check("桌面快捷方式 %s" % ("存在" if expected == "installed" else "已移除"),
          (len(desk) > 0) == (expected == "installed"))
    check("开始菜单快捷方式 %s" % ("存在" if expected == "installed" else "已移除"),
          (len(start) > 0) == (expected == "installed"))
    check("注册表项 %s" % ("存在" if expected == "installed" else "已清除"),
          reg_present() == (expected == "installed"))
    check("临时文件无残留", leftovers() == [])
    if expected == "removed":
        check("安装目录已删除", not os.path.isdir(INSTALL))
        if os.path.isdir(INSTALL):
            print("        残留:", os.listdir(INSTALL))


print("=== 清理旧状态 ===")
kill(["ACOCConfiguratorSetup.exe", "uninstall.exe", "ACOCConfigurator.exe"])
for p in leftovers():
    try:
        os.remove(p)
    except OSError:
        pass
if os.path.isdir(INSTALL):
    shutil.rmtree(INSTALL, ignore_errors=True)
time.sleep(0.5)

print("=== 安装 ===")
proc = subprocess.Popen([SETUP], cwd=os.path.dirname(SETUP))
time.sleep(2.5)
print(ps(TOOLS, "-Click", 104).strip())
print(ps(DISMISS, "-TargetPid", proc.pid).strip())
check("安装目录已创建", os.path.isdir(INSTALL))
if os.path.isdir(INSTALL):
    check("安装文件齐全", sorted(os.listdir(INSTALL)) ==
          ["ACOCConfigurator.exe", "uninstall.exe"])
plan("installed")
print(ps(TOOLS, "-Uncheck", 106, "-Click", 104).strip())
time.sleep(1.0)

print("=== 卸载 ===")
up = subprocess.Popen([os.path.join(INSTALL, "uninstall.exe")], cwd=INSTALL)
time.sleep(2.0)
print(ps(TOOLS, "-Title", "卸载 A.C.O.C. 点名系统配置器", "-Click", 104).strip())
time.sleep(0.8)
# 卸载完成会弹一个模态框，用户点掉之后程序才退出、清理才接管
print(ps(DISMISS, "-TargetPid", up.pid).strip())

# 清理副本要等父进程退出才动手，再重试几轮，所以这里轮询而不是死等
deadline = time.time() + 30
while time.time() < deadline and (os.path.isdir(INSTALL) or leftovers()):
    time.sleep(0.5)

plan("removed")
o = subprocess.run(["tasklist", "/FI", "IMAGENAME eq uninstall.exe"],
                   capture_output=True, text=True, encoding="gbk", errors="replace")
check("无残留卸载进程", "uninstall" not in o.stdout)

print()
print("结果：%s" % ("全部通过" if not fails else "失败 %d 项 -> %s" % (len(fails), fails)))
sys.exit(1 if fails else 0)
