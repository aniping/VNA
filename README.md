# Vector Network Analyzer

面向商用级矢量网络分析仪（VNA）的绿地软件项目。项目从统一业务内核出发，同时支持本地与远程访问、真实硬件与仿真后端，以及可按需启用的诊断能力。

当前已完成第一阶段的领域骨架、统一命令入口、最小仪表服务，以及连接真实服务状态的 ZNB 单屏前端。

## 文档

- [总体软件架构](docs/architecture.md)
- [领域语言](CONTEXT.md)
- [第一阶段实施范围](docs/phase-1.md)
- [平台支持矩阵](docs/support-matrix.md)
- [ZNB 单屏界面复刻基线](docs/ui-znb-v74-reference.md)
- [参考资料索引](docs/ref/README.md)
- [架构决策记录](docs/adr/)

## 当前基线

- 前端：Vue 3 + TypeScript
- 后端核心：C++17 模块化单体
- 目标平台：Windows 与 Linux 原生构建和运行
- 编译器：Windows/MinGW GCC 与 Linux/GCC
- HTTP/WebSocket：`yhirose/cpp-httplib`
- 外部交互：REST、WebSocket、SCPI、HTTP 文件流
- 测量后端：Simulation、Replay、Hardware、Proxy 可替换
- 核心模型：Instrument、Channel、Measurement、Trace、Window
- 核心流程：命令控制流（控制面）与测量数据流（数据面）

第一阶段先完成基于仿真后端的端到端闭环，真实硬件在业务内核稳定后接入。

## 工程目录

所有 C++ 后端代码统一位于 `vna/`，仓库根目录只保留前端、测试、构建和文档等工程级内容。当前目录结构如下：

```text
VectorNetworkAnalyzer/
├── vna/
│   ├── apps/vna-server/                    # 当前服务端入口
│   ├── core/
│   │   ├── instrument/                     # Instrument 与 Channel 领域状态
│   │   ├── control/                        # 命令、权限与 Operation
│   │   ├── sweep/                          # 扫频运行时、Preview 与发布管线
│   │   ├── acquisition/                    # 原始数据采集
│   │   ├── measurement/                    # S 参数合成
│   │   └── display/{model,projection,publication}/
│   ├── interfaces/web/                     # REST、WebSocket 与静态文件
│   ├── hardware/backends/simulation/       # 当前仿真采集后端
│   ├── infrastructure/platform/            # Windows/Linux 平台适配
│   ├── contracts/frames/                   # 帧与原始接收机数据合同
│   └── foundation/cpp-compat/              # C++17 与 GCC 7.3 兼容设施
├── frontend/                 # ZNB 风格本地 Web 前端
├── tests/                    # 单元、集成、契约和仿真测试
├── docs/                     # 产品、架构、规范和 ADR
│   └── ref/                  # 固定版本的本地参考资料及索引
├── cmake/                    # 构建辅助模块
├── packaging/                # 发布组装脚本
└── third-part/               # 固定版本的离线三方源码
```

`vna/core` 不依赖 Web 或具体硬件实现；`vna/apps` 只负责选择后端并组装进程。公开头文件仍使用 `#include <vna/...>`，目录归拢不改变 C++ namespace 或 CMake target。

## 三方依赖

C++ 三方源码以固定版本的 `.tar.xz` 归档提交在 `third-part/archives/`。首次 CMake 配置时会校验 SHA-256，并仅在源码目录不存在时解压到 `third-part/`；构建不需要 Git、网络或补丁工具。复制普通源码目录到离线机器即可构建。

当前固定版本：

- GoogleTest `v1.17.0`
- cpp-httplib `v0.51.0`
- JSON for Modern C++ `v3.12.0`
- spdlog `v1.17.0`（以静态库构建，仅用于私有运行日志实现）

前端包通过 `frontend/pnpm-lock.yaml` 固定精确版本，安装产物位于被忽略的 `frontend/node_modules/`，不提交到仓库。

## 构建

后端需要 CMake 3.25+、Ninja 与 GCC；Windows 使用 MinGW GCC，Linux 使用 GCC 7.3 或更新版本，不支持 MSVC。前端源码构建需要 Node.js 20.19+ 与 pnpm 11.9.0。首次配置会从仓库内归档解压 C++ 三方依赖，不使用 Git 或网络下载。

### 构建入口

| preset | 内容 | 输出 |
| --- | --- | --- |
| `frontend` | 仅编译并安装前端 | `release/VectorNetworkAnalyzer/web/` |
| `backend` | 仅编译并安装 C++ 后端 | `release/VectorNetworkAnalyzer/bin/` 及启动文件 |
| `test` | C++ 后端与全部 CTest 目标 | `out/test/` |
| `release` | 原子组装完整便携包 | `release/VectorNetworkAnalyzer/` |

