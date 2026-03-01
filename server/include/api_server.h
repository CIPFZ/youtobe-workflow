#pragma once

#include "asr_worker.h"
#include "audio_convert_worker.h"
#include "merge_worker.h"
#include "task_manager.h"

#include <string>

namespace avsvc {

class ApiServer {
public:
    ApiServer(TaskManager& manager, MergeWorker& worker, AsrWorker& asr_worker, AudioConvertWorker& audio_convert_worker);
    int start(unsigned short port) const;

private:
    TaskManager& manager_;
    MergeWorker& worker_;
    AsrWorker& asr_worker_;
    AudioConvertWorker& audio_convert_worker_;
};

}  // namespace avsvc
