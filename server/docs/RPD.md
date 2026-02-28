## 文档一：RPD (Requirement & Product Definition)
1.1 产品愿景
构建一个工业级、极高吞吐量的音视频重封装（Remuxing）引擎。利用 C++ 原生能力解决音视频合并中的高性能并发、状态持久化与实时反馈痛点。

1.2 核心功能需求
非阻塞任务下发：用户通过 RESTful API 提交本地音视频路径，即刻获得 TaskID。

高保真无损合并：支持 MP4 视频与 M4A 音频的 Stream Copy 合并，不触发重编码，保持原画质。

实时状态追踪：前端通过 SSE 流实时观察合并进度（0-100%）与 FFmpeg 内部日志。

任务幂等与防重：基于输入路径指纹，防止重复计算资源浪费。

异常自愈：支持任务超时自动释放、进程重启后的任务状态恢复。

1.3 性能指标 (SLO)
响应延迟：API 下发响应 < 50ms。

处理效率：1GB 文件的合并耗时仅受限于磁盘 I/O 吞吐。

并发能力：单机支持数千个 SSE 长连接监听进度。

## 文档二：Implementation Plan (技术实现方案)
2.1 核心架构图
2.2 后端技术栈
调度框架：sogou/workflow (利用其 Series, ThreadTask, MySQLTask)。

音视频库：FFmpeg C API (libavformat, libavutil)。

持久化：MySQL 8.0 (通过 Workflow 非阻塞协议连接)。

内存管理：基于 C++11 的自定义 RAII 包装器（Smart Pointers for FFmpeg Contexts）。
2.3 关键逻辑实现细节交替读取状态机：在 WFThreadTask 中，通过 av_compare_ts 比较视频与音频的 $DTS$，实现流式读取，内存占用恒定。时间戳修复流水线：对每一个 AVPacket 执行：
$$PTS_{out} = (PTS_{in} - Offset_{in}) \times \frac{Timebase_{in}}{Timebase_{out}}$$
SSE 循环推送：
使用递归 WFTimerTask 挂载在 WFHttpTask 的连接上，实现异步非阻塞的增量数据推送。
2.4 数据库状态机
状态,触发动作,备注
PENDING,HTTP POST 成功插入,初始占位
RUNNING,ThreadTask 开始执行,记录 start_time，心跳激活
SUCCESS,av_write_trailer 成功,临时文件 Rename 为正式文件
FAILED,捕获到 FFmpeg 返回值 < 0,记录 error_msg

## 文档三：Roadmap (项目路线图)
阶段一：核心引擎开发 (Week 1-2)
[ ] 搭建基于 sogou/workflow 的基础 C++ 开发环境。

[ ] 构建 FFmpeg RAII 框架，确保无内存泄漏。

[ ] 实现交替读取 (Interleaved Muxing) 核心算法。

[ ] 编写单元测试：处理不同长度的 A/V 合并及单流情况。

阶段二：服务化与持久化 (Week 3)
[ ] 集成 Workflow 的 MySQL 协议，实现任务状态落库。

[ ] 实现**任务指纹（SHA-256）**逻辑与 CAS 状态更新策略。

[ ] 开发 RESTful API：/api/v1/merge（下发）与 /api/v1/status（轮询）。

[ ] 压力测试：模拟高并发下任务争抢场景。

阶段三：实时交互增强 (Week 4)
[ ] 实现 SSE (Server-Sent Events) 异步推送模块。

[ ] 对接 FFmpeg av_log_callback，将实时日志注入 SSE 流。

[ ] 构建 Vue 3 监控面板：展示实时进度条与类似终端的日志窗口。

[ ] Nginx 调优：配置 proxy_buffering off 确保 SSE 流畅。

阶段四：鲁棒性与自动化 (Week 5)
[ ] 实现 Janitor 守护任务，自动清理僵尸任务和过期临时文件。

[ ] 完善 interrupt_callback，支持用户前端主动取消任务。

[ ] 编写 Multi-stage Dockerfile，优化最终镜像体积。

[ ] 整理 CineIris 项目文档。