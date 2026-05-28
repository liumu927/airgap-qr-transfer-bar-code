# AirGap QR Transfer 总体架构

## 背景和目标

AirGap QR Transfer 是一个面向物理隔离环境的离线文件摆渡工具。项目目标是在没有网络、U 盘、光盘、蓝牙、Wi-Fi 等传输介质的条件下，通过一台设备屏幕连续显示动态二维码，另一台设备用摄像头连续扫描，完成小型文件或中等大小文件的单向传输。

第一版 MVP 聚焦单向传输链路：

- 发送端选择单个文件。
- 核心协议生成 manifest 与 data chunk。
- 二维码适配层将协议帧编码为二维码图像。
- 发送端全屏循环播放二维码帧。
- 接收端通过摄像头识别二维码。
- 核心协议根据 `session_id`、`file_id`、`chunk_index` 去重并重组。
- 重组完成后校验 SHA-256，成功后才允许保存或确认接收完成。

项目明确不实现任何主动联网能力，不引入自动更新、远程日志、云同步或遥测统计。

## Windows + Android 跨平台策略

项目使用 Qt 6 + QML + C++20。

- UI 层：使用 QML 构建 Windows 与 Android 共用的主要界面。
- 应用桥接层：使用 C++ QObject 或类似控制器向 QML 暴露发送、接收、进度和错误状态。
- 核心协议层：使用独立 C++20 模块实现，不依赖 QML，不依赖真实摄像头和屏幕，便于单元测试。
- 摄像头层：优先使用 Qt Multimedia，在 Android 上处理运行时相机权限，在 Windows 上处理摄像头枚举和打开失败。
- 文件选择与保存：通过 Qt 跨平台 API 接入，平台差异封装在应用层，不进入核心协议。
- 二维码层：通过 adapter 接口封装二维码生成和识别，避免核心协议绑定具体二维码库。

跨平台边界原则：

- `/src/core` 不关心操作系统、摄像头、QML、图片格式和二维码库。
- `/src/qr` 不关心文件选择、会话状态机和 UI。
- `/src/app` 可以依赖 Qt/QML/Qt Multimedia，并负责把 UI 事件转换为核心协议调用。

## 模块划分

推荐目录结构：

```text
/src/core
  manifest        文件 manifest 数据结构、校验和序列化辅助
  frame           manifest/data/end 帧编码、解析和版本检查
  chunker         文件分片策略
  assembler       chunk 去重、收集、重组和完整性验证
  checksum        CRC32 与 SHA-256 计算接口
  session         session_id/file_id 生成与会话状态

/src/qr
  encoder_adapter 二维码编码接口及实现
  decoder_adapter 二维码识别接口及实现
  qr_payload      QR 容量限制、文本/二进制承载策略

/src/app
  main            Qt 应用入口
  controllers     暴露给 QML 的发送/接收控制器
  qml             QML 页面、状态视图和进度显示
  platform        Android 权限、Windows 文件路径等平台封装

/tests
  core            核心协议单元测试
  qr              adapter mock 或轻量集成测试

/docs
  architecture.md 总体架构
  protocol.md     动态二维码传输协议
  mvp-plan.md     MVP 开发计划
```

核心模块职责：

- Manifest Builder：读取文件元信息，生成 `session_id`、`file_id`、文件大小、chunk 参数和 SHA-256。
- Frame Codec：把 manifest、data、end 帧编码为二进制 payload，并从二进制 payload 安全解析回结构体。
- Chunker：按固定 chunk 大小读取文件内容，生成 chunk 序列。
- Transfer Player：将协议帧交给 QR Encoder，按固定帧率循环播放。
- QR Adapter：把二进制帧转换为二维码图像，或从摄像头图像中解码二进制帧。
- Receiver Session：维护接收状态，过滤不同 session/file 的帧，记录已收 chunk。
- Assembler：全部 chunk 收齐后重组文件，并进行 SHA-256 校验。

## 数据流

发送端数据流：

```text
文件路径
  -> 文件读取与 SHA-256
  -> manifest 生成
  -> 固定大小分片
  -> manifest/data/end 协议帧
  -> QR adapter 编码
  -> QML 全屏循环播放
```

接收端数据流：

```text
摄像头图像
  -> QR adapter 解码
  -> 协议帧解析与 CRC32 校验
  -> session/file 过滤
  -> manifest 记录
  -> data chunk 去重收集
  -> end 帧确认传输元信息
  -> 文件重组
  -> SHA-256 校验
  -> 保存结果与 UI 状态
```

