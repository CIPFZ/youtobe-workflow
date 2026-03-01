#include "audio_convert_worker.h"

#include <chrono>
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
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#endif

namespace avsvc {

std::string build_audio_convert_fingerprint(const std::string& input_path,
                                            const std::string& output_path) {
    const auto key = "m4a2wav|" + input_path + "|" + output_path;
    const auto hashed = std::hash<std::string>{}(key);
    std::ostringstream oss;
    oss << std::hex << hashed;
    return oss.str();
}

#ifdef HAVE_FFMPEG
namespace {

void cleanup(AVFormatContext** in_fmt,
             AVCodecContext** dec_ctx,
             AVFormatContext** out_fmt,
             AVCodecContext** enc_ctx,
             SwrContext** swr,
             AVFrame** in_frame,
             AVFrame** out_frame,
             AVPacket** packet,
             AVPacket** out_packet) {
    if (packet && *packet) av_packet_free(packet);
    if (out_packet && *out_packet) av_packet_free(out_packet);
    if (in_frame && *in_frame) av_frame_free(in_frame);
    if (out_frame && *out_frame) av_frame_free(out_frame);
    if (swr && *swr) swr_free(swr);
    if (dec_ctx && *dec_ctx) avcodec_free_context(dec_ctx);
    if (enc_ctx && *enc_ctx) avcodec_free_context(enc_ctx);

    if (out_fmt && *out_fmt) {
        if (!((*out_fmt)->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&(*out_fmt)->pb);
        }
        avformat_free_context(*out_fmt);
        *out_fmt = nullptr;
    }
    if (in_fmt && *in_fmt) {
        avformat_close_input(in_fmt);
    }
}

bool flush_encoder(AVCodecContext* enc_ctx,
                   AVFormatContext* out_fmt,
                   AVPacket* out_packet,
                   AudioConvertProgressCallback on_progress) {
    if (avcodec_send_frame(enc_ctx, nullptr) < 0) {
        if (on_progress) on_progress(0, "failed: avcodec_send_frame(encoder flush)");
        return false;
    }

    while (true) {
        const int ret = avcodec_receive_packet(enc_ctx, out_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            if (on_progress) on_progress(0, "failed: avcodec_receive_packet(encoder flush)");
            return false;
        }
        if (av_interleaved_write_frame(out_fmt, out_packet) < 0) {
            av_packet_unref(out_packet);
            if (on_progress) on_progress(0, "failed: av_interleaved_write_frame(flush)");
            return false;
        }
        av_packet_unref(out_packet);
    }
}

}  // namespace
#endif

int AudioConvertWorker::run_m4a_to_wav(const std::string& input_path,
                                       const std::string& output_path,
                                       AudioConvertProgressCallback on_progress) const {
    if (!std::filesystem::exists(input_path)) {
        if (on_progress) on_progress(0, "failed: input file not found");
        return -1;
    }

#ifdef HAVE_FFMPEG
    AVFormatContext* in_fmt = nullptr;
    AVCodecContext* dec_ctx = nullptr;
    AVFormatContext* out_fmt = nullptr;
    AVCodecContext* enc_ctx = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* in_frame = nullptr;
    AVFrame* out_frame = nullptr;
    AVPacket* packet = nullptr;
    AVPacket* out_packet = nullptr;

    if (on_progress) on_progress(5, "opening input");

    if (avformat_open_input(&in_fmt, input_path.c_str(), nullptr, nullptr) < 0) {
        if (on_progress) on_progress(0, "failed: avformat_open_input");
        return -2;
    }
    if (avformat_find_stream_info(in_fmt, nullptr) < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avformat_find_stream_info");
        return -3;
    }

    const int audio_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_idx < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: no audio stream");
        return -4;
    }

