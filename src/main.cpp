#include "qsv_transcode.h"
#include "vaapi_encode.h"
#include "sf_player.h"
#include "hw_device_test.h"
#include "hw_decode.h"
#include "mft_encode.h"
#include "MFEncoderH264.h"

int main(int argc, char** argv) {
	//play_video();

	// qsv_tanscode(argc, argv);

	//vaapi_encode(argc, argv);

	//hw_device_test();
	
	//hw_decode(argc, argv);

	// mft_encode();

	gstreamer_mft_encoder();
}
