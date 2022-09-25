#include <stdio.h>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../lib/stb/stb_image.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

}

bool LoadFrame(const char* filename, int* width, int* height, unsigned char** data) {
    // FIXME: handle memory leak if something failed. For now just exit the program 
    AVFormatContext* avFormatContext = avformat_alloc_context();
     
    if (!avFormatContext) {
        printf("Couldn't create AVFormatContext\n");
        return false;
    }

    if (avformat_open_input(&avFormatContext, filename, nullptr, nullptr) != 0) {
        printf("Couldn't open a video file\n");
        return false;
    }

    int videoStreamIndex = -1, audioStreamIndex = -1;
    AVCodecParameters* avCodecParams;
    AVCodec* avCodec;

    for (int i = 0; i < avFormatContext->nb_streams; i++) {
        AVStream* stream = avFormatContext->streams[i];
        
        avCodecParams = stream->codecpar;
        
        if (!avCodecParams) {
            continue;
        }

        avCodec = avcodec_find_decoder(avCodecParams->codec_id);

        if (!avCodec) {
            continue;
        }

        if (avCodec->type == AVMediaType::AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            continue;
        }

        if (avCodec->type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            continue;
        }
    }

    if (videoStreamIndex == -1) {
        printf("Couldn't find video stream\n");
        return false;
    }

    if (videoStreamIndex == -1) {
        printf("Couldn't find audio stream\n");
    }

    AVCodecContext* avCodecContext = avcodec_alloc_context3(avCodec);
    if (!avCodecContext) {
        printf("Couldn't create AVCodecContext\n");
        return false;
    }

    if (avcodec_parameters_to_context(avCodecContext, avCodecParams) < 0) {
        printf("Couldn't initialize AVCodecContext\n");
        return false;
    }

    if (avcodec_open2(avCodecContext, avCodec, nullptr) < 0) {
        printf("Couldn't open AVCodec\n");
        return false;
    }

    AVFrame* avFrame = av_frame_alloc();
    if (!avFrame) {
        printf("Couldn't allocate AVFrame\n");
        return false;
    }
    
    AVPacket* avPacket = av_packet_alloc();
    if (!avPacket) {
        printf("Couldn't allocate AVPacket\n");
        return false;
    }

    /* processing video packet stream */
    int response;
    while (av_read_frame(avFormatContext, avPacket) >= 0) {
        if (avPacket->stream_index != videoStreamIndex) {
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
            continue;
        }
        else if (response < 0) {
            printf("Failed to decode AVPacket\n");
            return false;

        }
        
        av_packet_unref(avPacket);
        break;
    }

    uint8_t* frameData = new uint8_t[avFrame->width * avFrame->height * 3];

    for (int y = 0; y < avFrame->height; y++) {
        for (int x = 0; x < avFrame->width; x++) {
            frameData[y * avFrame->width * 3 + x * 3 + 0] = avFrame->data[0][y * avFrame->linesize[0] + x];
            frameData[y * avFrame->width * 3 + x * 3 + 1] = avFrame->data[0][y * avFrame->linesize[0] + x];
            frameData[y * avFrame->width * 3 + x * 3 + 2] = avFrame->data[0][y * avFrame->linesize[0] + x];
        }
    }

    *width = avFrame->width;
    *height = avFrame->height;
    *data = frameData;

    /* post processing */
    avformat_close_input(&avFormatContext);
    avformat_free_context(avFormatContext);
    av_frame_free(&avFrame);
    av_packet_free(&avPacket);
    avcodec_free_context(&avCodecContext);

    return true;
}

int main(void)
{
    GLFWwindow* window;

    /* Initialize the library */
    if (!glfwInit())
        return -1;

    /* Create a windowed mode window and its OpenGL context */
    window = glfwCreateWindow(1280, 720, "ffmpeg-demo", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }
    
    /* Make the window's context current */
    glfwMakeContextCurrent(window);

    int frameWidth, frameHeight;
    unsigned char* frameData;
    
    if (!LoadFrame("video.mp4", &frameWidth, &frameHeight, &frameData)) {
        printf("Couldn't load video frame\n");
        return 1;
    }

    GLuint texHandle;
    glGenTextures(1, &texHandle);
    glBindTexture(GL_TEXTURE_2D, texHandle);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frameWidth, frameHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, frameData);;

    /* Loop until the user closes the window */
    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Set up Orthographic Projection */
        int startPoint = 300;
        int windowWidth, windowHeight;
        glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, windowWidth, windowHeight, 0.0, -1, 1);
        glMatrixMode(GL_MODELVIEW);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texHandle);
        glBegin(GL_QUADS);
            glTexCoord2d(0.0, 0.0); glVertex2i(300, 300);
            glTexCoord2d(1.0, 0.0); glVertex2i(300 + frameWidth, 300);
            glTexCoord2d(1.0, 1.0); glVertex2i(300 + frameWidth, 300 + frameHeight);
            glTexCoord2d(0.0, 1.0); glVertex2i(300, 300 + frameHeight);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}