#pragma once

#include <functional>
#include <string>

namespace avsvc {

using ProgressCallback = std::function<void(int, const std::string&)>;

class MergeWorker {
public:
    int run(const std::string& video_path,
            const std::string& audio_path,
            const std::string& output_path,
            ProgressCallback on_progress) const;
};

std::string build_fingerprint(const std::string& video_path,
                              const std::string& audio_path,
                              const std::string& output_path);

}  // namespace avsvc
