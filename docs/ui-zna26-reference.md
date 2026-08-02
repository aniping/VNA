# ZNB 单屏界面复刻基线

> 状态：第一阶段 UI 基线
>
> 主参考版本：ZNB User Manual 74；ZNA User Manual 41 仅作补充
>
> 日期：2026-08-01

## 1. 目标与边界

前端复刻用户截图与 ZNB v74 的单屏分区、信息密度、控件层级、颜色语义和
触控工作流，不进行独立视觉设计。参考冲突时依次采用用户明确指令和截图、
ZNB v74、ZNA v41；不得沿用与 ZNB 单屏操作方式冲突的 ZNA 双屏结论。

以下内容不得复制：

- Rohde & Schwarz、R&S、ZNB、ZNA 等品牌标识和商标。
- 原厂 Logo、截图、图标文件、字体文件和其他专有素材。
- 与本项目领域模型不一致的产品型号、选件和能力声明。

项目使用中性名称、自制图标和系统字体，但保持相同的几何布局、控件位置、
交互层级与仪表式视觉语言。

## 2. 参考资料

- 用户提供的当前页面截图：控制区职责、按钮语义和单屏密度的最高优先级证据。
- 本地 `ZNB_UserManual_en_74.pdf`：第 30–34 页为单触摸屏与前面板按键布局，
  第 41–49 页为 Toolbar、Diagram、Softtool、虚拟 Hardkey、菜单和状态栏，
  第 285–290 页为 Meas Softtool 的职责、层级与 S 参数控件顺序。
- 本地 `ZNA_UserManual_en_41.pdf`：仅在 ZNB v74 未覆盖的 Scale 等细节中作
  补充；不能据其 Control Window 或双屏布局覆盖 ZNB 主基线。

## 3. 基准画布

ZNB 将测量应用与控制入口组织在同一触摸屏；项目据此采用 1280×800 单屏逻辑
画布，并在窗口不足时整体等比缩放：

| 区域 | 尺寸 | 职责 |
| --- | --- | --- |
| 单屏工作台 | 1280×800 | Toolbar、测量图、Softtool、Hardkey、菜单与状态栏 |

当前开发验收截图以 1280×800、100% 缩放为主，并补充 1920×1080 等比缩放检查。

## 4. 主应用屏结构

从上到下、从左到右保持以下顺序。尺寸来自 1280×800 参考界面的像素量测，
实现时允许为边框和文字裁切做少量微调：

1. 顶部工具栏，约 42 px 高：撤销、缩放、最大化图表、增加 Trace/Marker、
   删除、截图等。
2. 图表工作区：手册 Preset 默认采用单个主图；每个真实 Window 对应一个
   Diagram，包含 Trace 信息条、黑底绘图区和 Channel 状态行。多个 Window
   仍按工作区布局显示，但不得固定补齐不存在的空 Diagram。
3. Softtool，约 270 px 宽：默认关闭；用户按下相应 Hard Key 后在图表右侧打开。
   Measurement Softtool 修改活动 Trace 的测量量；Format 始终属于独立 Softtool。
4. 虚拟 Hardkey 面板，约 160 px 宽：固定在最右侧，按 ZNB 前面板证据为各组
   保留不同列数，不能用一个固定两列算法重排全部按键。
5. 底部菜单与状态栏，约 34 px 高：File、Trace、Channel、Display、Tools、System、Help，
   以及活动 Channel、平均状态、进度和时间。

手册默认状态只显示一个包含 S21 dB Magnitude Trace 的主图。Diagram 数量必须
来自 `StateSnapshot` 中的真实 Window；只有服务中真实存在的 Trace 才显示 Trace
名称，采样数据接通前不绘制曲线。多个 Window 与单图最大化能力继续保留。

## 5. 虚拟 Hard Key 结构

项目将以下 ZNB 控制组放在单屏工作台最右侧；它不是独立辅助屏：

