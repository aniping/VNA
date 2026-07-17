# 商用 VNA 校准与处理链一手证据

> 研究日期：2026-07-17
> 适用对齐行：`IAC-039`～`IAC-046`、`MATH-01`～`MATH-10`、`NET-01`～`NET-10`、`TD-01`～`TD-07`。
> 资料范围：Keysight PNA/ENA、Rohde & Schwarz ZNA/ZNB、Copper Mountain Technologies（CMT）官方用户手册、编程手册、命令参考和厂商应用说明。

> **R&S 版本冻结。** 本基线固定引用 ZNA User Manual v39 与 ZNB/ZNBT User Manual v72；截至研究日，官方入口已发布 [ZNA v40（2026-03-27）](https://www.rohde-schwarz.com/uk/manual/rs-zna-user-manual_78701-601863.html) 和 [ZNB/ZNBT v73（2026-04-14）](https://www.rohde-schwarz.com/ca/manual/r-s-znb-znbt-user-manual-manuals_78701-29151.html)。升级参考版本必须重新核对章节、命令和数值回归；本文页码均指 PDF 页脚页码。

## 1. 研究问题与证据边界

本文回答两个问题：

1. 商用 VNA 对校准对象、校准方法、采集/求解/应用生命周期，以及参考面、夹具、混合模、时域和迹线处理公开了哪些可观察行为；
2. 这些行为能支持本项目哪些产品结论，哪些仍只是架构推导，哪些必须由真实单板或计量黄金数据验证。

本文不把厂商菜单名称当成内部架构，也不根据公开命令树猜测其闭源求解器、矩阵算法、线程、缓存或持久化实现。厂商公开“支持 SOLT、de-embedding 或 time gate”只证明外部能力存在，不证明我们的公式、符号、归一化、插值和数值稳定性已经正确。

### 1.1 证据等级必须分开使用

| 等级 | 本文使用规则 | 不能据此声称 |
|---|---|---|
| **E1 跨厂商官方共性** | 至少两家目标厂商的一手资料明确给出一致的外部对象或行为。 | 三家内部采用相同算法、数据结构或处理顺序。 |
| **E2 单厂商事实或厂商差异** | 某厂商明确支持，或几家在术语、边界、处理阶段、外推政策、选件上不同。 | 该行为是行业统一默认。 |
| **E3 项目架构推导** | 为满足 E1/E2 外部行为而设计的模块、不可变 revision、状态机、typed node、质量传播和接口。 | 这是商用品内部实现。 |
| **E4 硬件/平台事实** | 只能由公司底软、单板拓扑、目标 SDK、真实校准件和目标机实测确定。 | Mock 能做就代表真实板卡支持或达到计量精度。 |

一个条目可能同时需要 E1、E2、E3、E4，但本文始终分列陈述。例如，“全双端口 SOLT 产生双向完整误差修正”是 E1；“内部用不可变 `CorrectionSetRevision` 保存结果”是 E3；“本公司某单板能否完成双向所有接收路径”是 E4。

### 1.2 计量边界

官方手册足以确定产品对象和操作闭环，却不足以验收以下内容：

- SOL、SOLT/TOSM、one-path 等求解公式、误差项命名映射和数值精度；
- 标准件电路模型、data-based S 参数插值、频带拼接和连接重复性；
- 校准误差项插值/外推、端口延伸的符号与损耗模型；
- 复数参考阻抗下的 wave definition、夹具级联/反演、mixed-mode 变换；
- 时域变换归一化、DC 构造、窗函数、gate 正反变换；
- smoothing、hold、statistics 在边缘点、无效点和格式切换时的精确结果。

这些必须进入独立黄金数据和计量验收门禁，不能只做“界面能点通”的测试。

## 2. 三家官方资料给出的总体处理模型

### 2.1 Keysight PNA/ENA

- [Calibration Standards](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Calibration_Standards.htm) 明确区分 Cal Kit、物理 Standard、Standard Definition 和按误差模型组织的 Calibration Class；标准定义可含频率范围、`Z0`、delay、loss、open 电容/short 电感多项式，也可使用 data-based 标准。
- [Calibration programming topic](https://helpfiles.keysight.com/csg/N52xxB/Programming/CalTopic.htm) 暴露 Guided calibration 的生成步骤、步骤描述、逐步采集、计算/保存误差项，以及 Cal Set 的创建、复制、保存、激活和查询。
- [Read and Write Calibration Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Learning_about_COM/Read_and_Write_Calibration_Data_using_COM.htm) 明确把 Standard Measurement Data 与由其计算出的 Error Terms 分开，并说明校准数据存于 Cal Set。
- [Error Correction and Interpolation](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Error_Correction_and_Interpolation.htm) 明确 correction on/off、Response/Enhanced Response/Full N-Port 等状态、全双端口的 12 项双向采集，以及普通 S 参数插值和越出校准频段时的失效规则。
- [Accessing Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Accessing_Data_Descriptions.htm) 公开了 correction、fixture、trace math/memory、gating、phase correction、time domain、formatting、smoothing 等数据阶段，但该顺序是 Keysight 外部访问模型，不是跨厂商唯一处理顺序。

### 2.2 Rohde & Schwarz ZNA/ZNB

- [ZNA User Manual v39](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) §4.5.1 p.197-198 公开 Normalization、Reflection OSM、One Path Two Ports 与 TOSM：OSM 求三个反射误差项且只适用于反射；One Path 是驱动端完整一端口加传输归一化，只适用于单方向且负载端匹配良好的场景；TOSM 正反两向各求六个误差项。§4.1.7 p.125-128 给出 Channel/Trace data flow。
- [ZNB/ZNBT User Manual v72](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf) §3.4.1.3 p.81-82 公开选择端口/方法、连接并采集标准件、应用校准的向导闭环；§4.5.3 p.181 定义可独立存储并应用到不同 Channel/recall set 的 Calibration Pool/Cal Group；§4.5.4 p.182 区分 `Cal`、`Cal int`、`Cal Off`；§4.1.5 p.99-102 给出系统误差修正、de-/embedding、offset、mixed-mode、renormalization、average、trace math、time domain、gate、format、smooth、hold 等阶段。
- [Accurate Test Fixture Characterization and De-embedding](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1sl367/1SL367_0e_Test_Fixture_Characterization_and_De-embedding.pdf) 明确夹具 lead-in/lead-out 需要表征后再从 ZNA/ZNB 测量结果中数学去除。
- [Time Domain Measurements Using ZNA](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ep83/1EP83_0e_TimeDomain_ZNA7.pdf) p.6-8、17、20、22 公开 band-pass impulse、low-pass impulse/step、harmonic grid、DC、窗函数、aliasing、time gate、gate 后返回频域及自动 grid/DC 的可查询效果。

### 2.3 Copper Mountain Technologies

- [Calibration Standards and Calibration Kits](https://coppermountaintech.com/help-cmtvna/1-port/calibration-standards-and-kits.html)、[Classes of Calibration Standards](https://coppermountaintech.com/help-cmtvna/1-port/classes-of-calibration-standar.html) 和 [Classes Management](https://coppermountaintech.com/help-cmtvna/1-port/classes-management.html) 明确 Standard Definition、Cal Kit、SOLT/TRL class 和 class assignment。
- [Quick Calibration](https://coppermountaintech.com/help-cmtvna/1-port/quick-calibration.html) 公开选择端口/connector/cal kit/method、逐标准测量、完成标记和 Apply 的向导闭环。
- [Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html) 明确临时 Calibration Standard Measurement arrays、Error Term arrays、Corrected S-parameter arrays、Corrected/Formatted trace arrays，以及 math、electrical delay、gating、time transform、smoothing、hold 的阶段区别。
- [Error Correction Status](https://coppermountaintech.com/help-cmtvna/1-port/error-correction-status.html) 区分 exact、interpolated、extrapolated、off/no calibration，并逐 Trace 标出 response、one-path、full one-port、full two-port 等校准类型。
- [Time Domain Transformation](https://coppermountaintech.com/help-cmtvna/1-port/time-domain-transformation.html) 和 [Time Domain Gating](https://coppermountaintech.com/help-cmtvna/1-port/time-domain-gating.html) 公开 Chirp-Z、band-pass/low-pass、harmonic grid、DC、Kaiser window、距离轴和 gate 后逆变换。

## 3. 校准闭环逐项证据

下表中的 E1/E2 只描述厂商可观察行为；E3/E4 是本项目处置，不与厂商事实混写。

| ID / 对齐行 | E1 跨厂商官方共性 | E2 厂商事实或差异 | E3 项目架构处置 | E4 硬件/平台待证 |
|---|---|---|---|---|
| **CAL-01** Connector、Cal Kit、Standard、物理实例（`IAC-039`） | **[E1]** Keysight [Calibration Standards](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Calibration_Standards.htm)、R&S [ZNB manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf) 和 CMT [Standards and Kits](https://coppermountaintech.com/help-cmtvna/1-port/calibration-standards-and-kits.html) 都把 connector/type/gender、kit、standard 及其已知频率响应作为校准输入；模型至少可由电路参数或 S 参数数据描述。 | **[E2]** 文件格式、内置 kit、standard type 名称、序列号/characterization 管理和导入兼容不同；R&S 可导入部分 Keysight kit 格式，不能反推两者内部 schema 相同。 | **[E3]** 分为 `ConnectorDefinitionRevision`、`CalibrationKitRevision`、`StandardDefinitionRevision` 和带 serial/资产信息的 `PhysicalStandardInstance`；定义与实物不可合并为一个可变对象。 | **[E4]** 公司现有校准件的型号、序列号、connector gender、characterization 文件、温度和校准有效期必须盘点。 |
| **CAL-02** Standard type 与 calibration class/role（`IAC-039`） | **[E1]** Keysight 和 CMT 都明确：type 描述标准本身，class/role 描述它在某种误差模型中的用途；SOLT 与 TRL 使用不同 class 组合。 | **[E2]** Keysight class 标签、每 class 可选标准数量和 Guided/Unguided 规则，与 CMT 的 SOLT/TRL family、单个 standard 的 class assignment 规则不同。 | **[E3]** `StandardKind` 与 `CalibrationRole` 必须是两种类型；`CalibrationMethodDefinition` 决定所需 role，kit revision 提供候选标准，不能用 `if kind == OPEN` 代替 class resolution。 | **[E4]** 底软不决定 class；但真实接线能力、端口性别和可连接组合会限制可生成步骤。 |
| **CAL-03** Guided calibration/session 生命周期（`IAC-041`） | **[E1]** Keysight Guided commands、R&S ZNA Calibration Wizard 和 CMT Quick Calibration 都表现为：选择 Channel/ports/connectors/kits/method → 生成或显示步骤 → 逐步采集 → 完成计算并 apply/save；可见“开始”不等于“校准已完成”。 | **[E2]** Keysight 可查询每一步数量、描述、端口和标准；R&S/CMT 的 UI/远程粒度不同；厂商对跳步、重测、任意次序和自动校准模块的规则也不同。 | **[E3]** `CalibrationSession` 采用显式 `Draft → Ready → Acquiring → Solving → Completed/Failed/Aborted`，保存冻结的 method/kit/port/stimulus revisions；Web 与 SCPI 只映射同一组 Command/Query。 | **[E4]** 自动校准模块、内部开关状态和真实步骤耗时取决于公司板卡/附件；首版 Mock 不能宣称支持 ECal/ACM。 |
| **CAL-04** Response/Normalization（`IAC-040`） | **[E1]** Keysight [Error Correction](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Error_Correction_and_Interpolation.htm)、R&S ZNA manual 和 CMT [Calibration Methods](https://coppermountaintech.com/help-cmtvna/1-port/calibration-methods.html) 均把 reflection/transmission normalization 作为低阶 response correction，只修正相应 tracking/response，不等价于完整 mismatch correction。 | **[E2]** 可选 load/isolation、open/short/thru 的选择、状态名和适用参数不同；Keysight 另有 Enhanced Response。 | **[E3]** Response 必须是独立 `CalibrationMethodId` 和误差模型策略，不允许把它伪装为“缺少若干项的 Full SOLT”而丢失适用范围/状态语义。 | **[E4]** 哪些 receiver ratio 能做 reflection/transmission response 由真实 a/b 路径和激励方向决定。 |
| **CAL-05** Full 1-port SOL/OSM（`IAC-040`） | **[E1]** Keysight、R&S 和 CMT 均要求三个反射 role（典型 Short/Open/Load 或 Match），并求得 directivity、source match、reflection tracking 三类反射误差项。 | **[E2]** R&S 称 OSM，Keysight/CMT 常称 SOL；允许以非典型标准填充 class 的规则不同。 | **[E3]** `FullOnePortSolver` 的输入必须是带实际频率轴、标准定义 revision、端口和采集 provenance 的三个 role acquisition，而不是三个无类型复数数组。 | **[E4]** 端口 directivity、动态范围、switch term/接收路径质量及校准后的 residual error 需真实板卡与标准件验证。 |
| **CAL-06** One-path 2-port（`IAC-040`） | **[E1]** R&S ZNA manual 与 CMT [metrology description](https://coppermountaintech.com/vna-metrology-and-measurement/) 均说明 one-path 是一个端口的完整一端口校准加一条传输 tracking，适合只需正向或反向参数且 load port 匹配良好的场景；精度低于 full 2-port。 | **[E2]** 节点端口命名、是否增加 isolation、支持 N-port reduced calibration 的方式不同。 | **[E3]** 结果必须记录 calibrated direction/node port 和适用的 Sij 集合；查询不在适用集合内的参数时返回 `not_applicable`，不能套用系数后标成 full correction。 | **[E4]** 单板能否切换正/反向激励、load port 的原始匹配及方向间一致性必须实测。 |
| **CAL-07** Full 2-port SOLT/TOSM 与 error terms（`IAC-040`） | **[E1]** Keysight 明确 full 2-port 需正反两向扫频取得 12 error terms；R&S TOSM 给出每方向 6 项；CMT 也把 SOLT 描述为 full S-matrix 的 12-term correction。 | **[E2]** isolation/crosstalk 是否测量、Unknown Thru/UOSM、TRL 与多端口简化路径属于方法/Profile 差异，不能混进基础 SOLT 默认。 | **[E3]** `FullTwoPortSolver` 输出带明确 port pair、direction、term inventory 和 method revision 的 correction set；缺任一必需采集时不得生成“部分成功”的 full 2-port set。 | **[E4]** 本公司板卡必须能为同一频率点提供正反方向所需完整 a/b 接收路径；端口切换重复性、隔离和漂移决定实际可达精度。 |
| **CAL-08** 标准件采集、步骤质量与重测（`IAC-042`） | **[E1]** 三家向导都把每个标准件/连接步骤的测量完成状态显式呈现，并在必需步骤完成后才允许 solve/apply。 | **[E2]** 厂商公开的 connection check、confidence check、任意顺序、reduce reflection/transmission 和自动模块能力不同，不能承诺统一质量分数。 | **[E3]** 每次步骤产生不可变 `CalAcquisitionSnapshot`，含实际轴、原始/比值数据引用、quality flags、温度/时间、重复次数和步骤 revision；重测新建 revision，不原地覆盖审计记录。`CalStepQualityReport` 只报告可解释指标，不伪造“计量合格”。 | **[E4]** 底软可提供哪些逐点 a/b quality、过载/失锁/未稳幅/路由信息，必须由 Adapter contract 和真实故障注入确认。 |
| **CAL-09** Acquisition、solve、Error Terms、Correction Set 分离（`IAC-043`） | **[E1]** Keysight [Read/Write Calibration Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Learning_about_COM/Read_and_Write_Calibration_Data_using_COM.htm) 和 CMT [Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html) 均明确分开标准件采集数据与计算后的 error terms；Keysight Cal Set、R&S Cal Group/Pool 又证明 correction data set 是可保存和复用的外部对象。 | **[E2]** CMT 文档说明标准采集临时数组在求解后清除；Keysight Cal Set 可保存标准采集数据和误差项；R&S Cal Pool 文件生命周期不同。 | **[E3]** 项目保留 `CalibrationSessionRevision`、可选审计级 acquisition retention、不可变 `CorrectionSetRevision` 三层；solver 的数据结果是确定性输入/输出边界，但调用必须显式接收 `ExecutionContext{stop_token, monotonic_deadline, BudgetHandle, ProgressSink}`。协作 solver 有界检查 stop/deadline，不可中断第三方 solver 进入隔离 lane 并由 child Drain Operation 持有容量；repository 不暴露可变 coefficient buffer。 | **[E4]** acquisition 长期保留的存储预算、目标机文件系统耐久性、不可中断 solver 的平台行为和校准数据加密/备份策略需平台验证。 |
| **CAL-10** Correction binding、apply 与 on/off（`IAC-044`） | **[E1]** Keysight Cal Set 可激活到 Channel 且 correction 可 on/off；R&S Cal Group 可应用到不同 Channel/recall set；CMT 对 Channel/Trace 显示 correction 状态。Correction data 与当前 Channel 的绑定是独立于求解的动作。 | **[E2]** Keysight 尝试对活动 Channel 全部 measurements 开启 correction；CMT 同时有 Channel 总状态与逐 Trace 类型状态；R&S 的 Cal Pool/recall 行为不同。 | **[E3]** Channel 仅保存版本化 `CorrectionBinding{set_id, revision, enabled, policy}`；apply 是 revision-checked Command，失败保持旧 binding；Trace 状态由 binding + applicability 计算，不各自复制系数。 | **[E4]** 同一 correction set 能否跨板卡、跨端口实例或热插拔后复用，必须由硬件身份和路径校验决定。 |
| **CAL-11** Exact、interpolation、extrapolation、invalid（`IAC-045`） | **[E1]** Keysight 与 CMT 都把校准设置与当前 stimulus 的匹配状态显式暴露，并区分 exact 与 interpolated correction。 | **[E2]** Keysight 对普通 S 参数在频段向外扩展时关闭 correction，仅 power cal 有例外；CMT 明确显示 `C!` extrapolated。是否允许外推是厂商/Profile 差异，不是 E1。 | **[E3]** `CalibrationApplicabilityService` 返回 `Exact / Interpolated / Extrapolated / ChangedSettings / Incompatible` 及逐项原因；默认 Core 对普通 S 参数禁止静默外推，兼容方言可显式开启并标记。 | **[E4]** 真实板卡 band crossing、频率量化、功率档位、衰减器/路由变化对校准有效性的影响需真实映射。 |
| **CAL-12** 设置变化与准确度声明（`IAC-045`） | **[E1]** Keysight 指出 start/stop/points、sweep type、IFBW、sweep time、stepped mode、attenuator 等变化具有不同失效或不确定语义；CMT [Basic Guidelines](https://coppermountaintech.com/help-cmtvna/1-port/basic-calibration-guidelines.html) 要求 calibration 与 measurement 使用相同频率、点数、功率，并对 IFBW、连接变化和温漂给出重校准建议。 | **[E2]** 哪些变化仅标 `changed`、哪些关闭 correction、哪些可插值，各家和型号不同。 | **[E3]** applicability 以结构化 `StimulusSignature`、route/port identity、power/attenuator state、IFBW 和 environment metadata 比较；不得只比较 `start/stop/points` 三个字段。 | **[E4]** 底软实际档位、隐式自动量程、温度传感器和路径切换信息能否读出，决定 MatchReport 能否完整。 |
| **CAL-13** 校准准确度与计量验证（`IAC-042`～`IAC-046`） | **[E1]** 三家资料一致强调正确 kit/model、相同参考面、连接质量和设置匹配直接影响准确度；response、one-path、full SOLT 的准确度等级不同。 | **[E2]** 厂商提供的 uncertainty、confidence check、verification kit 和 residual specification 不同，不能照搬某一家阈值。 | **[E3]** 软件状态只声明 method、applicability、数据质量和验证证据，不自行显示“高精度/合格”；发布 Core 前需用合成误差盒、已知标准数据和商用 VNA 对照形成黄金集。 | **[E4]** 最终 directivity、tracking、source/load match residual、动态范围和温漂必须由真实单板 + 可溯源标准件测得。 |
| **CAL-14** Calibration Verification / Confidence Check（`IAC-046`） | **[E1]** Keysight [System Verification](https://helpfiles.keysight.com/csg/m9485a/support/system_verification.htm) 用近期校准测量 verification device 的四个 S 参数，将测量值与工厂表征数据及基于测量不确定度的限线比较；CMT [Confidence Check](https://coppermountaintech.com/help-cmtvna/1-port/confidence-check2.html) 测量校准时未使用的内部衰减器状态，并与模块内存的表征 S 参数比较。两家都把“校准之后测量一个已知独立状态并与参考比较”作为可观察工作流。 | **[E2]** Keysight 给出 system-level PASS/FAIL、uncertainty limit 和正式 verification kit，并明确它不认证任一单独部件、也不能替代 verification kit 认证；CMT 的 confidence check 更偏测试当前 calibration/cable/setup，显示 data/memory 并允许数学比较。阈值、覆盖端口、报告与“验证/置信检查/仪器认证”名称不能混用。 | **[E3]** 建立独立 `VerificationPlanRevision`、`CalibrationVerificationOperation` 与不可变 `CalibrationVerificationResultSnapshot`；Plan 固定 CorrectionSet、独立 artifact characterization、端口/轴、所需 S 参数、tolerance/uncertainty 与算法 revision。Operation 消费正式 B 层测量，输出逐点 residual/margin 及 Pass/Fail/Indeterminate；不得改变 CorrectionSet/Binding，也不得把求解时同源数据冒充独立验证。 | **[E4]** 公司采用何种 verification artifact、characterization 可溯源性/有效期、连接重复性、温度范围和 acceptance limit，必须由计量责任方与真实板卡验收；软件 Pass 不得被宣传为单板年度认证。 |

## 4. 参考面、夹具、混合模与时域逐项证据

| ID / 对齐行 | E1 跨厂商官方共性 | E2 厂商事实或差异 | E3 项目架构处置 | E4 硬件/平台待证 |
|---|---|---|---|---|
| **REF-01** Trace Electrical Delay（`NET-01`） | **[E1]** Keysight [Comparing the PNA Delay Functions](https://helpfiles.keysight.com/csg/N52xxA/Tutorials/Comparing_the_PNA_Delay_Functions.htm) 与 CMT [Electrical Delay](https://coppermountaintech.com/help-cmtvna/1-port/electrical-delay-setting.html) 都把 electrical delay 定义为逐 Measurement/Trace 去除线性相位斜率；它不等于端口级参考面移动。 | **[E2]** media、velocity factor、waveguide cutoff、自动 marker-to-delay 和范围限制不同。 | **[E3]** `TraceElectricalDelayNode` 只处理选定 complex trace，不修改完整 S-matrix、calibration plane 或其他 Trace；保存 phase convention 与单位。 | **[E4]** 无专属硬件要求；但实际频率轴量化和相位质量影响估计结果。 |
| **REF-02** Port Extension（`NET-02`） | **[E1]** Keysight [Port Extensions](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Port_Extensions.htm) 和 CMT [Port Extension](https://coppermountaintech.com/help-cmtvna/TR-Series/port-extension.html) 均说明它在校准后按端口把参考面移向 DUT，并以匹配传输线的 delay/loss 模型补偿；反射经历往返、传输累加两端口传播。 | **[E2]** loss 模型、automatic extension、在 data flow 中的具体位置及与 memory/fixture/balanced measurement 的耦合不同。 | **[E3]** `PortExtensionNode` 输入完整 corrected network，按涉及端口对所有 Sij 一致作用；每端口保存 delay/loss/media 和 input/output plane。 | **[E4]** 端口 extension 是否可由底软自动测量不是 Core 前提；若做 auto extension，需真实 OPEN/SHORT、动态范围和曲线拟合验收。 |
| **REF-03** Reference Impedance / Renormalization（`NET-03`） | **[E1]** Keysight ENA [Fixture Simulator overview](https://helpfiles.keysight.com/csg/e5072a/measurement/fixture_simulator/overview_of_fixture_simulator.htm) 与 R&S ZNB manual 都把 50/75 Ω 测量结果转换到任意端口参考阻抗；两家都表明这属于网络数学转换，不是改变物理测试端口。 | **[E2]** real/complex Z0、逐端口/平衡端口支持和 power-wave/其他 wave theory 不同；CMT [Port Z commands](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 也暴露 real/imag 与 conversion theory，但型号能力需区分。 | **[E3]** `RenormalizationNode` 必须声明每端口 Z0、wave convention、输入/输出 reference impedance；禁止逐 Trace 比例缩放。Core 先支持正实数 per-port Z0，复数 Z0/可选 wave theory 列 Pro。 | **[E4]** 算法本身不依赖板卡；但 Touchstone 导入导出和目标 SDK 的复数线代数稳定性需验证。 |
| **REF-04** Reference Plane provenance（`NET-04`） | **[E1]** 三家都用 calibration plane、extended plane、fixture/DUT plane 解释结果的物理位置。 | **[E2]** 厂商 UI 未提供统一可交换的 plane identity/schema。 | **[E3]** 每个网络处理节点保存 `input_plane_id[]`、`output_plane_id[]` 和 plane transform revision；Web/SCPI 查询同一 provenance chain。它是本项目可靠性设计，不冒充厂商内部对象。 | **[E4]** 实际 adapter/cable/fixture 资产与 plane 名称由现场配置确认。 |
| **FIX-01** Fixture 文件、端口和方向（`NET-05`） | **[E1]** Keysight、R&S 和 CMT 都使用 S 参数/Touchstone 描述夹具网络，并要求明确 analyzer-side/DUT-side 或端口方向。CMT [De-embedding](https://coppermountaintech.com/help-cmtvna/1-port/de-embedding.html) 明确 S11 朝 analyzer、S22 朝 DUT。 | **[E2]** 支持 S2P/S4P/SnP、topology file、fixture characterization 和频率外推政策不同。 | **[E3]** 导入生成不可变 `FixtureRevision{content_hash, ports, orientation, z0, frequency_axis, provenance}`；原文件被覆盖不改变历史结果。 | **[E4]** 目标机文件系统、最大文件/点数、可用存储和上传安全限制需验证。 |
| **FIX-02** Embedding / De-embedding（`NET-06`） | **[E1]** Keysight [Using Fixture Simulator](https://helpfiles.keysight.com/csg/N52xxB/Programming/Using_Fixture_Simulator.htm)、R&S [1SL367](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1sl367/1SL367_0e_Test_Fixture_Characterization_and_De-embedding.pdf) 和 CMT [Embedding](https://coppermountaintech.com/help-cmtvna/1-port/embedding.html)/[De-embedding](https://coppermountaintech.com/help-cmtvna/1-port/de-embedding.html) 均把它定义为网络级数学加入/去除 fixture 效应。 | **[E2]** 可用拓扑、N-port 能力、AFR/EZ de-embedding、fixture characterization 和选件不同。 | **[E3]** 2-port cascade/inverse 与 N-port 算法分模块；按完整同代 S-matrix 处理，不逐 Sij 独立运算；每频点输出 condition/quality。 | **[E4]** 高级 AFR/自动夹具提取需要额外测量拓扑、硬件能力和计量数据，不能由普通 Touchstone de-embedding 的 Mock 冒充。 |
| **FIX-03** 处理顺序与数值病态（`NET-07`、`NET-08`） | **[E1]** 三家公开数据流都证明 port extension、Z conversion、fixture、mixed-mode、trace math 和 formatting 是可区分阶段。 | **[E2]** Keysight 新 fixture blocks 可组合排序，旧命令顺序受限；R&S/CMT 默认 data flow 也不同，因此不存在可直接宣称的行业唯一总顺序。厂商手册通常不公开 condition-number 阈值。 | **[E3]** 使用 typed processing graph 限制合法连接；Profile 提供默认图但不伪装为行业统一顺序。矩阵反演逐点计算 condition metric，超过阈值输出 `ill_conditioned`，不得输出巨大数仍标 valid。 | **[E4]** 目标 CPU 的吞吐/内存与 Eigen3 数值路径需用最大端口数、最大点数和近奇异 fixture 实测。 |
| **MIX-01** Mixed-mode S 参数（`NET-09`） | **[E1]** Keysight ENA [Balanced Device Evaluation](https://helpfiles.keysight.com/csg/e5072a/measurement/fixture_simulator/evaluating_balanced_devices_balance_unbalance_conversion.htm) 与 R&S ZNB manual 都由成对 single-ended physical ports 定义 balanced logical port，并区分 differential/common mode 及 mode conversion S 参数。CMT 官方 [Balanced Measurement](https://coppermountaintech.com/wp-content/uploads/2022/06/BalancedMeas.pdf) 也列出 SDD/SCC/SDC/SCD 参数。 | **[E2]** 逻辑端口拓扑、默认 differential/common Z0、可混合 single-ended/balanced 的组合和产品支持范围不同。 | **[E3]** `MixedModeTransformNode` 明确 physical port pair、正/负方向、logical port、single-ended/differential/common Z0 和 wave convention；port mapping 与变换配置分离。 | **[E4]** 必须取得同一代完整相关 S-matrix；若单板只产生部分/非相干路径，功能应 capability reject，不能以缺项补零。 |
| **TDR-01** Transform 类型（`TD-01`） | **[E1]** Keysight、R&S 和 CMT 均公开 band-pass impulse、low-pass impulse、low-pass step 三种基础时域响应。 | **[E2]** CMT 明确使用 Chirp-Z；其他厂商实现和归一化不应猜测。时域在 R&S 常为 K2 选件，在 CMT 某些型号为标准、某些型号不可用。 | **[E3]** `TimeDomainTransformNode` 以 transform mode、response kind、normalization profile 为显式参数；实现可替换，但外部结果需由同一黄金集约束。 | **[E4]** 基础变换可纯软件实现；是否达到产品实时刷新率需在 AArch64 目标机实测。 |
| **TDR-02** 网格、DC 与输入条件（`TD-02`） | **[E1]** R&S [1EP83](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ep83/1EP83_0e_TimeDomain_ZNA7.pdf) 和 CMT [Time Domain Transformation](https://coppermountaintech.com/help-cmtvna/1-port/time-domain-transformation.html) 均要求 low-pass 使用 harmonic grid 并需要 DC 值（自动外推或手工给定）；线性离散频率步进决定时域无歧义范围。 | **[E2]** band-pass 对起始频率限制较少；自动 harmonic grid、DC extrapolation、容差和 resampling 行为不同。 | **[E3]** 默认只接受声明条件满足的轴；可选 resampler/DC estimator 是独立 node，输出 `imputed_input` provenance。Log/Segmented/缺点不得静默送入变换。 | **[E4]** 实际扫频轴误差、缺点和频率锁定质量来自 Board Adapter。 |
| **TDR-03** Window 与 normalization（`TD-03`） | **[E1]** 三家均公开窗函数用于在主瓣宽度/时域分辨率与旁瓣/振铃间取舍，并提供若干预设及可调参数。 | **[E2]** R&S 使用 Rectangle/Hamming/Hann/Bohman/Dolph-Chebyshev 类选项；CMT 使用 Kaiser β 及 Minimum/Normal/Maximum；名字相同不代表系数相同。 | **[E3]** 内部保存精确 window family、参数、coherent gain/normalization 和实现 revision；UI 的 `Minimum/Normal/Maximum` 只是 Profile preset，不能成为算法 ID。 | **[E4]** 无专属硬件事实；浮点一致性和性能需目标 SDK 验证。 |
| **TDR-04** 时间/距离轴、范围与 zero padding（`TD-04`） | **[E1]** R&S/CMT 均说明有限频宽决定分辨率、频率步进决定 alias-free/unambiguous range；CMT 可按 velocity factor 显示 time/distance 并区分 one-way/round-trip。 | **[E2]** 本次目标资料没有形成三家一致的用户可见 zero-padding 语义；不得把内部 FFT padding 自动写成商用共同功能。 | **[E3]** `TimeAxisMetadata` 保存 `Δf`、span、point count、transform length、`Δt`、unambiguous range、velocity factor 和 one-way/round-trip。若提供 zero padding，明确它改变采样密度而非物理分辨率。 | **[E4]** 目标点数上限和内存预算决定最大 transform length。 |
| **TDR-05** Gate 定义（`TD-05`） | **[E1]** 三家均提供 start/stop 或 center/span、band-pass（保留 gate 内）与 notch（去除 gate 内），并提供平滑 gate shape。 | **[E2]** gate shape 名称和具体 filter 不同；Keysight 还提供 trace gate coupling，不能视为基础统一行为。 | **[E3]** `GateDefinitionRevision` 保存 domain、type、shape family/params 和边界；gate 是 processing definition，不是 Diagram 上的遮罩。 | **[E4]** 无专属硬件要求；实时交互延迟需目标机验证。 |
| **TDR-06** Gated frequency result（`TD-06`） | **[E1]** Keysight [Using Gating](https://helpfiles.keysight.com/csg/N52xxB/Applications/Enhanced_Time_Domain_Analysis/Making_Measurements/Using_Gating.htm)、R&S 1EP83 和 CMT gating 都明确：频域数据变到时域、施加 gate、再变回频域，从而得到新的频率响应；不只是显示覆盖层。 | **[E2]** Keysight 明确 gate 不补偿被前级反射遮蔽的真实后级响应；各家 inverse normalization 和 gate coupling 不同。 | **[E3]** 该 Pro 模块的图固定为 `FrequencyTraceSnapshot → Transform → Gate → InverseTransform → GatedFrequencyTraceSnapshot`，输出可继续 format、Marker、Limit 和单 Trace 数据导出；必须带原输入与 gate revision。完整 S-matrix 同代门控是另一个显式 N-port Pro 节点。 | **[E4]** 无专属硬件事实；精度依赖原始频率覆盖、噪声和缺点。 |
| **TDR-07** 非局部质量传播（`TD-07`、`NET-10`） | **[E1]** 官方资料说明时域结果由整个离散频率响应变换而来，有限 span/缺点/噪声和窗口会影响全局响应。 | **[E2]** 厂商对坏点、缺测、插补和 warning 的精确政策未形成共同公开规则；Enhanced TDR、eye、AFR 为不同选件。 | **[E3]** 一个 invalid 频点默认使整次 transform `Rejected`；只有显式 imputation profile 才可继续，并把全体输出标为 `imputed`。高级 AFR/TDR/eye 独立于基础 transform/gate。 | **[E4]** 哪些高级功能需要额外硬件、授权、脉冲/相干能力由实际产品和板卡确认。 |

## 5. Memory、Math、Smoothing、Hold 与 Statistics 逐项证据

| ID / 对齐行 | E1 跨厂商官方共性 | E2 厂商事实或差异 | E3 项目架构处置 | E4 硬件/平台待证 |
|---|---|---|---|---|
| **MTH-01** Data → Memory（`MATH-01`） | **[E1]** Keysight [Math Operations](https://helpfiles.keysight.com/csg/N52xxB/S4_Collect/Math_Operations.htm)、R&S ZNB data flow 和 CMT [Mathematical Operations](https://coppermountaintech.com/help-cmtvna/1-port/mathematical-operations.html) 都提供当前 data 到 memory 的快照，用于显示或后续 math；新 Sweep 不更新已保存 memory。 | **[E2]** Keysight 通常每 measurement 一个 memory，另有 locked new trace；CMT 可选最多 8 个 FIFO memory；R&S 可有多个关联 memory trace。 | **[E3]** Core 使用不可变 `MemoryTraceSnapshot{complex_data, axis, source_snapshot, provenance}`；容量和淘汰由 catalog policy 管理，不让协议层持有裸数组。 | **[E4]** 最大 memory trace 数、点数和持久化容量需目标机预算。 |
| **MTH-02** Data/Memory 四则运算（`MATH-02`） | **[E1]** Keysight 和 CMT 明确支持 `Data ± Memory`、`Data × Memory`、`Data ÷ Memory`，且在 display formatting 前对 complex/real-imag 数据运算；R&S ZNB data flow 也把 Trace Math 放在 format 前。 | **[E2]** normalize 快捷键副作用、memory 选择和运算结果是否替换活动 data trace 不同。 | **[E3]** 四种运算是 typed complex node；除零、non-finite 和输入质量逐点传播，输出再进入 format/Marker/Limit。 | **[E4]** 纯软件能力；SIMD/性能只需目标平台验证。 |
| **MTH-03** Axis matching / memory interpolation（`MATH-03`） | **[E1]** 共同事实只到“data 与 memory 按刺激点对应运算”；没有证据支持不同轴可按下标静默运算。 | **[E2]** Keysight PNA 明确有 memory interpolation 开关，部分 ENA 型号不支持；CMT/R&S 的精确兼容政策不同或未在本次资料中明确。 | **[E3]** 默认 `ExactAxis`；显式 `InterpolateWithinOverlap` 才创建新对齐节点，禁止静默 extrapolation 和跨 segment gap 插值。 | **[E4]** 无硬件事实。 |
| **MTH-04** Frozen/reference trace（`MATH-04`） | **[E1]** Keysight `Data → New Trace` 生成不随 Sweep 更新的独立 locked trace；R&S/CMT 均可显示 saved/memory trace，因此“静态比较曲线”是成熟行为。 | **[E2]** locked trace、memory trace、文件导入 trace 的可编辑设置和保存限制不同。 | **[E3]** `FrozenTraceSnapshot` 与 Math Memory 分型；前者用于长期呈现/Workspace，后者是四则运算 operand，只有显式转换才互用。 | **[E4]** 长期保存容量与状态文件 schema 需平台验证。 |
| **MTH-05** Smoothing（`MATH-05`） | **[E1]** Keysight、R&S ZNB data flow 和 CMT 都把 smoothing 作为 Trace 级处理，并提供 aperture/窗口宽度；它不同于跨 Sweep averaging。 | **[E2]** Keysight/CMT 公开相邻点平滑和百分比 aperture，但 complex 还是 formatted scalar、边缘规则、窗口取整和 phase/Smith 行为不能假设一致。CMT 明确 formatted data 之后应用 smoothing。 | **[E3]** `SmoothingNode` 的 input stage、metric、window point count、edge policy 和 gap policy 全部显式；首个 Compatibility Profile 用黄金数组冻结，不以“20% aperture”一句代替算法。 | **[E4]** 无硬件事实；大点数实时重算性能需验证。 |
| **MTH-06** Min/Max Hold（`MATH-06`） | **[E1]** R&S ZNB data flow 与 CMT RVNA/RNVNA 型号族 [Trace Hold](https://coppermountaintech.com/help-r/trace-hold.html) 都提供跨重复测量的 min/max hold 和 restart；CMT internal arrays 把 hold 放在 formatted data 阶段。后一个 help-r 页面不能自动推广到全部 CMT VNA。 | **[E2]** Keysight PNA 本次目标资料没有形成与 CMT/R&S 同等明确的基础 hold 行为；各家在 axis/format 改变时 restart 规则不同。 | **[E3]** Hold 只对声明的 formatted scalar/metric 做逐点 accumulator；只接收成功完整 Sweep。axis、format 或 upstream revision 改变时默认 reset 并发出原因事件。 | **[E4]** 无算法硬件依赖；持续运行的内存和更新速率需目标机验证。 |
| **MTH-07** 沿 X 范围统计（`MATH-07`） | **[E1]** Keysight 和 CMT 都在全 span 或用户/marker 范围内计算 mean、standard deviation、peak-to-peak；R&S ZNA/ZNB 也公开 trace statistics。 | **[E2]** Keysight 明确按当前 display format 计算，Smith/Polar 默认用 LogMag 或可选 ohms；R&S/CMT 在 dB、复数格式和边界点上的精确公式需 Profile 验证。 | **[E3]** `RangeStatistics` 声明 scalar projection、unit、range、endpoint policy、invalid policy 和 sample count；结果绑定同一 Trace publication。 | **[E4]** 无硬件事实。 |
| **MTH-08** 跨 Sweep statistics（`MATH-08`） | **[E1]** 本次目标官方资料足以证明 averaging/hold 跨 Sweep 累积，但没有形成“三家把跨 Sweep mean/stddev 作为 Trace Statistics”这一共同事实。 | **[E2]** 厂商可能通过 averaging、history、应用选件或外部自动化实现，语义并不统一。 | **[E3]** 若产品需要，单独实现 `EnsembleStatisticsAccumulator`，不复用沿 X 的 `RangeStatistics`；列 Pro，输入必须同轴、同 pipeline revision。 | **[E4]** 长时累积容量和持续吞吐需目标机验证。 |
| **MTH-09** Statistics 显示与查询（`MATH-09`） | **[E1]** Keysight/CMT 均能显示统计结果并通过命令启停/选择范围；成熟远程控制需要读回与 UI 同源。 | **[E2]** 可查询字段、范围寄存器、RMS/min/max 和 marker 复用方式不同。 | **[E3]** Web/SCPI 查询同一 `StatisticsResultSnapshot{values, count, range, input_revision, validity}`；不存在结果时返回状态而非零值。 | **[E4]** 无硬件事实。 |
| **MTH-10** Equation Editor（`MATH-10`） | **[E1]** 不作为三家共同最低线。 | **[E2]** Keysight 明确 Equation Editor 可跨同/不同 Channel Trace 建自定义方程；R&S 高端 ZNA 也公开 equation/数学分析能力；CMT 基础文档以固定 math 为主。 | **[E3]** 列 Pro；使用有类型、无循环、资源受限的表达式图，不在 RTOS 引入任意脚本解释器。编译时验证 axis、unit、data generation 和最大节点/内存预算。 | **[E4]** 跨 Channel 同代数据能否取得取决于资源仲裁与扫描同步，不由表达式解析器保证。 |

## 6. 跨厂商处理顺序：可以确定什么，不能确定什么

三家公开 data flow 足以支持“分阶段 typed processing graph”，但不支持一条声称适用于所有商用 VNA 的固定顺序。

| 资料 | 厂商公开的关键阶段 | 证据处置 |
|---|---|---|
| Keysight [Accessing Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Accessing_Data_Descriptions.htm) | raw/ratio → apply error terms（fixture simulator 与此阶段关联）→ equation/trace math/memory → gating/phase correction/time domain → formatter → smoother | **E2** Keysight 外部访问顺序。 |
| R&S [ZNB/ZNBT User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf) | factory/user correction → de-/embedding/offset → single-ended/balanced/mixed-mode → renormalization/conversion/average；Trace 再经过 math/shift/time/gate/format/smooth/hold | **E2** R&S Channel/Trace data flow。 |
| CMT [Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html) | correction → port extension → Port Z conversion → embedding/de-embedding → trace math/electrical delay/gating/time transform/conversion → format → smoothing/hold | **E2** CMT 数组与 SCPI access stage。 |

项目结论是 **E3**：

1. A 层 `CompletedSweepBundle` 先形成同代 receiver-wave 完整采集输入，B 层 `CompletedMeasurementBundle` 再形成可供网络处理的正式测量；
2. correction、reference-plane/network、trace-complex、format、formatted-analysis 是不同 node category；
3. 每个节点声明输入/输出数据类型、axis、port topology、reference plane、Z0、quality 和 revision；
4. Product/Compatibility Profile 选择合法默认图；需要兼容某厂商顺序时用黄金数据回归，而不是散布条件分支；
5. Web、SCPI、Marker、Limit、保存和导出只引用命名 data stage，不能各自“取当前看起来像对的数组”。

## 7. Core / Pro / HW 推荐边界

以下是 **E3 产品与架构建议**，不是厂商内部事实。

| 能力 | 推荐等级 | 架构处置与完成门槛 |
|---|---|---|
| Cal Kit/Standard/Class、Guided session、Response、Full 1-port、One-path、Full 2-port SOLT、Correction Set、binding、applicability | **Core（按 BoardCapabilities 开放）** | 校准是 VNA 主链路，不应拆成占位 Pro。真实板卡缺少方向/路径时明确拒绝对应 method；Mock 必须覆盖全部状态机，但不能宣称计量通过。 |
| Trace electrical delay、Port extension、reference-plane provenance | **Core** | 基础相位/参考面能力；必须有理想传输线黄金数据和 reflection/transmission 符号测试。 |
| Renormalization | **Core + Pro 分层** | Core：正实数、逐端口 Z0、固定 wave convention；Pro：complex Z0、balanced-mode Z0、可选 wave theory。共享一个矩阵模块，不能写两套公式。 |
| Data→Memory、四则 math、Frozen Trace、Smoothing、Min/Max Hold、沿 X Statistics | **Core** | 固定且可解释的处理阶段、edge/invalid/reset policy；Web/SCPI 读同一 snapshot。 |
| Equation Editor、跨 Sweep Statistics | **Pro** | 有类型表达式图与有界 accumulator；不引入任意脚本运行时。 |
| Touchstone fixture import、2-port/N-port embedding/de-embedding、conditioning | **Pro** | 基础 seam 进入主 processing graph；未授权/未实现时 capability reject。必须通过 cascade/inverse/near-singular 黄金集。 |
| Mixed-mode | **Pro + HW capability** | 算法是软件 Pro；是否可用取决于同代完整多端口 S-matrix、端口配对和相干性。 |
| 基础 time transform/window/gate/gated-frequency | **Pro** | 独立 `vna-time-domain` 模块；窗口、DC、归一化、gate inverse 和质量传播全部有黄金数据。 |
| AFR、automatic fixture extraction、enhanced TDR、eye/mask | **HW/Option 或独立 Pro** | 不与基础 de-embedding/time-domain 混名；没有附件/算法/计量证据时明确不支持。 |

### 7.1 推荐 deep modules

- `vna-calibration`：Connector/Kit/Standard/Class catalog、CalibrationSession、method planners/solvers、CorrectionSet repository、Applicability/Binding；
- `vna-network-processing`：Port Extension、Renormalization、Fixture、Mixed-mode、reference-plane graph 和 numerical quality；
- `vna-trace-processing`：Memory/Frozen、fixed math、electrical delay、format、smoothing、hold、statistics；
- `vna-time-domain`：grid validation、DC policy、transform/window、gate/inverse、time/distance axis；
- `vna-processing-graph`：typed node、合法连接、revision/provenance、quality propagation 和 evaluation cache；
- Web/SCPI Adapter：只做参数解析、选择上下文和兼容映射，不复制 solver/processing 算法。

所有 `vna-calibration` solve 与 processing graph evaluate 入口都必须显式接收 `ExecutionContext{stop_token, monotonic_deadline, BudgetHandle, ProgressSink}`。可协作算法以有界粒度检查取消/deadline；不可中断的第三方调用只能进入隔离 worker/lane，父 Operation 超时或取消后由可见 child Drain Operation 继续持有预算与并发槽，直到真实返回，不能提前释放后复用。

## 8. 发布前必须建立的黄金数据与计量门禁

| 门禁 | 最小黄金集 | 失败时不得宣称 |
|---|---|---|
| **G-CAL-01 标准模型** | Ideal SOLT、带 delay/loss 的 polynomial standard、data-based S1P/S2P、频带边界与单位/方向错误。 | Cal Kit 兼容或标准件模型正确。 |
| **G-CAL-02 Error model solver** | 注入已知 directivity/source match/load match/reflection/transmission tracking/isolation 的合成 error box，分别验证 response、SOL、one-path、full 2-port；再与至少一台商用 VNA/可溯源 kit 对照。 | 校准算法达到商用或计量精度。 |
| **G-CAL-03 Applicability** | exact、缩窄频段、改变点数、越出频段、IFBW/power/attenuator/route 改变、band crossing、相位点间变化超过 180°。 | 插值/外推/失效政策可靠。 |
| **G-CAL-04 Verification** | 独立、已表征的 verification artifact；逐点 residual/uncertainty margin 的 Pass/Fail/Indeterminate；过期/缺失 characterization、轴/端口/温度不匹配、无效点、重复连接和取消；对照至少一种商用 System Verification/Confidence Check。 | 校准验证、置信检查或 system-level PASS 可靠；更不能宣称单个仪器/标准件已获认证。 |
| **G-REF-01 Delay/Extension** | 理想无损/有损传输线，正负 delay，S11/S22 往返与 S21/S12 两端累加，Trace delay 与 Port extension 对比。 | 参考面和相位符号正确。 |
| **G-NET-01 Renormalization** | 已知 2-port/4-port 网络，50→75→50 round trip、每端口不同 real Z0、complex Z0、不同 wave convention、near-singular case。 | arbitrary/complex Z0 正确。 |
| **G-FIX-01 Fixture** | `FixtureA × DUT × FixtureB` 正向合成和反向恢复、方向颠倒、Z0 不同、轴插值、奇异/近奇异 fixture。 | de-embedding 稳定或不放大噪声。 |
| **G-MIX-01 Mixed mode** | 已知 4-port single-ended 矩阵 ↔ SDD/SCC/SDC/SCD，正负端交换、差分/共模 Z0、缺 Sij。 | mixed-mode 参数正确。 |
| **G-TDR-01 Transform** | 理想延迟线/open/short/load、band-pass/low-pass impulse/step、harmonic grid、手工/外推 DC、每种 window 的幅值和峰位置。 | 时域幅值、距离或分辨率正确。 |
| **G-TDR-02 Gate** | 双反射可分离曲线，band-pass/notch、每种 shape、inverse 后频域参考；检查 leakage/ripple 与 normalization。 | gated frequency result 正确。 |
| **G-MTH-01 Trace processing** | 复数四则、除零、不同轴、smoothing edge/gap、min/max hold reset、formatted statistics、invalid/NaN、失败 Sweep 不累积。 | Math/Hold/Statistics 与其 Profile 一致。 |
| **G-HW-01 真实单板** | 实际逐点 a/b 路径、正反激励、过载/失锁/未稳幅/温度、路由量化、长时间漂移、目标机吞吐。 | Mock 能力等同真实硬件或达到产品容量。 |

## 9. 逐项状态建议与待决策边界

### 9.1 `IAC-039..046` 逐 ID 证据投影

下表与 [`01-instrument-acquisition-calibration.md`](../design/alignment/01-instrument-acquisition-calibration.md) 的正式 ID 和首要状态一一对应。状态只表示当前最先需要关闭的门禁；同一行可能仍有次级 E2/E3/E4 验证。

| 正式 ID | 一手证据/研究主题 | E1/E2/E3/E4 边界 | 当前状态 |
|---|---|---|---|
| IAC-039 | CAL-01/02 | E1 connector/kit/standard/role 分层；E2 文件与 class 方言；E3 revisioned catalog；E4 公司实物资产 | 已由证据定案 |
| IAC-040 | CAL-04..07 | E1 Response/Full 1-port/One-path/Full 2-port 范围；E2 方法变体；E3 typed MethodSpec；solver 精度需计量黄金集，路径需 E4 | 待算法/计量验证 |
| IAC-041 | CAL-03 | E1 引导式多阶段校准；E2 跳步/重测/自动模块差异；E3 可重试 Session 状态机；E4 附件与耗时 | 已由证据定案 |
| IAC-042 | CAL-08/13 | E1 标准件逐步采集与完成门禁；E2 confidence/quality 差异；E3 immutable attempt/quality；E4 真实质量位 | 已由证据定案 |
| IAC-043 | CAL-09/13 | E1 标准采集与 Error Terms 分离；E2 retention 差异；E3 原子不可变 CorrectionSet；数值求解需计量黄金集 | 待算法/计量验证 |
| IAC-044 | CAL-10 | E1 Correction Set 可绑定/启停；E2 Channel/Trace/recall 方言；E3 revision-checked binding；E4 跨板复用 | 已由证据定案 |
| IAC-045 | CAL-11/12 | E1 exact/interpolated/disabled 可见；E2 extrapolation/changed 规则不同；E3 MatchReport；E4 实际量化/路径条件 | 待兼容目标 |
| IAC-046 | CAL-13/14 | E1 校准后测量独立已知状态并与表征比较；E2 verification/confidence/certification 范围和阈值不同；E3 独立 Plan/Operation/Result；artifact 与 limit 需 E4/计量 | 待算法/计量验证 |

### 9.2 相关分析/网络功能的状态边界

| 对齐范围 | 建议状态 | 已定案内容 | 仍需闭合 |
|---|---|---|---|
| `IAC-039` / CAL-01～02 | **已由官方证据定案** | Connector、Kit、Standard Definition、Physical Instance、type/class 分离。 | kit 文件/类标签兼容放 **待兼容目标**；公司实物资产放 **待底软/硬件确认**。 |
| `IAC-040` / CAL-04～07 | **已由官方证据定案 + 待算法计量验证** | Response、Full 1-port、One-path、Full 2-port 的适用范围和相对完整性。 | solver 公式/精度为 **待算法计量验证**；实际方向/路径为 **待底软/硬件确认**。项目默认 Core 基线止于这四类；TRL/UOSM/isolation 仅在对应算法、接收机路径和静态 ProductProfile 能力齐备后暴露，不新增独立产品决策。 |
| `IAC-041`～`IAC-044` / CAL-03、08～10 | **已由官方证据定案** | Guided session、逐步 acquisition、solve、Correction Set/Cal Group、binding/apply 分层。 | 跳步/重测/自动校准模块语义为 **待兼容目标**；retention/容量由 `PLAT-09/10` 的目标机容量验证关闭，不再作为额外产品决策。 |
| `IAC-045` / CAL-11～13 | **已由官方证据定案 + 待兼容目标** | exact/interpolated/changed/incompatible 必须可见。 | 项目原生默认对普通 S 参数范围外点拒绝 correction；Keysight/CMT 等 extrapolation 差异由 Compatibility Profile 冻结，插值准确度为 **待算法计量验证**。 |
| `IAC-046` / CAL-13～14 | **Pro 工作流已定 + 待算法计量验证** | 校准验证独立于 solve/apply；绑定 CorrectionSet、独立 artifact characterization 和正式 B 层采集，输出不可变 Pass/Fail/Indeterminate 结果。 | artifact/characterization/uncertainty policy、residual 算法和计量阈值由 **算法/计量验证** 关闭；不能把 confidence check、system verification 和仪器/标准件认证混称。 |
| `NET-01`～`NET-02`、`NET-04` / REF-01～02、04 | **已由官方证据定案 + 待算法验证** | Trace delay 与 Port extension 分离；参考面 provenance 必须可追溯。 | delay/loss 符号、往返/单程和自动延伸为 **待算法计量验证**。 |
| `NET-03` / REF-03 | **待产品决策 + 待算法计量验证** | renormalization 是网络矩阵变换，不是物理端口改变。 | Core real-Z0 / Pro complex-Z0 分层、wave convention/理论兼容需冻结。 |
| `NET-05`～`NET-08` / FIX-01～03 | **Pro 已定方向 + 待算法计量验证** | Touchstone fixture、embed/de-embed、typed order 和 condition quality。 | 项目基线先关闭显式 2-port fixture；N-port 由文件端口数/算法 capability 静态门控，节点重排和外推政策由 **待兼容目标** 冻结；数值稳定性必须过黄金集，不新增产品决策。 |
| `NET-09` / MIX-01 | **Pro + 待底软/硬件确认** | balanced logical port、differential/common/mode-conversion 参数。 | port pair 是显式用户配置，Z0/wave convention 复用 `NET-03` 的既有产品决策；完整同代 S-matrix 和相干性为 **待底软/硬件确认**。 |
| `TD-01`～`TD-07` / TDR-01～07 | **Pro + 待算法计量验证** | transform/window/grid/DC/gate/inverse 的外部闭环已有官方证据。 | FFT/CZT、normalization、bad-point、zero-padding 由 **算法计量验证** 关闭，厂商 preset 名称由 Compatibility Profile 映射；不新增产品决策。 |
| `MATH-01`～`MATH-07`、`MATH-09` / MTH-01～07、09 | **Core；外部能力已定案** | memory、四则、frozen、smoothing、hold、沿 X statistics。 | 精确处理阶段、edge/gap/reset/format policy 为 **待兼容目标/算法验证**。 |
| `MATH-08`、`MATH-10` / MTH-08、10 | **Pro；待产品决策** | 不属于三家共同最低线。 | 跨 Sweep statistics 是否首发、Equation Editor 表达式范围和资源预算。 |
| `NET-10` | **HW/Option；待产品决策与硬件确认** | 不与基础 fixture/time-domain 混为一项。 | AFR、enhanced TDR、eye/mask 的附件、授权、算法和计量证据。 |

### 9.3 关键厂商差异

1. **校准容器**：Keysight 是 Cal Set，R&S 是 Cal Group/Calibration Pool，CMT 更直接暴露 coefficient/status；对外兼容名称和生命周期不能硬统一。
2. **校准外推**：Keysight 普通 S 参数越出校准频段时关闭 correction，CMT 明确暴露 extrapolated `C!`；项目原生默认拒绝范围外 correction，目标厂商差异由 Compatibility Profile 冻结。
3. **标准 class**：Keysight 与 CMT 都有 class/role，但标签、assignment、多标准覆盖和 Guided/Unguided 规则不同。
4. **处理顺序**：Keysight、R&S、CMT 公开的数据访问阶段并非同一总顺序；只能采用 typed graph + Profile，不能宣称一条“商用统一 pipeline”。
5. **Port Extension**：三家目标相同，但 loss model、自动估计、与 fixture/balanced/memory 的处理阶段不同。
6. **时域实现/选件**：CMT 明确 Chirp-Z/Kaiser；R&S 公开多种 Fourier window/gate 并常作为 K2 选件；算法名和 preset 名不能直接互换。
7. **Memory/Hold**：Keysight 有每 Measurement memory 和 locked trace，CMT 有 FIFO memory，R&S 可有多个 memory；Hold/reset 与格式/轴变化规则也不同。
8. **高级数学**：固定 Data/Memory math 是共同能力；Equation Editor、跨 Channel 方程和跨 Sweep statistics 不是共同最低线。

### 9.4 官方资料不能证明的边界

- 不能证明三家内部 solver、error-term array、fixture/mixed-mode/time-domain 公式和缓存结构相同；
- 不能证明本项目用 Eigen3 实现后具有与商用仪器相同的数值稳定性、残余误差或 uncertainty；
- 不能证明 Mock 声明的端口数、正反路径、相干性、质量标志、扫频速度和动态范围存在于真实单板；
- 不能证明某个菜单名相同就有相同的 window、gate、smoothing、statistics、interpolation 或 extrapolation 边界；
- 不能证明目标 AArch64 SDK 下的 CPU/内存/文件系统容量和实时刷新率；
- 不能用官方功能列表替代黄金数据、可溯源校准件、真实板卡和商用 VNA 对照验收。

### 9.5 对当前对齐行的总评

1. `IAC-039`～`IAC-046` 不应继续保持“只有名词”的候选状态：对象分层、方法族、session/acquisition/solve/apply、Correction Set/Binding/applicability，以及独立 calibration verification 的外部闭环已有充分 E1/E2；内部 revision、状态机、typed solver 和 verification result 是明确 E3。
2. `NET-01`～`NET-09` 应采用完整 network stage，而不是逐 Trace 修补。Port Extension/renormalization/fixture/mixed-mode 都依赖端口拓扑、Z0、参考面和同代 S-matrix。
3. `TD-01`～`TD-07` 的基础外部能力已有证据，但 FFT/CZT、window、DC、normalization 和 bad-point policy 不能由菜单名推定；应保持 Pro，直到黄金数据通过。
4. `MATH-01`～`MATH-07`、`MATH-09` 可作为 Core；`MATH-08` 跨 Sweep statistics 与 `MATH-10` Equation Editor 不属于共同最低线，推荐 Pro。
5. `NET-10` 的 AFR/enhanced TDR/eye 仍保持独立 HW/Option；Mock 不得通过空算法或补零数据把它们报告为 supported。

## 10. 一手资料索引

### Keysight PNA/ENA

- [Calibration Standards](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Calibration_Standards.htm)
- [Calibration programming topic](https://helpfiles.keysight.com/csg/N52xxB/Programming/CalTopic.htm)
- [Read and Write Calibration Data using COM](https://helpfiles.keysight.com/csg/N52xxB/Programming/Learning_about_COM/Read_and_Write_Calibration_Data_using_COM.htm)
- [CSET command reference](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/CSET.htm)
- [Error Correction and Interpolation](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Error_Correction_and_Interpolation.htm)
- [Accessing Data](https://helpfiles.keysight.com/csg/N52xxB/Programming/Accessing_Data_Descriptions.htm)
- [Comparing the PNA Delay Functions](https://helpfiles.keysight.com/csg/N52xxA/Tutorials/Comparing_the_PNA_Delay_Functions.htm)
- [Port Extensions](https://helpfiles.keysight.com/csg/N52xxB/S3_Cals/Port_Extensions.htm)
- [Using Fixture Simulator](https://helpfiles.keysight.com/csg/N52xxB/Programming/Using_Fixture_Simulator.htm)
- [ENA Fixture Simulator overview](https://helpfiles.keysight.com/csg/e5072a/measurement/fixture_simulator/overview_of_fixture_simulator.htm)
- [ENA Balanced Device Evaluation](https://helpfiles.keysight.com/csg/e5072a/measurement/fixture_simulator/evaluating_balanced_devices_balance_unbalance_conversion.htm)
- [Using Gating](https://helpfiles.keysight.com/csg/N52xxB/Applications/Enhanced_Time_Domain_Analysis/Making_Measurements/Using_Gating.htm)
- [Math Operations](https://helpfiles.keysight.com/csg/N52xxB/S4_Collect/Math_Operations.htm)
- [System Verification](https://helpfiles.keysight.com/csg/m9485a/support/system_verification.htm)
- [Validity of a Cal / ECal Confidence Check](https://helpfiles.keysight.com/csg/pxivna/S3_Cals/Quest_Cal.htm)

### Rohde & Schwarz ZNA/ZNB

- [R&S ZNA User Manual v39](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf)：§4.5.1 p.197-198；§4.1.7 p.125-128。
- [R&S ZNB/ZNBT User Manual v72](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf)：§3.4.1.3 p.81-82；§4.5.3 p.181；§4.5.4 p.182；§4.1.5 p.99-102。
- [Accurate Test Fixture Characterization and De-embedding, 1SL367](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1sl367/1SL367_0e_Test_Fixture_Characterization_and_De-embedding.pdf)
- [Time Domain Measurements Using Vector Network Analyzer ZNA, 1EP83](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ep83/1EP83_0e_TimeDomain_ZNA7.pdf)

### Copper Mountain Technologies

- [Calibration Standards and Calibration Kits](https://coppermountaintech.com/help-cmtvna/1-port/calibration-standards-and-kits.html)
- [Classes of Calibration Standards](https://coppermountaintech.com/help-cmtvna/1-port/classes-of-calibration-standar.html)
- [Classes Management](https://coppermountaintech.com/help-cmtvna/1-port/classes-management.html)
- [Basic Calibration Guidelines](https://coppermountaintech.com/help-cmtvna/1-port/basic-calibration-guidelines.html)
- [Quick Calibration](https://coppermountaintech.com/help-cmtvna/1-port/quick-calibration.html)
- [Error Correction Status](https://coppermountaintech.com/help-cmtvna/1-port/error-correction-status.html)
- [Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)
- [Electrical Delay](https://coppermountaintech.com/help-cmtvna/1-port/electrical-delay-setting.html)
- [Port Extension](https://coppermountaintech.com/help-cmtvna/TR-Series/port-extension.html)
- [Embedding](https://coppermountaintech.com/help-cmtvna/1-port/embedding.html)
- [De-embedding](https://coppermountaintech.com/help-cmtvna/1-port/de-embedding.html)
- [CMT Balanced Measurement](https://coppermountaintech.com/wp-content/uploads/2022/06/BalancedMeas.pdf)
- [Time Domain Transformation](https://coppermountaintech.com/help-cmtvna/1-port/time-domain-transformation.html)
- [Time Domain Gating](https://coppermountaintech.com/help-cmtvna/1-port/time-domain-gating.html)
- [Mathematical Operations](https://coppermountaintech.com/help-cmtvna/1-port/mathematical-operations.html)
- [Trace Statistics](https://coppermountaintech.com/help-cmtvna/1-port/trace-statistics.html)
- [Trace Hold](https://coppermountaintech.com/help-r/trace-hold.html)
- [Confidence Check](https://coppermountaintech.com/help-cmtvna/1-port/confidence-check2.html)
