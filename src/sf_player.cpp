#include "sf_player.h"
#include <stdio.h>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <string>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../lib/stb/stb_image.h"

#include "VideoDecoding.h"

int play_video(void) {
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


    VideoDecodingCtx ctx;
    if (!VideoOpenFile(&ctx, "video.mp4")) {
        printf("Couldn't open video file\n");
        return 1;
    }

    int frameWidth = ctx.width, frameHeight = ctx.height;
    uint8_t* frameData = new uint8_t[frameWidth * frameHeight * 4];

    GLuint texHandle;
    glGenTextures(1, &texHandle);
    glBindTexture(GL_TEXTURE_2D, texHandle);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* Loop until the user closes the window */
    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Set up Orthographic Projection */
        int windowWidth, windowHeight;
        glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, windowWidth, windowHeight, 0.0, -1, 1);
        glMatrixMode(GL_MODELVIEW);

        /* Read a video frame */
        int64_t pts;
        if (!VideoReadFrame(&ctx, frameData, &pts)) {
            printf("Couldn't read video frame\n");
            return 1;
        }
        glBindTexture(GL_TEXTURE_2D, texHandle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frameWidth, frameHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameData);;

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texHandle);
        glBegin(GL_QUADS);
        glTexCoord2d(0.0, 0.0); glVertex2i(0, 0);
        glTexCoord2d(1.0, 0.0); glVertex2i(frameWidth, 0);
        glTexCoord2d(1.0, 1.0); glVertex2i(frameWidth, frameHeight);
        glTexCoord2d(0.0, 1.0); glVertex2i(0, frameHeight);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }

    VideoCloseFile(&ctx);

    glfwTerminate();
    return 0;
}