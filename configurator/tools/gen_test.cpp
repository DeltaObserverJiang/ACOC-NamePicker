// 控制台测试：直接驱动 gen::Build，验证裁剪与名单注入，
// 输出结果再交给 headless Chrome 跑一遍。
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "features.h"
#include "generator.h"
#include "roster.h"

static std::string ReadAll(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool WriteAll(const char* path, const std::string& s) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(s.data(), (std::streamsize)s.size());
    return (bool)f;
}

static std::vector<roster::Student> Sample() {
    std::vector<roster::Student> v;
    const char* names[] = {"林知遥", "陈慕白", "周砚清", "沈聿", "顾南枝",
                           "苏行止", "叶怀瑾", "许砚书", "秦望舒", "何以安"};
    for (int i = 0; i < 10; ++i) v.push_back({i + 1, names[i]});
    return v;
}

static int Fail(const char* what) {
    std::printf("FAIL: %s\n", what);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: gen_test <template.html> <outdir>\n");
        return 1;
    }
    const std::string tpl = ReadAll(argv[1]);
    if (tpl.empty()) return Fail("模板为空");

    const std::string dir = argv[2];

    struct Case {
        const char* name;
        bool on;
    } cases[] = {{"all", true}, {"none", false}, {"mix", true}};

    for (const auto& cs : cases) {
        gen::Config cfg;
        for (const auto& g : feat::Groups())
            for (const auto& it : g.items) cfg.features.emplace_back(it.key, cs.on);
        if (std::string(cs.name) == "mix") {
            // 开一半关一半，专门盯住 HTML 标记那侧的裁剪
            const char* off[] = {"deduct", "char", "fixed", "theme_classic",
                                 "data", "exp", "glitch"};
            for (auto& kv : cfg.features)
                for (const char* o : off)
                    if (kv.first == o) kv.second = false;
        }
        cfg.students = Sample();
        cfg.className = "高三 (7) 班";

        std::string out, err;
        if (!gen::Build(tpl, cfg, out, err)) {
            std::printf("FAIL: %s -> %s\n", cs.name, err.c_str());
            return 1;
        }
        // 残留占位符检查
        const char* toks[] = {"@@FEATURES@@", "@@THEME@@", "@@ROSTER_CONST@@",
                              "@@ROSTER_BTN@@", "@@ROSTER_LISTENER@@",
                              "@@BOOT_TAIL@@"};
        for (const char* t : toks) {
            if (out.find(t) != std::string::npos) {
                std::printf("FAIL: %s 残留 %s\n", cs.name, t);
                return 1;
            }
        }
        const std::string path = dir + "/_" + cs.name + ".html";
        if (!WriteAll(path.c_str(), out)) return Fail("写出失败");
        std::printf("%-5s %8u chars  %s\n", cs.name, (unsigned)out.size(),
                    path.c_str());
    }
    std::printf("OK\n");
    return 0;
}
