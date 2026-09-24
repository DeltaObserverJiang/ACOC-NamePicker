#pragma once
// 最小 ZIP 读取：只实现 DEFLATE 解压和 stored，够读 .xlsx（本质是 zip）即可。
#include <cstdint>
#include <string>
#include <vector>

namespace zip {

struct Entry {
    std::string name;
    uint32_t method = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint32_t localOffset = 0;
};

// 解析中央目录；不是 zip 就返回 false。
bool List(const std::vector<uint8_t>& data, std::vector<Entry>& out);

// 取出指定条目并按需解压。名字大小写不敏感。
bool Extract(const std::vector<uint8_t>& data, const Entry& e,
             std::vector<uint8_t>& out);

// 裸 DEFLATE 解压流。
bool InflateRaw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out);

}  // namespace zip
