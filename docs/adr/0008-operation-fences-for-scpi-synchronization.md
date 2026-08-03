# 以 Operation 完成栅栏实现 SCPI 同步

## 决策

所有长时间业务动作由业务核心中的 OperationManager 管理。SCPI、Web 和
Backend 不得各自维护另一套“忙碌”或“完成”真值。长命令提交成功后立即返回
`operation_id`；OperationManager 负责状态转换、取消、完成通知和结果查询。

Operation 至少具有 Queued、Running、CancelRequested、Succeeded、Failed
和 Canceled 状态。Succeeded、Failed 和 Canceled 都是终态，必须保留来源
`command_id`、`session_id`、适用的 `state_revision` 以及结构化结果或错误。

## 完成定义

每种 Operation 必须在 interface 中声明自己的完成条件。一次 Sweep 只有在
采集结束、帧完整性校验完成、必要的数据处理完成且完整帧提交到
FrameRepository 后才进入 Succeeded。启动硬件、收到最后一个采样点或页面
绘制完成都不能单独代表 Sweep 完成。

失败和取消必须进入确定终态并唤醒等待者，不允许依赖超时推测完成。Abort
只有在 Backend 确认停止且资源释放后才能进入 Canceled。

## 完成栅栏

SCPI 顺序器在同步命令处创建 CompletionFence。栅栏捕获该命令流此前命令
已经启动的有限 Operation 集合或等价的单调水位，不包含栅栏之后启动的操作，
也不因后续连续扫频而永久等待。

栅栏在捕获的全部 Operation 到达任一终态后满足。失败或取消先更新结构化错误、
错误队列和状态寄存器，再满足栅栏。等待通过通知或条件变量实现，不允许固定
sleep、轮询业务状态或在等待期间持有控制事务锁。

## 单一 SCPI 会话

每台 Instrument 同时只允许一个活动 SCPI 控制会话。该会话拥有串行输入顺序、
输出队列、错误队列、ESR、ESE、SRE、STB 和 Operation Complete Command 状态。
第二个连接返回资源忙并关闭。

断线或 Local takeover 销毁会话队列、寄存器和未兑现的同步响应，但不隐式取消
业务 Operation。重新连接创建全新的 SCPI 会话。

## `*OPC`、`*OPC?` 与 `*WAI`

- `*OPC` 创建栅栏并注册异步动作，不阻塞后续命令。栅栏满足后设置 ESR 的
  Operation Complete 位；ESE 和 SRE 再决定 ESB 与 SRQ。
- `*OPC?` 暂停该 SCPI 会话的后续命令处理。栅栏满足后把 `1` 放入输出队列并
  设置 MAV；它不以设置 ESR 的 OPC 位代替查询响应。
- `*WAI` 暂停该 SCPI 会话的后续命令处理，栅栏满足后继续且不产生响应。
- `*CLS` 清理规定的事件状态和错误，并将 Operation Complete Command 状态
  置为 Idle，使尚未兑现的 `*OPC` 动作失效；它不取消底层 Operation。

`*ESR?` 的读取清除、ESE/SRE 掩码、STB 汇总、MAV 和 SRQ 必须由同一状态寄存
器模块实现，不能散落在各命令处理函数中。

## 验证

使用可人工推进状态的受控 Backend，通过 OperationManager 和 SCPI Session
的正式 interface 验证 `INIT;*OPC`、`INIT;*OPC;*CLS`、`INIT;*OPC?`、
`INIT;*WAI`、失败、Abort、断线和 Local takeover。测试必须证明栅栏不等待
未来 Operation，且任何同步路径都不使用固定 sleep。

## 依据与结果

该语义依据 IEEE 488.2/SCPI Operation Complete 模型，以及项目本地手册
[Remote Control SCPI Getting Started v04](../ref/remote-control-scpi-getting-started-v04.pdf)
第 5.3.3 节（印刷页 29）、第 7 节（印刷页 34–36）和第 8.4 节（印刷页 38–39），以及
[ZNA User Manual v41](../ref/zna-user-manual-v41.pdf) 第 6.4.6 节（印刷页 1058–1060）和
第 8.1.1.3 节（印刷页 1951–1953）的命令同步说明。

代价是需要 OperationManager、SCPI 顺序器和状态寄存器三个明确模块；收益是
Web、SCPI、仿真和真实硬件共享同一个完成事实，且 `*OPC` 能正确覆盖成功、
失败、中止、SRQ 和清状态交互。
