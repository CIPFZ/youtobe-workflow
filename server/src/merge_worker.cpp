#include "merge_worker.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <sstream>
#include <thread>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
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

#ifdef HAVE_FFMPEG
namespace {

int find_stream_index(AVFormatContext* fmt, AVMediaType type) {
    return av_find_best_stream(fmt, type, -1, -1, nullptr, 0);
}

void remap_packet(AVPacket* pkt, AVStream* in_stream, AVStream* out_stream) {
    pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base,
                                static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base,
                                static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
    pkt->pos = -1;
}

int64_t pts_to_us(int64_t pts, AVRational tb) {
    if (pts == AV_NOPTS_VALUE) {
        return 0;
    }
    return av_rescale_q(pts, tb, AVRational{1, AV_TIME_BASE});
}

}  // namespace
#endif

int MergeWorker::run(const std::string& video_path,
                     const std::string& audio_path,
                     const std::string& output_path,
                     ProgressCallback on_progress) const {
#ifdef HAVE_FFMPEG
    AVFormatContext* video_in = nullptr;
    AVFormatContext* audio_in = nullptr;
    AVFormatContext* out_fmt = nullptr;

    if (avformat_open_input(&video_in, video_path.c_str(), nullptr, nullptr) < 0) {
        if (on_progress) on_progress(0, "failed: avformat_open_input(video)");
        return -1;
    }
    if (avformat_find_stream_info(video_in, nullptr) < 0) {
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: avformat_find_stream_info(video)");
        return -2;
    }

    if (avformat_open_input(&audio_in, audio_path.c_str(), nullptr, nullptr) < 0) {
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: avformat_open_input(audio)");
        return -3;
    }
    if (avformat_find_stream_info(audio_in, nullptr) < 0) {
        avformat_close_input(&audio_in);
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: avformat_find_stream_info(audio)");
        return -4;
    }

    const int v_idx = find_stream_index(video_in, AVMEDIA_TYPE_VIDEO);
    const int a_idx = find_stream_index(audio_in, AVMEDIA_TYPE_AUDIO);
    if (v_idx < 0 || a_idx < 0) {
        avformat_close_input(&audio_in);
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: missing video/audio stream");
        return -5;
    }

    if (avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_path.c_str()) < 0 || !out_fmt) {
        avformat_close_input(&audio_in);
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: alloc output context");
        return -6;
    }

    AVStream* out_v = avformat_new_stream(out_fmt, nullptr);
    AVStream* out_a = avformat_new_stream(out_fmt, nullptr);
    if (!out_v || !out_a) {
        avformat_free_context(out_fmt);
        avformat_close_input(&audio_in);
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: create output stream");
        return -7;
    }

    AVStream* in_v = video_in->streams[v_idx];
    AVStream* in_a = audio_in->streams[a_idx];

    if (avcodec_parameters_copy(out_v->codecpar, in_v->codecpar) < 0 ||
        avcodec_parameters_copy(out_a->codecpar, in_a->codecpar) < 0) {
        avformat_free_context(out_fmt);
        avformat_close_input(&audio_in);
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: copy codec params");
        return -8;
    }
    out_v->codecpar->codec_tag = 0;
    out_a->codecpar->codec_tag = 0;
    out_v->time_base = in_v->time_base;
    out_a->time_base = in_a->time_base;

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avformat_free_context(out_fmt);
            avformat_close_input(&audio_in);
            avformat_close_input(&video_in);
            if (on_progress) on_progress(0, "failed: avio_open(output)");
            return -9;
        }
    }

    if (avformat_write_header(out_fmt, nullptr) < 0) {
        if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&out_fmt->pb);
        }
        avformat_free_context(out_fmt);
        avformat_close_input(&audio_in);
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: avformat_write_header");
        return -10;
    }

    const int64_t total_us = std::max(
        pts_to_us(in_v->duration, in_v->time_base),
        pts_to_us(in_a->duration, in_a->time_base));

    if (on_progress) on_progress(5, "started remux");

    AVPacket vpkt;
    AVPacket apkt;
    av_init_packet(&vpkt);
    av_init_packet(&apkt);
    vpkt.data = nullptr; vpkt.size = 0;
    apkt.data = nullptr; apkt.size = 0;

    bool has_v = false, has_a = false;
    bool end_v = false, end_a = false;
    int64_t progressed_us = 0;

    auto read_next = [](AVFormatContext* fmt, int stream_idx, AVPacket* pkt, bool* ended) {
        while (true) {
            const int ret = av_read_frame(fmt, pkt);
            if (ret < 0) {
                *ended = true;
                return ret;
            }
            if (pkt->stream_index == stream_idx) {
                return 0;
            }
            av_packet_unref(pkt);
        }
    };

    while (true) {
        if (!has_v && !end_v) {
            has_v = (read_next(video_in, v_idx, &vpkt, &end_v) == 0);
        }
        if (!has_a && !end_a) {
            has_a = (read_next(audio_in, a_idx, &apkt, &end_a) == 0);
        }

        if (!has_v && !has_a) {
            break;
        }

        const bool use_v = has_v && (!has_a || av_compare_ts(vpkt.dts, in_v->time_base, apkt.dts, in_a->time_base) <= 0);
        AVPacket* cur = use_v ? &vpkt : &apkt;
        AVStream* in_stream = use_v ? in_v : in_a;
        AVStream* out_stream = use_v ? out_v : out_a;

        remap_packet(cur, in_stream, out_stream);
        cur->stream_index = out_stream->index;

        if (av_interleaved_write_frame(out_fmt, cur) < 0) {
            av_packet_unref(cur);
            if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt->pb);
            avformat_free_context(out_fmt);
            avformat_close_input(&audio_in);
            avformat_close_input(&video_in);
            if (on_progress) on_progress(0, "failed: av_interleaved_write_frame");
            return -11;
        }

        progressed_us = std::max(progressed_us, pts_to_us(cur->dts, out_stream->time_base));
        if (on_progress && total_us > 0) {
            int p = static_cast<int>(std::min<int64_t>(95, (progressed_us * 95) / total_us));
            on_progress(std::max(5, p), "remuxing");
        }

        av_packet_unref(cur);
        if (use_v) has_v = false; else has_a = false;
    }

    if (av_write_trailer(out_fmt) < 0) {
        if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt->pb);
        avformat_free_context(out_fmt);
        avformat_close_input(&audio_in);
        avformat_close_input(&video_in);
        if (on_progress) on_progress(0, "failed: av_write_trailer");
        return -12;
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&out_fmt->pb);
    }
    avformat_free_context(out_fmt);
    avformat_close_input(&audio_in);
    avformat_close_input(&video_in);

    if (on_progress) on_progress(100, "done");
    return 0;
#else
    (void)video_path;
    (void)audio_path;
    (void)output_path;

    for (int p = 20; p <= 100; p += 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        if (on_progress) {
            on_progress(p, p == 100 ? "done" : "processing");
        }
    }
    return 0;
#endif
}

}  // namespace avsvc
