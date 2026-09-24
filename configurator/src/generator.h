#pragma once
#include <string>
#include <utility>
#include <vector>

#include "roster.h"

namespace gen {

// class 是关键字，所以叫 Klass
struct Klass {
    std::string name;  // UTF-8
    std::vector<roster::Student> students;
};

struct Config {
    std::vector<std::pair<std::string, bool>> features;
    std::vector<Klass> classes;  // 第一个是默认班级
};

bool Enabled(const Config& c, const char* key);

// 按功能开关裁剪模板，再把各班级名单写进去
bool Build(const std::string& tpl, const Config& c, std::string& out,
           std::string& err);

}  // namespace gen
