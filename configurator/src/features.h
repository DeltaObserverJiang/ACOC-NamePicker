#pragma once
// 可选功能的清单，分组方式沿用点名器设置菜单里的说法。
#include <string>
#include <vector>

namespace feat {

struct Item {
    const char* key;
    const char* label;
    const char* note;
};

struct Group {
    const char* title;
    const char* note;
    std::vector<Item> items;
};

inline const std::vector<Group>& Groups() {
    static const std::vector<Group> g = {
        {"抽取模式", "全随机模式为基础功能，始终保留。",
         {
             {"deduct", "不重复模式", "被抽中的目标将被标记，本轮结束前不再进入抽取池。"},
             {"multi", "连续抽取", "一次抽出多个目标，结果并排呈现。"},
             {"char", "轮字抽取", "逐字揭开姓名，把悬念留到最后一刻。"},
         }},
        {"设置菜单 · 数据面板", "对应设置菜单中的三个页签。",
         {
             {"prob", "概率修改", "可单独调整每个人的权重；锁定后不参与权重均分。"},
             {"fixed", "顺位设定", "指定连续抽取中某个顺位的固定人选。"},
             {"stats", "统计数据", "统计每人的被抽中次数与占比。"},
         }},
        {"界面主题", "两套主题彼此独立；只保留一套时，设置中不再出现切换入口。",
         {
             {"theme_at", "暗境节点 [AT]", "霓虹色带，暗色基调。"},
             {"theme_classic", "经典节点 [CLASSIC]", "暖色经典，亮色基调。"},
         }},
        {"名单与数据", "名单装载节点即启动时选择班级的界面。",
         {
             {"roster", "名单装载节点", "启动时可选择预设班级，也可从 Excel 导入外部名单。"},
             {"data", "高级数据管理", "以配置文件导出或导入，留存概率、顺位与统计。"},
         }},
        {"实验性", "默认收纳在设置菜单的“其他”页。",
         {
             {"exp", "实验性功能", "需以系统覆写密钥解锁的隐藏节点。"},
             {"glitch", "故障跳跃", "抽取途中先跳向随机目标，最后落回真实结果。"},
         }},
    };
    return g;
}

}  // namespace feat
