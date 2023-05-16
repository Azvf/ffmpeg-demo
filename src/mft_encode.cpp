#include "mft_encode.h"

// std
#include <iostream>
#include <string>

// Windows
#include <windows.h>
#include <atlbase.h>

// DirectX
#include <d3d11.h>

// Media Foundation
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <Codecapi.h>
#include <vector>
#include <audiosessiontypes.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

// COM
#include <combaseapi.h>

// ffmpeg
#include <wels/codec_app_def.h>

#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Winmm.lib")

#define CHECK(x, err) if (!(x)) { std::cerr << err << std::endl; break; }
#define CHECK_HR(x, err) if (FAILED(x)) { std::cerr << err << std::endl; break; }
#define  MAX_NAL_LEN 100

class MFTEncoder {
public:
	MFTEncoder();
	~MFTEncoder();

	int InitMFT();
	int StartEncoderExt();
	int EndEncoderExt();

private:
	std::wstring	m_strDisplayCardName;
	float			m_fFrameRate = 24.0;
	float			m_fMaxFrameRate;
	RC_MODES		m_iRCMode;					///< rate control mode
	GUID			m_guidMarjorType;			//MFMediaType_Video

	int32_t			m_nSrcPicWidth;
	int32_t			m_nSrcPicHeight;
	GUID			m_guidSrcColorFormat;			//MFVideoFormat_NV12

	int32_t			m_nGopSize;
	int32_t			m_nTargetWidth;
	int32_t			m_nTargetHeight;
	GUID			m_guidTargetColorFormat;		//MFVideoFormat_H264
	int32_t			m_nTargetBitrate;				///30000000

	DWORD	m_nInputStreamID;
	DWORD	m_nOutputStreamID;

	CComPtr<IMFMediaType> m_coInputMediaType;
	CComPtr<IMFMediaType> m_coOutputMediaType;

	CComPtr<IMFTransform> m_coTransform;
	CComQIPtr<IMFMediaEventGenerator> m_coEventGen;

	CComPtr<IMFMediaBuffer> m_coInputBuffer;
	CComPtr<IMFSample>		m_coInputSample;

	volatile bool		m_bInitialFlag;
	volatile bool		m_bProcessing;
	uint32_t m_bufferSize[2];
	void* m_buffer[2];
	int32_t  m_pNalLen[MAX_NAL_LEN];
};

MFTEncoder::MFTEncoder() 
	: m_strDisplayCardName(L"NVIDIA"), m_fFrameRate(24.0), m_fMaxFrameRate(60.0), m_iRCMode(RC_QUALITY_MODE)
	, m_guidMarjorType(MFMediaType_Video), m_nSrcPicWidth(1920), m_nSrcPicHeight(1080), m_guidSrcColorFormat(MFVideoFormat_NV12)
	, m_nTargetWidth(1920), m_nTargetHeight(1080), m_guidTargetColorFormat(MFVideoFormat_H264), m_nTargetBitrate(6000000)
	, m_bInitialFlag(false), m_bProcessing(false), m_nInputStreamID(0), m_nOutputStreamID(0)
	, m_coInputMediaType(NULL), m_coOutputMediaType(NULL), m_coTransform(NULL)
	, m_coInputBuffer(NULL), m_coInputSample(NULL), m_nGopSize(8) 
{
	memset(m_buffer, 0x00, sizeof(void*) * 2);
	memset(m_bufferSize, 0x00, sizeof(uint32_t) * 2);
	memset(m_pNalLen, 0x00, sizeof(uint32_t) * MAX_NAL_LEN);
}

MFTEncoder::~MFTEncoder() {

}

