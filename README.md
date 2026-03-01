# youtobe-workflow

当前目标：基于 **sogou/workflow** 提供 Web API，并在接口模块中集成 **FFmpeg C API** 完成音视频合并（先做一个简单可用功能）。

## 推荐部署方式：Docker（避免本地缺库）

你遇到的 `libworkflow.so / libavformat.so not found` 本质上是运行环境缺动态库。对于当前阶段，**最稳妥方式就是直接用 Docker 部署运行**。

### 1) 构建并启动

```bash
mkdir -p data/input data/output models/whisper
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose up --build -d

# 或者直接跑一键端到端脚本（会自动执行 health/merge/asr/task 轮询）
bash scripts/e2e_local.sh
```

### 2) 查看服务日志

```bash
docker compose logs -f av-service
```

> 已默认按 `LOCAL_UID/LOCAL_GID` 运行容器进程，避免输出文件归属 `root:root`。

### 3) 健康检查

```bash
curl http://127.0.0.1:8888/healthz
```

### 4) 提交合并任务

```bash
curl -X POST http://127.0.0.1:8888/api/v1/merge \
  -H 'Content-Type: application/json' \
  -d '{
    "video_path": "/data/input/video.mp4",
    "audio_path": "/data/input/audio.m4a",
    "output_path": "/data/output/out.mp4"
  }'
```

### 5) 查询任务状态

```bash
curl "http://127.0.0.1:8888/api/v1/task?task_id=task_xxx"
```

### 5.2) 一键端到端自测脚本

仓库内置 `scripts/e2e_local.sh`，默认会按下面顺序执行：

1. `docker compose up --build -d`
2. `GET /healthz`
3. `POST /api/v1/merge` + 轮询 `GET /api/v1/task`
4. `ffmpeg` 转 wav（16k/mono）
5. `POST /api/v1/asr` + 轮询 `GET /api/v1/task`
6. 校验 `/data/output/out.mp4` 与 `/data/output/audio.en.srt`

运行方式：

```bash
bash scripts/e2e_local.sh
```

可选变量（按需覆盖）：`BASE_URL`、`MODEL_NAME`、`LANGUAGE`、`MAX_RETRIES`、`SLEEP_SECONDS`。


### 5.1) 音频识别生成字幕（whisper.cpp C API）

先把输入音频转成 WAV（`pcm_s16le`, mono, 16kHz）：

```bash
ffmpeg -y -i data/input/audio.m4a -ar 16000 -ac 1 -c:a pcm_s16le data/input/audio.wav
```

然后提交识别任务：

```bash
curl -X POST http://127.0.0.1:8888/api/v1/asr \
  -H 'Content-Type: application/json' \
  -d '{
    "audio_path": "/data/input/audio.wav",
    "subtitle_path": "/data/output/audio.en.srt",
    "model_dir": "/models/whisper",
    "model_name": "ggml-base.en.bin",
    "language": "en"
  }'
```

> 当前 ASR 模块直接走 whisper.cpp C API。音频输入要求 WAV (`pcm_s16le`, mono, 16kHz)。


---

### 6) 一键更新（含健康检查与自动回滚）

```bash
chmod +x scripts/deploy_update.sh scripts/rollback_last.sh
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) ./scripts/deploy_update.sh
```

可选环境变量：

- `SERVICE_NAME`（默认 `av-service`）
- `LOCAL_UID` / `LOCAL_GID`（默认当前用户 uid/gid）
- `HEALTH_URL`（默认 `http://127.0.0.1:8888/healthz`）
- `MAX_RETRIES`（默认 `30`）
- `SLEEP_SECONDS`（默认 `2`）

如果需要手工回滚到某个镜像版本：

```bash
./scripts/rollback_last.sh ghcr.io/cipfz/youtobe-workflow/av-service:sha-<commit>
```



## Docker 镜像自动构建并发布（推荐）

我已经新增 GitHub Actions：`.github/workflows/docker-publish.yml`，会在 `main` 分支 push 后自动构建并发布镜像到 **GHCR**（GitHub Container Registry）。

镜像地址：

- `ghcr.io/<你的组织或用户名>/youtobe-workflow/av-service:latest`
- `ghcr.io/<你的组织或用户名>/youtobe-workflow/av-service:sha-<commit>`

本地服务器更新命令：

```bash
docker login ghcr.io
docker pull ghcr.io/<你的组织或用户名>/youtobe-workflow/av-service:latest
docker rm -f av-service || true
docker run -d --name av-service \
  -p 8888:8888 \
  -v $(pwd)/data/input:/data/input \
  -v $(pwd)/data/output:/data/output \
  -v $(pwd)/models/whisper:/models/whisper \
  ghcr.io/<你的组织或用户名>/youtobe-workflow/av-service:latest
```

如果你更喜欢 compose，可以把 `docker-compose.yml` 的 `image` 改成上面的 GHCR 镜像，然后：

```bash
docker compose pull
docker compose up -d
```

---

## GitHub Workflow 自动构建（无需本地编译）

- 工作流文件：`.github/workflows/server-ci.yml`
- 触发方式：
  - push 到 `main`
  - 对 `main` 发起 PR
  - Actions 页面手工点击 `Run workflow`

工作流两个档位：
1. `fallback`：关闭 workflow/ffmpeg 依赖，用于基础链路兜底验证。
2. `full`：安装并编译 `sogou/workflow` + FFmpeg 开发库，构建真实可运行服务。

`fallback` 的目的：
- 防止第三方依赖源波动导致 CI 完全不可用。
- 快速定位“代码问题”与“依赖环境问题”。

`full` 档位会上传发布包：`av-service-full.tar.gz`（包含可执行文件、依赖库、`run.sh`）。

---

## 本地（可选）

如果你临时想本地验证，才需要执行：

```bash
cmake -S server -B server/build
cmake --build server/build -j
ctest --test-dir server/build --output-on-failure
```

> 若本机未安装 workflow/FFmpeg 开发库，项目会进入 fallback 编译模式。
