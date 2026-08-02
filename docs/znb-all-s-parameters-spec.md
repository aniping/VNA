# ZNB All S-Params 功能规格

> 状态：已冻结；当前实现待按身份顺序修订并重新完成正式发布版验收
>
> 基线：`main@a589bf7`
>
> 主依据：`ZNB_UserManual_en_74.pdf` 印刷页 89、91、290、794、1063

## 1. 用户可见目标

用户从 Factory Preset 的 `Ch1 / Win1 / Trc1 = S21 / dB Mag` 状态进入
Measurement Softtool，点击 `All S-Params` 后，一次操作得到二端口完整
S 参数视图：四个 Diagram 按 2×2 排列，每个 Diagram 只显示 S11、S12、
S21、S22 中的一条 Trace。

该行为遵循 ZNB v74 印刷页 290。手册明确描述为创建 `n²` 个 Diagram、
每个 Diagram 显示一个 S 参数；因此本功能不得实现成同一个 Diagram 叠加
四条曲线。

## 2. 入口与命令合同

- 入口固定为 Measurement Softtool 的 `All S-Params` 按钮。
- 前端只发送一个命令，不得依次发送 Create Measurement、Create Window 或
  Create Trace 请求来拼装结果。
- 统一命令类型为 `ensureAllSParameters`，payload 为
  `{ "traceId": <活动 TraceId> }`。
- `traceId` 只用于确定操作发起时的活动 Trace、它所在的 Window，以及
  其 Measurement 所属的 Channel；前端不得额外提交或推算 ChannelId。
- 协议层只负责解码与编码。创建、复用、校验、幂等和事务规则属于应用核心。

## 3. 原子状态转换

首次从 Factory Preset 执行时，应用核心必须在一个候选事务中：

1. 使用现有 Channel1，不创建第二个 Channel。
2. 确保同一 Channel 内存在 S11、S12、S21、S22 四种 Measurement。
3. 真实 Window、Trace 和 Measurement 的可见绑定严格遵守第 4 节矩阵；
   点击前的 `Diagram1/Trc1=S21` 不是点击后的身份不变量。
4. 四条 Trace 的格式统一为 `LogMagnitude`。

一次真实改变只允许：

- `stateRevision` 增加一次；
- Trace publication `generation` 增加一次；
- 在成功结果可见前原子公开完整的新 Instrument、Display Workspace 和
  publication plan。

任一步校验、分配、Catalog prepare 或提交失败时，Instrument、Display
Workspace、revision、generation 和已保留 FrameSet 必须全部保持原值，不能
留下部分 Measurement、Window 或 Trace。

当目标 Channel 已经具备这四个独立 Diagram 时，再次执行为成功 no-op：复用
已有实体，不新增重复项，不增加 revision，不推进 generation，也不清除当前
FrameSet。相同命令的精确幂等重放必须返回原完整结果，不再次执行副作用；相同
幂等键搭配不同 payload 必须按既有 command-id reuse 规则拒绝。

## 4. Diagram 与活动 Trace

四个 Diagram 的有限展示矩阵固定为：

| | 第 1 列 | 第 2 列 |
| --- | --- | --- |
| 第 1 行 | Diagram1 / Trc1 / S11 | Diagram2 / Trc2 / S12 |
| 第 2 行 | Diagram3 / Trc3 / S21 | Diagram4 / Trc4 / S22 |

- 本矩阵只适用于本功能生成的二端口完整 S 参数视图。
- 呈现层按真实 Window/Trace 身份显示，不得用排序遮蔽 `2、3、1、4` 的错误编号；
  同时不得新增通用 Window 坐标、拖放、自动排版或布局持久化系统。
- 每个 Diagram 继续对应一个真实 Window，且本切片中每个 Window 只有一条
  Trace。不得制造占位 Diagram 或客户端 Trace。
- Preset 的活动 S21 不是命令完成后的活动身份不变量。
- 用户点击其他 Diagram 时，该 Diagram 的唯一 Trace 成为活动 Trace；活动
  Trace 信息条和右上角 Diagram 标识按现有 ZNB 基线高亮。
- Format、Scale 等现有活动 Trace 操作仍只作用于活动 Trace，不联动修改另外
  三条 Trace。

## 5. 显示帧一致性

真实改变推进 generation 时，旧 generation 的保留 FrameSet 必须先失效。
在新配置的首个完整 FrameSet 到达前，四个 Diagram 应立即显示各自的信息条、
坐标网格和 Channel 状态行，但不绘制旧曲线，也不把旧帧改标成新身份。

ContinuousAcquisition 仍是唯一 RawReceiver source owner。下一份完整 raw frame
必须由现有批量链一次派生 S11、S12、S21、S22，并以一个原子 FrameSet 发布
四条 Trace；不得按 Trace 重复采集、启动第二 worker，或通过 REST 轮询曲线。
如果本轮合成、投影或发布失败，不得发布部分曲线；消费者继续等待后续完整
FrameSet。

