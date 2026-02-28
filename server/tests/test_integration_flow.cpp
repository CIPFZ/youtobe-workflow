#include "merge_worker.h"
#include "task_manager.h"

#include <cassert>
#include <string>

using namespace avsvc;

int main() {
    TaskManager manager;
    MergeWorker worker;

    bool reused = false;
    const std::string fp = build_fingerprint("v.mp4", "a.m4a", "o.mp4");
    const std::string task_id = manager.create_or_reuse(fp, "v.mp4", "a.m4a", "o.mp4", &reused);
    assert(!task_id.empty());
    assert(!reused);

    manager.update_status(task_id, TaskStatus::Running, 0, "worker started");

    int rc = worker.run("v.mp4", "a.m4a", "o.mp4", [&](int p, const std::string& msg) {
        manager.update_status(task_id, p >= 100 ? TaskStatus::Success : TaskStatus::Running, p, msg);
    });

    if (rc != 0) {
        manager.update_status(task_id, TaskStatus::Failed, 0, "merge failed");
    }

    auto rec = manager.get_task(task_id);
    assert(rec.has_value());
#ifdef HAVE_FFMPEG
    assert(rec->status == TaskStatus::Failed);
#else
    assert(rec->status == TaskStatus::Success);
    assert(rec->progress == 100);
#endif

    return 0;
}
