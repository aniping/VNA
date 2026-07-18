# 01 — 让扫描任务真正等待异步单板

**What to build:** 已经被软件接受的扫描可以启动异步工作，并在 Mock 单板尚未返回时保持运行；软件只在收到唯一可靠终态后重新开放运行容量。若工作转入善后，工作状态可以移交，但等量 lane/capacity 必须继续被善后所有权占用到其真实终态。现有立即完成的测试工作仍然可用。

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

- [x] 工作的“启动”和“完成”成为两个独立时刻；派发调用不内联启动工作，也不内联发出完成回调。
- [x] 确定性 pump 启动工作后，即使连续多次 pump 都没有终态，工作仍保持 Running，运行槽不得被复用。
- [x] 每项已接受工作在派发前取得不可丢失的完成容量，并且最终完成消息恰好交付一次；进度消息拥塞不能影响完成消息。
- [x] 运行容量只在真实 Completed/Failed 终态后重新可用；进入善后时，工作可以离开 Running，但相应 lane/capacity 必须在同一个不可失败移交中由 Drain 接管，Drain terminal 前系统净可用容量不得增加。
- [x] 每项工作取得有界执行上下文，至少携带 stop、deadline、budget 和 progress 能力；测试通过虚拟时间和确定性 pump 驱动，不使用 sleep。
- [x] 现有立即完成工作通过明确适配继续通过测试，异步 Acquisition 不得退化回一次调用内返回终态的模型。
- [x] 现有 L2 提交合同仍证明 Accepted 先于派发可见；提交调用不等待异步工作完成。
- [x] 新增或修改的公开接口用 Doxygen 说明参数、结果、生命周期、所有权和完成时序。
- [x] MinGW Debug 相关测试及完整测试集通过；关闭测试构建时产品目标仍可编译且不提取、构建或链接 GoogleTest。

## Comments

- 2026-07-19：实现候选已完成。首次规范审核与需求审核发现回调重入、重复 WorkId、completion 预留时机、ExecutionContext 有界性及 Doxygen 等问题；已增加失败合同测试并完成修复。
- 2026-07-19：首次修复后需求复审通过；规范复审又发现不同控制器可误取未绑定 receiver 的 mailbox。已用 Runtime 签发的 move-only receiver capability 绑定 reserve、registration 与 pump，并增加双控制器合同测试。
- 2026-07-19：最终修复后相关合同测试通过 13/13，完整测试通过 23/23；`BUILD_TESTING=OFF` 产品目标构建通过，生成目录中无 GoogleTest 引用、依赖源码目录或测试目录。规范复审 0 项 blocking finding，需求复审 0 项 finding，最终双复审通过。