int MFTEncoder::InitMFT()
{
	HRESULT hr;

	CComPtr<ID3D11Device> device;
	CComPtr<ID3D11DeviceContext> context;
	CComPtr<IMFDXGIDeviceManager> deviceManager;
	CComPtr<IMFAttributes> attributes;

	long long ticksPerSecond(0);
	long long appStartTicks(0);
	long long encStartTicks(0);
	long long ticksPerFrame(0);

	do
	{
		hr = MFStartup(MF_VERSION);
		CHECK_HR(hr, "Failed to start Media Foundation");

		// ------------------------------------------------------------------------
		// Initialize D3D11
		// ------------------------------------------------------------------------

		// Driver types supported
		D3D_DRIVER_TYPE DriverTypes[] =
		{
			D3D_DRIVER_TYPE_HARDWARE,
			D3D_DRIVER_TYPE_WARP,
			D3D_DRIVER_TYPE_REFERENCE,
		};
		UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

		// Feature levels supported
		D3D_FEATURE_LEVEL FeatureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_1
		};
		UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

		D3D_FEATURE_LEVEL FeatureLevel;

		// Create device
		for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
		{
			hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr,
				D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
				FeatureLevels, NumFeatureLevels, D3D11_SDK_VERSION, &device, &FeatureLevel, &context);
			if (SUCCEEDED(hr))
			{
				// Device creation success, no need to loop anymore
				break;
			}
		}

		// Create device manager
		UINT resetToken;
		hr = MFCreateDXGIDeviceManager(&resetToken, &deviceManager);
		CHECK_HR(hr, "Failed to create DXGIDeviceManager");
		
		hr = deviceManager->ResetDevice(device, resetToken);
		CHECK_HR(hr, "Failed to assign D3D device to device manager");

		// ------------------------------------------------------------------------
		// Initialize hardware encoder MFT
		// ------------------------------------------------------------------------

		// Find encoder
		CComHeapPtr<IMFActivate*> activateRaw;
		UINT32 activateCount = 0;

		// h264 output
		MFT_REGISTER_TYPE_INFO inputType = { MFMediaType_Video, MFVideoFormat_NV12 };
		MFT_REGISTER_TYPE_INFO outputType = { MFMediaType_Video, MFVideoFormat_H264 };

		UINT32 flags =
			MFT_ENUM_FLAG_HARDWARE |
			MFT_ENUM_FLAG_SORTANDFILTER;

		hr = MFTEnumEx(
			MFT_CATEGORY_VIDEO_ENCODER,
			flags,
			&inputType,
			&outputType,
			&activateRaw,
			&activateCount
		);
		CHECK_HR(hr, "Failed to enumerate MFTs");

		CHECK(activateCount, "No MFTs found");
		// Choose the first available encoder
		CComPtr<IMFActivate> activate = activateRaw[0];

		for (UINT32 i = 0; i < activateCount; i++)
			activateRaw[i]->Release();

		// Activate
		hr = activate->ActivateObject(IID_PPV_ARGS(&m_coTransform));
		CHECK_HR(hr, "Failed to activate MFT");

		// Get attributes
		hr = m_coTransform->GetAttributes(&attributes);
		CHECK_HR(hr, "Failed to get MFT attributes");

		// Get encoder name
		UINT32 nameLength = 0;
		std::wstring DisplayCardName;
		hr = attributes->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength);
		//CHECK_HR(hr, "Failed to get MFT name length");

		// IMFAttributes::GetString returns a null-terminated wide string
		DisplayCardName.resize(nameLength + 1);

		hr = attributes->GetString(MFT_FRIENDLY_NAME_Attribute, &DisplayCardName[0], (UINT32)DisplayCardName.size(), &nameLength);
		//CHECK_HR(hr, "Failed to get MFT name");

		DisplayCardName.resize(nameLength);

		// Unlock the transform for async use and get event generator
		hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
		CHECK_HR(hr, "Failed to unlock MFT");

		m_coEventGen = m_coTransform;
		CHECK(m_coEventGen, "Failed to QI for event generator");

		// Get stream IDs (expect 1 input and 1 output stream)
		hr = m_coTransform->GetStreamIDs(1, &m_nInputStreamID, 1, &m_nOutputStreamID);
		if (hr == E_NOTIMPL)
		{
			m_nInputStreamID = 0;
			m_nOutputStreamID = 0;
			hr = S_OK;
		}
		CHECK_HR(hr, "Failed to get stream IDs");

		// ------------------------------------------------------------------------
		// Configure hardware encoder MFT
		// ------------------------------------------------------------------------

		// Set D3D manager
		// hr = m_coTransform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p));
		// CHECK_HR(hr, "Failed to set D3D manager");

		// Set low latency hint
		hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
		CHECK_HR(hr, "Failed to set MF_LOW_LATENCY");

		// 先设置OutputType，否则GetInputAvailableType会出错,当然也可以MFCreateMediaType创建一个，强制SetInputType
		// Set output type
		hr = MFCreateMediaType(&m_coOutputMediaType);
		CHECK_HR(hr, "Failed to create media type");

		hr = m_coOutputMediaType->SetGUID(MF_MT_MAJOR_TYPE, m_guidMarjorType);
		CHECK_HR(hr, "Failed to set MF_MT_MAJOR_TYPE on H264 output media type");

		hr = m_coOutputMediaType->SetGUID(MF_MT_SUBTYPE, m_guidTargetColorFormat);
		CHECK_HR(hr, "Failed to set MF_MT_SUBTYPE on H264 output media type");
		
		hr = m_coOutputMediaType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
		CHECK_HR(hr, "Failed to set MF_MT_MPEG2_PROFILE on H264 output media type");

		hr = m_coOutputMediaType->SetUINT32(MF_MT_AVG_BITRATE, m_nTargetBitrate);
		CHECK_HR(hr, "Failed to set average bit rate on H264 output media type");

		hr = MFSetAttributeSize(m_coOutputMediaType, MF_MT_FRAME_SIZE, m_nTargetWidth, m_nTargetHeight);
		CHECK_HR(hr, "Failed to set frame size on H264 MFT out type");

		hr = MFSetAttributeRatio(m_coOutputMediaType, MF_MT_FRAME_RATE, (UINT32)m_fFrameRate, 1);
		CHECK_HR(hr, "Failed to set frame rate on H264 MFT out type");

		//hr = s_outputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, 2);
		CHECK_HR(hr, "Failed to set MF_MT_INTERLACE_MODE on H.264 encoder MFT");

		//hr = s_outputMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
		CHECK_HR(hr, "Failed to set MF_MT_ALL_SAMPLES_INDEPENDENT on H.264 encoder MFT");
		
		hr = m_coTransform->SetOutputType(m_nOutputStreamID, m_coOutputMediaType, 0);
		CHECK_HR(hr, "Failed to set output media type on H.264 encoder MFT");

