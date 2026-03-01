#include "subtitle_embed_worker.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
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

std::string build_subtitle_embed_fingerprint(const std::string& video_path,
                                             const std::string& subtitle_path,
                                             const std::string& output_path) {
    const auto key = "ass2mp4|" + video_path + "|" + subtitle_path + "|" + output_path;
    const auto hashed = std::hash<std::string>{}(key);
    std::ostringstream oss;
    oss << std::hex << hashed;
    return oss.str();
}

#ifdef HAVE_FFMPEG
namespace {

void cleanup(AVFormatContext** video_in,
             AVFormatContext** sub_in,
             AVFormatContext** out_fmt,
             AVCodecContext** dec_ctx,
             AVCodecContext** enc_ctx,
             AVPacket** vpkt,
             AVPacket** spkt,
             AVPacket** out_pkt) {
    if (vpkt && *vpkt) av_packet_free(vpkt);
    if (spkt && *spkt) av_packet_free(spkt);
    if (out_pkt && *out_pkt) av_packet_free(out_pkt);
    if (dec_ctx && *dec_ctx) avcodec_free_context(dec_ctx);
    if (enc_ctx && *enc_ctx) avcodec_free_context(enc_ctx);

    if (out_fmt && *out_fmt) {
        if (!((*out_fmt)->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&(*out_fmt)->pb);
        }
        avformat_free_context(*out_fmt);
        *out_fmt = nullptr;
    }
    if (sub_in && *sub_in) avformat_close_input(sub_in);
    if (video_in && *video_in) avformat_close_input(video_in);
}

int64_t ts_us(int64_t ts, AVRational tb) {
    if (ts == AV_NOPTS_VALUE) return 0;
    return av_rescale_q(ts, tb, AVRational{1, AV_TIME_BASE});
}

}  // namespace
#endif

