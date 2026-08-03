# 平台支持矩阵

> 状态：第一阶段工程基线
>
> 日期：2026-07-31

## 支持范围

| 平台 | CPU | 编译器 | 构建工具 | 验证方式 |
| --- | --- | --- | --- | --- |
| Windows 11 | x86-64 | MinGW-W64 GCC 10.3 或更新版本 | CMake 3.25+、Ninja | 本地 GCC 10.3 验证；Windows CI 使用 MSYS2 MINGW64 当前稳定 GCC |
| Linux | x86-64 | GCC 7.3 或更新版本 | CMake 3.25+、Ninja | GCC 7.3 Release 构建与聚焦测试；Linux CI configure、build、test |

项目不配置或支持 MSVC、Clang、WSL、Wine、32 位平台和交叉编译。Linux 发行版
必须提供满足上表版本下限的 GCC、CMake 与 Ninja。

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
- HTTP/WebSocket 监听所有 IPv4 接口，局域网客户端必须使用服务器实际 IP，不能使用 `0.0.0.0`。
- 当前入口仅适用于可信局域网；公网或不可信网络部署前必须增加 TLS、认证及双平台安全测试。
