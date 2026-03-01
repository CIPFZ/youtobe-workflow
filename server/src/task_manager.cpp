#include "task_manager.h"

#include <atomic>
#include <sstream>

namespace avsvc {

namespace {
std::atomic<uint64_t> g_seq{0};
}

std::string TaskManager::now_id() {
    const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream oss;
    oss << "task_" << epoch_ms << "_" << g_seq.fetch_add(1);
    return oss.str();
}

std::string TaskManager::create_or_reuse(const std::string& fingerprint,
                                         const std::string& video_path,
                                         const std::string& audio_path,
                                         const std::string& output_path,
                                         bool* reused) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = fingerprint_index_.find(fingerprint);
    if (it != fingerprint_index_.end()) {
        if (reused) {
            *reused = true;
        }
        return it->second;
    }

    TaskRecord rec;
    rec.task_id = now_id();
    rec.fingerprint = fingerprint;
    rec.video_path = video_path;
    rec.audio_path = audio_path;
    rec.output_path = output_path;
    rec.status = TaskStatus::Pending;
    rec.progress = 0;
    rec.message = "queued";
    rec.created_at = std::chrono::system_clock::now();
    rec.updated_at = rec.created_at;

    fingerprint_index_[fingerprint] = rec.task_id;
    tasks_[rec.task_id] = rec;
    if (reused) {
        *reused = false;
    }
    return rec.task_id;
}

std::optional<TaskRecord> TaskManager::get_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void TaskManager::update_status(const std::string& task_id,
                                TaskStatus status,
                                int progress,
                                const std::string& message) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return;
    }
    it->second.status = status;
    it->second.progress = progress;
    it->second.message = message;
    it->second.updated_at = std::chrono::system_clock::now();
}

std::string to_string(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending:
            return "PENDING";
        case TaskStatus::Running:
            return "RUNNING";
        case TaskStatus::Success:
            return "SUCCESS";
        case TaskStatus::Failed:
            return "FAILED";
        case TaskStatus::Canceled:
            return "CANCELED";
    }
    return "UNKNOWN";
}

}  // namespace avsvc
