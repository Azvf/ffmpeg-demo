cmake_minimum_required(VERSION 3.14)
project(ffmpeg-demo C CXX)
set(CMAKE_CXX_STANDARD 20)

# vcpkg
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ffmpeg REQUIRED)

set(SOURCE_FILES src/main.cpp)
set(EXTERNAL_FILES lib/stb/stb_image.h)

add_executable(ffmpeg-demo ${SOURCE_FILES} ${EXTERNAL_FILES})

target_link_libraries(${CMAKE_PROJECT_NAME} glfw OpenGL::GL ${FFMPEG_LIBRARIES})