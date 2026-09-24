#pragma once
#include <string>
#include <utility>
#include <vector>

#include "roster.h"

namespace gen {

struct Config {
    std::vector<std::pair<std::string, bool>> features;
    std::vector<roster::Student> students;
    std::string className;  // UTF-8
};

bool Enabled(const Config& c, const char* key);

// 按功能开关裁剪模板，再把名单与班级名写进去
bool Build(const std::string& tpl, const Config& c, std::string& out,
           std::string& err);

}  // namespace gen
