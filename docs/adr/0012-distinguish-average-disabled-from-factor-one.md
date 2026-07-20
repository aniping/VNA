# 区分 Average Disabled 与 factor=1

> 状态：已接受

平均开关使用 `AverageApplication = Disabled | Enabled`，只有 Enabled 分支才携带 `AverageMode`、policy、generation、count、complete、`AverageContributionRef` 和 accumulator；Disabled 的每个 B 只引用当前 Logical Sweep，不创建任何平均状态。即使 `FiniteBatch(factor=1)` 的数值可能与关闭平均相同，两者的 clear、计数、触发、完成 fence、Web/SCPI 查询和 provenance 语义不同，因此禁止用 factor=1 代替 Disabled。本次普通 A→B 纵切只执行 Disabled 分支，但从首个 B schema 起保留这一类型边界。
