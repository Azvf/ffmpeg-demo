#include "VideoDecoding.h"

bool VideoOpenFile(VideoDecodingCtx* ctx, const char* filename) {
    auto& width = ctx->width;
    auto& height = ctx->height; 
    auto& timeBase = ctx->timeBase;
    auto& avFormatContext = ctx->avFormatContext;
	auto& avCodecContext = ctx->avCodecContext;
	auto& avPacket = ctx->avPacket;
	auto& avFrame = ctx->avFrame;
    auto& videoStreamIndex = ctx->videoStreamIndex;

    avFormatContext = avformat_alloc_context();
    if (!avFormatContext) {
        printf("Couldn't create AVFormatContext\n");
        return false;
    }

    if (avformat_open_input(&avFormatContext, filename, nullptr, nullptr) != 0) {
        printf("Couldn't open a video file\n");
        return false;
    }

    videoStreamIndex = -1;

    auto audioStreamIndex = -1;
    const AVCodecParameters* avCodecParams;
    const AVCodec* videoCodec;
    for (int i = 0; i < avFormatContext->nb_streams; i++) {
        const AVCodec* codec;
        const AVCodecParameters* param;
        AVStream* stream = avFormatContext->streams[i];

        param = stream->codecpar;

        if (!param) {
            continue;
        }

        codec = avcodec_find_decoder(param->codec_id);

        if (!codec) {
            continue;
        }

        if (codec->type == AVMediaType::AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            width = param->width;
            height = param->height;
            timeBase = stream->time_base;
            videoCodec = codec;
            avCodecParams = param;

            continue;
        }

        if (codec->type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            continue;
        }
    }

    if (videoStreamIndex == -1) {
        printf("Couldn't find video stream\n");
        return false;
    }

    if (audioStreamIndex == -1) {
        printf("Couldn't find audio stream\n");
    }

    avCodecContext = avcodec_alloc_context3(videoCodec);
    if (!avCodecContext) {
        printf("Couldn't create AVCodecContext\n");
        return false;
    }

    if (avcodec_parameters_to_context(avCodecContext, avCodecParams) < 0) {
        printf("Couldn't initialize AVCodecContext\n");
        return false;
    }

    if (avcodec_open2(avCodecContext, videoCodec, nullptr) < 0) {
        printf("Couldn't open AVCodec\n");
        return false;
    }

    avFrame = av_frame_alloc();
    if (!avFrame) {
        printf("Couldn't allocate AVFrame\n");
        return false;
    }

    avPacket = av_packet_alloc();
    if (!avPacket) {
        printf("Couldn't allocate AVPacket\n");
        return false;
    }

    return true;
}

bool VideoReadFrame(VideoDecodingCtx* ctx, uint8_t* frameBuffer, int64_t* pts) {
    auto& width = ctx->width;
    auto& height = ctx->height;
    auto& avFormatContext = ctx->avFormatContext;
    auto& avCodecContext = ctx->avCodecContext;
    auto& avPacket = ctx->avPacket;
    auto& avFrame = ctx->avFrame;
    auto& videoStreamIndex = ctx->videoStreamIndex;
    auto& swsScalerContext = ctx->swsScalerContext;

    /* processing video packet stream */
    int response;
    while (av_read_frame(avFormatContext, avPacket) >= 0) {
        if (avPacket->stream_index != videoStreamIndex) {
            av_packet_unref(avPacket);
            continue;
        }

        response = avcodec_send_packet(avCodecContext, avPacket);
        if (response < 0) {
            // printf("Failed to decode AVPacket: %s\n", av_err2str(response));
            printf("Failed to decode AVPacket\n");
            return false;
        }

        response = avcodec_receive_frame(avCodecContext, avFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            av_packet_unref(avPacket);
            continue;
        }
        else if (response < 0) {
            printf("Failed to decode AVPacket\n");
            return false;
        }

        av_packet_unref(avPacket);
        break;
    }

    *pts = avFrame->pts;

    printf(
        "Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d, display_picture_number %d]\n",
        av_get_picture_type_char(avFrame->pict_type),
        avCodecContext->frame_number,
        avFrame->pts,
        avFrame->pkt_dts,
        avFrame->key_frame,
        avFrame->coded_picture_number,
        avFrame->display_picture_number
    );

    swsScalerContext = sws_getContext(width, height, avCodecContext->pix_fmt,
                                      width, height, AV_PIX_FMT_RGB0,
                                      SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!swsScalerContext) {
        printf("Couldn't initialize sw scaler\n");
        return false;
    }

    uint8_t* frameData = new uint8_t[avFrame->width * avFrame->height * 4];
    uint8_t* dest[4] = { frameBuffer, nullptr, nullptr, nullptr };
    int dest_linesize[4] = { avFrame->width * 4, 0, 0, 0 };
    sws_scale(swsScalerContext, avFrame->data, avFrame->linesize, 0, avFrame->height, dest, dest_linesize);
    sws_freeContext(swsScalerContext);

    return true;
}

bool VideoCloseFile(VideoDecodingCtx* ctx) {
    /* post processing */
    avformat_close_input(&ctx->avFormatContext);
    avformat_free_context(ctx->avFormatContext);
    av_frame_free(&ctx->avFrame);
    av_packet_free(&ctx->avPacket);
    avcodec_free_context(&ctx->avCodecContext);

    return true;
}
