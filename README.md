# Vector Network Analyzer

面向商用级矢量网络分析仪（VNA）的绿地软件项目。项目从统一业务内核出发，同时支持本地与远程访问、真实硬件与仿真后端，以及可按需启用的诊断能力。

当前已完成第一阶段的领域骨架、统一命令入口、最小仪表服务，以及连接真实
服务状态的 ZNB 单屏前端。

## 文档

- [总体软件架构](docs/architecture.md)
- [领域语言](CONTEXT.md)
- [第一阶段实施范围](docs/phase-1.md)
- [平台支持矩阵](docs/support-matrix.md)
- [ZNB 单屏界面复刻基线](docs/ui-zna26-reference.md)
- [架构决策记录](docs/adr/)

## 当前基线

- 前端：Vue 3 + TypeScript
- 后端核心：C++20 模块化单体
- 目标平台：Windows 与 Linux 原生构建和运行
- 编译器：Windows/MinGW GCC 与 Linux/GCC
- HTTP/WebSocket：`yhirose/cpp-httplib`
- 外部交互：REST、WebSocket、SCPI、HTTP 文件流
- 测量后端：Simulation、Replay、Hardware、Proxy 可替换
- 核心模型：Instrument、Channel、Measurement、Trace、Window
- 核心流程：命令控制流（控制面）与测量数据流（数据面）

第一阶段先完成基于仿真后端的端到端闭环，真实硬件在业务内核稳定后接入。

## 三方依赖

C++ 三方源码统一放在 `third-part/`，并通过 Git submodule 固定版本。克隆后初始化依赖：

```powershell
git submodule update --init --recursive
```

当前固定版本：

- GoogleTest `v1.17.0`
- cpp-httplib `v0.51.0`
- JSON for Modern C++ `v3.12.0`

前端包通过 `frontend/pnpm-lock.yaml` 固定精确版本，安装产物位于被忽略的
`frontend/node_modules/`，不提交到仓库。

## 构建与测试

需要 CMake、Ninja 和 GCC。Windows 使用 MinGW GCC，Linux 使用系统 GCC；
项目不支持 MSVC。推荐的开发与测试 preset 还需要 `ccache` 位于 `PATH`。
首次构建先初始化第三方依赖：

```powershell
git submodule update --init --recursive
```

日常开发只构建 `vna-server`：

```powershell
cmake --preset dev
cmake --build --preset dev
```

`dev` 使用 `out/dev/`、12 个并行任务、ccache 和 `-O0 -g1`，并关闭测试目标；
它保留行号级调试信息，但不包含完整的变量调试信息。提交前执行完整测试构建：

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

如果没有 ccache，或需要完整 `-g` 调试信息，仍可使用原始命令：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

在 Windows 上执行前，请确认 `g++` 和 `ninja` 来自同一套 MinGW 工具链。

`build/`、`out/` 和 `frontend/dist/` 只是构建中间产物。组装包含服务端、前端静态文件、
日志目录和运行库的便携发布目录前，还需要 Node.js 20.19 或更高版本、pnpm
11.9.0，并在首次打包或锁文件变化后安装前端依赖：

```powershell
pnpm --dir frontend install --frozen-lockfile
```

随后执行显式发布 preset：

```powershell
cmake --preset release
cmake --build --preset release
```

该显式目标成功后生成 `release/VectorNetworkAnalyzer/`；普通 CMake 构建和
`pnpm run build` 都不会创建或替换发布目录。Windows/MinGW 从仓库根运行：

```powershell
.\release\VectorNetworkAnalyzer\start.cmd
```

Linux/GCC 使用相同目录结构，以下命令同样从仓库根运行：

```bash
./release/VectorNetworkAnalyzer/start.sh
```

也可以在任意工作目录使用 `start.cmd` 或 `start.sh` 的绝对路径。服务从自身
可执行文件位置定位相邻的 `web/` 与 `logs/`，不依赖当前工作目录；
浏览器打开 `http://127.0.0.1:8080/`。服务仅监听 `127.0.0.1:8080`，可通过
`/api/v1/health`、`/api/v1/state`
和 `/api/v1/commands` 验证当前 HTTP 切片。
启动脚本只向控制台输出启动状态、Web URL 和日志文件位置；结构化
`server.lifecycle` JSON Lines 只写入 `logs/vna.log.jsonl`。服务非零退出时，
脚本额外输出一行人类错误提示并保留原始退出码，不自动打开浏览器。
`/api/v1/commands` 的失败响应保留 `status` 与 `stateRevision`，并提供稳定的
`errorCode` 供客户端区分具体错误。
`commandId`、`sessionId` 和 `instrumentId` 必须为 1..128 bytes，且不得包含
ASCII 控制字节 `00..1F` 或 `7F`；非法 ID 返回 `400 invalidCommand`。
对进入幂等窗口且仍被保留的确定性命令结果，相同
`(instrumentId, sessionId, commandId)` 与相同命令内容会重放首次完整响应；
同一已保留键复用于不同内容时返回 `409 conflict` 和 `command-id-reuse`。
幂等窗口在当前进程中默认保留 1024 条确定结果；淘汰后旧键按届时状态重新
处理，不再承诺重放，应用层统计可观察条目数与淘汰数。

