# Manual Feedback Resend Design

## 背景

当前 MVP 使用发送端全量循环播放 `manifest -> data chunks -> end`。当文件较大、帧数很多时，如果接收端只漏掉少量 chunk 或漏掉 `end` 帧，接收端必须等待发送端完整循环一轮才可能再次遇到缺失帧，尾部等待时间会迅速放大，成为主要传输瓶颈。

本设计引入人工反馈重传模式：接收端在未完成时生成一个“缺失清单二维码”，发送端暂停全量播放并扫描该反馈二维码，然后只循环播放接收端仍需要的帧。

该机制不是实时 ACK，也不引入网络、蓝牙、Wi-Fi 或后台通信。它是一个人工触发、离线、二维码承载的反向控制消息。

## 目标

- 缩短大文件传输后期等待少量漏帧的时间。
- 保持第一版离线安全边界，不主动访问网络。
- 复用现有 `session_id`、`file_id` 和 chunk 去重逻辑。
- 支持接收端漏掉 data chunk，也支持只漏掉 `end` 帧。
- 允许发送端随时退出重传模式，回到全量循环播放。

## 非目标

- 不做实时自动 ACK。
- 不做双向速率协商。
- 不做网络、蓝牙、Wi-Fi、USB 等其他通道。
- 不要求发送端每个 chunk 等待确认。
- 不在第一阶段实现纠删码或 fountain code。

## 用户流程

1. 发送端按当前方式全量循环播放二维码。
2. 接收端持续扫描并收集 chunk。
3. 如果接收端长时间停在某个进度，例如 `985 / 1000`，用户点击接收端的 `Feedback`。
4. 接收端暂停扫描并显示反馈二维码，二维码内容是当前会话缺失的 chunk 范围和是否需要 `end`。
5. 发送端暂停播放，点击 `Scan Feedback`，用摄像头扫描接收端屏幕上的反馈二维码。
6. 发送端校验反馈二维码中的 `session_id`、`file_id`、`total_chunks` 是否匹配当前发送任务。
7. 匹配后，发送端进入 `Resend Missing` 模式，只循环播放：
   - manifest
   - 缺失的 data chunk
   - end
8. 接收端关闭反馈二维码，恢复扫描。
9. 接收端收齐后重组并做 SHA-256 校验。

## 反馈帧协议

反馈二维码使用普通 AQRT 二进制帧承载，新增帧类型：

```text
frame_type = 4  missing_request
```

反馈帧同样使用通用帧头和 CRC32。它不包含文件内容，只包含当前接收状态摘要。

### missing_request body

| 字段 | 类型 | 长度 | 说明 |
| --- | --- | --- | --- |
| session_id | bytes | 16 | 接收端当前会话 ID |
| file_id | bytes | 16 | 接收端当前文件 ID |
| request_flags | uint32 | 4 | bit 0 表示需要 end 帧；其余 bit 保留 |
| total_chunks | uint32 | 4 | manifest 中的 chunk 总数 |
| received_chunks | uint32 | 4 | 当前已接收的 chunk 数 |
| feedback_seq | uint32 | 4 | 接收端每生成一次反馈递增，用于 UI 显示和调试 |
| range_count | uint16 | 2 | 缺失区间数量 |
| ranges | repeated | range_count * 8 | 每项为 `start_index:uint32` + `count:uint32` |

缺失 chunk 使用连续区间压缩，例如缺失 `3,4,5,9` 表示为：

```text
(start=3, count=3), (start=9, count=1)
```

### 校验规则

发送端解析反馈帧后必须检查：

- `frame_type == 4`。
- `session_id` 与当前发送任务一致。
- `file_id` 与当前发送任务一致。
- `total_chunks` 与当前 manifest 一致。
- `received_chunks <= total_chunks`。
- 每个 range 的 `start_index < total_chunks`。
- 每个 range 的 `count > 0`。
- `start_index + count <= total_chunks`，且不得整数溢出。
- range 可以不排序，但发送端应排序、合并、去重后生成重传播放列表。

接收端生成反馈帧时应：

- 仅在已经收到 manifest 后允许生成反馈二维码。
- 如果没有缺失 data chunk 但还没有收到 `end`，设置 `request_flags bit 0`。
- 如果缺失 range 太多导致单个 QR 容量不足，按顺序生成多张反馈二维码，字段需扩展 `part_index` 和 `part_count`；第一版可以先限制 range 数量并提示用户继续全量循环。

