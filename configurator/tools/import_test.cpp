// 控制台测试：把 gen::Build 生成的页面再喂回 imp::ParseHtml，验证往返一致，
// 并覆盖旧格式（v0.1 的 CLASS_NAME / CLASS_NAMES）与被手改过的写法。
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "features.h"
#include "generator.h"
#include "importer.h"
#include "roster.h"

static int g_fail = 0;

static std::string ReadAll(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        g_fail++;
    }
}

static std::string Show(const std::vector<gen::Klass>& cs) {
    std::ostringstream o;
    for (const auto& k : cs) {
        o << "[" << k.name << " " << k.students.size() << "人";
        for (const auto& s : k.students) o << " " << s.id << ":" << s.name;
        o << "]";
    }
    return o.str();
}

static bool SameClasses(const std::vector<gen::Klass>& a,
                        const std::vector<gen::Klass>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].name != b[i].name) return false;
        if (a[i].students.size() != b[i].students.size()) return false;
        for (size_t j = 0; j < a[i].students.size(); j++) {
            if (a[i].students[j].id != b[i].students[j].id) return false;
            if (a[i].students[j].name != b[i].students[j].name) return false;
        }
    }
    return true;
}

static std::vector<roster::Student> Make(const std::vector<std::string>& names) {
    std::vector<roster::Student> v;
    for (size_t i = 0; i < names.size(); i++)
        v.push_back({static_cast<int>(i) + 1, names[i]});
    return v;
}

// ---- 用例 ----

static void TestRoundTrip(const std::string& tpl) {
    std::vector<gen::Klass> want;
    want.push_back({"高三 (7) 班", Make({"林知遥", "陈慕白", "周砚清", "沈聿"})});
    want.push_back({"A2班", Make({"赵一", "钱二", "孙三", "李四", "周五"})});
    // 名字里带引号与反斜杠，走一遍 JsString 的转义/反转义
    want.push_back({"特殊字符班", Make({"引号\"君", "反斜杠\\君", "普通君"})});

    gen::Config cfg;
    for (const auto& g : feat::Groups())
        for (const auto& it : g.items) cfg.features.emplace_back(it.key, true);
    cfg.classes = want;

    std::string html, err;
    if (!gen::Build(tpl, cfg, html, err)) {
        Check(false, "gen::Build 失败: " + err);
        return;
    }

    std::vector<gen::Klass> got;
    if (!imp::ParseHtml(html, got, err)) {
        Check(false, "ParseHtml 失败: " + err);
        return;
    }
    Check(SameClasses(want, got),
          "往返不一致\n  期望 " + Show(want) + "\n  实际 " + Show(got));
    if (SameClasses(want, got)) std::printf("往返 3 个班级 OK\n");
}

static void TestLegacy() {
    // v0.1 的产物：只有 CLASS_NAME 与 CLASS_NAMES
    const std::string html =
        "<html><body><script>\n"
        "        const CLASS_NAME = \"A6班\";\n"
        "        const CLASS_NAMES = [\n"
        "            { id: 1, name: \"安思远\" }, { id: 2, name: \"陈存良\" },\n"
        "            { id: 3, name: \"崔芸宁\" }\n"
        "        ];\n"
        "        function initSystemData(){}\n"
        "</script></body></html>";
    std::vector<gen::Klass> got;
    std::string err;
    Check(imp::ParseHtml(html, got, err), "旧格式解析失败: " + err);
    Check(got.size() == 1 && got[0].name == "A6班" && got[0].students.size() == 3,
          "旧格式内容不对: " + Show(got));
    Check(got.size() == 1 && got[0].students[2].name == "崔芸宁",
          "旧格式姓名不对: " + Show(got));
    if (got.size() == 1 && got[0].name == "A6班") std::printf("旧格式 v0.1 OK\n");
}