- Trace（三列）：Meas、Format、Scale；Trace Config、Line、Marker。
- Stimulus（两列）：Start、Stop；Center、Span。
- Channel（三列）：Power/BW/Avg、Sweep、Cal；Channel Config、Trigger、Offset/Embed。
- System（三列）：File/Print、Setup、Tools；Display、Help、Preset。Help 以问号图形呈现。

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
| 活动 Diagram | 右上角真实 WindowId（或名称）以青蓝色高亮；外框保持统一细深灰蓝 |
| 其他 Trace | 当前单 Trace 映射沿用既有黄色；不据此推导后端颜色接口 |
| 坐标网格 | 低对比灰蓝色 |
| 主要文本 | 白色或浅灰色，紧凑无衬线字体 |

手册第 935 页以右上角 Diagram 编号/名称高亮标识活动 Diagram；第 112 页以
蓝/青蓝色标识活动 Trace 信息条与活动 Channel；第 127–129、936 页截图显示
Diagram 外框始终为细深灰蓝，S21 测量量色块仍保持绿色。

控件以触控为第一输入方式。按钮视觉尺寸、间距、激活态、禁用态和按下反馈需与
基准截图逐项比对，同时保留键盘焦点与可读的无障碍名称。

## 7. 首个可验收切片

M1.5 必须先交付以下可操作内容：

- 1280×800 单应用窗口及等比缩放。
- 手册默认单个黑底主图、独立 Trace 信息条、Channel 状态行和底部菜单；多 Window
  状态只渲染快照中真实存在的 Diagram。
- 默认关闭 Softtool；Meas Hard Key 可在图表右侧打开 Measurement Softtool，
  最右侧分组 Hardkey 常驻。
- Meas 功能键与对应 Softtool 的联动。
- Meas 按 ZNB v74 第 288–290 页显示测量族、逻辑端口对、S11/S12/S21/S22、
  All S-Params、S-Param Wizard、Balanced Ports 和类别列。它不含 Format 或
  Create Trace；仅四个单独 S 参数按钮接入活动 Trace 的真实命令，当前选中项、
  请求期间及其余批量、向导、拓扑入口均保持原生禁用。
- Start、Stop、Points、IFBW、Power 数值输入入口。
- 服务连接状态和 `stateRevision`，融入状态栏而非另做 Dashboard 卡片。
- 所有业务数据来自 `vna-server`，无静态业务 Mock。

### Smith 圆图基线

ZNB v74 第 107–108、443 页锁定 Smith 外圆为 `Ref 1 U`，信息条为
`200 mU/ Ref 1 U`。圆图完整等比例居中，显示中心实轴、五等分径向圆、
`0/.2/.5/1/2/5/10/∞` 等阻标签，以及上感性、下容性的正负等抗弧。
前端不得显示笛卡尔式 `1/0` 边界标签，也不得把后端 `(real, imag)` 样本换算为
阻抗、导纳或裁切到单位圆内。

## 8. Scale Softtool 基线

本节依据 User Manual 41 的 PDF 页码与印刷页码 547-550；单窗口数值输入规则
见 PDF/印刷页 58-59。547 页给出入口和作用范围，548 页给出布局、状态与参数
关系，549-550 页给出各控件行为。不得复制手册截图或品牌素材。

Scale Values 采用左侧主控列、右侧标签列的两列布局，宽度约为 64%:36%。
主控列中的每个控件占该列全宽并纵向排列；右列顶部为 `Scale Values` 标签。
这只是 Softtool 内部布局，最右侧 Hardkey 仍遵守第 5 节的分组列数基线。

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

`DiagramPane` 的笛卡尔标签不得作为 Scale 命令输入；LogMagnitude 真值必须来自
`StateSnapshot`。Smith 静态圆图几何遵循上述显示合同，不作为客户端 Scale
业务状态，也不对后端样本做业务换算。

第一条建议切片只覆盖活动笛卡尔 Trace 的 Scale/Div，并仍须等待后端状态与
HTTP 契约明确批准。其他控件按上述顺序显示为禁用，不得先在客户端模拟成功。

## 9. 视觉回归

每个前端提交至少验证：

- 1280×800 基准截图没有溢出、错位或文本裁切。
- 主屏严格为 1280×800 逻辑像素；M1.5 不显示独立辅助屏。
- 图表、Softtool、Hard Key 和状态栏的顺序与官方参考一致。
- 页面中不存在原厂品牌名、Logo 或直接复制的原厂素材。
- 鼠标、触控和键盘焦点均能完成当前切片的操作。
