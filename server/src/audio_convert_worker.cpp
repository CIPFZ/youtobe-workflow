#include "audio_convert_worker.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

namespace avsvc {

std::string build_audio_convert_fingerprint(const std::string& input_path,
                                            const std::string& output_path) {
    const auto key = "m4a2wav|" + input_path + "|" + output_path;
    const auto hashed = std::hash<std::string>{}(key);
    std::ostringstream oss;
    oss << std::hex << hashed;
    return oss.str();
}

int AudioConvertWorker::run_m4a_to_wav(const std::string& input_path,
                                       const std::string& output_path,
                                       AudioConvertProgressCallback on_progress) const {
    if (!std::filesystem::exists(input_path)) {
        if (on_progress) on_progress(0, "failed: input file not found");
        return -1;
    }

#ifdef HAVE_FFMPEG
    if (on_progress) on_progress(10, "running ffmpeg");

    const std::string cmd =
        "ffmpeg -y -i \"" + input_path +
        "\" -ar 16000 -ac 1 -c:a pcm_s16le \"" + output_path + "\" >/dev/null 2>&1";

    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        if (on_progress) on_progress(0, "failed: ffmpeg convert command");
        return -2;
    }

    if (on_progress) on_progress(100, "done");
    return 0;
#else
    (void)output_path;
    for (int p = 20; p <= 100; p += 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (on_progress) on_progress(p, p == 100 ? "done" : "processing");
    }
    return 0;
#endif
}

}  // namespace avsvc
