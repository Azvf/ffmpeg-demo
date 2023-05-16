cmake_minimum_required(VERSION 3.14)
project(ffmpeg-demo C CXX)
set(CMAKE_CXX_STANDARD 20)

# vcpkg
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ffmpeg REQUIRED)

set(SOURCE_FILES 
	src/main.cpp 
	src/VideoDecoding.h 
	src/VideoDecoding.cpp

	src/sf_player.h 
	src/sf_player.cpp
	
	src/qsv_transcode.h 
	src/qsv_transcode.cpp 
	
	src/vaapi_encode.h 
	src/vaapi_encode.cpp 

	src/hw_device_test.h
	src/hw_device_test.cpp

	src/hw_decode.h
	src/hw_decode.cpp

	src/mft_encode.h
	src/mft_encode.cpp

	src/MFEncoderH264.h
	src/MFEncoderH264.cpp
)
set(EXTERNAL_FILES lib/stb/stb_image.h)

add_executable(ffmpeg-demo ${SOURCE_FILES} ${EXTERNAL_FILES})

target_link_libraries(${CMAKE_PROJECT_NAME} glfw OpenGL::GL ${FFMPEG_LIBRARIES})