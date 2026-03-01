#include "asr_worker.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifdef HAVE_WHISPERCPP
extern "C" {
#include <whisper.h>
}
#endif

namespace avsvc {

namespace {

bool read_wav_16k_mono_s16le(const std::string& path, std::vector<float>* pcm) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    char riff[4] = {0};
    in.read(riff, 4);
    if (std::string(riff, 4) != "RIFF") {
        return false;
    }
    in.seekg(22, std::ios::beg);

    uint16_t channels = 0;
    in.read(reinterpret_cast<char*>(&channels), sizeof(channels));
    uint32_t sample_rate = 0;
    in.read(reinterpret_cast<char*>(&sample_rate), sizeof(sample_rate));

    in.seekg(34, std::ios::beg);
    uint16_t bits_per_sample = 0;
    in.read(reinterpret_cast<char*>(&bits_per_sample), sizeof(bits_per_sample));

    if (channels != 1 || sample_rate != 16000 || bits_per_sample != 16) {
        return false;
    }

    in.seekg(12, std::ios::beg);
    while (in.good()) {
        char chunk_id[4] = {0};
        uint32_t chunk_size = 0;
        in.read(chunk_id, 4);
        in.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
        if (!in.good()) {
            break;
        }

        if (std::string(chunk_id, 4) == "data") {
            std::vector<int16_t> data(chunk_size / sizeof(int16_t));
            in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(chunk_size));
            if (!in.good()) {
                return false;
            }

            pcm->clear();
            pcm->reserve(data.size());
            for (int16_t v : data) {
                pcm->push_back(static_cast<float>(v) / 32768.0f);
            }
            return true;
        }

        in.seekg(chunk_size, std::ios::cur);
    }

    return false;
}

bool write_srt(const std::string& subtitle_path,
               const std::vector<std::pair<std::pair<int, int>, std::string>>& segments) {
    std::ofstream out(subtitle_path);
    if (!out.is_open()) {
        return false;
    }

    auto ms_to_ts = [](int ms) {
        int h = ms / 3600000;
        ms %= 3600000;
        int m = ms / 60000;
        ms %= 60000;
        int s = ms / 1000;
        int rem = ms % 1000;

        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", h, m, s, rem);
        return std::string(buf);
    };

    int idx = 1;
    for (const auto& seg : segments) {
        out << idx++ << "\n";
        out << ms_to_ts(seg.first.first) << " --> " << ms_to_ts(seg.first.second) << "\n";
        out << seg.second << "\n\n";
    }
    return true;
}

}  // namespace

int AsrWorker::run(const std::string& audio_path,
                   const std::string& subtitle_path,
                   const std::string& model_dir,
                   const std::string& model_name,
                   const std::string& language,
                   AsrProgressCallback on_progress) const {
#ifdef HAVE_WHISPERCPP
    if (on_progress) on_progress(5, "loading audio");

    std::vector<float> pcm;
    if (!read_wav_16k_mono_s16le(audio_path, &pcm)) {
        if (on_progress) {
            on_progress(0, "failed: audio must be WAV pcm_s16le mono 16kHz");
        }
        return -1;
    }

    const std::string model_path = model_dir + "/" + model_name;
    whisper_context_params cparams = whisper_context_default_params();
    whisper_context* ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!ctx) {
        if (on_progress) on_progress(0, "failed: whisper_init_from_file_with_params");
        return -2;
    }

    if (on_progress) on_progress(20, "running whisper");

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress = false;
    params.print_special = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.translate = false;
    params.language = language.empty() ? "en" : language.c_str();

    if (whisper_full(ctx, params, pcm.data(), static_cast<int>(pcm.size())) != 0) {
        whisper_free(ctx);
        if (on_progress) on_progress(0, "failed: whisper_full");
        return -3;
    }

    std::vector<std::pair<std::pair<int, int>, std::string>> segments;
    const int n = whisper_full_n_segments(ctx);
    segments.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int t0 = whisper_full_get_segment_t0(ctx, i) * 10;
        const int t1 = whisper_full_get_segment_t1(ctx, i) * 10;
        const char* txt = whisper_full_get_segment_text(ctx, i);
        segments.push_back({{t0, t1}, txt ? std::string(txt) : std::string()});
    }

    if (on_progress) on_progress(85, "writing subtitle");

    const bool ok = write_srt(subtitle_path, segments);
    whisper_free(ctx);

    if (!ok) {
        if (on_progress) on_progress(0, "failed: write subtitle file");
        return -4;
    }

    if (on_progress) on_progress(100, "done");
    return 0;
#else
    (void)audio_path;
    (void)subtitle_path;
    (void)model_dir;
    (void)model_name;
    (void)language;
    if (on_progress) on_progress(0, "failed: whisper.cpp C API not enabled");
    return -100;
#endif
}

}  // namespace avsvc
