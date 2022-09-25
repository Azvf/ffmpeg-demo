#include <stdio.h>
#include <cstring>
#include <inttypes.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../lib/stb/stb_image.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

bool LoadFrame(const char* filename, int* width, int* height, unsigned char** data) {
    *width = 100;
    *height = 100;

    *data = new unsigned char[100 * 100 * 3];

    auto ptr = *data;
    
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            *ptr++ = 0xff;
            *ptr++ = 0x00;
            *ptr++ = 0x00;
        }
    }
        
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

    int iw, ih, n;
    unsigned char* idata = stbi_load("image.jpg", &iw, &ih, &n, 0);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, iw, ih, 0, GL_RGB, GL_UNSIGNED_BYTE, idata);;
    // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frameWidth, frameHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, frameData);;

    /* Make the window's context current */
    glfwMakeContextCurrent(window);

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
        glOrtho(0.0, windowWidth, 0.0, windowHeight, -1, 1);
        glMatrixMode(GL_MODELVIEW);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texHandle);
        glBegin(GL_QUADS);
            glTexCoord2d(0.0, 0.0); glVertex2i(startPoint, startPoint);
            glTexCoord2d(1.0, 0.0); glVertex2i(startPoint + frameWidth, startPoint);
            glTexCoord2d(1.0, 1.0); glVertex2i(startPoint + frameWidth, startPoint + frameHeight);
            glTexCoord2d(0.0, 1.0); glVertex2i(startPoint, startPoint + frameHeight);
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