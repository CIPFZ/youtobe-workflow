#pragma once

#include <functional>
#include <string>

namespace avsvc {

using AudioConvertProgressCallback = std::function<void(int, const std::string&)>;

class AudioConvertWorker {
public:
    int run_m4a_to_wav(const std::string& input_path,
                       const std::string& output_path,
                       AudioConvertProgressCallback on_progress) const;
};

std::string build_audio_convert_fingerprint(const std::string& input_path,
                                            const std::string& output_path);

}  // namespace avsvc
