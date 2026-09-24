# -*- coding: utf-8 -*-
"""在生成结果末尾插入探针，用无头 Chrome 跑一遍并回读运行状态。"""
import io
import os
import subprocess
import sys

CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

PROBE = r"""
<script>
window.__err = [];
window.addEventListener('error', function (e) { window.__err.push(String(e.message)); });
window.addEventListener('unhandledrejection', function (e) { window.__err.push('rej:' + e.reason); });
function q(id) { return document.getElementById(id); }
setTimeout(function () {
    var r = {};
    try {
        var boot = q('bootSequenceOverlay'); if (boot) boot.remove();
        r.hasCustom = (typeof CLASS_NAMES !== 'undefined' && CLASS_NAMES.length) || 0;
        r.className = (typeof CLASS_NAME !== 'undefined') ? CLASS_NAME : null;
        initSystemData(CLASS_NAMES, CLASS_NAME);
        r.items = document.querySelectorAll('#nameList .student-item').length;
        r.bandItems = document.querySelectorAll('.at-band-list .student-item').length;
        if (FEATURES.prob) { renderProbTable(); r.probRows = document.querySelectorAll('#probListBody tr').length; }
        if (FEATURES.fixed) { renderFixedTable(); r.fixedRows = document.querySelectorAll('#fixedListBody tr').length; }
        if (FEATURES.stats) { updateStatsTable(); r.statRows = document.querySelectorAll('#statsListBody tr').length; }
        r.modeTitle = q('menuModeTitle') ? q('menuModeTitle').textContent : null;
        r.zones = document.querySelectorAll('.mode-option-zone').length;
        r.themeBtns = document.querySelectorAll('#switchThemeBtn').length;
        r.menuBtn = document.querySelectorAll('#openMenuBtn').length;
        r.tabs = document.querySelectorAll('.tab-btn').length;
        r.startup = document.querySelectorAll('#startupScreen').length;
        // 多班级：预设数组、按钮数量，以及点下去是否真的换班
        r.presets = (typeof CLASS_PRESETS !== 'undefined') ? CLASS_PRESETS.length : 0;
        r.presetBtns = document.querySelectorAll('[id^=loadPresetBtn]').length;
        if (r.presets > 1) {
            q('loadPresetBtn1').click();
            r.afterClick = classNameStr;
            r.afterClickItems =
                document.querySelectorAll('#nameList .student-item').length;
        }
        initSystemData(CLASS_NAMES, CLASS_NAME);
        r.expBtn = document.querySelectorAll('#unlockExpBtn').length;
        r.glitch = document.querySelectorAll('#glitchProbInput').length;
        r.dataBtn = document.querySelectorAll('#exportConfigBtn').length;
        r.theme = document.documentElement.getAttribute('data-theme');
        // 每个功能对应的 DOM 是否与开关一致
        r.present = {
            deduct: !!q('zone-deduct'), multi: !!q('zone-multi'), char: !!q('zone-char'),
            prob: !!q('tab-prob'), fixed: !!q('tab-fixed'), stats: !!q('tab-stats'),
            other: !!q('tab-other'), theme_switch: !!q('switchThemeBtn'),
            data: !!q('exportConfigBtn'), exp: !!q('unlockExpBtn'),
            glitch: !!q('glitchProbInput'), roster: !!q('startupScreen'),
            menu: !!q('openMenuBtn')
        };
        r.want = {
            deduct: FEATURES.deduct, multi: FEATURES.multi, char: FEATURES.char,
            prob: FEATURES.prob, fixed: FEATURES.fixed, stats: FEATURES.stats,
            other: FEATURES.other, theme_switch: FEATURES.theme_switch,
            data: FEATURES.data, exp: FEATURES.exp, glitch: FEATURES.glitch,
            roster: FEATURES.roster, menu: FEATURES.menu
        };
        r.mismatch = [];
        for (var k in r.want) if (r.present[k] !== r.want[k]) r.mismatch.push(k);
        if (FEATURES.theme_switch) { switchTheme('classic'); }
        r.afterSwitch = document.documentElement.getAttribute('data-theme');
        r.err = window.__err;
    } catch (e) { r.fatal = e.message + ' | ' + (e.stack || '').split('\n')[1]; }
    var d = document.createElement('div');
    d.id = 'PROBE_RESULT';
    d.textContent = JSON.stringify(r);
    document.body.appendChild(d);
}, 60);
</script>
"""


def run(src, out_png=None, size="1600,900", budget=20000):
    text = io.open(src, encoding="utf-8").read()
    i = text.rindex("</body>")
    text = text[:i] + PROBE + text[i:]
    tmp = os.path.abspath(src.replace(".html", "_probe.html"))
    io.open(tmp, "w", encoding="utf-8", newline="").write(text)
    import shutil
    prof = os.path.abspath("_cdp_profile")
    shutil.rmtree(prof, ignore_errors=True)
    cmd = [CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
           "--window-size=" + size,
           "--virtual-time-budget=%d" % budget,
           "--user-data-dir=" + prof, "--dump-dom",
           "file:///" + tmp.replace("\\", "/")]
    p = subprocess.run(cmd, capture_output=True)
    dom = p.stdout.decode("utf-8", "replace")
    k = dom.find('id="PROBE_RESULT"')
    if k < 0:
        return {"error": "no probe result", "tail": dom[-400:]}
    seg = dom[k:]
    a = seg.find(">") + 1
    b = seg.find("</div>", a)
    import json
    try:
        return json.loads(seg[a:b])
    except Exception as e:
        return {"error": str(e), "raw": seg[:600]}


if __name__ == "__main__":
    import json
    print(json.dumps(run(sys.argv[1]), ensure_ascii=False, indent=1))
