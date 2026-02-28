#include "merge_worker.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <sstream>
#include <thread>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

namespace avsvc {

std::string build_fingerprint(const std::string& video_path,
                              const std::string& audio_path,
                              const std::string& output_path) {
    const auto key = video_path + "|" + audio_path + "|" + output_path;
    const auto hashed = std::hash<std::string>{}(key);
    std::ostringstream oss;
    oss << std::hex << hashed;
    return oss.str();
}

int MergeWorker::run(const std::string& video_path,
                     const std::string& audio_path,
                     const std::string& output_path,
                     ProgressCallback on_progress) const {
    (void)video_path;
    (void)audio_path;
    (void)output_path;

#ifdef HAVE_FFMPEG
    // 起步阶段：先建立 libavformat 探测与可用性校验，后续接入完整 remux 流程。
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, video_path.c_str(), nullptr, nullptr) != 0) {
        if (on_progress) {
            on_progress(0, "failed: avformat_open_input(video)");
        }
        return -1;
    }

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        if (on_progress) {
            on_progress(0, "failed: avformat_find_stream_info(video)");
        }
        return -2;
    }

    avformat_close_input(&fmt);
    if (on_progress) {
        on_progress(10, "ffmpeg libavformat probe ready");
    }
#endif

    // 临时模拟处理流程：用于先跑通 Task 状态机与 API。
    for (int p = 20; p <= 100; p += 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        if (on_progress) {
            on_progress(p, p == 100 ? "done" : "processing");
        }
    }
    return 0;
}

}  // namespace avsvc