当前可用 `updateTraceScalePerDivision` 命令更新 Log Magnitude Trace 的
Scale/Div，payload 为 `{"traceId": <id>, "scalePerDivision": <number>}`。
`/api/v1/state` 会在每条 Trace 上返回 `scale`；Log Magnitude 包含完整 dB
显示比例快照，尚未开放该能力的 Phase 与 Smith 返回 `null`。
当前发布版启动后由后台 `ContinuousAcquisition` 自动、持续采集，并以约 10 Hz
的模拟节奏更新默认 S21 显示帧。生产组合不再启动第二个单扫 worker；遗留的
`startSingleSweep` 当前返回 `409 conflict` 和 `resource-busy`。
默认开路仿真的 S21 是仪器内部耦合泄漏叠加确定性接收机噪声，不代表 DUT 直通。
`GET /api/v1/traces/<traceId>/display-frame` 返回最新完整 Log Magnitude dB
帧；Trace 存在但尚无可用帧时返回空的 `204`，Trace 不存在时返回 `404`。该
接口用于读取最新完整帧，不作为连续曲线高频轮询通道。

`WS /api/v1/display-frames` 是连续显示通道，固定推送 FactoryPreset 的唯一默认
Trace。每次连接（包括重连）都从新的 sequence baseline 开始；若已有 retained
latest 帧会立即发送。之后采用 latest-only 语义，慢客户端可以跳过中间帧但不会
倒退 sequence，服务端不保存每连接历史队列。客户端发来的 WebSocket 消息不作为
业务命令；连续曲线必须使用该推送通道，不得轮询上面的 REST 诊断接口。

## 前端开发

需要 Node.js 20.19 或更高版本和 pnpm 11.9.0。保持 `vna-server` 运行，另开
一个终端启动单窗口前端：

```powershell
cd frontend
pnpm install --frozen-lockfile
pnpm run dev
```

浏览器打开 `http://127.0.0.1:5173/`。开发服务器会把 `/api` 请求代理到
`http://127.0.0.1:8080`，因此页面显示的是本地仪表服务的真实状态。

当前单窗口界面只渲染服务快照中真实存在的 Window：一个 Window 对应一个主图，
多个 Window 也不会补齐不存在的空白 Diagram。Softtool 默认关闭，点击 `Meas`
可打开或重新关闭 Measurement Softtool。Meas 只呈现活动 Trace 的 S 参数测量量；
修改测量量的命令契约尚未开放，因此相关入口保持禁用，不创建本地 Trace 或 Window。
默认 S21 Trace 的色块和曲线为绿色；活动 Trace 信息条、活动 Channel 和右上角
真实 WindowId 使用蓝/青蓝色高亮，所有 Diagram 外框保持统一细深灰蓝（手册
第 112、127–129、935–936 页，ZNA v41 补充）。尚无测量帧时 Diagram 显示空态；后台连续采集
会自动更新显示帧，Toolbar 的
`Restart Sweep` 在当前发布版中暂不可用并按禁用处理。
Phase/Smith 当前暂无首版显示帧，页面保留网格并显示 `NO DISPLAY DATA`，Format
仍可切回 LogMagnitude；切回后会建立新的实时显示连接，不生成伪造数据。
`Maximize Diagram` 可切换活动 Diagram 的最大化状态；其余尚未实现的 Toolbar
项保持禁用。
底部 `File` 至 `Help` 菜单尚未接通并保持禁用；连接、revision 和实体计数仍
显示本地仪表服务的真实状态。

`Start`、`Stop`、`Center` 和 `Span` Hard Key 已连接到真实 Channel 状态。修改
Center 时保持当前 Span，修改 Span 时保持当前 Center，并通过 revision 冲突检查
提交完整扫频设置。

`Power / Bw / Avg` 当前提供 Power 与 IF Bandwidth 设置，`Sweep` 提供 Points
设置；尚未进入领域模型的 Averaging 不显示伪造状态。这些设置与 Stimulus 共用
同一个 Channel 更新命令。

活动 Trace 的 `scale` 快照非空时，`Scale` Hard Key 会打开 Scale Values
Softtool。当前仅 Scale/Div 可编辑：输入有限且大于零的数值后按 Enter 提交，
失焦或按 Escape 只恢复服务快照，不发送命令。其余 Scale 控件保持禁用；笛卡尔
Diagram 的上下界只显示服务返回的 maximum/minimum，Scale 不可用时显示 `—`。

执行前端 Node 测试、类型检查和生产构建：

```powershell
cd frontend
pnpm test
pnpm run build
```
