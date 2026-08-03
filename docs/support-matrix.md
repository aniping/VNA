# 平台支持矩阵

> 状态：第一阶段工程基线
>
> 日期：2026-07-31

## 支持范围

| 平台 | CPU | 编译器 | 构建工具 | 验证方式 |
| --- | --- | --- | --- | --- |
| Windows 11 | x86-64 | MinGW-W64 GCC 10.3 或更新版本 | CMake 3.25+、Ninja | 本地 GCC 10.3 验证；Windows CI 使用 MSYS2 MINGW64 当前稳定 GCC |
| Ubuntu 22.04 LTS | x86-64 | GCC 11 或更新版本 | CMake 3.25+、Ninja | Linux CI configure、build、test |

项目不配置或支持 MSVC、Clang、WSL、Wine、32 位平台和交叉编译。其他 Linux
发行版可自行构建，但在加入 CI 前不属于承诺的支持范围。

## CI 必须执行

两个平台都必须执行同一组工程入口：

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

流水线必须从仓库内 `third-part/archives/` 离线解压固定依赖，并输出实际 GCC、
CMake 和 Ninja 版本，便于失败时还原工具链基线。构建不得下载三方源码。

## 协议兼容性门槛

- 本地网页壳：HTTP 与 WebSocket 冒烟测试必须在两个平台通过。
- HTTPS、WSS 或远程监听：启用前必须增加并通过两个平台的 TLS 冒烟测试。
- TLS 尚未通过前，服务只能监听回环地址，不得作为远程访问入口。
