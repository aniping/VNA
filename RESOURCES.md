# VNA 实时采集链路资源

## Knowledge

- [服务组合根](apps/vna-server/main.cpp)
  当前生产对象如何组装、启动和按依赖顺序销毁的第一事实来源。
- [连续采集契约](core/acquisition/include/vna/acquisition/continuous_acquisition.hpp) 与 [实现](core/acquisition/src/continuous_acquisition.cpp)
  用于核对唯一采集 worker、latest-only 原始帧槽、停止与失败语义。
- [原始接收机帧契约](contracts/frames/include/vna/frames/raw_receiver.hpp)
  用于理解 source port、reference、responses，以及为何原始层没有 Trace 身份。
- [当前连续 Trace 发布器](core/application/src/continuous_trace_publisher.cpp)
  用于追踪当前 S21 → Log Magnitude → 单 Trace 显示帧路径。
- [多端口连续显示 ADR](docs/adr/0009-multiport-continuous-display.md)
  用于区分目标架构与当前已接通行为，尤其是 generation 与原子 frame set。
- [发布版行为说明](README.md)
  用于核对约 10 Hz、单默认 Trace、latest-only WebSocket 等用户可见承诺。

## Wisdom (Communities)

- 当前主题以仓库代码、测试和 ADR 为权威；遇到仪器物理语义争议时，再补充仪器手册或 RF 测量工程师复核。
