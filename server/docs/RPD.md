## 文档一：RPD (Requirement & Product Definition)

### 1.1 产品愿景
构建一个工业级、高吞吐、可观测的音视频处理 Web 服务，基于 **sogou/workflow + FFmpeg libavformat/libavcodec 深度集成** 提供“音视频合并（主场景）+ 统一任务编排能力”。

目标是让服务具备以下能力：
- 对外提供低延迟 API 下发与查询接口。
- 对内以 C++ 原生性能执行音视频任务。
- 在 Linux 环境下可重复构建，并通过 Docker 标准化部署。

### 1.2 核心功能需求
1. **非阻塞任务下发**
   - 用户通过 RESTful API 提交本地文件路径（或挂载目录内路径）与输出参数。
   - 服务快速返回 `task_id`，不阻塞主线程。

2. **高保真无损合并（优先）**
   - 支持 MP4（视频）+ M4A（音频）等常见容器与编码组合。
   - 默认走 `stream copy`（`-c copy` 思路），在可行条件下不重编码，保持画质与速度。

3. **实时状态追踪**
   - 通过 SSE 推送任务状态、进度（0~100）和关键日志。
   - 前端可订阅单任务或任务集合视图。

4. **任务幂等与防重**
   - 基于输入文件指纹（路径 + 文件大小 + mtime + 可选 SHA-256）进行幂等判断。
   - 重复请求可直接返回已有 `task_id` 与当前状态。

5. **异常自愈与可恢复**
   - 任务超时、进程异常退出后可恢复状态。
   - 服务重启时自动扫描并回收“僵尸任务”。

### 1.3 性能指标 (SLO)
- **API 下发延迟**：P95 < 50ms（不含文件上传，仅提交任务元数据）。
- **处理效率**：1GB 文件合并耗时主要受磁盘 I/O 与容器资源限制。
- **并发能力**：单机支持数千 SSE 长连接（结合 Nginx/内核参数调优）。
- **稳定性**：任务状态最终一致；异常场景具备可追踪日志与重试策略。

---

## 文档二：Implementation Plan (技术实现方案)

### 2.1 核心架构图（逻辑）
1. Client / Web UI
2. HTTP API（workflow 的 WFHttpServer）
3. Task Manager（内存状态机 + MySQL 持久化）
4. Worker（WFThreadTask 执行 FFmpeg 处理）
5. SSE Pusher（WFTimerTask 周期推送）
6. Storage（输入/输出挂载卷）
7. MySQL（任务状态、元数据、审计日志）

### 2.2 后端技术栈
- **调度框架**：sogou/workflow（SeriesWork、WFThreadTask、WFMySQLTask、WFTimerTask）。
- **音视频处理（核心）**：FFmpeg C API（`libavformat`、`libavcodec`、`libavutil`、`libswresample` 按需）。
- **持久化**：MySQL 8.0。
- **日志/观测**：结构化日志 + FFmpeg `av_log_set_callback` + SSE 实时推送。
- **语言标准**：C++17（建议，兼容 workflow 编译链）。

### 2.3 关键逻辑实现细节
1. **任务生命周期状态机**：
   - `PENDING -> RUNNING -> SUCCESS | FAILED | CANCELED`

2. **进度计算策略（基于库级上下文）**：
   - 通过 `avformat_find_stream_info` 获取输入时长与流信息。
   - 在 packet 处理循环中基于 `pts/dts` 与 `time_base` 换算进度。
   - 若容器缺失可靠时长信息，则退化为阶段性状态推送（避免假进度）。

3. **SSE 异步推送模型**：
   - 每个订阅连接绑定定时推送任务。
   - 推送字段：`event`、`task_id`、`status`、`progress`、`message`、`ts`。

4. **并发与限流**：
   - API 层限流 + Worker 线程池并发阈值。
   - 避免单机过量并发导致 demux/mux 与磁盘 I/O 抢占。

5. **幂等策略**：
   - 指纹键 `fingerprint_key` 建立唯一索引。
   - 插入冲突时直接查询并复用已有任务。

