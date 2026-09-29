#pragma once
#include <string>
#include <vector>

#include "generator.h"

namespace imp {

// 这个 HTML 是不是点名器？
//
// 认两样东西：标题里的「点名 / A.C.O.C.」，以及页面里那几处只有点名器才有的
// 特征（initSystemData、studentsData、big-name、MODE_INFO…）。标题命中即算，
// 标题没写但特征凑够两条也算。title / why 都只为把判断依据讲清楚。
bool LooksLikePicker(const std::string& utf8, std::string& title,
                     std::string& why);

// 从点名器页面里还原班级与名单，与 gen::Build 对称。
//
// 不要求文件由本配置器生成，也不要求它带 CLASS_PRESETS：依次尝试
//   1) CLASS_PRESETS（v0.4 起的生成物）
//   2) CLASS_NAME + CLASS_NAMES
//   3) initSystemData(A2_NAMES, "A2班 (预设)") 这类调用与它引用的数组（v0.1~v0.4）
//   4) 脚本里任何 `标识符 = [ {id, name} … ]` 或姓名数组字面量
//   5) 页面里的表格、以及 class 名带 student / name 的元素
// 第一种能读出班级的就用它，班级名一律照抄原文——本机存档正是按班级名存的，
// 名字改动一个字，累积的统计就对不上了。
//
// method 非空时带回最终用的是哪一种依据，方便界面把过程说清楚。
bool ParseHtml(const std::string& utf8, std::vector<gen::Klass>& classes,
               std::string& err, std::string* method = nullptr);

// 读文件（自动辨认 UTF-8 / UTF-16 / GBK）后再解析
bool ImportFile(const std::wstring& path, std::vector<gen::Klass>& classes,
                std::string& err, std::string* method = nullptr);

}  // namespace imp
