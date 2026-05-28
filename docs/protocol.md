# 动态二维码文件传输协议

## 目标和范围

本协议用于 AirGap QR Transfer 第一版 MVP 的单向动态二维码文件传输。发送端循环播放二维码帧，接收端持续扫描、去重、重组并校验文件。

第一版协议目标：

- 支持单文件传输。
- 支持接收端中途开始扫描。
- 支持重复帧去重。
- 支持畸形帧安全拒绝。
- 支持 SHA-256 完整性校验。
- 不依赖 ACK、网络、蓝牙、Wi-Fi 或外部存储介质。

## 基本约定

- 帧承载：二维码 payload 内放置一段协议二进制帧。
- 整数端序：所有多字节整数使用 little-endian。
- 文本编码：文件名使用 UTF-8。
- 哈希算法：文件完整性使用 SHA-256。
- 快速损坏检测：单帧使用 CRC32。
- 版本号：所有帧必须包含主版本和次版本。
- 第一版主版本：`1`。

建议的二维码播放序列：

```text
manifest, data[0], data[1], ..., data[n-1], end
```

发送端持续循环该序列。为了提高中途加入成功率，实际播放时可以周期性插入 manifest，例如：

```text
manifest, data[0], data[1], data[2], manifest, data[3], ...
```

## 通用帧头

所有帧共享同一个通用帧头。

| 字段 | 类型 | 长度 | 说明 |
| --- | --- | --- | --- |
| magic | bytes | 4 | 固定为 `AQRT`，表示 AirGap QR Transfer |
| version_major | uint8 | 1 | 主版本，第一版为 `1` |
| version_minor | uint8 | 1 | 次版本，第一版为 `0` |
| frame_type | uint8 | 1 | `1` manifest，`2` data，`3` end |
| header_flags | uint8 | 1 | 第一版保留，发送端置 `0`，接收端忽略未知 bit |
| header_size | uint16 | 2 | 通用帧头长度，便于后续扩展 |
| body_size | uint32 | 4 | 帧 body 长度，不包含通用帧头和 CRC32 |
| crc32 | uint32 | 4 | 对通用帧头中 `crc32` 之前字段与 body 计算 CRC32 |

通用帧头长度第一版为 18 字节。解析器必须检查：

- `magic` 是否匹配。
- `header_size` 是否不小于第一版通用帧头长度。
- `body_size` 是否与实际 payload 长度匹配。
- `crc32` 是否正确。
- 版本和帧类型是否可处理。

## session_id 设计

`session_id` 用于隔离一次发送会话，避免多个正在播放的文件帧混淆。

第一版建议：

- 长度：16 字节。
- 来源：发送端在开始一次发送时生成的随机值。
- 生命周期：一次用户点击“开始发送”到停止发送。
- 表示：协议中按原始 16 字节存储；UI 可显示为十六进制短码。

接收端处理规则：

- 活动接收会话建立后，只接收相同 `session_id` 的帧。
- 不同 `session_id` 的帧必须忽略或作为“发现其他会话”提示。
- 若用户重新开始接收，可以清空当前会话并接受新的 `session_id`。

## file_id 设计

`file_id` 用于标识同一会话中的文件。第一版只传输单文件，但仍保留 `file_id`，为后续多文件或重新发送同名文件打基础。

第一版建议：

- 长度：16 字节。
- 来源：由 manifest 关键字段计算或随机生成。
- 推荐生成方式：`SHA-256(session_id || file_size || file_sha256 || utf8_file_name)` 的前 16 字节。

接收端处理规则：

- `file_id` 必须与 manifest、data、end 帧一致。
- 同一 `session_id` 内出现不同 `file_id` 时，第一版不做多文件并发接收，默认忽略非当前文件。

## manifest 帧

manifest 帧描述文件元信息和分片参数。接收端可以从任意一次循环中的 manifest 建立接收会话。

`frame_type = 1`

### manifest body 字段

| 字段 | 类型 | 长度 | 说明 |
| --- | --- | --- | --- |
| session_id | bytes | 16 | 发送会话 ID |
| file_id | bytes | 16 | 文件 ID |
| manifest_flags | uint32 | 4 | manifest 扩展标记；bit 0 表示 UTF-8 纯文本消息，其余 bit 第一版保留 |
| file_size | uint64 | 8 | 原始文件字节数 |
| chunk_size | uint32 | 4 | 除最后一片外的 data payload 目标大小 |
| total_chunks | uint32 | 4 | data chunk 总数 |
| file_sha256 | bytes | 32 | 原始完整文件 SHA-256 |
| file_name_size | uint16 | 2 | UTF-8 文件名长度 |
| file_name | bytes | variable | UTF-8 文件名，不包含路径 |

