# ZNA26 界面复刻基线

> 状态：第一阶段 UI 基线
>
> 参考版本：ZNA User Manual 41，Firmware V3.20 或更高版本
>
> 日期：2026-07-31

## 1. 目标与边界

前端复刻 ZNA26 的屏幕分区、信息密度、控件层级、颜色语义和触控工作流，
不进行独立视觉设计。复刻基准以官方资料中的默认仪表界面为准。

以下内容不得复制：

- Rohde & Schwarz、R&S、ZNA 等品牌标识和商标。
- 原厂 Logo、截图、图标文件、字体文件和其他专有素材。
- 与本项目领域模型不一致的产品型号、选件和能力声明。

项目使用中性名称、自制图标和系统字体，但保持相同的几何布局、控件位置、
交互层级与仪表式视觉语言。

## 2. 官方参考

- [ZNA User Manual 41](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_41.pdf)：
  第 32-33 页说明双屏分区；第 44-48 页说明工具栏、虚拟功能键、Softtool
  和菜单栏；第 126-129 页说明图表、Trace 信息条和上下文菜单。
- [ZNA Specifications 21.00](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_brochures_and_datasheets/specifications/5215_4652_22/ZNA_specs_en_5215-4652-22_v2100.pdf)：
  主屏为 1280×800，辅助屏为 480×800，均为 125 dpi。

## 3. 基准画布

桌面完整模式使用 1760×800 逻辑像素画布：

| 区域 | 尺寸 | 职责 |
| --- | --- | --- |
| 主应用屏 | 1280×800 | 菜单、工具栏、测量图、Trace 信息、Softtool、状态栏 |
| 辅助控制屏 | 480×800 | 虚拟功能键、数值输入、方向与确认键 |

浏览器视口足够宽时按 1280:480 并排显示。空间不足时整体等比缩放，不允许
改变两个屏幕的内部比例或将控制区重排为普通响应式侧栏。开发验收截图以
1760×800、100% 缩放为主，并补充 1920×1080 等比缩放检查。

## 4. 主应用屏结构

从上到下、从左到右保持以下顺序：

1. 深色工具栏：撤销、缩放、最大化图表、增加 Trace/Marker、删除、截图等。
2. 图表工作区：黑色背景、细灰蓝网格、Trace 信息条和图表编号。
3. Softtool：位于图表右侧，使用页签、分组标题、矩形按钮和展开箭头。
4. 图表状态行：显示 Channel、Start、Stop、Power、IFBW 等当前条件。
5. 底部菜单与状态栏：File、Trace、Channel、Display、Tools、System、Help，
   以及活动 Channel、平均状态、进度和时间。

初始 M1.5 页面显示一个笛卡尔图表。后续支持与参考界面一致的 1×2、2×1、
2×2 排列和单图最大化；每个图表独立显示 Trace 信息条及状态行。

## 5. 辅助控制屏结构

辅助屏上半区保留成组的虚拟功能键：

- Trace：Meas、Format、Scale、Trace Config、Line、Marker。
- Stimulus：Start、Stop、Center、Span。
- Channel：Power/BW/Avg、Sweep、Cal、Channel Config、Trigger。
- System：Mode、Offset/Embed、File/Print、Setup、Tools、Display、Help、Preset。

下半区为数据输入面板，包含当前输入值、数字键、单位/步进选择、退格、方向键、
确认和取消。点击功能键必须打开主屏中对应的 Softtool，不能另造网页导航页。

## 6. 视觉令牌

令牌以官方截图量测后再微调，首版使用以下语义：

| 语义 | 表现 |
| --- | --- |
| 应用和图表背景 | 近黑色 |
| 面板背景 | 深蓝灰色 |
| 普通按钮 | 中灰蓝色、细深色分隔线 |
| 活动按钮 | 高饱和青蓝色 |
| 帮助按钮 | 橙黄色 |
| Trace 1 | 黄色 |
| Trace 2 | 绿色 |
| Trace 3 | 青色 |
| Trace 4 | 洋红色 |
| 坐标网格 | 低对比灰蓝色 |
| 主要文本 | 白色或浅灰色，紧凑无衬线字体 |

控件以触控为第一输入方式。按钮视觉尺寸、间距、激活态、禁用态和按下反馈需与
基准截图逐项比对，同时保留键盘焦点与可读的无障碍名称。

## 7. 首个可验收切片

M1.5 必须先交付以下可操作内容：

- 1760×800 双屏壳及等比缩放。
- 一个黑底笛卡尔图表、Trace 信息条、Channel 状态行和底部菜单。
- Meas 功能键与对应 Softtool 的联动。
- Start、Stop、Points、IFBW、Power 数值输入入口。
- 服务连接状态和 `stateRevision`，融入状态栏而非另做 Dashboard 卡片。
- 所有业务数据来自 `vna-server`，无静态业务 Mock。

## 8. 视觉回归

每个前端提交至少验证：

- 1760×800 基准截图没有溢出、错位或文本裁切。
- 主屏严格为 1280 逻辑像素，辅助屏严格为 480 逻辑像素。
- 功能键、Softtool、图表和状态栏的顺序与官方参考一致。
- 页面中不存在原厂品牌名、Logo 或直接复制的原厂素材。
- 鼠标、触控和键盘焦点均能完成当前切片的操作。
