# youtobe-workflow

当前目标：基于 **sogou/workflow** 提供 Web API，并在接口模块中集成 **FFmpeg C API** 完成音视频合并（先做一个简单可用功能）。

## 推荐部署方式：Docker（避免本地缺库）

你遇到的 `libworkflow.so / libavformat.so not found` 本质上是运行环境缺动态库。对于当前阶段，**最稳妥方式就是直接用 Docker 部署运行**。

### 1) 构建并启动

```bash
docker compose up --build -d
```

### 2) 查看服务日志

```bash
docker compose logs -f av-service
```

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
