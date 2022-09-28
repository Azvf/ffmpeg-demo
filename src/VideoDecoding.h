extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

struct VideoDecodingCtx {
    int width;
    int height;
    int videoStreamIndex;
    AVRational timeBase;

    AVFormatContext* avFormatContext;
    AVCodecContext* avCodecContext;
    AVPacket* avPacket;
    AVFrame* avFrame;
    SwsContext* swsScalerContext;
};


bool VideoOpenFile(VideoDecodingCtx* ctx, const char* filename);

bool VideoReadFrame(VideoDecodingCtx* ctx, uint8_t* frameBuffer, int64_t* pts);

bool VideoCloseFile(VideoDecodingCtx* ctx);

