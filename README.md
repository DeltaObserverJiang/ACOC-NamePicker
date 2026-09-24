# A.C.O.C. 点名系统

一个课堂随机点名工具，以及它的配置器。

`index.html` 是点名器本体：单文件、零构建，双击就能用，也可以直接丢到任何
静态托管上。名字、班级都是从文件内部读的，不联网也不写任何东西。

## 配置器

`configurator/` 是配套的 Windows 配置器。点几下勾出需要的功能、填好名单，
它会现场生成一份只含这些功能的 `index.html`。生成的成品也是一样单文件双击即用。

能选的东西：

| 分组 | 选项 |
| --- | --- |
| 抽取模式 | 不重复、连续抽取、轮字抽取 |
| 设置菜单 | 概率修改、顺位设定、统计数据 |
| 界面主题 | 暗境节点 [AT]、经典节点 [CLASSIC] |
| 名单与数据 | 名单装载节点、高级数据管理 |
| 实验性 | 实验性功能、故障跳跃 |

全随机模式是基础功能，始终保留。关掉"名单装载节点"后，点名器会直接用
班级列表里的第一个班级启动。

## 名单

支持 Excel（`.xlsx` / `.xlsm`）和 CSV。表格形态宽松，下面几种都能认：

- 只有姓名一列
- 学号一列 + 姓名一列
- 上面两种再加一行"学号 / 姓名"表头
- 前面有空行、有说明行

认不出来时会明确告诉你是哪一步对不上。没有表格也行，配置器里可以直接手工
增删改。

一个点名器可以带多个班级，名单页左边是班级列表，第一个是默认班级。

## 构建

只有 Windows 版，用 CMake + MinGW-w64（UCRT）：

```sh
cmake -S configurator -B configurator/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build configurator/build
```

装包由 `configurator/installer/` 用同一套工具链构建，产物是
`ACOCConfiguratorSetup.exe`，装到当前用户目录，不需要管理员权限。
它同时兼作卸载程序。

改完 `index.html` 之后要重新生成模板：

```sh
python configurator/tools/make_template.py
```

## 运行环境

Windows 7 SP1 及以上。程序依赖系统的通用 C 运行库
（`api-ms-win-crt-*.dll`），Win10 / Win11 自带；Win7 和 8.1 需要先装
[KB2999226](https://www.microsoft.com/en-us/download/details.aspx?id=49093)，
装完重启即可。

## 目录

```
index.html              点名器本体
configurator/
  src/                  配置器源码
  res/                  图标、清单、模板
  installer/            安装程序
  tools/                构建与测试脚本
  _test/                测试夹具与界面自动化
```

## 版本

- **v0.2.1** —— 安装器界面重排：字号与留白、安装过程最少两秒、
  覆盖提示改到界面内、每页之间加过渡动效
- **v0.2** —— 一个点名器带多个班级、安装器覆盖安装提示、界面修补与动效
- **v0.1** —— 首个可用版本

历史里 v0.1 那个提交是重建的：它的源码在开发 v0.2 时被原地改写、二进制也被
覆盖过，仓库里的是按当时的改动逐条回退还原出来的，不是原始文件。