6. **libavformat/libavcodec 深度集成边界**：
   - 统一封装 `FormatContext/CodecContext/Packet/Frame` 的 RAII 生命周期管理。
   - 主链路使用 `av_read_frame -> 时间戳重映射 -> av_interleaved_write_frame` 实现高性能合并。
   - 对不可直接 copy 的输入，预留解码/重编码分支，并保证与主状态机一致。

### 2.4 数据库状态机（建议）
| 状态 | 触发动作 | 备注 |
|---|---|---|
| PENDING | HTTP POST 成功插入 | 初始占位，等待调度 |
| RUNNING | Worker 开始执行 | 记录 `start_time`、`worker_id` |
| SUCCESS | `av_write_trailer` 成功返回 | 输出文件原子 rename 生效 |
| FAILED | FFmpeg API 返回错误码（<0）或内部异常 | 记录 `error_code/error_msg` |
| CANCELED | 用户取消或系统中断 | 记录取消来源与时间 |

### 2.5 Linux 开发环境规范
- OS：Ubuntu 22.04 LTS（建议）
- 编译工具：`build-essential`、`cmake`、`pkg-config`
- 依赖建议：
  - workflow（源码编译或子模块）
  - FFmpeg 开发包（`libavformat-dev`、`libavcodec-dev`、`libavutil-dev` 等）
  - MySQL Client Dev（如需 C API）
- 目录约定：
  - `/app/bin`：服务可执行文件
  - `/data/input`：输入媒体
  - `/data/output`：输出媒体
  - `/data/temp`：临时文件

### 2.6 Docker 化部署方案

#### 2.6.1 镜像策略（Multi-stage）
- **builder 阶段**：安装编译依赖，构建 workflow + 服务二进制。
- **runtime 阶段**：仅保留运行时动态库依赖（libav*、libstdc++、ca-certificates、tzdata、可执行文件）。
- 目标：减小镜像体积并提升安全性。

#### 2.6.2 容器运行约束
- 通过 volume 挂载媒体目录，避免把大文件写入容器层。
- 对容器设置 CPU / memory 限制，避免多任务时资源抢占失控。
- 将日志输出到 stdout/stderr，便于 Docker/K8s 采集。

#### 2.6.3 docker-compose（建议拓扑）
- `av-service`：C++ workflow 服务。
- `mysql`：状态存储。
- `nginx`（可选）：统一入口与 SSE 反向代理（`proxy_buffering off`）。

#### 2.6.4 部署流程（最小闭环）
1. `docker compose build`
2. `docker compose up -d`
3. 初始化数据库表结构。
4. 健康检查：`/healthz`。
5. 调用 `/api/v1/merge` 下发任务并通过 SSE 验证实时推送。

---

## 文档三：Roadmap (项目路线图)

### 阶段一：核心引擎开发（Week 1-2）
- [ ] 搭建基于 sogou/workflow 的 C++ 开发工程（Linux）。
- [ ] 打通 FFmpeg C API 执行链路（demux/decode/encode 或 stream copy mux）。
- [ ] 实现合并核心流程与错误码映射。
- [ ] 编写单元测试：正常合并、无音频、无视频、时间轴异常。

### 阶段二：服务化与持久化（Week 3）
- [ ] 开发 API：`/api/v1/merge`、`/api/v1/task/{id}`、`/api/v1/cancel/{id}`。
- [ ] 集成 MySQL 持久化与任务状态机。
- [ ] 实现任务指纹与幂等复用。
- [ ] 压测并验证并发任务调度与资源阈值策略。

### 阶段三：实时交互增强（Week 4）
- [ ] SSE 推送：进度、日志、状态变化事件。
- [ ] 对接 `av_log_callback` 与内部进度事件并标准化 SSE 格式。
- [ ] 增加前端监控页（可选）：任务列表、日志流、失败重试。
- [ ] Nginx SSE 配置优化（长连接稳定性）。

### 阶段四：鲁棒性与自动化（Week 5）
- [ ] 增加 janitor 定时清理：超时任务、临时文件、孤儿记录。
- [ ] 支持服务重启后的任务恢复与一致性修复。
- [ ] 完成 Multi-stage Dockerfile 与 compose 部署模板。
- [ ] 输出运维手册：容量规划、日志排障、升级回滚。
