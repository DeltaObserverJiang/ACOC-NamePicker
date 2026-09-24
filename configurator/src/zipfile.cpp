#include "zipfile.h"

#include <cstring>

namespace zip {
namespace {

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                                 (static_cast<uint32_t>(p[3]) << 24));
}

// ---- DEFLATE ----

struct BitReader {
    const uint8_t* p;
    size_t len;
    size_t pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool bad = false;

    uint32_t Bits(int need) {
        while (bitcnt < need) {
            if (pos >= len) {
                bad = true;
                return 0;
            }
            bitbuf |= static_cast<uint32_t>(p[pos++]) << bitcnt;
            bitcnt += 8;
        }
        uint32_t v = bitbuf & ((1u << need) - 1);
        bitbuf >>= need;
        bitcnt -= need;
        return v;
    }
};

struct Huffman {
    // 按码长分组的规范 Huffman 解码表
    uint16_t count[16] = {0};
    std::vector<uint16_t> symbol;

    bool Build(const uint8_t* lengths, int n) {
        for (int i = 0; i < 16; i++) count[i] = 0;
        for (int i = 0; i < n; i++) count[lengths[i]]++;
        count[0] = 0;
        int left = 1;
        for (int i = 1; i < 16; i++) {
            left <<= 1;
            left -= count[i];
            if (left < 0) return false;
        }
        std::vector<uint16_t> offs(16, 0);
        for (int i = 1; i < 15; i++)
            offs[i + 1] = static_cast<uint16_t>(offs[i] + count[i]);
        symbol.assign(offs[15] + count[15], 0);
        for (int i = 0; i < n; i++)
            if (lengths[i]) symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
        return true;
    }

    int Decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            code |= static_cast<int>(br.Bits(1));
            if (br.bad) return -1;
            int cnt = count[len];
            if (code - cnt < first) return symbol[index + (code - first)];
            index += cnt;
            first = (first + cnt) << 1;
            code <<= 1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                               15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                               67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,
                                13,   17,   25,   33,   49,   65,    97,
                                129,  193,  257,  385,  513,  769,   1025,
                                1537, 2049, 3073, 4097, 6145, 8193,  12289,
                                16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5, 5, 6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

Huffman g_fixedLit, g_fixedDist;
bool g_fixedReady = false;

void BuildFixed() {
    if (g_fixedReady) return;
    uint8_t lit[288];
    for (int i = 0; i < 144; i++) lit[i] = 8;
    for (int i = 144; i < 256; i++) lit[i] = 9;
    for (int i = 256; i < 280; i++) lit[i] = 7;
    for (int i = 280; i < 288; i++) lit[i] = 8;
    g_fixedLit.Build(lit, 288);
    uint8_t dist[30];
    for (int i = 0; i < 30; i++) dist[i] = 5;
    g_fixedDist.Build(dist, 30);
    g_fixedReady = true;
}

int InflateBlock(BitReader& br, std::vector<uint8_t>& out) {
    int final = static_cast<int>(br.Bits(1));
    int type = static_cast<int>(br.Bits(2));
    if (br.bad) return false;

    if (type == 0) {
        br.bitbuf = 0;
        br.bitcnt = 0;
        if (br.pos + 4 > br.len) return false;
        uint16_t len = rd16(br.p + br.pos);
        br.pos += 4;  // len + ~len
        if (br.pos + len > br.len) return false;
        out.insert(out.end(), br.p + br.pos, br.p + br.pos + len);
        br.pos += len;
        return final != 0;
    }

    Huffman lit, dist;
    if (type == 1) {
        BuildFixed();
        lit = g_fixedLit;
        dist = g_fixedDist;
    } else if (type == 2) {
        static const uint8_t kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                           11, 4,  12, 3, 13, 2, 14, 1, 15};
        int hlit = static_cast<int>(br.Bits(5)) + 257;
        int hdist = static_cast<int>(br.Bits(5)) + 1;
        int hclen = static_cast<int>(br.Bits(4)) + 4;
        if (br.bad) return -1;
        uint8_t cl[19] = {0};
        for (int i = 0; i < hclen; i++) cl[kOrder[i]] = static_cast<uint8_t>(br.Bits(3));
        Huffman clh;
        if (!clh.Build(cl, 19)) return false;

        std::vector<uint8_t> lengths;
        lengths.reserve(hlit + hdist);
        while (static_cast<int>(lengths.size()) < hlit + hdist) {
            int sym = clh.Decode(br);
            if (sym < 0) return false;
            if (sym < 16) {
                lengths.push_back(static_cast<uint8_t>(sym));
            } else {
                uint8_t prev = lengths.empty() ? 0 : lengths.back();
                int rep, val;
                if (sym == 16) {
                    if (lengths.empty()) return false;
                    rep = 3 + static_cast<int>(br.Bits(2));
                    val = prev;
                } else if (sym == 17) {
                    rep = 3 + static_cast<int>(br.Bits(3));
                    val = 0;
                } else {
                    rep = 11 + static_cast<int>(br.Bits(7));
                    val = 0;
                }
                for (int i = 0; i < rep; i++) lengths.push_back(static_cast<uint8_t>(val));
            }
        }
        if (static_cast<int>(lengths.size()) > hlit + hdist) return false;
        if (!lit.Build(lengths.data(), hlit)) return false;
        if (!dist.Build(lengths.data() + hlit, hdist)) return false;
    } else {
        return false;
    }