int SubtitleEmbedWorker::run_ass_to_mp4(const std::string& video_path,
                                        const std::string& subtitle_path,
                                        const std::string& output_path,
                                        SubtitleEmbedProgressCallback on_progress) const {
    if (!std::filesystem::exists(video_path) || !std::filesystem::exists(subtitle_path)) {
        if (on_progress) on_progress(0, "failed: video/subtitle file not found");
        return -1;
    }

#ifdef HAVE_FFMPEG
    AVFormatContext* video_in = nullptr;
    AVFormatContext* sub_in = nullptr;
    AVFormatContext* out_fmt = nullptr;
    AVCodecContext* dec_ctx = nullptr;
    AVCodecContext* enc_ctx = nullptr;
    AVPacket* vpkt = nullptr;
    AVPacket* spkt = nullptr;
    AVPacket* out_pkt = nullptr;

    if (avformat_open_input(&video_in, video_path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(video_in, nullptr) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: open video input");
        return -2;
    }

    if (avformat_open_input(&sub_in, subtitle_path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(sub_in, nullptr) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: open subtitle input");
        return -3;
    }

    int sub_idx = av_find_best_stream(sub_in, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
    if (sub_idx < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: no subtitle stream in ass file");
        return -4;
    }

    if (avformat_alloc_output_context2(&out_fmt, nullptr, "mp4", output_path.c_str()) < 0 || !out_fmt) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: alloc output mp4");
        return -5;
    }

    std::vector<int> stream_map(video_in->nb_streams, -1);
    for (unsigned int i = 0; i < video_in->nb_streams; ++i) {
        AVStream* in_st = video_in->streams[i];
        AVStream* out_st = avformat_new_stream(out_fmt, nullptr);
        if (!out_st || avcodec_parameters_copy(out_st->codecpar, in_st->codecpar) < 0) {
            cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
            if (on_progress) on_progress(0, "failed: copy video/audio stream");
            return -6;
        }
        out_st->codecpar->codec_tag = 0;
        out_st->time_base = in_st->time_base;
        stream_map[i] = out_st->index;
    }

    AVStream* in_sub_stream = sub_in->streams[sub_idx];
    const AVCodec* sub_dec = avcodec_find_decoder(in_sub_stream->codecpar->codec_id);
    const AVCodec* sub_enc = avcodec_find_encoder(AV_CODEC_ID_MOV_TEXT);
    if (!sub_dec || !sub_enc) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: subtitle decoder/encoder not found");
        return -7;
    }

    dec_ctx = avcodec_alloc_context3(sub_dec);
    enc_ctx = avcodec_alloc_context3(sub_enc);
    if (!dec_ctx || !enc_ctx ||
        avcodec_parameters_to_context(dec_ctx, in_sub_stream->codecpar) < 0 ||
        avcodec_open2(dec_ctx, sub_dec, nullptr) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: open subtitle decoder");
        return -8;
    }

    enc_ctx->time_base = AVRational{1, 1000};
    if (avcodec_open2(enc_ctx, sub_enc, nullptr) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: open mov_text encoder");
        return -9;
    }

    AVStream* out_sub_stream = avformat_new_stream(out_fmt, nullptr);
    if (!out_sub_stream || avcodec_parameters_from_context(out_sub_stream->codecpar, enc_ctx) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: create output subtitle stream");
        return -10;
    }
    out_sub_stream->time_base = enc_ctx->time_base;

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE) && avio_open(&out_fmt->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: open output file");
        return -11;
    }

    if (avformat_write_header(out_fmt, nullptr) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: avformat_write_header");
        return -12;
    }

    vpkt = av_packet_alloc();
    spkt = av_packet_alloc();
    out_pkt = av_packet_alloc();
    if (!vpkt || !spkt || !out_pkt) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: alloc packets");
        return -13;
    }

    bool has_v = false, end_v = false;
    bool has_s = false, end_s = false;

    auto read_video = [&]() {
        while (true) {
            const int ret = av_read_frame(video_in, vpkt);
            if (ret < 0) {
                end_v = true;
                return;
            }
            has_v = true;
            return;
        }
    };

    auto read_sub = [&]() {
        while (true) {
            const int ret = av_read_frame(sub_in, spkt);
            if (ret < 0) {
                end_s = true;
                return;
            }
            if (spkt->stream_index == sub_idx) {
                has_s = true;
                return;
            }
            av_packet_unref(spkt);
        }
    };

    if (on_progress) on_progress(20, "muxing video/audio with subtitle");

    while (true) {
        if (!has_v && !end_v) read_video();
        if (!has_s && !end_s) read_sub();
        if (!has_v && !has_s) break;

        const int64_t v_us = has_v ? ts_us(vpkt->dts, video_in->streams[vpkt->stream_index]->time_base) : INT64_MAX;
        const int64_t s_us = has_s ? ts_us(spkt->dts, in_sub_stream->time_base) : INT64_MAX;

        if (v_us <= s_us) {
            AVStream* in_st = video_in->streams[vpkt->stream_index];
            const int out_idx = stream_map[vpkt->stream_index];
            AVStream* out_st = out_fmt->streams[out_idx];

            av_packet_rescale_ts(vpkt, in_st->time_base, out_st->time_base);
            vpkt->stream_index = out_idx;
            if (av_interleaved_write_frame(out_fmt, vpkt) < 0) {
                av_packet_unref(vpkt);
                cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
                if (on_progress) on_progress(0, "failed: write video/audio packet");
                return -14;
            }
            av_packet_unref(vpkt);
            has_v = false;
        } else {
            AVSubtitle sub{};
            int got_sub = 0;
            int ret = avcodec_decode_subtitle2(dec_ctx, &sub, &got_sub, spkt);
            av_packet_unref(spkt);
            has_s = false;
            if (ret < 0 || !got_sub) {
                continue;
            }

            std::vector<uint8_t> buf(64 * 1024);
            const int enc_size = avcodec_encode_subtitle(enc_ctx, buf.data(), static_cast<int>(buf.size()), &sub);
            avsubtitle_free(&sub);
            if (enc_size <= 0) {
                continue;
            }

            av_packet_unref(out_pkt);
            if (av_new_packet(out_pkt, enc_size) < 0) {
                cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
                if (on_progress) on_progress(0, "failed: alloc subtitle packet");
                return -15;
            }
            std::memcpy(out_pkt->data, buf.data(), static_cast<size_t>(enc_size));

            const int64_t base_pts = (sub.pts == AV_NOPTS_VALUE) ? 0 : (sub.pts / 1000);
            out_pkt->pts = base_pts + sub.start_display_time;
            out_pkt->dts = out_pkt->pts;
            out_pkt->duration = std::max(1u, sub.end_display_time > sub.start_display_time ?
                                             (sub.end_display_time - sub.start_display_time) : 1000u);
            out_pkt->stream_index = out_sub_stream->index;

            if (av_interleaved_write_frame(out_fmt, out_pkt) < 0) {
                av_packet_unref(out_pkt);
                cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
                if (on_progress) on_progress(0, "failed: write subtitle packet");
                return -16;
            }
            av_packet_unref(out_pkt);
        }

        if (on_progress) on_progress(80, "muxing");
    }

    if (av_write_trailer(out_fmt) < 0) {
        cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
        if (on_progress) on_progress(0, "failed: av_write_trailer");
        return -17;
    }

    cleanup(&video_in, &sub_in, &out_fmt, &dec_ctx, &enc_ctx, &vpkt, &spkt, &out_pkt);
    if (on_progress) on_progress(100, "done");
    return 0;
#else
    (void)video_path;
    (void)subtitle_path;
    (void)output_path;

    for (int p = 20; p <= 100; p += 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (on_progress) on_progress(p, p == 100 ? "done" : "processing");
    }
    return 0;
#endif
}

}  // namespace avsvc