## 发送端重传播放列表

当前发送端完整播放列表可视为：

```text
slot 0: manifest
slot 1..N: data[0..N-1]
slot N+1: end
```

收到 missing_request 后，发送端构造临时播放列表：

```text
manifest, data[missing_0], data[missing_1], ..., end
```

设计约定：

- manifest 总是放在重传列表开头，帮助接收端保持会话上下文。
- end 总是放在重传列表末尾；如果 `request_flags bit 0` 置位，end 必须播放。
- 如果缺失 chunk 数量为 0 且只需要 end，则播放 `manifest, end`。
- 如果缺失 chunk 数量占比过高，例如超过 30%，UI 可以提示“缺失过多，全量循环可能更合适”，但仍允许重传。
- 发送端必须保留完整播放列表，用户可一键退出重传模式回到全量循环。

## 接收端状态需求

接收端需要从 assembler 暴露以下只读状态：

- 是否已有 manifest。
- 是否已有 end。
- `session_id`。
- `file_id`。
- `total_chunks`。
- `received_chunks`。
- 已接收 chunk bitset 或缺失区间列表。

缺失区间生成逻辑应放在核心或 app-core 层，便于单元测试，不依赖 QML 或摄像头。

## UI 设计

### 接收端

在 Receive 页增加：

- `Feedback` 按钮：仅在已有 manifest、未完成时启用。
- 点击后暂停扫描并显示反馈二维码。
- 显示简短状态：`Missing 15 / 1000`、`Need end frame`。
- `Resume Scan`：关闭反馈二维码并恢复摄像头扫描。

### 发送端

在 Send 页增加：

- `Scan Feedback`：暂停当前播放，用摄像头扫描接收端的反馈二维码。
- `Resend Missing` 状态：显示缺失 chunk 数量和临时播放帧数量。
- `Full Loop`：退出重传模式，恢复全量播放列表。

第一阶段如果发送端摄像头接入成本较高，可以先提供调试入口：从文本框粘贴反馈 payload 的 Base64。正式 UI 应以扫描反馈二维码为主。

## 错误处理

- 无当前发送任务：发送端拒绝反馈二维码。
- session/file 不匹配：提示“Feedback does not match current transfer”。
- 反馈帧损坏或 CRC32 错误：忽略并继续等待扫描。
- range 越界：拒绝反馈帧。
- range 数量过多：拒绝或提示使用全量循环。
- 接收端尚未收到 manifest：不能生成反馈二维码。
- 接收端已经完成：不生成反馈二维码。

## 安全性

- 反馈二维码不包含文件内容。
- 反馈二维码只暴露 `session_id`、`file_id`、总 chunk 数、已收 chunk 数和缺失索引。
- 发送端必须只接受匹配当前任务的反馈帧，避免被其他会话干扰。
- 旧反馈帧不会破坏正确性，最多导致发送端重复播放已经收到的 chunk。
- 不新增任何联网能力。

## 实施阶段

### 阶段 A：协议与核心测试

- 新增 `MissingRequestFrame` 数据结构。
- 新增 `frame_type = 4` 编码和解析。
- ReceiverAssembler 暴露缺失 range 生成方法。
- 单元测试覆盖正常反馈、range 合并、越界、session/file 不匹配。

### 阶段 B：接收端反馈二维码

- 接收端生成 missing_request payload。
- 使用 QR encoder 显示反馈二维码。
- Receive 页增加 `Feedback` / `Resume Scan`。

### 阶段 C：发送端重传播放列表

- 发送端保留完整 payload/QR 帧。
- 解析 missing_request 后构建临时播放列表。
- Send 页增加 `Resend Missing` / `Full Loop`。

### 阶段 D：发送端扫描反馈二维码

- 发送端接入摄像头扫描反馈 QR。
- 扫描成功后自动进入重传模式。
- 保留 Base64 调试入口用于自动化和问题排查。

### 阶段 E：端到端验证

- 构造 500+ 帧文件，人工遮挡造成漏帧。
- 对比全量循环等待时间和 feedback resend 时间。
- 记录不同缺失数量下的收益和操作步骤。
