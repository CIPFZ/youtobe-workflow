#pragma once

#include <functional>
#include <string>

namespace avsvc {

using SubtitleEmbedProgressCallback = std::function<void(int, const std::string&)>;

class SubtitleEmbedWorker {
public:
    int run_ass_to_mp4(const std::string& video_path,
                       const std::string& subtitle_path,
                       const std::string& output_path,
                       SubtitleEmbedProgressCallback on_progress) const;
};

std::string build_subtitle_embed_fingerprint(const std::string& video_path,
                                             const std::string& subtitle_path,
                                             const std::string& output_path);

}  // namespace avsvc