## 6. 颜色合同

- 同一 Trace 的信息条色块、曲线、参考标记和参考线必须使用同一项目颜色。
- Trace 颜色必须由稳定 Trace 身份决定；state refresh、WebSocket 重连、raw
  sequence 或同一配置 generation 的变化不得使颜色跳变。
- Preset S21 继续使用已经验收的项目绿色。
- 若 ZNB v74 的可核验证据不能确定其余三条 Trace 的精确 RGB，则沿用项目自有
  调色板分配稳定且可区分的颜色，并在验收记录中注明“项目色值”；不得猜测、
  宣称或复制原厂 RGB。
- 颜色是呈现合同，本切片不新增后端颜色编辑状态或颜色命令。

## 7. 错误与可用性

- 活动 Trace 不存在时，按钮保持禁用，不发送请求。
- 服务拒绝不存在的 `traceId`，并沿用稳定的 Trace not found 业务错误。
- anchor Trace 的 Measurement 或 Channel 引用不完整属于候选配置失败；失败必须
  遵守第 3 节的原子性。
- 请求进行期间按钮禁用，成功后从权威 `/api/v1/state` 刷新，不创建乐观业务
  快照；拒绝后保留原页面和现有曲线，并显示既有命令错误反馈。

## 8. 明确排除

本切片不启用或实现：

- Delete、Add Trace、Trace Config、Trace Manager；
- 同一个 Diagram 内叠加多条 Trace；
- 多 Channel 或跨 Channel 的 All S-Params；
- 用户编辑、多 Window 拖放、通用自动布局或布局持久化；
- Marker、Limit、Auto Scale、Scale Coupling、Numeric Editor；
- Trigger、Single Sweep、Restart Sweep 或第二采集 worker；
- SCPI、校准、硬件配置、Record/Replay；
- S-Param Wizard、Balanced Ports 或超过二端口的 `n²` 扩展。

这些入口继续遵守未实现功能的隐藏或原生禁用规则，不得发送假请求或维护第二套
客户端业务状态。

## 9. 自动化验收

至少覆盖以下公共接口和页面合同：

1. Factory Preset 执行一次命令后仍只有一个 Channel，并形成四种 Measurement、
   四个 Window 和四条一一对应的 LogMagnitude Trace。
2. 点击前为 `Diagram1/Trc1=S21`，点击后身份严格匹配第 4 节矩阵。
3. 一次真实改变只增加一次 revision 和 generation，旧 FrameSet 在成功返回前
   已不可见。
4. 任一步失败不改变状态、Catalog 或 Repository；重复执行为 no-op；精确重放
   不重复推进代次。
5. 一份 raw frame 产生一个含四条 Trace 的完整 FrameSet，且 Sij、TraceId、
   MeasurementId、revision、generation 和 sequence 身份匹配。
6. 真实 HTTP `ensureAllSParameters` 请求成功后，`/api/v1/state` 返回四个真实
   Window/Trace；非法或不存在的 anchor 返回既有稳定错误合同。
7. 前端只发送一个命令，按 `S11 / S12 / S21 / S22` 组成 2×2；首个新 FrameSet
   前保留网格且无旧曲线。
8. 初始活动 Trace 仍是 S21；点击另外三个 Diagram 能切换活动 Trace，且颜色在
   状态刷新和 WebSocket 重连后保持稳定。

## 10. Windows 正式发布版验收

只从正式 `release/VectorNetworkAnalyzer` 目录启动服务，在 Windows 浏览器访问
`127.0.0.1:8080`，以 1280×800、100% 缩放执行：

1. 确认启动页仍为一个 `Ch1 / Win1 / Trc1 = S21 / dB Mag` Diagram。
2. 打开 Trace – Meas，确认 `All S-Params` 位于 ZNB v74 第 288–290 页规定的
  位置且当前可用。
3. 点击一次，确认浏览器只发出一个 `ensureAllSParameters` POST。
4. 确认页面形成 `S11 / S12`、`S21 / S22` 的 2×2 四 Diagram，且每图只有一条
   Trace；不存在同图四曲线。
5. 确认四组 Diagram/Trace 编号与 S 参数映射严格匹配第 4 节；逐一点击 Diagram，
   活动信息条和 Diagram 标识正确切换，曲线与色块颜色一致且不跳变。
6. 在首个新完整 FrameSet 前不得闪回旧 S21 曲线；随后四条真实曲线一起出现并
   持续刷新。
7. 再次点击 `All S-Params`，确认实体数量、revision、generation 和颜色均不变。
8. 保存 1280×800 截图、Network 中的单命令与 WebSocket FrameSet 证据，并按
   `docs/znb-conformance-gate.md` 记录功能和页面 UI 门禁结论。

本规格的自动化测试不能替代上述正式 release 的真实浏览器目视和交互验收。
