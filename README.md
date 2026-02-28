# youtobe-workflow

当前目标：基于 **sogou/workflow** 提供 Web API，并在接口模块中集成 **FFmpeg C API** 完成音视频合并（先做一个简单可用功能）。

## 1) 使用 GitHub Workflow 自动构建（无需本地编译）

你已经把代码合入 `main` 后，可以完全依赖 GitHub Actions：

- 工作流文件：`.github/workflows/server-ci.yml`
- 触发方式：
  - push 到 `main`
  - 对 `main` 发起 PR
  - Actions 页面手工点击 `Run workflow`

工作流会执行两个构建档位：
1. `fallback`：关闭 workflow/ffmpeg 依赖，验证基础工程与测试链路。
2. `ffmpeg`：开启 FFmpeg C API（安装 `libav*` 开发包），验证 FFmpeg 集成链路可编译可测试。

每次运行会上传构建产物（`av_service`）和测试输出（`Testing/**`）到 Actions Artifacts，方便你直接下载查看。

---

## 2) 本地（可选）

如果你临时想本地验证，才需要执行：

```bash
cmake -S server -B server/build
cmake --build server/build -j
ctest --test-dir server/build --output-on-failure
```

> 若本机未安装 workflow/FFmpeg 开发库，项目会进入 fallback 编译模式。

---

## 3) 接口说明（第一版）

启动服务（需要 workflow 依赖可用）：

```bash
./server/build/av_service
```

健康检查：

```bash
curl http://127.0.0.1:8888/healthz
```

提交合并任务：

```bash
curl -X POST http://127.0.0.1:8888/api/v1/merge \
  -H 'Content-Type: application/json' \
  -d '{
    "video_path": "/data/input/video.mp4",
    "audio_path": "/data/input/audio.m4a",
    "output_path": "/data/output/out.mp4"
  }'
```

查询任务状态：

```bash
curl "http://127.0.0.1:8888/api/v1/task?task_id=task_xxx"
```