static void TestHandEdited() {
    // 引号键名、注释、键序颠倒、\u 转义、十六进制学号
    const std::string html =
        "<script>\n"
        "const CLASS_PRESETS = [\n"
        "  {\n"
        "    \"students\": [\n"
        "      { name: \"\\u738b\\u4e94\", id: 0x5 },\n"
        "      { /* 中间夹注释 */ id: 6, name: '单引号名字' },\n"
        "      { id: 7, name: \"两\\u4e2a\\u5b57\" }\n"
        "    ],\n"
        "    name: \"手改班\" // 行尾注释\n"
        "  },\n"
        "  { name: \"空班\", students: [] }\n"
        "];\n"
        "</script>";
    std::vector<gen::Klass> got;
    std::string err;
    Check(imp::ParseHtml(html, got, err), "手改格式解析失败: " + err);
    Check(got.size() == 2, "手改格式班级数不对: " + Show(got));
    if (got.size() == 2) {
        Check(got[0].name == "手改班", "班级名不对: " + Show(got));
        Check(got[0].students.size() == 3, "学生数不对: " + Show(got));
        Check(got[0].students[0].name == "王五" && got[0].students[0].id == 5,
              "\\u 转义或十六进制学号不对: " + Show(got));
        Check(got[0].students[1].name == "单引号名字",
              "单引号字符串不对: " + Show(got));
        Check(got[1].name == "空班" && got[1].students.empty(),
              "空班不对: " + Show(got));
    }
    if (got.size() == 2 && got[0].students.size() == 3)
        std::printf("手改写法 OK\n");
}

static void TestBadInput() {
    std::vector<gen::Klass> got;
    std::string err;

    // 不是点名器的页面
    Check(!imp::ParseHtml("<html><body>你好</body></html>", got, err),
          "普通 HTML 应当被拒绝");
    Check(!err.empty(), "拒绝时应当给出原因");
    std::printf("非点名器页面被拒：%s\n", err.c_str());

    // 引用但没定义（只有 forEach 那处）
    got.clear();
    err.clear();
    Check(!imp::ParseHtml("<script>CLASS_PRESETS.forEach(x => x);</script>", got, err),
          "只有引用没有定义时应当被拒绝");

    // 括号不闭合
    got.clear();
    err.clear();
    Check(!imp::ParseHtml("<script>const CLASS_PRESETS = [ { name: \"甲\", students: [",
                          got, err),
          "括号不闭合时应当被拒绝");
}

static void TestDegenerate() {
    std::vector<gen::Klass> got;
    std::string err;

    // 学号重复 / 缺失，导入后应当补成不重复的正数
    const std::string html =
        "<script>const CLASS_PRESETS = [ { name: \"甲\", students: ["
        "{ id: 3, name: \"甲甲\" }, { id: 3, name: \"乙乙\" }, { name: \"丙丙\" }"
        "] } ];</script>";
    Check(imp::ParseHtml(html, got, err), "退化用例解析失败: " + err);
    if (got.size() == 1) {
        Check(got[0].students[0].id == 3, "第一个应当保留 3");
        Check(got[0].students[1].id == 2, "重复学号应当改写");
        Check(got[0].students[2].id == 3, "缺学号的应当补位");
        // 只要互不相同且为正即可
        Check(got[0].students[0].id > 0 && got[0].students[1].id > 0 &&
                  got[0].students[2].id > 0,
              "学号应当都是正数");
    }

    // 班级没名字：给个兜底名，别让工作区里出现无名班级
    got.clear();
    const std::string noName =
        "<script>const CLASS_PRESETS = [ { students: [ { id:1, name:\"甲\" }, "
        "{ id:2, name:\"乙\" } ] } ];</script>";
    Check(imp::ParseHtml(noName, got, err), "无名班级解析失败: " + err);
    Check(got.size() == 1 && !got[0].name.empty(), "无名班级应当有兜底名");
    if (got.size() == 1) std::printf("退化输入 OK（兜底名「%s」）\n", got[0].name.c_str());
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: import_test <template.html>\n");
        return 1;
    }
    const std::string tpl = ReadAll(argv[1]);
    if (tpl.empty()) {
        std::printf("FAIL: 模板为空\n");
        return 1;
    }

    TestRoundTrip(tpl);
    TestLegacy();
    TestHandEdited();
    TestBadInput();
    TestDegenerate();

    if (g_fail) {
        std::printf("\n%d 项失败\n", g_fail);
        return 1;
    }
    std::printf("\n全部通过\n");
    return 0;
}
