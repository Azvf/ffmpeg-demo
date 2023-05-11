#pragma once

// example: qsv_transcode input.mp4 hevc_qsv output_hevc.mp4 "g 60 async_depth 1" 100 "g 120" (initialize codec with gop_size 60 and change it to 120 after 100 frames)
int qsv_tanscode(int argc, char** argv);