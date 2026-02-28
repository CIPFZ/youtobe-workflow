# youtobe-workflow

当前目标：基于 **sogou/workflow** 提供 Web API，并在接口模块中集成 **FFmpeg C API** 完成音视频合并（先做一个简单可用功能）。

## 1. 本地构建与测试

```bash
cmake -S server -B server/build
cmake --build server/build -j
ctest --test-dir server/build --output-on-failure
```

> 若本机未安装 workflow/FFmpeg 开发库，项目会进入 fallback 编译模式（用于先验证工程结构与测试链路）。

---

## 2. 启动服务（需要 workflow）

当检测到 workflow 头文件 + 库后，服务会监听 `0.0.0.0:8888`：

```bash
./server/build/av_service
```

健康检查：

```bash
curl http://127.0.0.1:8888/healthz
```

---

## 3. 调用合并接口（第一版）

### 下发任务

```bash
curl -X POST http://127.0.0.1:8888/api/v1/merge \
  -H 'Content-Type: application/json' \
  -d '{
    "video_path": "/data/input/video.mp4",
    "audio_path": "/data/input/audio.m4a",
    "output_path": "/data/output/out.mp4"
  }'
```

返回示例：

```json
{"task_id":"task_...","reused":false}
```

### 查询任务状态

```bash
curl "http://127.0.0.1:8888/api/v1/task?task_id=task_xxx"
```

返回示例：

```json
{"task_id":"task_xxx","status":"RUNNING","progress":40,"message":"remuxing"}
```

---

## 4. GitHub 自动构建

仓库内已包含 `.github/workflows/server-ci.yml`，push / PR 会自动执行：
1. CMake configure
2. CMake build
3. CTest
4. 服务 smoke run