    AVStream* in_stream = in_fmt->streams[audio_idx];
    const AVCodec* decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
    if (!decoder) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: decoder not found");
        return -5;
    }

    dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avcodec_alloc_context3(decoder)");
        return -6;
    }
    if (avcodec_parameters_to_context(dec_ctx, in_stream->codecpar) < 0 || avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: open decoder");
        return -7;
    }

    if (on_progress) on_progress(15, "initializing output wav");

    if (avformat_alloc_output_context2(&out_fmt, nullptr, "wav", output_path.c_str()) < 0 || !out_fmt) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avformat_alloc_output_context2(wav)");
        return -8;
    }

    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!encoder) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: PCM_S16LE encoder not found");
        return -9;
    }

    AVStream* out_stream = avformat_new_stream(out_fmt, nullptr);
    if (!out_stream) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avformat_new_stream");
        return -10;
    }

    enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avcodec_alloc_context3(encoder)");
        return -11;
    }

    enc_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    enc_ctx->sample_rate = 16000;
    enc_ctx->channel_layout = AV_CH_LAYOUT_MONO;
    enc_ctx->channels = 1;
    enc_ctx->time_base = AVRational{1, enc_ctx->sample_rate};

    if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avcodec_open2(encoder)");
        return -12;
    }

    if (avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avcodec_parameters_from_context");
        return -13;
    }
    out_stream->time_base = enc_ctx->time_base;

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
            if (on_progress) on_progress(0, "failed: avio_open(output)");
            return -14;
        }
    }

    if (avformat_write_header(out_fmt, nullptr) < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: avformat_write_header");
        return -15;
    }

    const int64_t in_ch_layout = dec_ctx->channel_layout != 0 ? dec_ctx->channel_layout
                                                               : av_get_default_channel_layout(dec_ctx->channels);

    swr = swr_alloc_set_opts(nullptr,
                             enc_ctx->channel_layout,
                             enc_ctx->sample_fmt,
                             enc_ctx->sample_rate,
                             in_ch_layout,
                             dec_ctx->sample_fmt,
                             dec_ctx->sample_rate,
                             0,
                             nullptr);
    if (!swr || swr_init(swr) < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: swr_init");
        return -16;
    }

    in_frame = av_frame_alloc();
    out_frame = av_frame_alloc();
    packet = av_packet_alloc();
    out_packet = av_packet_alloc();
    if (!in_frame || !out_frame || !packet || !out_packet) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: alloc frame/packet");
        return -17;
    }

    if (on_progress) on_progress(25, "converting");

    while (av_read_frame(in_fmt, packet) >= 0) {
        if (packet->stream_index != audio_idx) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(dec_ctx, packet) < 0) {
            av_packet_unref(packet);
            cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
            if (on_progress) on_progress(0, "failed: avcodec_send_packet");
            return -18;
        }
        av_packet_unref(packet);

        while (true) {
            const int ret = avcodec_receive_frame(dec_ctx, in_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
                if (on_progress) on_progress(0, "failed: avcodec_receive_frame");
                return -19;
            }

            const int dst_nb_samples = av_rescale_rnd(
                swr_get_delay(swr, dec_ctx->sample_rate) + in_frame->nb_samples,
                enc_ctx->sample_rate,
                dec_ctx->sample_rate,
                AV_ROUND_UP);

            av_frame_unref(out_frame);
            out_frame->format = enc_ctx->sample_fmt;
            out_frame->channel_layout = enc_ctx->channel_layout;
            out_frame->sample_rate = enc_ctx->sample_rate;
            out_frame->nb_samples = dst_nb_samples;

            if (av_frame_get_buffer(out_frame, 0) < 0) {
                cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
                if (on_progress) on_progress(0, "failed: av_frame_get_buffer");
                return -20;
            }

            const int converted = swr_convert(
                swr,
                out_frame->data,
                dst_nb_samples,
                const_cast<const uint8_t**>(in_frame->extended_data),
                in_frame->nb_samples);

            if (converted < 0) {
                cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
                if (on_progress) on_progress(0, "failed: swr_convert");
                return -21;
            }

            out_frame->nb_samples = converted;
            out_frame->pts = av_rescale_q(in_frame->pts, in_stream->time_base, enc_ctx->time_base);

            if (avcodec_send_frame(enc_ctx, out_frame) < 0) {
                cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
                if (on_progress) on_progress(0, "failed: avcodec_send_frame");
                return -22;
            }

            while (true) {
                const int pret = avcodec_receive_packet(enc_ctx, out_packet);
                if (pret == AVERROR(EAGAIN) || pret == AVERROR_EOF) {
                    break;
                }
                if (pret < 0) {
                    cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
                    if (on_progress) on_progress(0, "failed: avcodec_receive_packet");
                    return -23;
                }

                out_packet->stream_index = out_stream->index;
                if (av_interleaved_write_frame(out_fmt, out_packet) < 0) {
                    av_packet_unref(out_packet);
                    cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
                    if (on_progress) on_progress(0, "failed: av_interleaved_write_frame");
                    return -24;
                }
                av_packet_unref(out_packet);
            }

            if (on_progress) on_progress(80, "converting");
        }
    }

    if (!flush_encoder(enc_ctx, out_fmt, out_packet, on_progress)) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        return -25;
    }

    if (av_write_trailer(out_fmt) < 0) {
        cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
        if (on_progress) on_progress(0, "failed: av_write_trailer");
        return -26;
    }

    cleanup(&in_fmt, &dec_ctx, &out_fmt, &enc_ctx, &swr, &in_frame, &out_frame, &packet, &out_packet);
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
