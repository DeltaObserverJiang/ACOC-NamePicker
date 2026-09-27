#pragma once
#include <string>
#include <vector>

#include "generator.h"

namespace imp {

// 从点名器导出的 HTML 里还原班级与名单，与 gen::Build 对称。
//
// 生成物里的班级名单是一段规整的 JS 字面量（CLASS_PRESETS），解析它比在
// 任意 HTML 里找表格稳得多。同时兼容 v0.1 的产物：那时还没有 CLASS_PRESETS，
// 只有 CLASS_NAME（班级名）与 CLASS_NAMES（默认班级的学生数组）。
bool ParseHtml(const std::string& utf8, std::vector<gen::Klass>& classes,
               std::string& err);

// 读文件（自动辨认 UTF-8 / UTF-16 / GBK）后再解析
bool ImportFile(const std::wstring& path, std::vector<gen::Klass>& classes,
                std::string& err);

}  // namespace imp