### manifest 校验规则

接收端必须检查：

- `file_size` 不超过实现设定的 MVP 最大文件大小。
- `chunk_size > 0`。
- `total_chunks > 0`，除非后续明确支持空文件。
- `ceil(file_size / chunk_size) == total_chunks`。
- `file_name_size` 与剩余 body 长度一致。
- 文件名不得包含路径分隔符、驱动器前缀或控制字符。
- `file_sha256` 长度固定为 32 字节。

空文件策略：第一版建议暂不支持空文件，或在核心协议阶段显式定义 `total_chunks = 0` 的特殊路径。MVP 初期可先拒绝空文件，降低复杂度。

## data 帧

data 帧承载一个文件 chunk。

`frame_type = 2`

### data body 字段

| 字段 | 类型 | 长度 | 说明 |
| --- | --- | --- | --- |
| session_id | bytes | 16 | 发送会话 ID |
| file_id | bytes | 16 | 文件 ID |
| chunk_index | uint32 | 4 | 从 `0` 开始的 chunk 序号 |
| total_chunks | uint32 | 4 | chunk 总数，必须与 manifest 一致 |
| chunk_offset | uint64 | 8 | 该 chunk 在原文件中的字节偏移 |
| data_size | uint32 | 4 | chunk payload 字节数 |
| data | bytes | variable | 文件数据 |

### chunk_index / total_chunks 规则

- `chunk_index` 从 `0` 开始。
- `chunk_index < total_chunks`。
- `total_chunks` 必须等于 manifest 中的 `total_chunks`。
- `chunk_offset` 必须等于 `chunk_index * chunk_size`。
- 非最后一个 chunk 的 `data_size` 必须等于 manifest 中的 `chunk_size`。
- 最后一个 chunk 的 `data_size` 必须等于 `file_size - chunk_size * (total_chunks - 1)`。

接收端必须按 `chunk_index` 去重：

- 首次收到某个 `chunk_index` 时保存该 chunk。
- 再次收到相同 `chunk_index` 且内容相同，可以忽略。
- 再次收到相同 `chunk_index` 但内容不同，必须标记为冲突错误，并拒绝该帧。

## end 帧

end 帧用于重复声明发送端认为一次文件帧序列已经完整播放到末尾。第一版中 end 帧不是 ACK，也不代表接收端已经收齐，它只是接收端尝试重组的触发信号之一。

`frame_type = 3`

### end body 字段

| 字段 | 类型 | 长度 | 说明 |
| --- | --- | --- | --- |
| session_id | bytes | 16 | 发送会话 ID |
| file_id | bytes | 16 | 文件 ID |
| total_chunks | uint32 | 4 | chunk 总数 |
| file_size | uint64 | 8 | 原始文件字节数 |
| file_sha256 | bytes | 32 | 原始完整文件 SHA-256 |

接收端处理规则：

- end 帧必须与 manifest 中的 `session_id`、`file_id`、`total_chunks`、`file_size`、`file_sha256` 一致。
- 如果 chunk 已全部收齐，可以立即重组并做 SHA-256 校验。
- 如果尚未收齐，继续扫描，等待下一轮循环。
- 未收到 end 帧但已收齐所有 chunk 时，也可以重组；end 帧主要用于 UI 状态和完整性确认。

## CRC32

CRC32 用于快速检测单个二维码 payload 是否损坏或误识别。

第一版建议：

- 算法：标准 CRC-32/ISO-HDLC。
- 多项式：`0x04C11DB7`。
- 初始值：`0xFFFFFFFF`。
- 输入反射：true。
- 输出反射：true。
- 结果异或：`0xFFFFFFFF`。

CRC32 覆盖范围：

- 从 `magic` 到 `body_size` 的通用帧头字段。
- 完整 body。
- 不包含 `crc32` 字段自身。

CRC32 不替代 SHA-256。CRC32 只判断单帧是否明显损坏，SHA-256 才判断完整文件是否正确。

## SHA-256

SHA-256 用于完整文件校验。

发送端：

- 在生成 manifest 前读取完整文件计算 SHA-256。
- 将 32 字节 SHA-256 写入 manifest 和 end 帧。

接收端：

