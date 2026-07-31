# 在 Scale 和 Marker 前拆分显示模型

## 决策

在实现 Scale 或 Marker 前建立 `core/display-model` 模块。Window、Trace、
TraceFormat、Scale、Marker 和显示布局属于该模块；Channel、Measurement、
SweepSettings 和采集需求继续属于 `core/domain`。

显示模型可以通过公开标识引用 Measurement，但测量领域不得依赖显示模型。
Trace 的增加、删除或格式变化不得直接启动采集，也不得改变 Measurement
Planner 计算出的激励次数。

## 事务与引用

应用层组合测量状态与 DisplayWorkspace，并在同一个控制事务中校验跨模块
引用。创建 Trace 时，应用层先确认 Measurement 存在，再调用显示模型的
interface；显示模型自己维护 Window 和 Trace 内部不变量。

两个模块继续共享同一个 Instrument `stateRevision`。显示配置变化会递增
revision，但采集计划只根据 Channel 和 Measurement 变化决定是否重编译。
完成帧仍携带产生它的 revision，避免旧数据伪装成新显示配置的结果。

## 对外契约

拆分不得迫使 Web 或前端立即改变 StateSnapshot 的外部 JSON 形状。协议
适配器可以继续编码当前扁平快照；内部快照由应用层组合，不向协议暴露模块
实现细节。

模块公开 interface 位于各自 include 根部。测试通过领域和显示模型的正式
interface 验证，不跨模块访问内部容器。

## 结果

该决策增加一次早期移动和应用层编排，但能在 Scale、Marker、Limit、Math
和多 Window 扩展前阻止单个 Instrument 类同时承担测量与显示职责。它不改变
模块化单体部署，也不允许显示模型成为第二份仪表业务真值。
