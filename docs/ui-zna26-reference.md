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
2. 图表工作区：手册 Preset 默认采用单个主图；每个真实 Window 对应一个
   Diagram，包含 Trace 信息条、黑底绘图区和 Channel 状态行。多个 Window
   仍按工作区布局显示，但不得固定补齐不存在的空 Diagram。
3. Softtool，约 270 px 宽：默认关闭；用户按下相应 Hard Key 后在图表右侧打开。
   Measurement Softtool 包含测量设置区、S 参数选择区与测量类别导航区。
4. 虚拟 Hard Key 面板，约 150 px 宽：固定在最右侧，以两列按钮分为 Trace、
   Stimulus、Channel、System 四组。
5. 底部菜单与状态栏，约 34 px 高：File、Trace、Channel、Display、Tools、System、Help，
   以及活动 Channel、平均状态、进度和时间。

手册默认状态只显示一个包含 S21 dB Magnitude Trace 的主图。Diagram 数量必须
来自 `StateSnapshot` 中的真实 Window；只有服务中真实存在的 Trace 才显示 Trace
名称，采样数据接通前不绘制曲线。多个 Window 与单图最大化能力继续保留。

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
| 默认 S21 Trace | 绿色；色块与曲线一致 |
| 活动 Diagram | 黄色边框，不改变 Trace 颜色 |
| 其他 Trace | 当前单 Trace 映射沿用既有黄色；不据此推导后端颜色接口 |
| 坐标网格 | 低对比灰蓝色 |
| 主要文本 | 白色或浅灰色，紧凑无衬线字体 |

控件以触控为第一输入方式。按钮视觉尺寸、间距、激活态、禁用态和按下反馈需与
基准截图逐项比对，同时保留键盘焦点与可读的无障碍名称。

## 7. 首个可验收切片

M1.5 必须先交付以下可操作内容：

- 1280×800 单应用窗口及等比缩放。
- 手册默认单个黑底主图、独立 Trace 信息条、Channel 状态行和底部菜单；多 Window
  状态只渲染快照中真实存在的 Diagram。
- 默认关闭 Softtool；Meas Hard Key 可在图表右侧打开 Measurement Softtool，
  最右两列虚拟 Hard Key 常驻。
- Meas 功能键与对应 Softtool 的联动。
- Start、Stop、Points、IFBW、Power 数值输入入口。
- 服务连接状态和 `stateRevision`，融入状态栏而非另做 Dashboard 卡片。
- 所有业务数据来自 `vna-server`，无静态业务 Mock。

## 8. Scale Softtool 基线

本节依据 User Manual 41 的 PDF 页码与印刷页码 547-550；单窗口数值输入规则
见 PDF/印刷页 58-59。547 页给出入口和作用范围，548 页给出布局、状态与参数
关系，549-550 页给出各控件行为。不得复制手册截图或品牌素材。

Scale Values 采用左侧主控列、右侧标签列的两列布局，宽度约为 64%:36%。
主控列中的每个控件占该列全宽并纵向排列；右列顶部为 `Scale Values` 标签。
这只是 Softtool 内部布局，最右侧 Hard Key 仍遵守第 5 节的两列基线。

控件顺序和行为固定如下：

| 顺序 | 控件 | 行为与可用性 |
| --- | --- | --- |
| 1 | Auto Scale Trace | 自动缩放活动 Trace，不改变激励或水平轴 |
| 2 | Auto Scale Diagram | 独立缩放活动 Diagram 中的全部 Trace |
| 3 | Auto Scale Diag. (Common Scale) | 对活动 Diagram 中相同 Format 的 Trace 使用共同缩放 |
| 4 | Ref Value = Marker | 使用活动 Marker 的响应值，Marker 未支持时禁用 |
| 5 | Scale/Div | 设置相邻网格线的增量；圆图禁用 |
| 6 | Ref Value | 设置笛卡尔参考线值或圆图外圆值 |
| 7 | Ref Pos | 设置笛卡尔参考线位置 0-10；圆图禁用 |
| 8 | Max | 设置笛卡尔图上边界；圆图禁用 |
| 9 | Min | 设置笛卡尔图下边界；圆图禁用 |
| 10 | Auto Scale Tr. Cont. | 扫频期间持续自动缩放活动 Trace |

活动 `Scale` Hard Key 和 `Scale Values` 标签使用蓝色；截图中蓝色的
`Scale/Div` 表示当前焦点行，不表示业务开关。`Ref Value = Marker` 为禁用态，
`Auto Scale Tr. Cont.` 显示 `Off` 则是有效的关闭状态。手册所称的 recall set
（召回集合）至少包含两条 Trace，且活动 Trace 不是 reference trace（参考迹线）
时，Scale Coupling 才可用；这两个概念尚未进入项目领域模型，因此当前禁用。

单位由 Trace Format 决定：dB Mag 使用 dB，Phase 和 Unwr Phase 使用 degree，
Delay 使用 ns，其他无量纲 Format 使用 U；圆图的 Ref Value 使用 U，Ref Pos
无单位。手册截图中的 `1 ns`、`0 s`、`5`、`5 ns`、`-5 ns` 和 `Off` 是 Delay
示例当前值，不是项目默认值。单窗口允许外接键盘就地编辑数值；原机 Numeric
Editor、单位键和 Step Size 面板在项目支持前不得伪造。

能力范围分为三类：

- 当前真实状态：Scale Hard Key 保持禁用；前端仅可复用活动 Trace、Softtool
  路由、命令生命周期与刷新机制，不保存 Scale 业务状态。
- 等待后端契约：Scale/Div、Ref Value、Ref Pos、Max、Min、自动缩放、持续自动
  缩放、Format 切换后的默认值和 Diagram 级成员关系均来自 display-model 与
  `StateSnapshot`。
- 暂不支持并禁用：Marker 派生缩放、Scale Coupling、未进入 TraceFormat 的格式，
  以及原机 Numeric Editor。

缩放参数相互耦合：`Max - Min = Scale/Div × 网格分格数`，Ref Pos 为 10 时
`Max = Ref Value`，Ref Pos 为 0 时 `Min = Ref Value`。这些关系由 display-model
后端维护，前端不得自行计算或用组件状态补齐。

`DiagramPane` 当前显示的笛卡尔 `10 dB/-90 dB` 与 Smith `1/0` 仅为占位视觉，
不得作为 Scale 真值、默认值或命令输入。真实标签必须来自 `StateSnapshot`。

第一条建议切片只覆盖活动笛卡尔 Trace 的 Scale/Div，并仍须等待后端状态与
HTTP 契约明确批准。其他控件按上述顺序显示为禁用，不得先在客户端模拟成功。

## 9. 视觉回归

每个前端提交至少验证：

- 1280×800 基准截图没有溢出、错位或文本裁切。
- 主屏严格为 1280×800 逻辑像素；M1.5 不显示独立辅助屏。
- 图表、Softtool、Hard Key 和状态栏的顺序与官方参考一致。
- 页面中不存在原厂品牌名、Logo 或直接复制的原厂素材。
- 鼠标、触控和键盘焦点均能完成当前切片的操作。
