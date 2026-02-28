#include "merge_worker.h"
#include "task_manager.h"

#include <iostream>
#include <thread>

namespace avsvc {

void run_demo_task(TaskManager& manager,
                   MergeWorker& worker,
                   const std::string& video,
                   const std::string& audio,
                   const std::string& output) {
    bool reused = false;
    const auto fp = build_fingerprint(video, audio, output);
    const auto task_id = manager.create_or_reuse(fp, video, audio, output, &reused);

    if (reused) {
        std::cout << "reused task_id=" << task_id << '\n';
        return;
    }

    manager.update_status(task_id, TaskStatus::Running, 0, "worker started");
    const int rc = worker.run(video, audio, output, [&](int progress, const std::string& msg) {
        auto status = progress >= 100 ? TaskStatus::Success : TaskStatus::Running;
        manager.update_status(task_id, status, progress, msg);
        std::cout << "task=" << task_id << " status=" << to_string(status)
                  << " progress=" << progress << " msg=" << msg << '\n';
    });

    if (rc != 0) {
        manager.update_status(task_id, TaskStatus::Failed, 0, "merge failed");
        std::cout << "task=" << task_id << " status=FAILED\n";
    }
}

}  // namespace avsvc

int main() {
    std::cout << "av_service bootstrap (workflow+ffmpeg deep integration, phase-1)\n";
#ifdef HAVE_WORKFLOW
    std::cout << "workflow headers detected\n";
#else
    std::cout << "workflow headers not detected: running standalone bootstrap mode\n";
#endif

    avsvc::TaskManager manager;
    avsvc::MergeWorker worker;

    std::thread t1(avsvc::run_demo_task, std::ref(manager), std::ref(worker),
                   "/data/input/video.mp4", "/data/input/audio.m4a", "/data/output/out.mp4");
    t1.join();

    return 0;
}