只保留以上四个公开 preset，默认使用 8 路并行；下面显式覆盖为 24 路。`frontend`/`release` 要求 Node.js 和 pnpm，`backend`/`test` 不探测前端。

首次前端构建或锁文件变化后先安装依赖：

```powershell
pnpm --dir frontend install --frozen-lockfile
```

### 独立构建前端或后端

```powershell
cmake --preset frontend
cmake --build --preset frontend --parallel 24

cmake --preset backend
cmake --build --preset backend --parallel 24
```

`frontend` 只更新 `web/`；`backend` 只更新 `bin/`、启动脚本、说明和日志，不会触碰前端。完成后从仓库根启动：

```powershell
.\release\VectorNetworkAnalyzer\start.cmd
```

后端运行时从产品目录寻找 `web/`；缺失时退出并记录到 `logs/vna.log`。

### 测试

```powershell
cmake --preset test
cmake --build --preset test --parallel 24
ctest --preset test
```

Windows 上的 `g++` 与 `ninja` 必须来自兼容的 MinGW 工具链。QEMU 门禁见 [Linux/ARM64 QEMU 验证](docs/linux-arm64-qemu-validation.md)。

### 正式发布

```powershell
cmake --preset release
cmake --build --preset release --parallel 24
```

`release` 会先在私有暂存目录组装并验证完整 `bin/web/logs`，成功后原子替换 `release/VectorNetworkAnalyzer/`；失败不会暴露半成品。它与可增量组合的 `frontend`、`backend` 入口使用同一最终目录，但拥有更严格的完整包承诺。

Linux 完整包从仓库根运行 `./release/VectorNetworkAnalyzer/start.sh`。

### Linux GCC 7.3

```bash
export LD_LIBRARY_PATH=/opt/vna-gcc73/usr/lib/x86_64-linux-gnu
cmake -S . -B out/gcc73-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DVNA_BUILD_FRONTEND=OFF \
  -DCMAKE_C_COMPILER=/opt/vna-gcc73/usr/bin/gcc-7 \
  -DCMAKE_CXX_COMPILER=/opt/vna-gcc73/usr/bin/g++-7
cmake --build out/gcc73-release --target vna-server --parallel "$(nproc)"
```

全量测试把 `BUILD_TESTING` 改为 `ON`，随后执行：

```bash
cmake --build out/gcc73-release --parallel "$(nproc)"
ctest --test-dir out/gcc73-release --parallel "$(nproc)" --output-on-failure
```

## 运行与接口

服务监听 `0.0.0.0:8080`。本机打开 `http://127.0.0.1:8080/`，局域网设备使用
服务器实际 IPv4 地址，例如 `http://192.168.1.10:8080/`；`0.0.0.0` 只是绑定地址，
不能作为浏览器入口。当前为无 TLS、无认证的可信局域网入口，不得直接暴露到公网。
服务从自身可执行文件位置定位 `web/`，启动脚本输出本机 Web URL 和日志路径。

`/api/v1/health`、`/api/v1/state` 和 `/api/v1/commands` 分别提供健康检查、权威
快照和统一命令入口。命令保持 revision、结构化错误和 1024 条幂等窗口语义；
`startSingleSweep` 返回可查询的 `operationId`。完整接口合同见
[总体软件架构](docs/architecture.md) 和相关协议测试。

`WS /api/v1/display-frames` 推送完整 Trace 帧集，`WS /api/v1/sweep-previews`
推送 latest-only 累计前缀与权威扫频状态；两者均不反压采集。默认开路仿真的
S21 是内部耦合泄漏叠加确定性接收机噪声，不代表 DUT 直通。

## 前端开发

保持后端运行，在另一个终端启动 Vite：

```powershell
cd frontend
pnpm install --frozen-lockfile
pnpm run dev
```

打开 `http://127.0.0.1:5173/`；开发服务器把 `/api` 代理到本地后端。前端检查：

```powershell
pnpm test
pnpm run build
```

### 当前界面

当前前端按 ZNB v74 单窗口结构呈现真实 Channel、Measurement、Trace、Window、完整帧与渐进扫频数据；所有可用控件都提交服务端命令，未实现项保持隐藏或禁用。页面布局证据与已知差距见 [ZNB 单屏界面复刻基线](docs/ui-znb-v74-reference.md)，扫频语义见 [ZNB Sweep Runtime 规范](docs/znb-sweep-runtime-spec.md)。
