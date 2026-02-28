#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace avsvc {

enum class TaskStatus {
    Pending,
    Running,
    Success,
    Failed,
    Canceled,
};

struct TaskRecord {
    std::string task_id;
    std::string fingerprint;
    std::string video_path;
    std::string audio_path;
    std::string output_path;
    TaskStatus status{TaskStatus::Pending};
    int progress{0};
    std::string message;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
};

class TaskManager {
public:
    std::string create_or_reuse(const std::string& fingerprint,
                                const std::string& video_path,
                                const std::string& audio_path,
                                const std::string& output_path,
                                bool* reused);

    std::optional<TaskRecord> get_task(const std::string& task_id) const;
    void update_status(const std::string& task_id, TaskStatus status, int progress, const std::string& message);

private:
    static std::string now_id();

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, TaskRecord> tasks_;
    std::unordered_map<std::string, std::string> fingerprint_index_;
};

std::string to_string(TaskStatus status);

}  // namespace avsvc
