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
        {"抽取模式", "全随机模式是基础功能，始终保留。",
         {
             {"deduct", "不重复模式", "被抽中的目标将被标记，直到一轮结束前不会再次出现。"},
             {"multi", "连续抽取", "一次性抽取多个目标，结果以卡片墙展示。"},
             {"char", "轮字抽取", "逐字解析目标姓名，极大增加揭晓悬念。"},
         }},
        {"设置菜单 · 数据面板", "对应设置菜单里的三个页签。",
         {
             {"prob", "概率修改", "单独调整某人的权重，被锁定的权重不参与平均。"},
             {"fixed", "顺位设定", "固定连续抽取中某个顺位出现的目标。"},
             {"stats", "统计数据", "记录每人被抽中的次数与占比。"},
         }},
        {"界面主题", "两套主题各自独立，只留一套时设置里不再出现切换入口。",
         {
             {"theme_at", "暗境节点 [AT]", "霓虹色带 · 暗色主题。"},
             {"theme_classic", "经典节点 [CLASSIC]", "暖色经典 · 亮色主题。"},
         }},
        {"名单与数据", "名单装载节点是启动时选择班级的界面。",
         {
             {"roster", "名单装载节点", "启动时选择预设班级，或从 Excel 导入外部名单。"},
             {"data", "高级数据管理", "导出与导入配置文件，备份概率、顺位与统计。"},
         }},
        {"实验性", "默认收起在设置菜单的“其他”页里。",
         {
             {"exp", "实验性功能", "需要系统覆写密钥才能解锁的隐藏节点。"},
             {"glitch", "故障跳跃", "抽取途中随机跳向另一目标，再落回真实结果。"},
         }},
    };
    return g;
}

}  // namespace feat
