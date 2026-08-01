# 一次原始帧驱动多端口连续显示

下一阶段以 `ContinuousAcquisition` 作为唯一原始采集所有者。一次完成的原始
采集必须派生当前 Channel 的全部 Measurement 和 Trace，不得按 Trace 重复
扫描，也不得因 Measurement 或 Trace Format 变化启动第二个 RawSweepSource。

## S 参数端口语义

统一采用 `Sij = b[i] / a[j]`：`j` 是激励源端口，`i` 是响应端口，公开端口号
均从 1 开始。Raw Source State 的 `sourcePort` 表示 `j`；每个频点的
`reference` 是 `a[j]`，`responses[i - 1]` 是 `b[i]`。实现必须按
`sourcePort` 身份查找激励状态，不能把容器下标当作端口号。

| Measurement | 激励端口 `j` | 响应端口 `i` |
| --- | ---: | ---: |
| S11 | 1 | 1 |
| S21 | 1 | 2 |
| S12 | 2 | 1 |
| S22 | 2 | 2 |

二端口连续采集计划包含源端口 1 和 2。改变 Measurement 或 Trace Format 只
改变原始帧的消费方式，不重启采集，也不改变这个阶段的采集计划。

## 批量测量与迹线投影

一个原始帧先批量合成当前 Channel 的 Measurement，再由这些复数 S 参数派生
全部 Trace。同类型 S 参数只计算一次；空 Measurement 输入成功产生空结果。
独立的 S 参数类型最多四种，Measurement 实体总数继续服从领域容量，不在
发布器中复制另一套上限。

批量合成、Trace 投影和 frame set 提交都遵守全有或全无：任一需要的输入
缺失、结果非有限或容量校验失败时，本轮不发布任何 Trace，并保留同一配置
generation 的上一组完整结果。

显示投影语义如下：

- LogMagnitude 为 `20 * log10(abs(Sij))`，单位为 `dB`。
- Phase 为 Sij 的主值相位，单位为 degree，规范范围为 `[-180, 180)`，不进行
  相位展开；精确零复数显示为 `0°`，这是确定性显示约定，不表示其物理相位
  有定义。
- Smith 直接携带复数 Sij，单位为 `U`，不转换为阻抗、导纳或对数值。

Phase 的 `[-180°, 180°]` 绘图域不是对商用仪表 Scale/Div、Ref Value 或
Ref Position 默认值的臆测；Phase 和 Smith 的正式 Scale 状态仍由后续有依据
的切片定义。前端只执行屏幕坐标映射，不计算 S 参数、dB、相位或 Smith 业务
变换。

## 显示帧与配置代次

`TraceDisplayFrame` 除现有帧、Trace、状态版本和采集序号外，还携带
`measurementId`、`measurementType` 和配置 `generation`。Cartesian 格式
携带标量数组；Smith 携带复数 Sij，协议使用紧凑的 `[real, imaginary]`
二元数组，`valueUnit` 为 `U`。

同一原始帧派生的全部 Trace 共享 `frameId`、`stateRevision` 和单调递增的
`sequenceNumber`。`sequenceNumber` 标识原始采集顺序，不因显示配置变化
重置；`generation` 标识一组一致的 Measurement 与 Trace 配置。

Measurement 重新绑定、Trace 创建或删除、Trace Format 实际变化会递增
generation，并原子失效旧 frame set。旧 generation 的迟到处理结果必须在
提交点被拒绝。下一组完整结果到达前可以没有曲线；不得把旧结果标成新配置。
同一 generation 的处理失败保留 last complete，并继续消费后续原始帧。

Measurement 选择针对显式 TraceId，而不是服务端全局“活动 Trace”。应用事务
在同一 Channel 内复用目标类型的 Measurement，没有时才创建，并只重新绑定
目标 Trace；其他 Trace 不随之改变。首个界面切片可以只有 Window1 和一个
活动 Trace，但后端批量契约不得据此硬编码单 Window 或单 Trace。

## 范围边界

本决策只处理一个 RawReceiverFrame 所属的 Channel。多 Channel 调度、触发
扩展、真实硬件、校准、Marker、第二采集 worker、Single Sweep 和 Restart
仲裁均不属于本阶段。浏览器关闭不停止 ContinuousAcquisition，显示刷新也不
使用 REST 轮询或传输原始接收机 JSON。
