# Vector Network Analyzer

面向商用级矢量网络分析仪（VNA）的绿地软件项目。项目从统一业务内核出发，同时支持本地与远程访问、真实硬件与仿真后端，以及可按需启用的诊断能力。

当前已完成第一阶段的领域骨架、统一命令入口、最小仪表服务，以及连接真实
服务状态的 ZNA 单窗口前端。

## 文档

- [总体软件架构](docs/architecture.md)
- [领域语言](CONTEXT.md)
- [第一阶段实施范围](docs/phase-1.md)
- [平台支持矩阵](docs/support-matrix.md)
- [ZNA26 界面复刻基线](docs/ui-zna26-reference.md)
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
项目不支持 MSVC。

```powershell
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

在 Windows 上执行前，请确认 `g++` 和 `ninja` 来自同一套 MinGW 工具链。

启动本地服务。Windows/MinGW：

```powershell
.\build\apps\vna-server\vna-server.exe
```

Linux/GCC：

```bash
./build/apps/vna-server/vna-server
```

服务仅监听 `127.0.0.1:8080`。可通过 `/api/v1/health`、`/api/v1/state`
和 `/api/v1/commands` 验证当前 HTTP 切片。

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

当前单窗口界面默认使用 2×2 测量图区，右侧依次为 Measurement Softtool 和
两列虚拟 Hard Key。点击 `Meas` 可关闭或重新打开 Softtool；已有 Channel 时可在
S 参数区创建 Trace。采样后端接通前只显示坐标网格，不绘制静态测量曲线。

执行前端类型检查和生产构建：

```powershell
cd frontend
pnpm run build
```