## 发送端流程

1. 用户选择单个文件。
2. 应用层检查文件是否存在、是否可读、大小是否在 MVP 限制内。
3. 核心层生成 `session_id` 与 `file_id`。
4. 核心层计算文件 SHA-256。
5. 核心层生成 manifest，包含文件名、文件大小、chunk 大小、chunk 总数、SHA-256 等字段。
6. 核心层按固定 chunk 大小生成 data 帧。
7. 核心层生成 end 帧，用于重复声明完整传输的结束元信息。
8. 应用层将帧序列交给二维码编码 adapter。
9. UI 进入全屏发送模式，按固定间隔循环播放：
   - manifest 帧重复出现，帮助接收端中途加入。
   - data 帧按 `chunk_index` 顺序循环。
   - end 帧周期性出现，帮助接收端判断可尝试重组。
10. 用户手动停止发送。

第一版发送端不等待接收端反馈，也不实现 ACK、重传请求或双向协商。

## 接收端流程

1. 用户进入接收模式。
2. 应用层请求并打开摄像头。
3. 摄像头图像传入二维码识别 adapter。
4. adapter 输出可能的二维码 payload。
5. 核心层解析协议帧：
   - 检查 magic、版本、帧类型和长度。
   - 校验 CRC32。
   - 对字段范围进行验证。
6. 若收到 manifest 帧：
   - 如果当前无活动会话，则创建接收会话。
   - 如果 `session_id` 和 `file_id` 匹配，则更新或确认 manifest。
   - 如果不匹配，则忽略或提示发现其他会话。
7. 若收到 data 帧：
   - 必须先匹配已知 manifest，或者进入等待 manifest 的暂存策略。
   - 检查 `chunk_index < total_chunks`。
   - 检查 payload 长度与 chunk 规则一致。
   - 对重复 chunk 去重。
8. 若收到 end 帧：
   - 检查 `session_id`、`file_id`、`total_chunks`、文件大小和 SHA-256 是否与 manifest 一致。
   - 如果所有 chunk 已收齐，则触发重组。
9. 重组文件内容并计算 SHA-256。
10. SHA-256 匹配后，显示成功并允许保存到用户选择的位置。
11. SHA-256 不匹配时，显示失败，不把结果当作有效文件。

## 错误处理

核心原则：错误输入不能导致崩溃，所有解析和重组错误必须转化为明确状态。

协议解析错误：

- magic 不匹配：忽略帧。
- 版本不支持：忽略帧，并可向 UI 报告版本不兼容。
- 帧类型未知：忽略帧。
- 长度不匹配：拒绝帧。
- CRC32 不匹配：拒绝帧。
- 字段越界：拒绝帧。

会话错误：

- `session_id` 不匹配：忽略跨会话帧。
- `file_id` 不匹配：忽略跨文件帧。
- manifest 冲突：保留首次确认的 manifest，后续冲突帧标记为错误或忽略。
- data 早于 manifest：第一版可以短暂缓存有限数量 data 帧，也可以直接忽略；具体策略由核心协议阶段确定。

文件错误：

- 文件不可读：发送端显示错误，不进入发送模式。
- 文件过大：提示超出 MVP 限制。
- 保存路径不可写：接收端提示用户重新选择路径。
- SHA-256 不匹配：传输失败，不标记为完成。

摄像头与二维码错误：

- 无摄像头或权限被拒绝：接收端显示错误状态。
- 二维码识别失败：不作为致命错误，继续扫描。
- 二维码容量不足：发送端降低 chunk 大小或提示当前配置不可用。

## 后续扩展方向

协议扩展：

- 支持压缩前置处理。
- 支持更强纠错或 fountain code，降低漏帧影响。
- 支持多文件 manifest。
- 支持目录打包传输。
- 支持加密传输，但密钥交换必须保持离线安全边界。

传输效率扩展：

- 根据设备屏幕、摄像头帧率和二维码识别质量自适应 chunk 大小。
- 根据识别成功率调整播放帧率。
- 增加可选双向 ACK 模式，但必须作为后续版本，不进入第一版 MVP。

平台扩展：

- 支持更多桌面平台。
- 优化 Android 权限、后台恢复和屏幕常亮策略。
- 增加 Windows 摄像头选择与多显示器全屏发送。

可维护性扩展：

- 为 QR adapter 增加多实现选择。
- 增加端到端录屏或图像序列测试。
- 增加协议兼容性测试集。
