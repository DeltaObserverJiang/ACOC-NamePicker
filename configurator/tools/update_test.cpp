// 控制台测试：把各个历史版本的点名器喂给 imp::ParseHtml，再用 gen::Build 输出
// 最新版；输入本身就是当前版本时，要求输出与它逐字节一致（模板没走样）。
//
// 用法: update_test <template.html> <index.html> <outdir> [其它点名器.html ...]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "features.h"
#include "generator.h"
#include "importer.h"
#include "roster.h"

static std::string ReadAll(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool WriteAll(const std::string& path, const std::string& s) {
    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    f.write(s.data(), (std::streamsize)s.size());
    return (bool)f;
}

static std::string Norm(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        o += s[i];
    }
    return o;
}

static std::string BaseName(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

static gen::Config AllOn() {
    gen::Config cfg;
    for (const auto& g : feat::Groups())
        for (const auto& it : g.items) cfg.features.emplace_back(it.key, true);
    return cfg;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: update_test <template.html> <index.html> <outdir> [more.html ...]\n");
        return 1;
    }
    const std::string tpl = ReadAll(argv[1]);
    if (tpl.empty()) {
        std::printf("FAIL: 模板读不出来\n");
        return 1;
    }
    const std::string selfPath = argv[2];
    const std::string outdir = argv[3];
    const std::string selfText = ReadAll(selfPath.c_str());

    std::vector<std::string> inputs;
    inputs.push_back(selfPath);
    for (int i = 4; i < argc; i++) inputs.push_back(argv[i]);

    int bad = 0;
    for (size_t i = 0; i < inputs.size(); i++) {
        const std::string& path = inputs[i];
        const std::string text = ReadAll(path.c_str());
        const std::string name = BaseName(path);
        if (text.empty()) {
            std::printf("SKIP %-28s 读不出来\n", name.c_str());
            continue;
        }

        std::string title, why;
        bool picker = imp::LooksLikePicker(text, title, why);
        std::vector<gen::Klass> classes;
        std::string err, how;
        bool ok = imp::ParseHtml(text, classes, err, &how);
        size_t total = 0;
        for (size_t k = 0; k < classes.size(); k++) total += classes[k].students.size();

        std::printf("%-28s 点名器=%-3s 班级=%zu 人数=%zu  依据=%s\n", name.c_str(),
                    picker ? "是" : "否", classes.size(), total,
                    ok ? how.c_str() : err.c_str());
        for (size_t k = 0; k < classes.size() && k < 6; k++)
            std::printf("      - %-16s %zu 人\n", classes[k].name.c_str(),
                        classes[k].students.size());
        if (!ok || classes.empty()) {
            std::printf("      FAIL: 没读出名单（%s）\n", err.c_str());
            bad++;
            continue;
        }

        gen::Config cfg = AllOn();
        cfg.classes = classes;
        std::string html, buildErr;
        if (!gen::Build(tpl, cfg, html, buildErr)) {
            std::printf("      FAIL: 生成失败（%s）\n", buildErr.c_str());
            bad++;
            continue;
        }
        const std::string out = outdir + "/updated_" + name;
        if (!WriteAll(out, html)) {
            std::printf("      FAIL: 写出失败\n");
            bad++;
            continue;
        }

        // 输入就是当前版本时，输出必须和它一模一样
        if (path == selfPath) {
            std::string a = Norm(selfText), b = Norm(html);
            if (a == b) {
                std::printf("      OK: 与 index.html 逐字节一致\n");
            } else {
                size_t p = 0;
                while (p < a.size() && p < b.size() && a[p] == b[p]) p++;
                size_t la = 1, lb = 1;
                for (size_t k = 0; k < p && k < a.size(); k++)
                    if (a[k] == '\n') la++;
                lb = la;
                std::printf("      FAIL: 与 index.html 不一致（第 %zu 行附近，%zu/%zu 字节）\n",
                            la, a.size(), b.size());
                std::string ca, cb;
                for (size_t k = p; k < a.size() && ca.size() < 90; k++)
                    ca += (a[k] == '\n' ? '|' : a[k]);
                for (size_t k = p; k < b.size() && cb.size() < 90; k++)
                    cb += (b[k] == '\n' ? '|' : b[k]);
                std::printf("        本体: %s\n", ca.c_str());
                std::printf("        生成: %s\n", cb.c_str());
                bad++;
            }
        }
    }

    if (bad) {
        std::printf("FAIL: %d 项没过\n", bad);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
