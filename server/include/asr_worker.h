#pragma once

#include <functional>
#include <string>

namespace avsvc {

using AsrProgressCallback = std::function<void(int, const std::string&)>;

class AsrWorker {
public:
    int run(const std::string& audio_path,
            const std::string& subtitle_path,
            const std::string& model_dir,
            const std::string& model_name,
            const std::string& language,
            AsrProgressCallback on_progress) const;
};

}  // namespace avsvc