- 只在所有 chunk 收齐后重组完整文件。
- 对重组后的完整字节序列计算 SHA-256。
- 只有计算结果与 manifest 中的 `file_sha256` 完全一致时，才认为接收成功。
- 校验失败时，必须显示失败状态，不得把结果标记为有效文件。

## 纯文本消息

第一版的纯文本传递不新增独立帧类型，也不引入新的传输通道。发送端将用户输入的 UTF-8 文本作为一段普通 payload，复用现有的 manifest/data/end 流程发送：

- `manifest_flags` 必须设置 bit 0，也就是 `0x00000001`，表示该会话是 UTF-8 纯文本消息。
- `file_name` 固定使用虚拟名称 `message.txt`。
- `file_size`、`chunk_size`、`total_chunks`、CRC32 和 SHA-256 规则与普通文件完全一致。
- 接收端必须先完成全部 chunk 重组并通过 SHA-256 校验，之后才可以把内容作为可复制文本展示。
- 接收端只有在 manifest text bit 已设置、且重组结果是合法 UTF-8 时才显示复制入口；否则仍按普通文件接收结果处理。
- 空文本第一版直接拒绝，不生成传输会话。

这种设计保持了协议最小化：文本消息与文件传输共享 `session_id`、`file_id`、去重、校验和循环播放机制，后续如需区分更多 MIME 类型或消息类型，可以继续使用 `manifest_flags` 或次版本扩展字段演进。

## 手动反馈重传扩展

当文件帧数很多时，全量循环播放会在传输末尾出现明显长尾：接收端只缺少少数 chunk 时，仍必须等待发送端完整循环一轮才可能补齐。为降低这个瓶颈，后续版本引入人工触发的 missing_request 反馈二维码。

该扩展建议新增：

```text
frame_type = 4  missing_request
```

missing_request 由接收端在暂停扫描时生成，包含 `session_id`、`file_id`、`total_chunks`、`received_chunks`、是否需要 `end` 帧，以及缺失 chunk 的连续区间列表。发送端扫描该反馈二维码后，必须验证它属于当前发送任务，然后临时只循环播放 `manifest + 缺失 data chunk + end`。

该机制不是实时 ACK，也不是自动双向握手；它仍然是离线、人工触发、二维码承载的反向状态摘要。详细设计见 `docs/manual-feedback-resend.md`。

## 版本兼容策略

协议使用 `version_major` 和 `version_minor`。

- `version_major` 不同：视为不兼容，接收端拒绝帧。
- `version_major` 相同但 `version_minor` 更高：接收端可以在识别已知字段、忽略未知 flags 的前提下处理；若 `header_size` 或 body 字段无法安全跳过，则拒绝。
- `header_size` 允许未来扩展通用帧头。
- `header_flags` 中未知 bit 第一版应忽略，但未来可定义“必须理解”的 flags 区间。
- 帧 body 第一版不支持任意 TLV 扩展，后续如需扩展应优先新增次版本并明确兼容规则。

第一版实现应只生成 `1.0` 帧。

## 为什么第一版采用循环播放而不是 ACK

ACK 模式要求接收端也能向发送端反馈状态。这会带来明显复杂度：

- 发送端需要摄像头或其他输入通道识别接收端 ACK。
- 两端都要同时具备发送和接收二维码的 UI 状态。
- 协议需要处理 ACK 丢失、重传、乱序、超时和状态同步。
- Android 与 Windows 的摄像头和屏幕同时使用会增加权限、性能和交互复杂度。
- 第一版目标是验证离线二维码摆渡的最小闭环，不应过早引入双向握手。

循环播放的优势：

- 发送端实现简单，只需重复展示完整帧序列。
- 接收端可中途加入，只要持续扫描最终能收齐 chunk。
- 丢帧不是致命错误，下一轮循环仍可补齐。
- 更适合物理隔离环境中的人工操作。

循环播放的代价：

- 传输效率低于 ACK 定向重传。
- 文件越大，等待补齐最后少数 chunk 的时间可能越长。
- 无法自动通知发送端停止播放。

因此第一版采用循环播放；ACK、定向重传和速率协商放入后续扩展版本。

## 第一版限制

- 单文件传输。
- 单活动接收会话。
- 不支持加密。
- 不支持压缩。
- 不支持联网或任何外部传输通道。
- 不保证大文件效率，MVP 应设置合理文件大小上限。
- chunk 大小需根据二维码库容量、屏幕显示质量和摄像头识别能力在实现阶段调优。
