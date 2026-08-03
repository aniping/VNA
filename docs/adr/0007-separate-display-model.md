# 在 Scale 和 Marker 前拆分显示模型

## 决策

在实现 Scale 或 Marker 前建立 `core/display/model` 模块。Window、Trace、
TraceFormat、Scale、Marker 和显示布局属于该模块；Channel、Measurement、
SweepSettings 和采集需求继续属于 `core/instrument`。

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

## 显示比例初始策略

第一条 Scale 垂直切片只允许 LogMagnitude Trace 修改 Scale/Div。

ZNA User Manual v41 第 1406 页示例在 `*RST` 后查询
`DISPlay[:WINDow<Wnd>]:TRACe<WndTr>:Y[:SCALe]:PDIVision?`，返回 `10`；
查询 `DISPlay[:WINDow<Wnd>]:TRACe<WndTr>:Y[:SCALe]:RLEVel?`，返回 `0`。

当前产品的系统 Preset 对齐 ZNB User Manual v74：单个 Diagram 中建立
Trc1/S21/dB Mag，使用 Scale/Div `10 dB`、Ref Value `0 dB`、Ref Pos `9`，
派生 Min `-90 dB`、Max `10 dB`。这是 LogMagnitude 的唯一默认比例，普通
`createTrace` 与跨格式恢复均复用它；重复设置相同格式不重置当前比例。

显示模型只保存 Scale/Div、Ref Value 和 Ref Pos 三项主值。Min、Max 和单位
均在生成快照时派生，不作为可独立修改的状态。更新 Scale/Div 时，输入及派生
出的 Min、Max 都必须是有限数值，否则命令失败且状态不变。

Phase 的默认数值尚无官方手册或实机依据，Smith 的圆图 Ref Value 也未进入
当前切片，因此两种格式暂不提供 Scale 状态。这表示当前能力未落地，不表示
商用仪表不支持。实际格式切换到 LogMagnitude 时重新使用上述官方默认值；
重复设置同一格式不重置当前 Scale，跨格式历史不保留。

本切片不包含 Auto Scale、Marker、Scale Coupling、Numeric Editor 或真实
测量数据。

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
