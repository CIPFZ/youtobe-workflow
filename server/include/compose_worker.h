#pragma once

#include <functional>
#include <string>

namespace avsvc {

using ComposeProgressCallback = std::function<void(int, const std::string&)>;

class ComposeWorker {
public:
    int run(const std::string& video_path,
            const std::string& audio_path,
            const std::string& subtitle_path,
            const std::string& output_path,
            ComposeProgressCallback on_progress) const;
};

std::string build_compose_fingerprint(const std::string& video_path,
                                      const std::string& audio_path,
                                      const std::string& subtitle_path,
                                      const std::string& output_path);

}  // namespace avsvc
