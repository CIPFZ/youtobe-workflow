#include "task_manager.h"

#include <cassert>
#include <string>

using namespace avsvc;

int main() {
    TaskManager manager;

    bool reused = false;
    std::string t1 = manager.create_or_reuse("fp1", "v1.mp4", "a1.m4a", "o1.mp4", &reused);
    assert(!t1.empty());
    assert(reused == false);

    std::string t2 = manager.create_or_reuse("fp1", "v1.mp4", "a1.m4a", "o1.mp4", &reused);
    assert(t2 == t1);
    assert(reused == true);

    auto rec = manager.get_task(t1);
    assert(rec.has_value());
    assert(rec->status == TaskStatus::Pending);
    assert(rec->progress == 0);

    manager.update_status(t1, TaskStatus::Running, 37, "processing");
    rec = manager.get_task(t1);
    assert(rec.has_value());
    assert(rec->status == TaskStatus::Running);
    assert(rec->progress == 37);
    assert(rec->message == "processing");

    manager.update_status(t1, TaskStatus::Success, 100, "done");
    rec = manager.get_task(t1);
    assert(rec.has_value());
    assert(rec->status == TaskStatus::Success);
    assert(rec->progress == 100);
    assert(to_string(rec->status) == "SUCCESS");

    return 0;
}
