# 商用 VNA 控制、状态、文件、安全与目标平台的一手证据

> 研究日期：2026-07-17
> 研究对象：Keysight PNA/ENA、Rohde & Schwarz ZNA/ZNB、Copper Mountain Technologies VNA；cpp-httplib、Eigen、nlohmann/json；目标系统 `RTOS 208.5.0.SPC1 / Linux 5.10.0 / aarch64`。
> 用途：给 `docs/design/alignment/03-control-files-platform.md` 提供可追溯证据，不反推厂商内部代码结构。

> **R&S 版本冻结。** 本基线固定引用 ZNA User Manual v39 与 ZNB/ZNBT User Manual v72；截至研究日，官方入口已发布 [ZNA v40（2026-03-27）](https://www.rohde-schwarz.com/uk/manual/rs-zna-user-manual_78701-601863.html) 和 [ZNB/ZNBT v73（2026-04-14）](https://www.rohde-schwarz.com/ca/manual/r-s-znb-znbt-user-manual-manuals_78701-29151.html)。升级参考版本必须重核章节、命令和兼容回归；本文页码均指 PDF 页脚页码。

## 1. 结论先行

1. **raw TCP SCPI 是可靠的跨厂商交集，但“端口 5025、完整命令树、选择作用域和队列细节”不是同一个兼容承诺。** Keysight 和 R&S 的官方资料都给出 `TCPIP0::<host>::5025::SOCKET`；CMT 也提供 TCP/IP Socket，但端口可配置。因此产品应有 raw Socket Transport 和可配置监听端口，命令树、selection、错误文本、删除副作用和同步行为必须由 `ScpiCompatibilityProfile` 冻结。
2. **三家公开资料都没有证明“每个 TCP 连接天然拥有一台独立仪器”。** Keysight 明确区分全机唯一 Active Measurement 与每 Channel 的 Selected Measurement；CMT 命令大量依赖 active Channel/Trace；R&S 也公开 active trace/channel 语义。Web 页面可以有 session-local 焦点，但 SCPI selection scope 必须是兼容配置，不能擅自固定为 per-connection。
3. **`*OPC?`、错误队列和状态模型是成熟自动化的必要组成，不是附加功能。** Keysight、R&S、CMT 都公开了等待已提交操作的机制；Keysight 和 R&S 明确指出未读取 query response、异步测量和错误队列的时序陷阱。实现必须把“命令已接受、Sweep 已结束、分析已发布、响应已读取”分开。
4. **State 不是一个含义模糊的 JSON 文件。** 厂商明确区分仅设置、设置加校准、设置加 Trace/Memory、完整包或 CalSet 引用/实值。项目 State 必须用 inclusion profile 和 manifest 明示内容；Recall 必须先验证再原子提交，并默认恢复到 Hold + RF safe/off。厂商资料支持 inclusion 与静态数据 Hold 的外部需求，但不证明我们的原子 staging、CRC、迁移链、掉电安全或 RF 激活策略实现。
5. **Touchstone 和 CSV 是跨厂商共性；“缺少的 S 参数补零”不能成为本产品默认行为。** Touchstone 导出必须来自同一次 completed network snapshot，携带实际频率轴、端口映射、数据格式和参考阻抗。若硬件没有完整 S 矩阵，应拒绝相应 sNp、导出明确的子矩阵，或要求用户显式选择兼容降级；不得静默伪造零值。
6. **厂商仪器的 Windows Web/LXI、用户账户、防火墙和远程桌面资料，只能证明成熟仪器会管理远程访问面，不能证明本项目的 Web 认证、角色、CSRF、TLS、SCPI ACL 或审计方案。** 这些由项目安全架构、部署策略和 E4 验证关闭；真正的产品取舍只保留 `SEC-01` 的身份/角色深度与 `SEC-03` 的 SCPI 暴露/认证边界，其余不是让用户判断的架构题。
7. **上游库文档只能把依赖列为候选，不能证明可在公司 RTOS SDK 或指定 MinGW 开发环境中使用。** 当前 cpp-httplib 官方仓库公开支持 HTTP/1.1、SSE、WebSocket 和多种 TLS 后端，同时明确使用 blocking I/O，WebSocket 长连接占线程，默认待处理队列可无限；其 Windows 注记还明确只正式支持最新 Visual Studio，Cygwin 和 MSYS2（包括 MinGW）既不支持也未测试。Eigen 是 header-only，但并行、对齐、向量化和数值结果受编译选项影响；nlohmann/json 是 header-only，但默认广泛使用异常，关闭异常会把默认失败路径改为 `abort()`。三者都必须 pin 精确版本并做 E4 compile/link/run/memory/soak 验证。
8. **用户提供的系统输出只证明“当前镜像自报为 aarch64 Linux 5.10 PREEMPT”。** 它不证明这是 PREEMPT_RT，不证明存在可用 C++17 标准库、OpenSSL、原子 rename/fsync、可靠 monotonic clock、线程优先级、足够 RAM/Flash，也不证明 MinGW 可生成该目标程序。Windows 开发使用 MinGW-w64；生产构建必须使用公司 AArch64 Linux SDK/sysroot。

## 2. 证据等级与研究边界

| 等级 | 本文用法 | 可以定案 | 不能据此定案 |
|---|---|---|---|
| E1 | 至少两家 VNA 厂商的一手资料公开相同外部行为 | 行业级最低外部能力、必须预留的协议语义 | 厂商内部类、线程、数据库或对象所有权 |
| E2 | 单一厂商/型号的一手资料，或厂商之间有明显差异 | 某个兼容 Profile 的候选行为、产品差异点 | 跨厂商统一语义 |
| E3 | 为满足 E1/E2 外部行为而作的项目架构推导 | 本项目可评审、可测试的内部规则 | “商用仪器内部就是这样实现”的宣传 |
| E4 | 公司底软/板卡资料、目标 SDK 编译链接、目标机运行与测量 | 目标平台、容量、实时性、硬件能力和依赖准入 | 仅凭 PC 或上游 CI 推断目标可用 |

研究只使用厂商官方手册、厂商官方帮助、上游项目官方文档。没有找到的内容记为“未证明”，不等同于“不支持”。厂商型号族行为也不自动推广到该厂商所有 VNA。

## 3. 厂商控制模型证据

### 3.1 raw Socket、方言与选择作用域

| 主题 | Keysight | Rohde & Schwarz | CMT | 跨厂商结论 |
|---|---|---|---|---|
| raw TCP | [PNA Socket Client](https://helpfiles.keysight.com/csg/N52xxB/Programming/GPIB_Example_Programs/Socket_Client.htm) 给出 VNA 端口 5025 和 VISA Socket 资源。 | [R&S VISA and tools](https://www.rohde-schwarz.com/nl/driver-pages/remote-control/3-visa-and-tools_231388.html) 给出 RawSocket 的 `TCPIP0::<IP>::5025::SOCKET`。 | [Connection Setup](https://coppermountaintech.com/help-cmtvna/Programming-Manual/connection-setup.html) 要求启用 Socket server，并允许配置端口；[Programming](https://coppermountaintech.com/help-cmtvna/Programming-Manual/programming.html) 说明 SCPI ASCII 可经 HiSLIP 或 TCP/IP Socket 发送。 | **E1：** raw TCP SCPI 是共同能力。**E2：** 5025 是强惯例而非不可配置协议常量。 |
| 方言 | PNA 的 Channel/Measurement/Window/Trace 命令树和 Measurement name/number 具有自己的对象模型。 | ZNA/ZNB 的 active trace、channel、diagram 和命令树与 Keysight 不同。 | CMT 的命令常以 active Channel/Trace 为上下文，且对象数量/命令副作用有自身规则。 | **E2：** 不存在“把三家命令拼在一起就叫兼容”的依据；必须选首要兼容目标并维护方言回归套件。 |
| selection | [Referring to Traces...](https://helpfiles.keysight.com/csg/N52xxA/Programming/Learning_about_GPIB/Referring_to_Traces_Measurements_Channels_Windows_Using_SCPI.htm) 明确全机一个 Active Measurement、每 Channel 一个 Selected Measurement，并区分 Measurement Name、Window-local Trace Number 和 global Measurement Number。 | [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 的 GUI/命令参考使用 active trace/channel/diagram；未公开“每连接隔离”的保证。 | [Selection of Active Trace/Channel](https://coppermountaintech.com/help-cmtvna/1-port/selection-of-active-trace_channel.html) 和 [CALCulate tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/calculate.html) 表明 Marker、Limit、格式和数据查询依赖 active Trace 或显式 Channel/Trace。 | **E1：** 商用品存在共享仪器选择上下文。**未证明：** TCP 连接天然拥有独立 selected 对象。 |

架构处置：Transport 只负责字节流和连接生命周期；`ScpiCompatibilityProfile` 决定命令树、编号、选择作用域、错误文本与副作用；类型化 Command/Query 进入同一个 Instrument Kernel。Web 的 session-local 页面焦点不得被用来推导 SCPI 的 selection scope。

### 3.2 命令、查询、错误、状态与完成同步

| 主题 | 一手证据 | 结论与架构约束 |
|---|---|---|
| 顺序命令与 overlapped 操作 | Keysight [Command Synchronization](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm) 区分 sequential command 与 `INIT` 等 overlapped operation，并警告在测量完成前读取 Marker 可能得到不准确结果。R&S [Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html) 同样把命令发送和测量完成分开。 | **E1：** parser 接收完成不等于测量/分析完成。Session submission 保序；Sweep/Cal/Recall/Self-test 是可观察 Operation。 |
| `*OPC?/*OPC/*WAI` | Keysight 说明 `*OPC?` 等待 pending commands 后返回 `1`，`*OPC` 在完成后设置 ESR bit 0，`*WAI` 阻止后续仪器命令越过同步点。R&S 说明 `*OPC?` 可在 RawSocket 上工作，但响应必须被读取。CMT [`*OPC?`](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html) 明确等待此前全部 pending operations，典型用于 `TRIG:SING`。 | **E1：** 必须有完成同步。**E2/E3：** pending 工作集合、continuous sweep 和跨 session 操作不应凭想象；Profile 决定外部语义，内部用 session operation fence 固定同步点之前的目标工作。 |
| Output Queue | Keysight 同一同步文档说明：前一个 query response 未读时又发送 query，前一响应可被清除并形成 query 错误。R&S 同步文档也警告 query response 未读取就发送下一条命令会产生 Query Interrupted。 | **E1：** query response 有严格消费时序。**E2：** 覆盖/拒绝/错误文本是方言行为。项目本地队列必须有界、响应与已接受 query 的目标及快照绑定。 |
| Error Queue | Keysight 同步文档描述 FIFO、`SYST:ERR?` 逐条弹出、`*CLS` 清除及 overflow 行为；R&S [Instrument Error Checking](https://www.rohde-schwarz.com/us/driver-pages/remote-control/instrument-error-checking_231244.html) 说明 `SYST:ERR?` 取出并删除一项，直到 `0,"No Error"`；CMT RVNA/RNVNA 型号族 [`SYST:ERR?`](https://coppermountaintech.com/help-r/systerr_.html) 也公开 FIFO 弹出语义，不能自动推广到全部 CMT VNA。 | **E1：** FIFO/read-pop/empty sentinel 是稳定交集。**E2：** 深度、满队列保留哪端、错误码和 `*CLS` 影响范围不同。命令错误队列与设备故障/诊断日志必须分层。 |
| IEEE 488.2 状态 | Keysight [Status Commands](https://helpfiles.keysight.com/csg/N52xxA/Programming/GP-IB_Command_Finder/Status.htm) 公开 Status Byte、Standard Event、Operation、Questionable、enable/event/condition 和 `*STB?/*SRE/*ESE/*ESR?/*CLS`。R&S 同步/错误资料用 STB 的 EAV/ESB 与 `*STB?` 轮询。 | **E1：** 状态寄存器不是一个布尔 busy。实现 condition/event/enable/summary 分层状态机；读清、副作用和位映射按 Profile 回归。 |
| RawSocket 与 SRQ | R&S 同步资料明确：其 RawSocket 没有 VISA control channel，`*STB?` 查询可用，但 VISA ReadSTB 与异步 SRQ 不可用。Keysight [PNA Socket Client](https://helpfiles.keysight.com/csg/N52xxB/Programming/GPIB_Example_Programs/Socket_Client.htm) 则公开了 PNA 专有的第二条 TCP control connection，可接收 SRQ 并执行 Device Clear。 | **E2：** SRQ 是明显方言/Transport 差异，不能笼统宣称“所有 raw Socket 都有”或“所有 raw Socket 都没有”。基础 Profile 只承诺 `*STB?` polling；PNA control socket、HiSLIP/VXI-11 或项目事件协议必须作为独立 capability。 |
| 自检 | IEEE 488.2 有 `*TST?` 通用命令名，但本次公开资料没有形成三家对 quick/deep self-test 范围、耗时、侵入性和返回码的共同承诺。 | **E2/E4：** 可以提供 `*TST?` 兼容入口，但只能报告底软/硬件实际执行的测试；传感器、锁定、环回、RF 安全和恢复均需板卡 capability 与目标实测。 |

### 3.3 大数组、数据阶段与二进制块

- Keysight [Format SCPI](https://helpfiles.keysight.com/csg/N52xxA/Programming/GP-IB_Command_Finder/Format_SCPI.htm) 公开 `FORM:DATA ASCII|REAL,32|REAL,64`、字节序设置和 IEEE definite-length block，并提醒高精度频率轴需要 64-bit 表示；[Getting and Putting Data](https://helpfiles.keysight.com/csg/N52xxA/Programming/GPIB_Example_Programs/Getting_and_Putting_Data.htm) 给出块长度解析示例。
- CMT [Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html) 明确区分 raw receiver、raw S 参数、corrected receiver/S 参数、corrected trace、formatted trace、memory 和 stimulus axis，说明“当前数据”不是一个无类型数组。
- R&S ZNA 的正式命令参考同样提供格式/数据传输命令，但具体 `FORM` 参数和字节序应在选定固件的命令手册中冻结，不能拿 Keysight 参数表代替 R&S 兼容测试。

结论：**E1** 支持 ASCII 与二进制块、明确数据阶段是成熟远控能力；**E2** 命令名、dtype、字节序默认值和数据阶段命名存在方言差异；**E3** query 接受时解析并 pin 对象、axis、stage、completed snapshot，之后 selection 或新 Sweep 不得撕裂响应；**E4** 必须做 MinGW/AArch64 交叉解码、endianness、NaN/Inf、部分写、断线和最大点数 hash 测试。

## 4. State、文件交换与恢复证据

### 4.1 State inclusion 与 Recall 粒度

| 厂商 | 一手证据 | 能证明什么 | 不能证明什么 |
|---|---|---|---|
| Keysight PNA | [Save Method](https://helpfiles.keysight.com/csg/NA520xA/Programming/COM_Reference/Methods/Save_Method.htm) 区分 `.sta` 仅 Instrument State、`.cst` State + CalSet reference、`.cal` 校准归档、`.csa` State + actual calibration data。 | State、校准引用和校准实值是不同 inclusion；扩展名具有明确恢复语义。 | 我们应复制其私有文件格式；PNA 内部是否用事务数据库。 |
| Keysight ENA | [Saving and Recalling File](https://helpfiles.keysight.com/csg/e5072a/programming/remote_control/saving_and_recalling/saving_and_recalling_file.htm) 给出四种内容：状态；状态+校准系数；状态+corrected data/memory；三者全部；另有全机文件和每 Channel 挥发寄存器。 | inclusion profile、全机/Channel 粒度和 volatile/persistent 区分是实际商用品行为。 | ENA 选项自动等于 PNA 或本产品 Profile。 |
| R&S ZNA | [ZNA User Manual v39](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) §4.1.1 p.112 指出 global settings 不属于 recall set、也不受 Preset 影响；§4.1.2 p.113 公开 parallel recall sets。 | 测量 State 与全局/系统设置存在保护边界。 | 项目已把网络、账户、证书、密钥、审计和工厂计量数据设为普通测量 State 之外的 E3 保护域；FactoryReset 的外部副作用再由 Compatibility/Security policy 验证。 |
| R&S ZNL/ZNLE 同系 | [ZNL/ZNLE User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_5966_01/ZNL_ZNLE_UserManual_en_22.pdf) 明确 “Instrument with all Channels” 与 “Current Channel”；Recall 全机包替换全机设置，Channel 包创建新的 Channel setup。 | R&S 同系确有 scope-aware recall。 | ZNA 当前固件必须完全相同；这里只是 E2 补证。 |
| CMT | [Analyzer State](https://coppermountaintech.com/help-cmtvna/1-port/analyzer-state.html) 区分 State、State & Cal、State & Trace、All、State & Cal & Mem；带 trace 的 Recall 自动进入 Hold，避免新测量覆盖恢复数据。 | 设置、校准、Trace、Memory inclusion 和 Recall 后保护数据的行为。 | State 文件原子性、校验、跨板兼容和 schema migration。 |

架构处置：定义 `SettingsOnly`、`StateAndCalibration`、`StateAndTraceMemory`、`All` 等显式 profile；包内含 schema/version、ProductProfile、BoardCapability、对象清单、引用、单位和 checksum。Recall 作为 Operation 在 staging 中完成 schema、完整性、引用、capability 和容量校验，成功后一次提交新 instrument revision。`RecallActivationPolicy` 默认 `RestoreInHoldSafeOff`，不恢复 RF-on、Continuous/Groups、Armed/WaitingTrigger 或未完成 Operation；`ExplicitRestoreRunState` 需额外授权，仍先安全恢复，再以新的完整 admission Operation 启动。异常重启永远保持 HoldSafeOff；网络配置、账户、证书、密钥、审计日志、工厂校准/计量数据和可绕过授权的 auto-run 能力默认不进入普通测量 State。

### 4.2 Touchstone、CSV 与领域文件

- Keysight ENA 官方资料公开 active Channel 的 Touchstone、active Trace 的 CSV，并能独立保存/恢复 Segment 与 Limit 表；PNA `Save` 也公开 `.s1p` 至 `.s4p` 和逗号分隔 trace 文件。
- R&S [ZNA User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf) 公开 Touchstone 1.1/2.0 和 ASCII `.csv` trace files；导出前建议单 Sweep/Hold，以确保所导数据来自完整 Sweep。
- CMT [Touchstone](https://coppermountaintech.com/help-cmtvna/TR-Series/trace-data-touchstone-file.html) 公开频率、S 参数、RI/MA/DB、参考阻抗、端口映射和 sNp；RVNA/RNVNA 型号族 [CSV](https://coppermountaintech.com/help-r/trace-data-csv-file.html) 公开 active/all traces、stimulus、单位、显示值或复数格式及区域小数点选项，后者不能自动推广到全部当前 CMT VNA。
- CMT 某些受限产品会在请求 `.s2p` 而硬件只有部分 S 参数时把缺失项填零。这是**厂商/型号特例 E2**，不是可信数据交换的跨厂商要求。本项目默认禁止静默填零。

架构处置：Touchstone 只从同代 B 层 `CompletedMeasurementBundle` 导出；CSV/TSV 使用实际全分辨率 X axis，不使用画面抽稀点，并显式写 `data_stage`。复数/network stage 固定 B 层 `measurement_snapshot_id`；formatted/derived Trace 固定 C 层 `analysis_publication_id`，同时记录 trace/projection/unit revision，单个文件不得跨 B/C 代次。Cal Kit、CalSet、Segment、Limit、Fixture、Frozen Trace 各自有 versioned schema，不塞进不可审计的通用 JSON blob。Web/SCPI 文件接口只访问虚拟 ExchangeFileStore，禁止任意系统路径。

## 5. 诊断、安全与远程控制证据边界

### 5.1 诊断与 self-test

- Keysight [Error Messages](https://helpfiles.keysight.com/csg/N52xxA/Support/About_Error_Messages.htm) 公开仪器/操作系统错误、严重级别和 Error Log；这证明成熟产品需要用户可见错误历史，但不等同于 SCPI session error queue。
- R&S ZNA Getting Started 指向包含 performance test、troubleshooting、fault elimination 的 Service Manual，并另有 Instrument Security Procedures；部分资料需要注册访问。这能证明诊断/安全是独立产品面，不能公开证明内部 watchdog 或自检算法。
- CMT [System Ready](https://coppermountaintech.com/help-cmtvna/Programming-Manual/systready_.html) 提供 readiness query；CMT Demo Mode 证明无硬件自动化模拟是商用品实践，但不证明其 mock 架构可直接复制。

项目必须区分：SCPI command error queue、Operation failure、device/Questionable fault、health telemetry、持久诊断日志和审计日志。Quick self-test、deep self-test、在线 health 的真实 scope 来自 Board Adapter capability；底软未提供的传感器/环回不得由上层伪造。

### 5.2 用户、网络与安全

- Keysight [LXI Compliance](https://helpfiles.keysight.com/csg/NA520xA/S0_Start/LXI_Compliance.htm) 公开浏览器访问、用户名/密码和 Web configuration password reset；[Product Cybersecurity](https://helpfiles.keysight.com/csg/N52xxB/Product_and_Solution_Cybersecurity.htm) 说明 Windows Firewall 默认启用，并建议更新和反恶意软件。
- R&S [ZNA Getting Started](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/getting_started/1178_6456_01/ZNA_GettingStarted_en_25.pdf) 公开两个不同权限账户、远程访问凭据、修改默认密码、Windows Firewall 默认启用和远程控制端口；也公开 LAN/Remote Desktop 控制。
- 这些证据是 **E1/E2 的风险与产品面证据**，不是本项目方案的直接证明。它们尤其不能证明：cpp-httplib 自动提供认证/RBAC/CSRF/CORS；raw SCPI 自带身份；目标 SDK 有 TLS/安全随机数；State 可以包含密码/私钥；或只有“在内网”就无需连接/输入上限。

架构处置：Web 至少有管理员/操作者/只读角色、密码变更、会话超时、SameSite/HttpOnly cookie、CSRF token、Origin 检查和上传限制；SCPI 默认只绑定可信管理网并支持 allowlist/连接上限，是否增加认证必须由部署 Profile 决定；TLS 明确选择设备内终止或受管反向代理，绝不把明文公网暴露当默认。证书、密钥、密码哈希和审计保护域独立于普通 State。

## 6. 平台与依赖：厂商仪器资料不能证明什么

用户提供：

```text
cat /etc/RTOS-Release
RTOS 208.5.0.SPC1

uname -a
Linux HI1213 5.10.0 #1 SMP PREEMPT Thu Jun 1 CST 2023 aarch64 GNU/Linux
```

这是一条待纳入平台基线的 **E4 现场观察**。它证明当前镜像自报 aarch64、Linux 5.10、SMP、PREEMPT；它不证明：

- `PREEMPT_RT` 或任何硬实时延迟上界；
- 公司生产 SDK 的编译器版本、sysroot、libc/libstdc++、C++17、异常/RTTI策略；
- MinGW-w64 可以生成 AArch64 Linux 可执行文件；
- POSIX socket、epoll、线程优先级、线程栈、monotonic clock、locale 和 DNS 的可用细节；
- OpenSSL/mbedTLS/wolfSSL、CA store、安全随机数和证书更新路径；
- 文件系统类型、`rename`/`fsync`/目录同步的掉电原子性和 Flash 磨损策略；
- 可用 RAM/Flash、连续内存、最大文件、页缓存压力、实际 CPU/SIMD 能力；
- 板卡端口、源、接收机、route、trigger、温度/锁定传感器和 a/b 波形质量契约。

### 6.1 cpp-httplib

[cpp-httplib 官方仓库](https://github.com/yhirose/cpp-httplib) 当前公开：C++11 single-file header-only、跨平台 HTTP/HTTPS；仅 HTTP/1.1；blocking socket I/O；支持 SSE、WebSocket 和 OpenSSL/mbedTLS/wolfSSL。其 Windows 注记明确“正式支持”只覆盖最新 Visual Studio，并把 Cygwin 与 MSYS2（包括 MinGW）列为既不支持也未测试。因此用户指定的 MinGW-w64 不能从“cross platform/header-only”推导为已支持，只能作为项目自行承担验证责任的候选。它同时明确：

- 32-bit 平台不受支持；目标 aarch64 是 64-bit，但这仍不等于目标 SDK 已通过；
- Windows MinGW/MSYS2 不在上游支持和测试范围内；开发侧必须冻结精确 MinGW-w64 版本与编译选项并独立准入；
- WebSocket 每个连接在整个生命周期占一个线程，heartbeat 还可增加线程；
- 默认 ThreadPool 会动态扩展；默认 `max_queued_requests=0` 表示 pending request queue 无上限；
- TLS 后端有明确版本/链接依赖；WebSocket extensions 未实现；正则路由可能产生栈风险。

因此当前上游已公开 WebSocket，不能继续写成“cpp-httplib 天然没有 WebSocket”；但它仍只是候选，并必须 pin 精确 release，因为较早版本能力不同。准入分成两个独立门禁：先在项目指定的精确 MinGW-w64 上 compile/link/run 基础 HTTP server、静态资源、REST、binary streaming、慢连接和关闭流程，再验证 SSE/WebSocket/TLS；生产侧另用公司 AArch64 SDK/目标机重复完整准入并固定线程池和 queue。若只有 WebSocket 线程模型不满足容量预算，Realtime Transport seam 可切换 SSE + REST；若基础 HTTP 在 MinGW 或目标 SDK 上失败，必须替换整个 Web HTTP Transport Adapter，不能仅从 WebSocket 切到 SSE 掩盖基础不兼容。

### 6.2 Eigen

[Eigen 3.4 Getting Started](https://libeigen.gitlab.io/eigen/docs-3.4/GettingStarted.html) 说明只需头文件、没有二进制库需要链接；[Eigen 3.4 multi-threading](https://libeigen.gitlab.io/eigen/docs-3.4/TopicMultiThreading.html) 说明部分算法可经 OpenMP 并行，可用 `Eigen::setNbThreads` 或 `EIGEN_DONT_PARALLELIZE` 控制，并警告过多线程可能显著变慢；[Eigen 3.4 preprocessor directives](https://libeigen.gitlab.io/eigen/docs-3.4/TopicPreprocessorDirectives.html) 明确 alignment、vectorization、fast-math、stack allocation 等宏会改变行为，部分宏不一致会导致 ABI/API 或隐蔽错误。

架构上 Eigen 只允许出现在 `vna-compute` 实现层，公共 seam 使用项目自己的 Buffer/View。生产准入是 **E4**：pin Eigen3 版本；用两工具链跑校准/去嵌/时域黄金数据；冻结并行、fast-math、alignment/SIMD 配置；测动态分配、临时矩阵、栈上限、异常和 ARM 数值差异。上游“header-only”不证明确定性、内存上界或目标实时适用。

### 6.3 nlohmann/json

[nlohmann/json Integration](https://json.nlohmann.me/integration/index.html) 提供 header-only 集成；[Exceptions](https://json.nlohmann.me/home/exceptions/) 说明库广泛使用异常，`JSON_NOEXCEPTION`/`-fno-exceptions` 下默认以 `abort()` 替代，并允许用户覆盖 throw/try/catch 宏；开启 `JSON_DIAGNOSTICS` 会为每个值增加父指针和运行时开销；[SAX Interface](https://json.nlohmann.me/features/parsing/sax_interface/) 提供不构造完整 DOM 的事件式解析接口。

它可作为 Web metadata 和受控小型配置的候选，不得进入 Instrument Kernel 类型边界，也不得用 DOM 承载测量大数组。生产准入是 **E4**：pin 版本；确认目标异常/RTTI策略；对 body/depth/string/array/object 设置硬上限；测 malformed UTF-8、深嵌套、超大数字、内存峰值和 fuzz。若目标禁止异常，必须设计显式错误返回或受控 SAX codec，不能只定义 `JSON_NOEXCEPTION` 后接受进程 `abort()`。

## 7. 逐项证据—架构矩阵

下表使用研究行 `WEB-E/SCPI/STA/FIL/DIA/SEC-E/BLD/DEP/CAP`。`WEB-E`、`SEC-E` 特意与正式 `WEB-xx`、`SEC-xx` 功能 ID 分开；SCPI 研究行与正式 ID 一一对应。一行同时出现两个等级时，前者是外部证据，后者是项目处置或准入门禁。

### 7.1 WEB

| 行 | 证据判断 | 等级 | 架构处置 | 剩余闭合门禁 | 对齐行 |
|---|---|---|---|---|---|
| WEB-E01 | R&S 明确 GUI 与 remote command 双向关联；CMT GUI、SCPI 和 Demo Mode 面向同一仪器能力。厂商资料不证明我们的页面信息架构。 | E1 / E3 | Web/SCPI 只做协议适配，共用 Kernel、Command、Query、Operation、Snapshot；所有已暴露的 Core/Pro/HW 能力都必须有完整 Web 操作面，ProductProfile 只负责 capability gating。 | 页面信息架构、响应式布局和可用性验收；不缩减已暴露功能。 | WEB-01, WEB-02, WEB-12 |
| WEB-E02 | 厂商 Web/LXI 证明浏览器可成为远程入口，但不证明完整 VNA Web API、初始快照或增量事件协议。 | E2 / E3 | 内部从同一 Catalog cut 捕获状态与 replay cut，只向 Web 返回业务状态和不可比较、不可用于 mutation 的不透明 `WatchResumeToken`。Watch 在内部从对应 cursor 后重放并去重；epoch/retention gap 或 actor/session 的 access-set 任一扩张/收缩都关闭旧 Watch，重新鉴权并取得新 Snapshot。 | 事件保留窗口、重连 SLA、角色/ACL 变化测试；公共 schema 无 catalog/object revision。 | WEB-03, WEB-04 |
| WEB-E03 | Sweep 完成后才读取数据/Marker 是跨厂商要求；progress preview 传输是项目设计。 | E1 / E3 | Preview 标 `provisional`、可丢/合并；B 层先发布 `measurement.completed`，C 层逐 Trace 原子发布 Trace/Marker/Limit，单 Trace 失败隔离。 | Preview 速率、抽稀策略、目标吞吐。 | WEB-05, WEB-06 |
| WEB-E04 | SCPI binary block 证明大数组应避免文本膨胀；HTTP binary endpoint 不是厂商共同事实。 | E1 / E3/E4 | JSON 仅 metadata；全分辨率独立 binary endpoint。QueryTicket 到 Ready 时先取得有配额 `ResultPinLease`，`open_read` 再原子转为 ReaderLease，限 size/time/lease。 | dtype/endian、下载上限、目标吞吐与 pin 配额。 | WEB-07, PLAT-09 |
| WEB-E05 | 复合表格原子编辑、多浏览器 optimistic concurrency 未由厂商手册规定。 | E3 | ADR-0014 记录产品决策：Web/SCPI 不暴露 revision；字段/稳定行 ID patch 在 Control Admission Cut 上整体校验并用内部 revision 原子提交；普通同字段竞争以后接受且成功提交者生效，不使用 edit lease；需要持续 owner 的长 Operation 继续用 lease。 | 后来非法命令不得覆盖先前状态；长 Operation 接管权限与 lease timeout；协议错误不得回传内部版本值。 | WEB-08, WEB-09 |
| WEB-E06 | Sweep/Cal/Recall/Export/self-test 是长操作；刷新不取消共享操作是项目可靠性规则。 | E1 / E3 | 统一 Operation Catalog，HTTP 仅创建/查询/取消/订阅；owner 为 actor/session。 | cancel 点、断线 grace、管理员接管。 | WEB-10 |
| WEB-E07 | 厂商资料不证明目标机需要 Node 或浏览器可计算校准真值。 | E3/E4 | 前端主机构建静态 bundle；浏览器只交互/渲染；正式分析在 C++；API/bundle schema 版本绑定。 | 前端栈、升级/回滚和缓存策略。 | WEB-11, WEB-12 |

### 7.2 SCPI

| 行 | 证据判断 | 等级 | 架构处置 | 剩余闭合门禁 | 对齐行 |
|---|---|---|---|---|---|
| SCPI-01 | 三家均公开 raw TCP SCPI。 | E1 / E4 | 可配置 bind/port；连接独立 parser/input/output/lifecycle，共享 Instrument。per-session Error FIFO/ESR/ESE/SRE 的权威状态归 `ScpiSessionStateCatalog`，不归 Socket 对象。 | 默认端口、连接数、idle timeout、目标 socket soak。 | SCPI-01 |
| SCPI-02 | 命令树、编号、错误文本和副作用明显不同。 | E2 | `ScpiCompatibilityProfile`；推荐选一套 PNA 公共子集，原生 Profile 使用显式 ID。 | 首个兼容型号/固件与承诺命令清单。 | SCPI-02 |
| SCPI-03 | SCPI 有长短关键字、层级、query、字符串和数值语法；厂商不证明我们的 parser 结构。 | E1 / E3 | 独立 lexer/parser/command tree，产出类型化 Command/Query；fuzz 和长度上限。 | 单位集合、分号链边界、兼容缩写。 | SCPI-03 |
| SCPI-04 | 存在全机 Active、每 Channel Selected/Active；无 per-connection 隔离共识。 | E1/E2 / E3 | `SelectionScopePolicy` 支持 SharedInstrument、PerChannelShared、SessionLocal；query 接受时解析固定目标。 | 目标方言的 scope、UI 与 remote active 联动。 | SCPI-04 |
| SCPI-05 | sequential 与 overlapped operation 跨厂商存在。 | E1 / E3 | 同 session submission 保序；异步 Operation 不阻 parser；同步由 fence/query 完成。 | `INIT;Marker?` 等精确方言行为。 | SCPI-05 |
| SCPI-06 | Keysight/R&S 都警告 query response 未读的错误，具体覆盖行为不同。 | E1/E2 / E3 | 有界 output queue；响应绑定 query 目标/快照；明确 unread-response policy。 | PNA-compatible 覆盖还是拒绝、队列深度。 | SCPI-06 |
| SCPI-07 | 三家公开 FIFO/read-pop error queue。 | E1/E2 / E3 | `ScpiSessionStateCatalog` 保存每 session 有界 command/execution/query error queue；parser error 也按 session sequence 写入。错误记录、`SYST:ERR?` pop 与 overflow sentinel/latch 经 Control Executor 独占、每 Session 有预留容量的 `SessionStateIngress`；device fault 单独进 Instrument 状态/诊断。 | overflow sentinel/保留端、`*CLS`、全局故障复制策略。 | SCPI-07 |
| SCPI-08 | IEEE common commands 广泛存在；`*TST?` 的真实范围不统一。 | E1/E2/E4 | 实现身份、reset、clear、event/status；`*TST?` 只映射真实 self-test。`*CLS`、`*ESR?` read-clear 与 enable/register 写统一经 SessionStateIngress 保序。 | `*RST`/Preset 保留矩阵；read-clear 精确语义；self-test scope。 | SCPI-08 |
| SCPI-09 | 三家公开 `*OPC?`；Keysight 还公开 `*OPC/*WAI`。 | E1/E2 / E3 | per-session operation fence 只捕获同步点前应等待工作；continuous 不得无限等。 | `*OPC/*WAI` 的兼容细节与 timeout。 | SCPI-09 |
| SCPI-10 | Standard Event、STB、Operation、Questionable 是成熟状态模型。 | E1 / E3 | Instrument `StatusRegisterCatalog` 保存共享 Operation/Questionable，per-session `ScpiSessionStateCatalog` 保存 ESR/ESE/SRE/Error FIFO；非破坏性 `*STB?`/Condition 可走 PriorityRead，`*ESR?` read-clear、enable 写和 `*CLS` 经 SessionStateIngress；错误摘要、锁存和 overflow 与 error queue 原子一致。 | 精确 bit map、两类 Catalog 的摘要映射、read-clear/overflow Profile 和 device fault 分类。 | SCPI-10 |
| SCPI-11 | INIT/continuous/hold/single/abort 是行业能力，命令名/范围有差异。 | E1/E2 / E3 | 映射统一 Sweep Operation 与 Channel mode；Abort 按 owner/profile 解析目标。 | trigger scope、ABOR 范围、publish 阶段取消。 | SCPI-11 |
| SCPI-12 | 厂商公开 raw/corrected/formatted/stimulus 等不同数据阶段。 | E1/E2 / E3 | 每个 query 指定 MeasurementDataStage、axis、target、snapshot。缺少 receiver/ratio/corrected/ProcessedNetwork 等非 C 层结果时启动/共享 `MaterializeMeasurementStageOperation`，产生只绑定 A/B、RF/network graph 与 Profile 的 `MeasurementStageSnapshot`；formatted Trace 才进入 `EvaluateTraceOperation`。 | 首发公开哪些 receiver/raw/cal stage。 | SCPI-12 |
| SCPI-13 | ASCII、REAL32/64 和 binary block 是成熟能力。 | E1/E2 / E4 | definite-length block 从不可变 Buffer 流式发送；格式和 endian 由 Profile。 | dtype/default endian、最大响应、跨架构验证。 | SCPI-13 |
| SCPI-14 | 厂商同步语义要求完整结果，但不公开 immutable snapshot pin 算法。 | E3 | query 接受时按 Profile 解析 selection，再 pin 最近兼容 completed snapshot。 | pin timeout、回收压力、stale policy。 | SCPI-14 |
| SCPI-15 | 断线释放传输资源但是否取消共享 Sweep 未有跨厂商统一规则。 | E3 | cancel waiter/transfer；Operation 按 owner/command 语义继续或取消。 | Cal 断线 grace、共享 Sweep、管理员接管。 | SCPI-15 |
| SCPI-16 | R&S RawSocket 无 SRQ control channel；Keysight PNA raw Socket 可另开专有 control connection 接收 SRQ。 | E2 | 基础 raw TCP 只承诺 `*STB?` polling；异步服务请求作为独立 Transport capability，不能由寄存器实现自动推导。 | 是否兼容 PNA control socket，或增加 HiSLIP/VXI-11/项目事件协议。 | SCPI-16 |
| SCPI-17 | `*IDN?`/产品与固件版本/选件查询是成熟自动化面，项目 capability manifest 为推导。 | E1/E2 / E3/E4 | 返回产品身份、软件/固件版本、选件和实际能力值；内部 ProfileSet、ScpiCompatibilityProfile、ProductProfile 与 BoardCapability revision 不进入 SCPI 响应，但命令暴露与拒绝仍由同一冻结内部 Profile 决定。 | 产品 ID、版本 schema、运行时降级规则。 | SCPI-17 |

### 7.3 STA 与 FIL

| 行 | 证据判断 | 等级 | 架构处置 | 剩余闭合门禁 | 对齐行 |
|---|---|---|---|---|---|
| STA-01 | Keysight/CMT 均明确设置、校准、Trace/Memory inclusion。 | E1 / E3 | Core 固定提供 `SettingsOnly`、`StateAndCalibration`、`StateAndTraceMemory`、`All` 四种显式 inclusion profile 和内容 manifest；未指定时使用 `SettingsOnly`。另带 `RecallActivationPolicy`，默认 HoldSafeOff，不让普通 State 携带可绕过授权的 auto-run 能力。 | 各 Profile 的对象闭包、容量、跨版本 round-trip 和 RF 安全恢复测试。 | FILE-01 |
| STA-02 | Keysight 明确 CalSet reference 与 actual calibration data 不同。 | E2 / E3 | 引用和内嵌 blob 分型；`StateAndCalibration/All` 可显式选择 reference 或 embedded，恢复时验证引用、硬件、频率/端口适用性。 | portable embedded CalSet 的跨板适用性与容量由 E4/MatchReport 验证。 | FILE-01, FILE-06 |
| STA-03 | ENA/R&S 同系公开全机/Channel scope；各家副作用不同。 | E2 | State 包记录 scope；Core 同时提供全机 Recall 与 `ImportChannelFromState` 两个不同 Command。 | 目标方言是否把 Channel import 暴露为 recall 及其编号/副作用，归 Compatibility Profile。 | FILE-01, FILE-02 |
| STA-04 | CMT 带 Trace Recall 自动 Hold；完整数据不应立即被新 Sweep 覆盖。 | E2 / E3 | 恢复 Frozen/Trace data 时显式进入 Hold 或作为独立 FrozenTrace 导入；普通 Recall/异常启动统一 HoldSafeOff，不恢复 RF-on、Continuous/Groups 或 WaitingTrigger。 | 兼容 Profile 的默认行为；显式运行恢复的授权/审计。 | FILE-01, FILE-02 |
| STA-05 | 厂商资料不证明 staging/CRC/atomic commit/掉电恢复或 RF 激活安全。 | E3/E4 | Recall Operation 带 ExecutionContext，先校验后单 revision commit；写入 temp+flush+checksum+replace+last-good。显式恢复运行态也先安全 commit，再以新的 Compiler→admission→prepare→reservation→ExecutionLease Operation 启动。 | 目标文件系统原子性、断电、safe-state/readback 与启动 admission 试验。 | FILE-02, FILE-07, FILE-09 |
| STA-06 | R&S 公开 global settings 不受 Recall/Preset；`*RST`、Preset、Factory Reset 没有跨厂商统一保留矩阵。 | E2 / E3 | 四类生命周期 Command 分开，工厂校准和安全数据独立保护域。 | 每类对象保留/删除矩阵与授权。 | FILE-03 |
| FIL-01 | 三家支持 Touchstone；格式/版本/端口能力有差异。 | E1/E2 / E3 | 从 completed network snapshot 导出 sNp、RI/MA/DB、axis、Z0、port map；需要矩阵级 network graph 时先物化 `MeasurementStageSnapshot(ProcessedNetwork)`，不伪造 Trace。Export 全程带 ExecutionContext 与 TypedSnapshotLeaseSet。 | Touchstone 1.1/2.0、复杂 Z0、最大端口。 | FILE-04 |
| FIL-02 | CMT 某型号会补零，这是单厂商特例。 | E2 / E3 | 默认拒绝不完整矩阵的完整 sNp，绝不静默伪造零。 | 是否提供显式 legacy-compatible 降级。 | FILE-04 |
| FIL-03 | 三家支持 CSV/ASCII trace export。 | E1/E2 / E3 | 全分辨率实际 X；schema 明示 `data_stage`：复数/network 固定 B 层 `measurement_snapshot_id`，惰性矩阵 stage 固定 `measurement_stage_snapshot_id`，formatted/derived Trace 固定 C 层 `analysis_publication_id`；流式 Export 带 ExecutionContext/TypedSnapshotLeaseSet。 | CSV/TSV、locale、NaN、公式注入策略。 | FILE-05 |
| FIL-04 | Keysight ENA 可独立存 Limit/Segment；厂商领域文件集合不统一。 | E2 / E3 | Core 为 CalKit、CalSet、Limit、Segment、Fixture、FrozenTrace 六类对象分别提供 versioned import/export codec；不支持的算法/硬件引用在导入验证时明确拒绝。 | 厂商文件兼容版本、大小上限和 schema migration；不再形成额外产品范围决策。 | FILE-06 |
| FIL-05 | 厂商路径命令不能证明本项目可暴露任意文件系统。 | E3/E4 | ExchangeFileStore 虚拟根、canonical path、symlink/path traversal/配额防护。 | SCPI `MMEM` 可见命名空间和上传下载权限。 | FILE-08 |
| FIL-06 | 厂商可读旧文件不证明我们的 schema migration。 | E3 | 显式 migration chain；未知新版本拒绝写入；保留原包。 | 支持版本窗口与降级策略。 | FILE-09 |
| FIL-07 | 厂商支持屏幕图、打印/报告，但模板和签名不是共同最低线。 | E2 / E3 | 报告/图片绑定 immutable snapshot 和模板 revision，列为 Pro。 | PDF/图片格式、签名和中文字体。 | FILE-10 |

### 7.4 DIA 与 SEC

| 行 | 证据判断 | 等级 | 架构处置 | 剩余闭合门禁 | 对齐行 |
|---|---|---|---|---|---|
| DIA-01 | 厂商公开 Ready、错误历史和服务诊断面；板能力字段不统一。 | E2/E4 | `BoardAdapter::describe()` 返回 versioned capability，supported/unsupported/temporary/unknown 分开，并显式包含 Clock/Coherence Domain、timebase lock、同步 trigger/epoch、skew 与实际轴保证；未知时不得合成跨板相干矩阵。 | 底软 capability ABI、同步保证与 Mock profiles。 | DIAG-01 |
| DIA-02 | `*TST?` 名称不证明 quick self-test 的具体内容。 | E2/E4 | 启动 quick test 生成不可变 scoped result，状态 Ready/Degraded/Fault。 | 驱动、固件、参考锁、存储、网络哪些可真实检测。 | DIAG-02 |
| DIA-03 | deep test 的侵入性和 RF 安全未形成公开共识。 | E3/E4 | 独占 diagnostics lease；Diagnostics worker 接 ExecutionContext；需 Board/RF 的步骤由 Kernel 编排，结束/取消/失败经不依赖 Acquisition/Recovery 的 `BoardSafetyLane` 恢复并 readback 安全状态。 | 板卡环回、RF output、最大耗时、可取消点与独立 kill。 | DIAG-03 |
| DIA-04 | 厂商状态/错误面支持健康可观察性，但传感器取决于硬件。 | E2/E4 | Health Aggregator 分硬件 telemetry 与上层 queue/last-good；stale 明示。 | 温度、锁定、过载、a/b quality 字段和阈值。 | DIAG-04 |
| DIA-05 | 厂商 Error Log 不等于本项目结构化、配额日志。 | E2 / E3/E4 | command error、device fault、internal log 分层；关联 operation/sweep/session；环形配额和脱敏。 | 保留期、Flash 写放大、可信时钟。 | DIAG-05 |
| DIA-06 | 诊断包、watchdog、审计链是项目运维需求，未由命令手册规定。 | E3/E4 | Diagnostics Export Operation 接 ExecutionContext 并持确切 Catalog/Snapshot lease 到 package commit；不可中断压缩/文件调用转 Drain。Board worker quarantine/reinit 与独立 SafetyLane/kill 分离；actor/session/revision 审计。 | 包内容/脱敏、恢复策略、匿名 SCPI actor。 | DIAG-06, DIAG-07, DIAG-08 |
| SEC-E01 | Keysight/R&S 均公开账号/密码、防火墙和远程网络控制。 | E1/E2 / E3 | Auth Adapter + Policy；管理员/操作者/只读；默认密码强制变更。 | 本地账号、LDAP/OIDC 是否需要；锁定策略。 | SEC-01 |
| SEC-E02 | 厂商资料不证明 cpp-httplib 提供 CSRF/CORS/session security。 | E3/E4 | SameSite/HttpOnly、CSRF、Origin、body/upload/route 上限；WebSocket 握手同身份。 | cookie/token 模型、允许 Origin、目标渗透测试。 | SEC-02, SEC-05 |
| SEC-E03 | raw SCPI 是明文 Socket；没有跨厂商统一认证承诺。 | E2 / E3 | 默认管理网 bind、allowlist、连接/速率限额和来源审计。 | 是否增加认证、VPN/网闸/专网部署。 | SEC-03 |
| SEC-E04 | 厂商网络安全实践证明需要保护远程面；不证明目标 TLS 可用。 | E1/E2 / E3/E4 | 设备内 TLS 或反向代理二选一；证书/密钥独立保护域。 | TLS backend、CA、证书轮换、时钟/RNG。 | SEC-04 |
| SEC-E05 | State/Recall 不应覆盖安全全局设置有 R&S 方向性证据。 | E2 / E3 | 账户、网络、证书、密钥、审计、工厂数据排除普通测量 State。 | FactoryReset 的 Profile 副作用、安全授权和恢复测试。 | FILE-03, SEC-01, SEC-04 |

### 7.5 BLD、DEP 与 CAP

| 行 | 证据判断 | 等级 | 架构处置 | 剩余闭合门禁 | 对齐行 |
|---|---|---|---|---|---|
| BLD-01 | `uname` 不证明 SDK C++17/stdlib；MinGW 不是 AArch64 Linux 生产编译器。 | E4 | C++17；MinGW-w64 仅主机 Mock/test；公司 SDK/sysroot 生产构建。 | 编译器、libc++/libstdc++、异常/RTTI。 | PLAT-01, PLAT-02 |
| BLD-02 | `PREEMPT` 不等于 PREEMPT_RT 或硬实时保证。 | E4 | 硬实时留底软；上层 control/data plane 解耦，Adapter worker 不回调 Kernel。 | 调度策略、优先级、栈、延迟预算。 | PLAT-03 |
| BLD-03 | 厂商资料不证明固定任务模型。 | E3/E4 | 固定 Control/Acquisition/条件 Prepare/每板独立 Safety/Processing/Solver/Recovery/Web/SCPI/Persistence/Diag workers，有界 queue/backpressure；stage/Trace/solve/file/diagnostics 长操作都接 ExecutionContext，不可中断调用转 Drain。 | 每池线程数、优先级、shutdown deadline 与 kill/readback SLA。 | PLAT-08 |
| BLD-04 | 动态插件 ABI 对双工具链风险高，厂商资料无关。 | E3/E4 | CMake feature selection + static registration + ProductProfile gating。 | Core/Pro/HW 组合和 license 方式。 | PLAT-12 |
| DEP-01 | cpp-httplib 上游公开 HTTP/SSE/WebSocket/TLS 和 blocking/thread/queue 限制；Windows 仅正式支持最新 Visual Studio，MSYS2（含 MinGW）不支持且未测试。 | E4 准入 | 仅作候选；pin 版本；精确 MinGW 先过基础 HTTP→SSE/WS/TLS，AArch64 SDK 独立准入；固定 pool、queue、timeout、payload。基础 HTTP 失败则替换整个 Web HTTP Transport Adapter。 | WebSocket 或 SSE；若 HTTP Adapter 替换则选择何种已验证实现；TLS backend。 | PLAT-05, PLAT-07, PLAT-11 |
| DEP-02 | Eigen header-only 不证明 ARM 数值/内存/实时性。 | E4 准入 | 仅 compute implementation；冻结 parallel/SIMD/fast-math/alignment；黄金数据。 | Eigen3 精确版本和编译宏。 | PLAT-04, PLAT-11 |
| DEP-03 | nlohmann/json header-only；异常关闭默认可 abort；DOM 有内存风险。 | E4 准入 | 仅 codec/web metadata；硬上限；异常策略；必要时 SAX/受控 codec。 | 是否准入及精确版本。 | PLAT-06, PLAT-11 |
| DEP-04 | 上游 TLS backend 声明不证明目标库、CA/RNG 可用。 | E4 | 独立 TLS spike：compile/link/handshake/cert rotation/clock/RNG。 | OpenSSL、mbedTLS、wolfSSL 或代理终止。 | PLAT-05, SEC-04, PLAT-11 |
| CAP-01 | 系统自报 aarch64 Linux 5.10 SMP PREEMPT。 | E4（待复现） | 保存 SDK/image/build-id 基线，目标冒烟打印 ABI/endianness/features。 | 正式 SDK 与镜像版本。 | PLAT-01, PLAT-02, PLAT-03 |
| CAP-02 | RAM/Flash/吞吐/点数组合无厂商或 OS 输出依据。 | E4 | ProductProfile 容量矩阵；Sweep 开始前 reservation；BufferPool；QueryTicket Ready 的 `ResultPinLease` 与 Reading 的 ReaderLease 均计入全局/Session pin bytes。 | Channel/Trace/point/client/history/pin 上限。 | PLAT-09, PLAT-10 |
| CAP-03 | 文件系统原子性和 Flash 行为未知。 | E4 | 目标上验证 rename/fsync/掉电/空间不足/磨损；不通过则适配 journal/双槽。 | 状态分区、配额、last-good 数量。 | FILE-07, DIAG-05 |
| CAP-04 | 板卡可输出 a/b raw waveform 或跨板相干采集的设想不等于接口事实。 | E4 | Board Adapter + Mock；capability 驱动 route/trigger/quality/Clock/Coherence Domain；默认一个 Logical Sweep 绑定一个 Board Session，跨板 matrix/mixed-mode/calibration bundle 只有在 timebase/trigger/epoch/skew/axis 全部被证明时允许，unsupported/unknown 明确拒绝。 | 底软 SDK、buffer ownership、timestamp、取消、错误与跨板同步契约。 | DIAG-01..04, PLAT-10 |
| CAP-05 | HTTP/WebSocket/SCPI 并发和长期稳定性未知。 | E4 | 目标慢连接、断线、连接洪泛、最大下载、24h/72h soak 和内存上界。 | SLA、最大客户端、heartbeat、backpressure。 | WEB-05, WEB-07, SEC-05, PLAT-05, PLAT-07..10 |

## 8. 待闭合项：产品取舍、兼容目标与 E4 门禁

### 8.1 真正的产品/部署取舍（对应总矩阵 4 项）

1. `SEC-01`：Web 账号来源、管理员/操作者/只读角色、锁定和会话策略；推荐设备本地三角色基线。
2. `SEC-03`：SCPI 只限可信管理网/IP allowlist，还是额外增加认证；推荐先采用受控管理网。
3. `FILE-10`：报告、图片、签名结果包是否作为 Pro 交付；推荐与不可变 Snapshot/模板 revision 绑定。
4. `PLAT-12`：Pro/HW 能力的静态 ProductProfile 组合与授权方式；推荐静态注册，不采用目标机动态 C++ ABI 插件。

其余 5 项真正产品取舍列在总矩阵的分析/显示组：`LIM-10`、`MATH-08`、`MATH-10`、`NET-03`、`NET-10`。`LIM-05` 的核心 Indeterminate 与生产 fail-safe 汇总政策已经定案；本节后续条目不是额外产品决策。

### 8.2 兼容目标、工程默认与硬件闭合（不新增产品决策）

1. 首个 SCPI 兼容目标必须绑定具体型号、固件和命令清单；每个 Profile 冻结 selection scope、未读 output response、Error Queue overflow、`*CLS`、`*RST`、Preset、Abort、`*OPC/*WAI` 和删除副作用。
2. TLS 终止位置、backend、证书轮换和无可信时钟启动策略由 `SEC-04/PLAT-05/11` 的部署 spike 与目标平台验证关闭。
3. Preview transport 保持可替换 seam；WebSocket 或 SSE + REST、客户端数、更新率和抽稀由 `WEB-05/07` 的目标机压力测试选择。
4. State inclusion 与引用闭包对应 `FILE-01`，原子 Recall/Channel-only recall 对应 `FILE-02`，CalSet 引用/内嵌对应 `FILE-06`，FrozenTrace 恢复后的 Hold 语义还对应 `MATH-04`；只有 Preset/`*RST`/FactoryReset 的保留矩阵归 `FILE-03`。这些由 Compatibility Profile/状态 schema 冻结。
5. Touchstone 版本、最大端口和不完整矩阵策略主要由 `FILE-04`、`NET-03` 及 Board/算法/容量 capability 关闭；`FILE-05` 只负责版本化 CSV/TSV schema，报告范围只落在既有 `FILE-10` 决策。
6. `*TST?` quick-test scope、deep self-test 的 RF 安全/恢复及 health 指标由 `DIAG-01..08` 的底软能力与真实板验收关闭。

### 8.3 必须由目标 SDK/目标机关闭的 E4 门禁

1. 公司 AArch64 SDK/sysroot：C++17、stdlib、exceptions、RTTI、threads、filesystem、atomic、endianness、double/complex ABI。
2. Eigen3、cpp-httplib、候选 JSON、TLS backend 的精确版本 compile/link/run 和许可证/漏洞维护报告；cpp-httplib 必须分别冻结并验证项目指定 MinGW-w64 与公司 AArch64 SDK，不得把上游 latest-Visual-Studio 支持范围冒充 MinGW 支持。
3. cpp-httplib 先过基础 HTTP route/static/binary/slow-client/优雅退出，再验证 SSE/WebSocket/TLS 的固定任务/队列、heartbeat、断线、上传/下载和长期 soak；基础 HTTP 失败时验证完整替代 Web HTTP Transport Adapter。
4. TLS handshake、证书链、hostname、密钥权限、安全随机数、系统时钟错误和证书轮换。
5. 目标文件系统 rename/fsync/目录同步、掉电、空间不足、损坏恢复、配额和 Flash 写放大。
6. 最大 Sweep、完整 S 矩阵、校准中间量、派生 Trace、Marker/Limit、历史快照和多客户端下载时的 RAM 峰值。
7. PREEMPT 调度、worker 优先级/栈、优先级反转、长 syscall 和 Acquisition→Processing 延迟；不得把上层测试结果宣传成硬实时保证。
8. 底软接口：端口/源/接收机/route/trigger、a/b buffer ownership、时间戳、chunk/complete、cancel、quality、telemetry、self-test、断线/reinitialize。

## 9. 与现有对齐矩阵的覆盖索引

下表逐行镜像 [03 控制、文件与平台对齐矩阵](../design/alignment/03-control-files-platform.md) 的 64 个正式 ID。`研究主题` 指向本文前述证据/设计主题；`当前状态` 只复制正式矩阵，不在研究文档中另造第二套状态。SCPI 主题与正式 ID 同名，但分别位于本文研究表和对齐矩阵两个语境。

| 正式 ID | 研究主题 | 边界摘要 | 当前状态 |
|---|---|---|---|
| WEB-01 | WEB-E01 | 厂商只证明 GUI/远程能力同源；完整 Web 页面范围是已冻结的产品基线。 | 已明确 |
| WEB-02 | WEB-E01 | Web/SCPI 共用 Kernel、Command、Query、Operation 和 Snapshot；这是 E3 一致性边界。 | 已由证据定案 |
| WEB-03 | WEB-E02 | 厂商 Web/LXI 不证明项目快照协议；同一内部 Catalog cut 的业务 Snapshot 与不透明 Watch token 是 E3 设计，内部 revision 不序列化。 | 已由证据定案 |
| WEB-04 | WEB-E02 | 内部 sequence、gap 检测与 resync 是 E3 可靠性协议；Web 只回传不透明 token，access-set 任一升降都关闭旧 Watch 并要求重新鉴权/取快照。 | 已由证据定案 |
| WEB-05 | WEB-E03, CAP-05 | Preview 仅是可丢的临时视图；具体传输、吞吐和长稳必须在目标机 E4 验证。 | 待平台验证 |
| WEB-06 | WEB-E03 | B 层 Measurement 与逐 Trace 的 C 层发布分离；失败隔离和原子代次是 E3 正式边界。 | 已由证据定案 |
| WEB-07 | WEB-E04, CAP-05 | binary endpoint 与 snapshot pin 是 E3；dtype、限额和目标吞吐留 E4。 | 待平台验证 |
| WEB-08 | WEB-E05 | 复合 Patch 按明确字段/行范围整体校验并用内部 revision 原子提交，外部不见半套配置或版本字段。 | 已由证据定案 |
| WEB-09 | WEB-E05 | Web/SCPI 都不使用 expected revision；普通配置按 Control Executor 接受顺序线性化，同字段以后接受且成功提交者生效，普通编辑无 lease，长 Operation 保留 lease。 | 已明确 |
| WEB-10 | WEB-E06 | 长操作统一进入 Operation Catalog，HTTP 生命周期不拥有共享操作。 | 已由证据定案 |
| WEB-11 | WEB-E07 | 主机构建静态 bundle、目标机托管及升级回滚需 E4 平台闭环。 | 待平台验证 |
| WEB-12 | WEB-E01, WEB-E07 | 浏览器只交互/渲染，正式测量与分析在共享 C++ 核心，属于 E3 职责边界。 | 已由证据定案 |
| SCPI-01 | SCPI-01 | 三家公开 raw TCP SCPI；本项目端口策略和目标 socket soak 另行验证。 | 已明确 |
| SCPI-02 | SCPI-02 | 厂商命令树和副作用不同，必须先冻结首个型号/固件兼容 Profile。 | 待兼容目标 |
| SCPI-03 | SCPI-03 | SCPI 语法有 E1 依据；独立 lexer/parser 与类型化映射是 E3 实现边界。 | 已由证据定案 |
| SCPI-04 | SCPI-04 | 无跨厂商 per-connection selection 共识，scope 必须由目标方言回归确定。 | 待兼容目标 |
| SCPI-05 | SCPI-05 | sequential/overlapped 行为存在，但 INIT 后查询等精确顺序由目标方言定案。 | 待兼容目标 |
| SCPI-06 | SCPI-06 | 未读 response 的覆盖/拒绝规则和 output queue 深度必须跟随目标方言。 | 待兼容目标 |
| SCPI-07 | SCPI-07 | Error Queue 基线明确；overflow、`*CLS` 和全局故障映射需兼容回归。 | 待兼容目标 |
| SCPI-08 | SCPI-08 | IEEE 公共命令存在，`*RST` 保留矩阵与 `*TST?` scope 不能跨厂商臆测。 | 待兼容目标 |
| SCPI-09 | SCPI-09 | per-session fence 是 E3；`*OPC/*WAI` 捕获范围与 timeout 由目标方言冻结。 | 待兼容目标 |
| SCPI-10 | SCPI-10 | 状态寄存器分层有官方依据，精确 bit map、锁存和读清副作用需兼容回归。 | 待兼容目标 |
| SCPI-11 | SCPI-11 | Sweep/Abort 能力通用，命令范围、trigger scope 和发布阶段取消由 Profile 决定。 | 待兼容目标 |
| SCPI-12 | SCPI-12 | 数据阶段通用；非 C stage 用独立 Materialize Operation/Snapshot，formatted Trace 才进入 C；首发公开清单仍需目标方言。 | 待兼容目标 |
| SCPI-13 | SCPI-13 | ASCII/binary block 有官方依据；dtype、endian、上限和跨架构发送留 E4。 | 待平台验证 |
| SCPI-14 | SCPI-14 | query 接受时固定目标和 completed snapshot，是 E3 防撕裂语义。 | 已由证据定案 |
| SCPI-15 | SCPI-15 | 断线释放会话资源但不任意终止共享 Operation，是 E3 生命周期规则。 | 已由证据定案 |
| SCPI-16 | SCPI-16 | RawSocket 的 SRQ/control-channel 能力不一致，是否兼容副通道必须显式选择。 | 待兼容目标 |
| SCPI-17 | SCPI-17 | 产品/固件版本和实际能力值同源暴露；内部 Profile、Product 与 Board capability revision 只驱动命令显示和拒绝，不进入 SCPI 响应。 | 已由证据定案 |
| FILE-01 | STA-01, STA-02, STA-03, STA-04 | inclusion、CalSet 引用、scope、Trace/Memory 恢复与默认 HoldSafeOff 共同约束 State manifest/RecallActivationPolicy。 | 已由证据定案 |
| FILE-02 | STA-03, STA-04, STA-05 | Recall scope 与 Hold 有厂商方向性证据；staging 后单 revision commit、显式 run 另起完整 admission Operation 是 E3。 | 已由证据定案 |
| FILE-03 | STA-06, SEC-E05 | Preset/`*RST`/FactoryReset 无统一保留矩阵，必须随兼容目标冻结。 | 待兼容目标 |
| FILE-04 | FIL-01, FIL-02 | Touchstone 是成熟交换面；B/ProcessedNetwork stage 与 ExecutionContext/lease 是 E3；不完整矩阵默认拒绝，legacy 补零不得静默。 | 已由证据定案 |
| FILE-05 | FIL-03 | CSV/ASCII trace export 有官方依据；B/stage/C `data_stage`、代次 ID、全分辨率 X、ExecutionContext/lease、metadata 和单位是项目契约。 | 已由证据定案 |
| FILE-06 | STA-02, FIL-04 | CalSet 引用/内嵌分型，领域对象各用独立 versioned codec。 | 已由证据定案 |
| FILE-07 | STA-05, CAP-03 | 原子持久化是 E3 方案，rename/fsync/掉电/Flash 行为必须目标机 E4 验证。 | 待平台验证 |
| FILE-08 | FIL-05 | 任意厂商路径命令不授权暴露系统路径；虚拟根与 traversal 防护需平台验证。 | 待平台验证 |
| FILE-09 | STA-05, FIL-06 | migration chain、未知新版本拒绝和保留原包是 E3 schema 规则。 | 已由证据定案 |
| FILE-10 | FIL-07 | 图片/报告有商用品方向性证据；模板、PDF、签名与字体属于产品范围选择。 | 待产品确认 |
| DIAG-01 | DIA-01, CAP-04 | versioned capability 形态含 Clock/Coherence/timebase/trigger/skew；未知相干能力不得合成跨板结果，真实字段、状态和 ABI 等待底软确认。 | 待底软/硬件确认 |
| DIAG-02 | DIA-02 | quick test 必须只报告真实可检测项；范围和终态依赖底软。 | 待底软/硬件确认 |
| DIAG-03 | DIA-03 | deep test 的独占、ExecutionContext/取消与独立 RF SafetyLane 边界已定，真实能力与时限依赖硬件。 | 待底软/硬件确认 |
| DIAG-04 | DIA-04 | Health Aggregator 分离硬件 telemetry 与上层指标；传感器/quality 字段待板卡确认。 | 待底软/硬件确认 |
| DIAG-05 | DIA-05, CAP-03 | 结构化分层已定；配额、可信时钟、Flash 写放大和掉电行为需 E4。 | 待平台验证 |
| DIAG-06 | DIA-06 | 诊断包作为有配额、脱敏、持输入 lease 且受 ExecutionContext/Drain 管理的 Operation，属于 E3 运维边界。 | 已由证据定案 |
| DIAG-07 | DIA-03, DIA-06, CAP-04 | quarantine/reinitialize 与独立 SafetyLane/physical kill 规则已定，真实故障和取消契约待底软验证。 | 待底软/硬件确认 |
| DIAG-08 | DIA-06 | actor/session/command/revision/result 审计链是 E3 控制一致性要求。 | 已由证据定案 |
| SEC-01 | SEC-E01, SEC-E05 | 厂商证明账号和远程风险面；账号来源、角色深度与锁定策略是产品选择。 | 待产品确认 |
| SEC-02 | SEC-E02 | Web 安全中间件不是 cpp-httplib 自动保证，需目标渗透与配置验证。 | 待平台验证 |
| SEC-03 | SEC-E03 | raw SCPI 无统一认证；管理网、allowlist 或额外认证由产品/部署选择。 | 待产品确认 |
| SEC-04 | SEC-E04, DEP-04 | TLS 终止边界已列出，backend、CA、RNG、时钟和轮换必须 E4。 | 待平台验证 |
| SEC-05 | SEC-E02, CAP-05 | 输入硬上限是架构要求，HTTP/SCPI/文件 fuzz 与目标压力测试负责闭环。 | 待平台验证 |
| PLAT-01 | BLD-01, CAP-01 | C++17 目标明确，但 SDK 标准库、异常/RTTI 和 ABI 必须实际编译验证。 | 待平台验证 |
| PLAT-02 | BLD-01 | MinGW-w64 仅主机 Mock/test、公司 AArch64 SDK 生产构建已明确。 | 已明确 |
| PLAT-03 | BLD-02, CAP-01 | PREEMPT 不等于 PREEMPT_RT；上层实时边界和调度延迟需目标机实测。 | 待平台验证 |
| PLAT-04 | DEP-02 | Eigen 仅限 compute implementation，精确版本、宏、数值与内存留 E4。 | 待平台验证 |
| PLAT-05 | DEP-01, DEP-04, CAP-05 | cpp-httplib 仅是候选；上游不支持/不测试 MinGW，精确 pinned MinGW 与目标 SDK 必须分别验证基础 HTTP 和实时/TLS 能力，基础 HTTP 失败则替换整个 Adapter。 | 待平台验证 |
| PLAT-06 | DEP-03 | JSON 候选的异常、内存、深度、许可证和目标运行均未完成准入。 | 待平台验证 |
| PLAT-07 | DEP-01, CAP-05 | WebSocket/SSE 只是基础 HTTP 通过后的可替换实时 Transport；基础 HTTP 失败必须整体替换 Adapter，最终选择依赖双工具链与目标长稳。 | 待平台验证 |
| PLAT-08 | BLD-03 | 固定 worker、每板 SafetyLane、有界 queue、统一 ExecutionContext/Drain 是 E3 方案，线程数、优先级、kill/readback 和退出时限需 E4。 | 待平台验证 |
| PLAT-09 | CAP-02, CAP-05 | reservation、BufferPool、Ready `ResultPinLease`、Reading ReaderLease 与失败/TTL/cancel 释放规则已定，RAM 峰值和 pin 上限不得猜测。 | 待平台验证 |
| PLAT-10 | CAP-02, CAP-04, CAP-05 | 容量由真实板能力、RAM/吞吐与多客户端组合测试后冻结。 | 待平台验证 |
| PLAT-11 | DEP-01, DEP-02, DEP-03, DEP-04 | 每个依赖都要过 compile/link/target-run/内存/许可证/维护准入。 | 待平台验证 |
| PLAT-12 | BLD-04 | 静态组合边界已定，Core/Pro/HW 组合与授权方式仍是产品选择。 | 待产品确认 |

## 10. 一手资料索引

### Keysight

- [PNA Socket Client](https://helpfiles.keysight.com/csg/N52xxB/Programming/GPIB_Example_Programs/Socket_Client.htm)
- [Referring to Traces, Measurements, Channels, and Windows Using SCPI](https://helpfiles.keysight.com/csg/N52xxA/Programming/Learning_about_GPIB/Referring_to_Traces_Measurements_Channels_Windows_Using_SCPI.htm)
- [Understanding Command Synchronization](https://helpfiles.keysight.com/csg/NA520xA/Programming/Learning_about_GPIB/Understanding_Command_Synchronization.htm)
- [Status Commands](https://helpfiles.keysight.com/csg/N52xxA/Programming/GP-IB_Command_Finder/Status.htm)
- [Format SCPI](https://helpfiles.keysight.com/csg/N52xxA/Programming/GP-IB_Command_Finder/Format_SCPI.htm)
- [Getting and Putting Data](https://helpfiles.keysight.com/csg/N52xxA/Programming/GPIB_Example_Programs/Getting_and_Putting_Data.htm)
- [Save Method](https://helpfiles.keysight.com/csg/NA520xA/Programming/COM_Reference/Methods/Save_Method.htm)
- [ENA Saving and Recalling File](https://helpfiles.keysight.com/csg/e5072a/programming/remote_control/saving_and_recalling/saving_and_recalling_file.htm)
- [About Error Messages](https://helpfiles.keysight.com/csg/N52xxA/Support/About_Error_Messages.htm)
- [LXI Compliance](https://helpfiles.keysight.com/csg/NA520xA/S0_Start/LXI_Compliance.htm)
- [Product and Solution Cybersecurity](https://helpfiles.keysight.com/csg/N52xxB/Product_and_Solution_Cybersecurity.htm)

### Rohde & Schwarz

- [VISA and tools: VXI-11, HiSLIP and RawSocket](https://www.rohde-schwarz.com/nl/driver-pages/remote-control/3-visa-and-tools_231388.html)
- [Measurement Synchronization](https://www.rohde-schwarz.com/us/driver-pages/remote-control/measurements-synchronization_231248.html)
- [Instrument Error Checking](https://www.rohde-schwarz.com/us/driver-pages/remote-control/instrument-error-checking_231244.html)
- [R&S ZNA User Manual v39](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_6462_01/ZNA_UserManual_en_39.pdf)
- [R&S ZNA Getting Started v25](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/getting_started/1178_6456_01/ZNA_GettingStarted_en_25.pdf)：§3.9 p.20（用户账户与防火墙）、§7.2 p.72-76（LAN、远程桌面与防火墙）。这些只证明成熟仪器会管理远程攻击面，不证明本项目应照搬 Windows 服务或安全内部实现。
- [R&S ZNL/ZNLE User Manual](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1178_5966_01/ZNL_ZNLE_UserManual_en_22.pdf)（仅作 R&S 同系 State scope 补证，不替代 ZNA 固件验证。）
- [R&S ZNB/ZNBT/ZNC/ZND Instrument Security Manuals](https://www.rohde-schwarz.com/manual/r-s-znb-znbt-znc-znd-instrument-security-manuals_78701-196161.html)

### Copper Mountain Technologies

- [CMT Programming Manual](https://coppermountaintech.com/help-cmtvna/Programming-Manual/index.html)
- [Programming over HiSLIP or TCP/IP Socket](https://coppermountaintech.com/help-cmtvna/Programming-Manual/programming.html)
- [Connection Setup](https://coppermountaintech.com/help-cmtvna/Programming-Manual/connection-setup.html)
- [`*OPC?`](https://coppermountaintech.com/help-cmtvna/Programming-Manual/opc_question.html)
- [`SYST:ERR?`](https://coppermountaintech.com/help-r/systerr_.html)
- [Internal Data Arrays](https://coppermountaintech.com/help-cmtvna/Programming-Manual/internal-data-arrays.html)
- [Analyzer State](https://coppermountaintech.com/help-cmtvna/1-port/analyzer-state.html)
- [MMEMory command tree](https://coppermountaintech.com/help-cmtvna/Programming-Manual/mmemory.html)
- [Trace Data Touchstone File](https://coppermountaintech.com/help-cmtvna/TR-Series/trace-data-touchstone-file.html)
- [Trace Data CSV File](https://coppermountaintech.com/help-r/trace-data-csv-file.html)
- [System Ready](https://coppermountaintech.com/help-cmtvna/Programming-Manual/systready_.html)

### 第三方库官方资料

- [cpp-httplib official repository](https://github.com/yhirose/cpp-httplib)
- [cpp-httplib official documentation](https://yhirose.github.io/cpp-httplib/en/)
- [Eigen 3.4 Getting Started](https://libeigen.gitlab.io/eigen/docs-3.4/GettingStarted.html)
- [Eigen 3.4 and multi-threading](https://libeigen.gitlab.io/eigen/docs-3.4/TopicMultiThreading.html)
- [Eigen 3.4 preprocessor directives](https://libeigen.gitlab.io/eigen/docs-3.4/TopicPreprocessorDirectives.html)
- [nlohmann/json integration](https://json.nlohmann.me/integration/index.html)
- [nlohmann/json exceptions](https://json.nlohmann.me/home/exceptions/)
- [nlohmann/json SAX interface](https://json.nlohmann.me/features/parsing/sax_interface/)