#if 0
		CComPtr<IMFMediaType> temp = nullptr;
		hr = m_coTransform->GetOutputCurrentType(m_nOutputStreamID, &temp);
#endif

		// Set input type
		hr = MFCreateMediaType(&m_coInputMediaType);
		CHECK_HR(hr, "Failed to create media type");

		for (DWORD i = 0; i < 8; i++)
		{
			GUID srcGuid;
			m_coInputMediaType = nullptr;

			hr = m_coTransform->GetInputAvailableType(m_nInputStreamID, i, &m_coInputMediaType);
			CHECK_HR(hr, "Failed to get input type");
			
			if (hr == MF_E_NO_MORE_TYPES) {
				break;
			}

			hr = m_coInputMediaType->GetGUID(MF_MT_SUBTYPE, &srcGuid);
			if (srcGuid != MFVideoFormat_NV12)
			{
				m_coInputMediaType.Release();
				continue;
			}

			hr = m_coInputMediaType->SetGUID(MF_MT_MAJOR_TYPE, m_guidMarjorType);
			CHECK_HR(hr, "Failed to set MF_MT_MAJOR_TYPE on H264 MFT input type");

			hr = m_coInputMediaType->SetGUID(MF_MT_SUBTYPE, m_guidSrcColorFormat);
			CHECK_HR(hr, "Failed to set MF_MT_SUBTYPE on H264 MFT input type");

			hr = MFSetAttributeSize(m_coInputMediaType, MF_MT_FRAME_SIZE, m_nSrcPicWidth, m_nSrcPicHeight);
			CHECK_HR(hr, "Failed to set MF_MT_FRAME_SIZE on H264 MFT input type");

			hr = MFSetAttributeRatio(m_coInputMediaType, MF_MT_FRAME_RATE, (UINT32)m_fFrameRate, 1);
			CHECK_HR(hr, "Failed to set MF_MT_FRAME_RATE on H264 MFT input type");

			hr = m_coTransform->SetInputType(m_nInputStreamID, m_coInputMediaType, NULL);
			CHECK_HR(hr, "Failed to set input type");

#if 1
			CComPtr<IMFMediaType> temp = nullptr;
			hr = m_coTransform->GetInputCurrentType(m_nOutputStreamID, &temp);
#endif

			break;
		}

		GUID inputCodec;
		GUID outputCodec;
		
		hr = m_coInputMediaType->GetGUID(MF_MT_SUBTYPE, &inputCodec);
		hr = m_coOutputMediaType->GetGUID(MF_MT_SUBTYPE, &outputCodec);
		
		return cmResultSuccess;
	} while (0);

	return cmUnknownReason;
}

int MFTEncoder::StartEncoderExt() {
	EndEncoderExt();
	if (m_bProcessing) {
		return cmResultSuccess;
	}

	do
	{
		HRESULT hr;
		hr = m_coTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
		CHECK_HR(hr, "Failed to process FLUSH command on H.264 MFT");

		hr = m_coTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
		CHECK_HR(hr, "Failed to process BEGIN_STREAMING command on H.264 MFT");

		hr = m_coTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
		CHECK_HR(hr, "Failed to process START_OF_STREAM command on H.264 MFT");
		m_bProcessing = true;

		return cmResultSuccess;
	} while (0);
	return cmUnknownReason;
}

int MFTEncoder::EndEncoderExt()
{
	HRESULT hr;
	do
	{
		if (m_bProcessing)
		{
			hr = m_coTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
			CHECK_HR(hr, "Failed to process END_OF_STREAM command on H.264 MFT");

			hr = m_coTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);
			CHECK_HR(hr, "Failed to process END_STREAMING command on H.264 MFT");

			hr = m_coTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
			CHECK_HR(hr, "Failed to process FLUSH command on H.264 MFT");
			m_bProcessing = false;
		}
		return cmResultSuccess;
	} while (0);
	return cmUnknownReason;

}

void mft_encode() {
	MFTEncoder encoder;
	encoder.InitMFT();
	encoder.StartEncoderExt();
}