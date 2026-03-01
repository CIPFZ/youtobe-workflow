#pragma once

#include "merge_worker.h"
#include "task_manager.h"

#include <string>

namespace avsvc {

class ApiServer {
public:
    ApiServer(TaskManager& manager, MergeWorker& worker);
    int start(unsigned short port) const;

private:
    TaskManager& manager_;
    MergeWorker& worker_;
};

}  // namespace avsvc
