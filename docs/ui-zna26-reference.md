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
  和菜单栏；“Dual-window mode vs. single-window mode”说明单窗口输入方式；
  第 126-129 页说明图表、Trace 信息条和上下文菜单。
- [ZNA Specifications 21.00](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_brochures_and_datasheets/specifications/5215_4652_22/ZNA_specs_en_5215-4652-22_v2100.pdf)：
  主屏为 1280×800，辅助屏为 480×800，均为 125 dpi。

## 3. 基准画布

ZNA 物理前面板配有两个触摸屏，但软件也提供只显示应用窗口的 Single Window
Mode。该模式仍可在应用窗口内排列多个图表，并在最右侧启用虚拟 Hard Key
面板。M1.5 采用该模式，以 1280×800 主应用屏作为基准画布：

| 区域 | 尺寸 | 职责 |
| --- | --- | --- |
| 主应用屏 | 1280×800 | 菜单、工具栏、测量图、Softtool、Hard Key、状态栏 |
| 辅助控制屏 | 480×800 | 虚拟功能键、数值输入、方向与确认键 |

当前页面只渲染主应用屏，空间不足时整体等比缩放。辅助控制屏作为后续模式，
启用时仍按 1280:480 并排显示，不得重排为普通响应式侧栏。当前开发验收截图以
1280×800、100% 缩放为主，并补充 1920×1080 等比缩放检查。

## 4. 主应用屏结构

从上到下、从左到右保持以下顺序。尺寸来自 1280×800 参考界面的像素量测，
实现时允许为边框和文字裁切做少量微调：

1. 顶部工具栏，约 42 px 高：撤销、缩放、最大化图表、增加 Trace/Marker、
   删除、截图等。
2. 图表工作区，约 848 px 宽：默认采用 2×2 排列。每格都包含 Trace 信息条、
   黑底绘图区和 Channel 状态行；无采样帧时只画坐标网格，不伪造业务曲线。
3. Measurement Softtool，约 270 px 宽：紧邻图表右侧，包含测量设置区、
   S 参数选择区与测量类别导航区。
4. 虚拟 Hard Key 面板，约 150 px 宽：固定在最右侧，以两列按钮分为 Trace、
   Stimulus、Channel、System 四组。
5. 底部菜单与状态栏，约 34 px 高：File、Trace、Channel、Display、Tools、System、Help，
   以及活动 Channel、平均状态、进度和时间。

初始 M1.5 页面显示四格布局，其中两个笛卡尔网格和两个 Smith 网格用于验证
几何结构。只有服务中真实存在的 Trace 才显示 Trace 名称；采样数据接通前不绘制
黄色曲线。后续支持 1×2、2×1 和单图最大化。

## 5. 虚拟 Hard Key 结构

Single Window Mode 将以下虚拟功能键放在应用窗口最右侧；它不是独立辅助屏：

- Trace：Meas、Format、Scale、Trace Config、Line、Marker。
- Stimulus：Start、Stop、Center、Span。
- Channel：Power/BW/Avg、Sweep、Cal、Channel Config、Trigger。
- System：Mode、Offset/Embed、File/Print、Setup、Tools、Display、Help、Preset。

点击功能键必须打开相邻的 Softtool，不能另造网页导航页。数字输入面板只在编辑
数值时临时出现，不占用当前首屏的常驻布局。

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

- 1280×800 单应用窗口及等比缩放。
- 2×2 黑底图表、独立 Trace 信息条、Channel 状态行和底部菜单。
- 图表右侧 Measurement Softtool 与最右两列虚拟 Hard Key。
- Meas 功能键与对应 Softtool 的联动。
- Start、Stop、Points、IFBW、Power 数值输入入口。
- 服务连接状态和 `stateRevision`，融入状态栏而非另做 Dashboard 卡片。
- 所有业务数据来自 `vna-server`，无静态业务 Mock。

## 8. 视觉回归

每个前端提交至少验证：

- 1280×800 基准截图没有溢出、错位或文本裁切。
- 主屏严格为 1280×800 逻辑像素；M1.5 不显示独立辅助屏。
- 图表、Softtool、Hard Key 和状态栏的顺序与官方参考一致。
- 页面中不存在原厂品牌名、Logo 或直接复制的原厂素材。
- 鼠标、触控和键盘焦点均能完成当前切片的操作。
