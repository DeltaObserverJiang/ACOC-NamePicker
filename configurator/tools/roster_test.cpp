// 控制台测试：把 _test/roster 下的表格逐个走一遍识别逻辑。
#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>

#include "roster.h"

static std::vector<std::string> ListDir(const std::wstring& dir) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char buf[512] = {0};
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, buf, sizeof(buf) - 1,
                            nullptr, nullptr);
        out.push_back(buf);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

static std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: roster_test <dir>\n");
        return 1;
    }
    SetConsoleOutputCP(65001);
    const std::wstring dir = Widen(argv[1]);
    int bad = 0;

    for (const std::string& name : ListDir(dir)) {
        const std::wstring path = dir + L"\\" + Widen(name);
        bool csv = name.size() > 4 && name.substr(name.size() - 4) == ".csv";
        roster::Sheet sheet;
        std::string err;
        bool ok = csv ? roster::ReadCsv(path, sheet, err)
                      : roster::ReadXlsx(path, sheet, err);

        std::printf("\n== %s\n", name.c_str());
        if (!ok) {
            std::printf("   读取失败: %s\n", err.c_str());
            bad++;
            continue;
        }
        std::printf("   读到 %d 行 x ", (int)sheet.size());
        std::printf("%d 列\n", sheet.empty() ? 0 : (int)sheet[0].size());

        roster::Result r = roster::Detect(sheet);
        // bad_ 开头的表本来就是坏的，识别不出来才算对
        if (name.compare(0, 4, "bad_") == 0) {
            if (r.ok) {
                std::printf("   本该拒绝，却认成了 %d 个人\n",
                            (int)r.students.size());
                bad++;
            } else {
                std::printf("   按预期拒绝: %s\n", r.message.c_str());
            }
            continue;
        }
        if (!r.ok) {
            std::printf("   识别失败: %s\n", r.message.c_str());
            bad++;
            continue;
        }
        std::printf("   ok  姓名列=%d 学号列=%d 跳过表头=%d 人数=%d\n",
                    r.nameColumn, r.idColumn, (int)r.headerSkipped,
                    (int)r.students.size());
        std::printf("   首: ");
        for (int i = 0; i < 3 && i < (int)r.students.size(); i++)
            std::printf("#%d %s  ", r.students[i].id, r.students[i].name.c_str());
        std::printf("\n   末: #%d %s\n", r.students.back().id,
                    r.students.back().name.c_str());
    }
    std::printf("\n%s\n", bad ? "有失败项" : "全部通过");
    return bad ? 1 : 0;
}
