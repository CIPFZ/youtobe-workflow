#include "compose_worker.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
#endif

namespace avsvc {

std::string build_compose_fingerprint(const std::string& video_path,
                                      const std::string& audio_path,
                                      const std::string& subtitle_path,
                                      const std::string& output_path) {
    const auto key = "compose|" + video_path + "|" + audio_path + "|" + subtitle_path + "|" + output_path;
    const auto hashed = std::hash<std::string>{}(key);
    std::ostringstream oss;
    oss << std::hex << hashed;
    return oss.str();
}

#ifdef HAVE_FFMPEG
namespace {

void cleanup(AVFormatContext** video_in,
             AVFormatContext** audio_in,
             AVFormatContext** sub_in,
             AVFormatContext** out_fmt,
             AVCodecContext** sub_dec_ctx,
             AVCodecContext** sub_enc_ctx,
             AVPacket** vpkt,
             AVPacket** apkt,
             AVPacket** spkt,
             AVPacket** out_spkt) {
    if (vpkt && *vpkt) av_packet_free(vpkt);
    if (apkt && *apkt) av_packet_free(apkt);
    if (spkt && *spkt) av_packet_free(spkt);
    if (out_spkt && *out_spkt) av_packet_free(out_spkt);
    if (sub_dec_ctx && *sub_dec_ctx) avcodec_free_context(sub_dec_ctx);
    if (sub_enc_ctx && *sub_enc_ctx) avcodec_free_context(sub_enc_ctx);

    if (out_fmt && *out_fmt) {
        if (!((*out_fmt)->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&(*out_fmt)->pb);
        }
        avformat_free_context(*out_fmt);
        *out_fmt = nullptr;
    }
    if (sub_in && *sub_in) avformat_close_input(sub_in);
    if (audio_in && *audio_in) avformat_close_input(audio_in);
    if (video_in && *video_in) avformat_close_input(video_in);
}

int64_t ts_us(int64_t ts, AVRational tb) {
    if (ts == AV_NOPTS_VALUE) return 0;
    return av_rescale_q(ts, tb, AVRational{1, AV_TIME_BASE});
}

}  // namespace
#endif

int ComposeWorker::run(const std::string& video_path,
                       const std::string& audio_path,
                       const std::string& subtitle_path,
                       const std::string& output_path,
                       ComposeProgressCallback on_progress) const {
    if (!std::filesystem::exists(video_path) ||
        !std::filesystem::exists(audio_path) ||
        !std::filesystem::exists(subtitle_path)) {
        if (on_progress) on_progress(0, "failed: video/audio/subtitle file not found");
        return -1;
    }

#ifdef HAVE_FFMPEG
    AVFormatContext* video_in = nullptr;
    AVFormatContext* audio_in = nullptr;
    AVFormatContext* sub_in = nullptr;
    AVFormatContext* out_fmt = nullptr;
    AVCodecContext* sub_dec_ctx = nullptr;
    AVCodecContext* sub_enc_ctx = nullptr;
    AVPacket* vpkt = nullptr;
    AVPacket* apkt = nullptr;
    AVPacket* spkt = nullptr;
    AVPacket* out_spkt = nullptr;

    if (on_progress) on_progress(5, "opening inputs");

    if (avformat_open_input(&video_in, video_path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(video_in, nullptr) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: open video input");
        return -2;
    }
    if (avformat_open_input(&audio_in, audio_path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(audio_in, nullptr) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: open audio input");
        return -3;
    }
    if (avformat_open_input(&sub_in, subtitle_path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(sub_in, nullptr) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: open subtitle input");
        return -4;
    }

    const int v_idx = av_find_best_stream(video_in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int a_idx = av_find_best_stream(audio_in, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    const int s_idx = av_find_best_stream(sub_in, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
    if (v_idx < 0 || a_idx < 0 || s_idx < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: missing video/audio/subtitle stream");
        return -5;
    }

    if (avformat_alloc_output_context2(&out_fmt, nullptr, "mp4", output_path.c_str()) < 0 || !out_fmt) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: alloc output mp4");
        return -6;
    }

    AVStream* in_v = video_in->streams[v_idx];
    AVStream* in_a = audio_in->streams[a_idx];

    AVStream* out_v = avformat_new_stream(out_fmt, nullptr);
    AVStream* out_a = avformat_new_stream(out_fmt, nullptr);
    if (!out_v || !out_a ||
        avcodec_parameters_copy(out_v->codecpar, in_v->codecpar) < 0 ||
        avcodec_parameters_copy(out_a->codecpar, in_a->codecpar) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: create/copy AV output streams");
        return -7;
    }
    out_v->codecpar->codec_tag = 0;
    out_a->codecpar->codec_tag = 0;
    out_v->time_base = in_v->time_base;
    out_a->time_base = in_a->time_base;

    AVStream* in_s = sub_in->streams[s_idx];
    const AVCodec* sub_dec = avcodec_find_decoder(in_s->codecpar->codec_id);
    const AVCodec* sub_enc = avcodec_find_encoder(AV_CODEC_ID_MOV_TEXT);
    if (!sub_dec || !sub_enc) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: subtitle decoder/encoder not found");
        return -8;
    }

    sub_dec_ctx = avcodec_alloc_context3(sub_dec);
    sub_enc_ctx = avcodec_alloc_context3(sub_enc);
    if (!sub_dec_ctx || !sub_enc_ctx ||
        avcodec_parameters_to_context(sub_dec_ctx, in_s->codecpar) < 0 ||
        avcodec_open2(sub_dec_ctx, sub_dec, nullptr) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: open subtitle decoder");
        return -9;
    }

    sub_enc_ctx->time_base = AVRational{1, 1000};
    if (avcodec_open2(sub_enc_ctx, sub_enc, nullptr) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: open mov_text encoder");
        return -10;
    }

    AVStream* out_s = avformat_new_stream(out_fmt, nullptr);
    if (!out_s || avcodec_parameters_from_context(out_s->codecpar, sub_enc_ctx) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: create output subtitle stream");
        return -11;
    }
    out_s->time_base = sub_enc_ctx->time_base;

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE) && avio_open(&out_fmt->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: open output file");
        return -12;
    }

    if (avformat_write_header(out_fmt, nullptr) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: avformat_write_header");
        return -13;
    }

    vpkt = av_packet_alloc();
    apkt = av_packet_alloc();
    spkt = av_packet_alloc();
    out_spkt = av_packet_alloc();
    if (!vpkt || !apkt || !spkt || !out_spkt) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: alloc packets");
        return -14;
    }

    bool has_v = false, end_v = false;
    bool has_a = false, end_a = false;
    bool has_s = false, end_s = false;

    auto read_v = [&]() {
        while (true) {
            const int ret = av_read_frame(video_in, vpkt);
            if (ret < 0) {
                end_v = true;
                return;
            }
            if (vpkt->stream_index == v_idx) {
                has_v = true;
                return;
            }
            av_packet_unref(vpkt);
        }
    };
    auto read_a = [&]() {
        while (true) {
            const int ret = av_read_frame(audio_in, apkt);
            if (ret < 0) {
                end_a = true;
                return;
            }
            if (apkt->stream_index == a_idx) {
                has_a = true;
                return;
            }
            av_packet_unref(apkt);
        }
    };
    auto read_s = [&]() {
        while (true) {
            const int ret = av_read_frame(sub_in, spkt);
            if (ret < 0) {
                end_s = true;
                return;
            }
            if (spkt->stream_index == s_idx) {
                has_s = true;
                return;
            }
            av_packet_unref(spkt);
        }
    };

    if (on_progress) on_progress(20, "composing streams");

    while (true) {
        if (!has_v && !end_v) read_v();
        if (!has_a && !end_a) read_a();
        if (!has_s && !end_s) read_s();
        if (!has_v && !has_a && !has_s) break;

        const int64_t v_us = has_v ? ts_us(vpkt->dts, in_v->time_base) : std::numeric_limits<int64_t>::max();
        const int64_t a_us = has_a ? ts_us(apkt->dts, in_a->time_base) : std::numeric_limits<int64_t>::max();
        const int64_t s_us = has_s ? ts_us(spkt->dts, in_s->time_base) : std::numeric_limits<int64_t>::max();

        if (v_us <= a_us && v_us <= s_us) {
            av_packet_rescale_ts(vpkt, in_v->time_base, out_v->time_base);
            vpkt->stream_index = out_v->index;
            if (av_interleaved_write_frame(out_fmt, vpkt) < 0) {
                av_packet_unref(vpkt);
                cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
                if (on_progress) on_progress(0, "failed: write video packet");
                return -15;
            }
            av_packet_unref(vpkt);
            has_v = false;
        } else if (a_us <= v_us && a_us <= s_us) {
            av_packet_rescale_ts(apkt, in_a->time_base, out_a->time_base);
            apkt->stream_index = out_a->index;
            if (av_interleaved_write_frame(out_fmt, apkt) < 0) {
                av_packet_unref(apkt);
                cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
                if (on_progress) on_progress(0, "failed: write audio packet");
                return -16;
            }
            av_packet_unref(apkt);
            has_a = false;
        } else {
            AVSubtitle sub{};
            int got_sub = 0;
            const int ret = avcodec_decode_subtitle2(sub_dec_ctx, &sub, &got_sub, spkt);
            av_packet_unref(spkt);
            has_s = false;
            if (ret < 0 || !got_sub) {
                continue;
            }

            std::vector<uint8_t> buf(64 * 1024);
            const int enc_size = avcodec_encode_subtitle(sub_enc_ctx, buf.data(), static_cast<int>(buf.size()), &sub);
            const int start = sub.start_display_time;
            const int end = sub.end_display_time;
            const int64_t pts_from_sub = sub.pts;
            avsubtitle_free(&sub);

            if (enc_size <= 0) {
                continue;
            }

            av_packet_unref(out_spkt);
            if (av_new_packet(out_spkt, enc_size) < 0) {
                cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
                if (on_progress) on_progress(0, "failed: alloc subtitle packet");
                return -17;
            }
            std::memcpy(out_spkt->data, buf.data(), static_cast<size_t>(enc_size));

            const int64_t base_pts = (pts_from_sub == AV_NOPTS_VALUE) ? 0 : (pts_from_sub / 1000);
            out_spkt->pts = base_pts + start;
            out_spkt->dts = out_spkt->pts;
            out_spkt->duration = std::max(1, end > start ? end - start : 1000);
            out_spkt->stream_index = out_s->index;

            if (av_interleaved_write_frame(out_fmt, out_spkt) < 0) {
                av_packet_unref(out_spkt);
                cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
                if (on_progress) on_progress(0, "failed: write subtitle packet");
                return -18;
            }
            av_packet_unref(out_spkt);
        }

        if (on_progress) on_progress(80, "composing");
    }

    if (av_write_trailer(out_fmt) < 0) {
        cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
        if (on_progress) on_progress(0, "failed: av_write_trailer");
        return -19;
    }

    cleanup(&video_in, &audio_in, &sub_in, &out_fmt, &sub_dec_ctx, &sub_enc_ctx, &vpkt, &apkt, &spkt, &out_spkt);
    if (on_progress) on_progress(100, "done");
    return 0;
#else
    (void)video_path;
    (void)audio_path;
    (void)subtitle_path;
    (void)output_path;

    for (int p = 20; p <= 100; p += 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (on_progress) on_progress(p, p == 100 ? "done" : "processing");
    }
    return 0;
#endif
}

}  // namespace avsvc
