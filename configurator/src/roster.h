#pragma once
#include <string>
#include <vector>

namespace roster {

struct Student {
    int id = 0;
    std::string name;  // UTF-8
};

// 表格原始内容，行 × 列，单元格为 UTF-8
using Sheet = std::vector<std::vector<std::string>>;

struct Result {
    std::vector<Student> students;
    bool ok = false;
    std::string message;   // 成功时是识别说明，失败时是原因
    int nameColumn = -1;
    int idColumn = -1;
    bool headerSkipped = false;
};

// 读取 .xlsx / .xlsm（zip + DEFLATE）
bool ReadXlsx(const std::wstring& path, Sheet& sheet, std::string& err);
// 读取 .csv / .txt，自动区分 UTF-8 与 GBK
bool ReadCsv(const std::wstring& path, Sheet& sheet, std::string& err);

// 从表格里辨认学号列与姓名列。表格可能只有姓名，也可能带表头。
Result Detect(const Sheet& sheet);

// 姓名字符串清洗：去首尾空白、全角空格与零宽字符
std::string Trim(const std::string& s);

}  // namespace roster