    for (;;) {
        int sym = lit.Decode(br);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return final != 0;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int length = kLenBase[sym] + static_cast<int>(br.Bits(kLenExtra[sym]));
            int dsym = dist.Decode(br);
            if (dsym < 0 || dsym >= 30) return false;
            int distance = kDistBase[dsym] + static_cast<int>(br.Bits(kDistExtra[dsym]));
            if (distance <= 0 || static_cast<size_t>(distance) > out.size()) return false;
            size_t start = out.size() - distance;
            for (int i = 0; i < length; i++) out.push_back(out[start + i]);
        }
    }
}

}  // namespace

bool InflateRaw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
    BitReader br{src, srcLen};
    for (;;) {
        int r = InflateBlock(br, out);
        if (r == -1) return false;
        if (r == 1) return true;
        if (br.bad) return false;
    }
}

bool List(const std::vector<uint8_t>& data, std::vector<Entry>& out) {
    out.clear();
    if (data.size() < 22) return false;
    // 从尾部往回找 EOCD（注释最长 65535）
    size_t limit = data.size() > 65557 ? data.size() - 65557 : 0;
    size_t eocd = std::string::npos;
    for (size_t i = data.size() - 22;; i--) {
        if (rd32(&data[i]) == 0x06054b50) {
            eocd = i;
            break;
        }
        if (i == limit) break;
    }
    if (eocd == std::string::npos) return false;

    uint16_t total = rd16(&data[eocd + 10]);
    uint32_t off = rd32(&data[eocd + 16]);
    size_t p = off;
    for (int i = 0; i < total; i++) {
        if (p + 46 > data.size() || rd32(&data[p]) != 0x02014b50) return false;
        Entry e;
        e.method = rd16(&data[p + 10]);
        e.compSize = rd32(&data[p + 20]);
        e.uncompSize = rd32(&data[p + 24]);
        uint16_t nameLen = rd16(&data[p + 28]);
        uint16_t extraLen = rd16(&data[p + 30]);
        uint16_t cmtLen = rd16(&data[p + 32]);
        e.localOffset = rd32(&data[p + 42]);
        if (p + 46 + nameLen > data.size()) return false;
        e.name.assign(reinterpret_cast<const char*>(&data[p + 46]), nameLen);
        out.push_back(e);
        p += 46 + nameLen + extraLen + cmtLen;
    }
    return true;
}

bool Extract(const std::vector<uint8_t>& data, const Entry& e,
             std::vector<uint8_t>& out) {
    size_t p = e.localOffset;
    if (p + 30 > data.size() || rd32(&data[p]) != 0x04034b50) return false;
    uint16_t nameLen = rd16(&data[p + 26]);
    uint16_t extraLen = rd16(&data[p + 28]);
    size_t start = p + 30 + nameLen + extraLen;
    if (start + e.compSize > data.size()) return false;

    if (e.method == 0) {
        out.assign(data.begin() + start, data.begin() + start + e.compSize);
        return true;
    }
    if (e.method != 8) return false;
    out.clear();
    return InflateRaw(&data[start], e.compSize, out) &&
           (e.uncompSize == 0 || out.size() == e.uncompSize);
}

}  // namespace zip
