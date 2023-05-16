#pragma warning(disable : 4995)
#pragma warning(disable : 4996)

#include "MFEncoderH264.h"
#include <sstream>
#include <windows.h>
#include <fstream>
#include <vector>

using namespace std;

CEncoderH264::CEncoderH264(const char* fileName, BOOL bWriteMP4, BOOL bWriteH264)
: m_pTransform(NULL)
, m_pH264File(NULL)
, m_hnsSampleTime(NULL)
, m_hnsSampleDuration(NULL)
, m_pMFBSOutputFile(NULL)
, m_pMediaSink(NULL)
, m_pStreamSink(NULL)
, m_pSinkWriter(NULL)
, m_pSample(NULL)
, m_pCodecApi(NULL)
, m_bExiting(FALSE)
{
	// Initialize Media Foundation
	HRESULT hr = MFStartup(MF_VERSION);

#if 0
	{	// init mft hw codec
		HRESULT hr = S_OK;
		UINT32 count = 0;
		IMFActivate** activate = NULL;
		MFT_REGISTER_TYPE_INFO info = { 0 };

		info.guidMajorType = MFMediaType_Video;
		info.guidSubtype = MFVideoFormat_H264;
		UINT32 flags = 
			// MFT_ENUM_FLAG_SYNCMFT 
			// | MFT_ENUM_FLAG_ASYNCMFT 
			// | MFT_ENUM_FLAG_LOCALMFT 
			// | MFT_ENUM_FLAG_SORTANDFILTER
			MFT_ENUM_FLAG_HARDWARE
			| MFT_ENUM_FLAG_SORTANDFILTER
			;

		CHECK_HR(hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
			flags,
			NULL,
			&info,
			&activate,
			&count));

		if (count == 0) {
			goto done;
		}
		CHECK_HR(hr = activate[0]->ActivateObject(IID_PPV_ARGS(&m_pTransform)));

	done:
		for (UINT32 idx = 0; idx < count; idx++) {
			activate[idx]->Release();
		}
		CoTaskMemFree(activate);
	}
#else 
	// Create IMFTransform for h.264 encoder
	if (IsWindows8OrGreater()) {
		CComPtr<IUnknown> spXferUnk;
		HRESULT hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&spXferUnk);
		if (SUCCEEDED(hr))
			hr = spXferUnk->QueryInterface(IID_PPV_ARGS(&m_pTransform));
		if (FAILED(hr))
			m_pTransform = NULL;
	}
	else {
		HRESULT hr = S_OK;
		UINT32 count = 0;
		IMFActivate** activate = NULL;
		MFT_REGISTER_TYPE_INFO info = { 0 };

		info.guidMajorType = MFMediaType_Video;
		info.guidSubtype = MFVideoFormat_H264;
		UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_TRANSCODE_ONLY | MFT_ENUM_FLAG_SORTANDFILTER;

		CHECK_HR(hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
			flags,
			NULL,
			&info,
			&activate,
			&count));

		if (count == 0) {
			goto done;
		}
		CHECK_HR(hr = activate[count - 1]->ActivateObject(IID_PPV_ARGS(&m_pTransform)));

	done:
		for (UINT32 idx = 0; idx < count; idx++) {
			activate[idx]->Release();
		}
		CoTaskMemFree(activate);
	}

#endif

	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char szFileOut[_MAX_PATH];
	wchar_t	pszOutputFile[_MAX_PATH];
	_splitpath(fileName, drive, dir, fname, NULL);

	if(bWriteH264) {
		_makepath(szFileOut, drive, dir, fname, ".h264" );
		m_pH264File = fopen (szFileOut,"wb");
	}

	if(bWriteMP4) {
		_makepath(szFileOut, drive, dir, fname, ".mp4" );
		int flen = ::MultiByteToWideChar(CP_UTF8, 0, szFileOut, -1, pszOutputFile, arraySize(pszOutputFile));
		pszOutputFile[flen] = 0;

		HRESULT hr = MFCreateFile(
			MF_ACCESSMODE_READWRITE,
			MF_OPENMODE_DELETE_IF_EXIST,
			MF_FILEFLAGS_NONE,
			pszOutputFile,
			&m_pMFBSOutputFile);
		if(FAILED(hr))
		{
			m_pMFBSOutputFile = NULL;
		}
	}
}

CEncoderH264::~CEncoderH264()
{
	SafeRelease(&m_pTransform);
	SafeRelease(&m_pStreamSink);
	SafeRelease(&m_pMediaSink);
	SafeRelease(&m_pSinkWriter);
	SafeRelease(&m_pMFBSOutputFile);
	SafeRelease(&m_pSample);
	SafeRelease(&m_pCodecApi);

	if(m_pH264File) {
		fclose(m_pH264File);
		m_pH264File = NULL;
	}

	HRESULT hr = MFShutdown();
}

HRESULT CEncoderH264::ConfigureEncoder(DWORD fccFormat, UINT32 width, UINT32 height, MFRatio frameRate, UINT nVBRQuality, UINT nMeanBitrate, UINT nMaxBitrate)
{
	HRESULT hr = S_OK;
	DWORD i = 0;
	GUID subtype = { 0 };
	GUID subtypeSource = { 0 };
	IMFMediaType* pInputType = NULL;
	IMFMediaType* pSourceType = NULL;
	IMFMediaType* pOutputType = NULL;
	//MFRatio par = { 16, 9 };
	MFRatio par = { 1,1 };

	auto attributesToCopy = {
		MF_MT_MAJOR_TYPE,
		MF_MT_FRAME_SIZE,
		MF_MT_FRAME_RATE,
		MF_MT_PIXEL_ASPECT_RATIO,
		MF_MT_INTERLACE_MODE
	};
	
	CHECK_HR(hr = CreateUncompressedVideoType(
		fccFormat,  // FOURCC or D3DFORMAT value.     
		width,	
		height,	
		MFVideoInterlace_Progressive,
		frameRate,
		par,
		&pSourceType
	));
	CHECK_HR(hr = CreateMFSampleFromMediaType(pSourceType, &m_pSample));
	CHECK_HR(hr = MFCreateMediaType(&pOutputType));

	for (auto&& attribute : attributesToCopy) {
		hr = CopyAttribute(pSourceType, pOutputType, attribute);
		if (FAILED(hr)) {
			if (hr == MF_E_ATTRIBUTENOTFOUND) {
				hr = S_OK;
			} else {
				break;
			}
		}
	}
	CHECK_HR(hr = pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
	CHECK_HR(hr = pOutputType->SetUINT32(MF_MT_AVG_BITRATE, 6000000));
	CHECK_HR(hr = pOutputType->SetUINT32(MF_MT_MAX_KEYFRAME_SPACING, 30));

	if (!pSourceType)
		return E_POINTER;

	if (!pOutputType)
		return E_POINTER;

	if (!m_pTransform)
		return E_POINTER;

	//Create MP4 MediaSink, ByteStreamSink, and SinkWriter
	if (m_pMFBSOutputFile) {
		CHECK_HR(hr = MFCreateMPEG4MediaSink(
			m_pMFBSOutputFile,
			pOutputType,   //Video
			NULL,          //Audio
			&m_pMediaSink));

		CHECK_HR(hr = m_pMediaSink->GetStreamSinkByIndex(0, &m_pStreamSink));
		CHECK_HR(hr = MFCreateSinkWriterFromMediaSink(m_pMediaSink, NULL, &m_pSinkWriter));
	}
	CHECK_HR(hr = pSourceType->GetGUID(MF_MT_SUBTYPE, &subtypeSource));

	{	//Configure encoder parameters via ICodecAPI
		VARIANT var;
		CHECK_HR(hr = m_pTransform->QueryInterface<ICodecAPI>(&m_pCodecApi));
		//Set requested bitrate on media type
	
		//Quality-based VBR for VOD recordings using the VBR quality parameter
		VariantInit(&var);
		var.vt = VT_UI4;
		var.ulVal = eAVEncCommonRateControlMode_Quality;
		CHECK_HR(hr = m_pCodecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var));
		VariantClear(&var);
	
		VariantInit(&var);
		var.vt = VT_UI4;
		var.ulVal = nVBRQuality;
		CHECK_HR(hr = m_pCodecApi->SetValue(&CODECAPI_AVEncCommonQuality, &var));
		VariantClear(&var);
	
		VariantInit(&var);
		var.vt = VT_UI4;
		var.ulVal = nMeanBitrate;
		CHECK_HR(hr = m_pCodecApi->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var));
	}

	//Set Output type on the encoder
	CHECK_HR(hr = pOutputType->SetUINT32(MF_MT_AVG_BITRATE, nMeanBitrate));
	CHECK_HR(hr = m_pTransform->SetOutputType(0, pOutputType, 0));

	//Look for compatible input type that the encoder supports
	while (SUCCEEDED(hr))
	{
		hr = m_pTransform->GetInputAvailableType(0, i, &pInputType);
		if (SUCCEEDED(hr))
		{
			hr = pInputType->GetGUID(MF_MT_SUBTYPE, &subtype);
			if (SUCCEEDED(hr))
			{
				if (IsEqualGUID(subtype, subtypeSource))
				{
					hr = m_pTransform->SetInputType(0, pInputType, 0);
					if (SUCCEEDED(hr))
					{
						break;
					}
				}
			}
		}
		i++;
	}

done:
#ifdef DEBUG_MEDIA_TYPES
	wprintf(L"CEncoderH264::ConfigureEncoder:pSourceType Media Type:\n");
	LogMediaType(pSourceType);
	wprintf(L"CEncoderH264::ConfigureEncoder:pInputType Media Type:\n");
	LogMediaType(pInputType);
	wprintf(L"CEncoderH264::ConfigureEncoder:pOutputType Media Type:\n");
	LogMediaType(pOutputType);
#endif
	SafeRelease(&pInputType);
	return hr;
}

HRESULT CEncoderH264::Encode(BYTE* pYuvData, UINT32 nYuvSize, BYTE* pH264Data, UINT32* nH264Size, LONGLONG sampleTime, LONGLONG sampleDuration)
{
	if (!m_pSample)
	{
		return E_INVALIDARG;
	}

	if (!m_pTransform)
	{
		return MF_E_NOT_INITIALIZED;
	}
	
	HRESULT hr = S_OK, hrRes = S_OK;
	DWORD dwStatus = 0;
	DWORD cbTotalLength = 0, cbCurrentLength = 0;
	BYTE *pDataIn = NULL;
	BYTE *pDataOut = NULL;
	IMFMediaBuffer* pBufferIn = NULL;
	IMFMediaBuffer* pBufferOut = NULL;
	IMFSample* pSampleOut = NULL;
	IMFMediaType* pMediaType = NULL;

	//get the size of the output buffer processed by the encoder.
	//There is only one output so the output stream id is 0.
	MFT_OUTPUT_STREAM_INFO mftStreamInfo;
	ZeroMemory(&mftStreamInfo, sizeof(MFT_OUTPUT_STREAM_INFO));
	CHECK_HR (hr =  m_pTransform->GetOutputStreamInfo(0, &mftStreamInfo));
	
	//Send input to the encoder.
	CHECK_HR(hr = m_pSample->GetBufferByIndex(0, &pBufferIn));
	CHECK_HR(hr = pBufferIn->Lock(&pDataIn, NULL, NULL));
	memcpy(pDataIn, pYuvData, nYuvSize);
	CHECK_HR(hr = pBufferIn->Unlock());
	// CHECK_HR(hr = m_pSample->SetSampleTime(sampleTime));
	// CHECK_HR(hr = m_pSample->SetSampleDuration(sampleDuration));
	CHECK_HR(hr = m_pTransform->ProcessInput(0, m_pSample, 0));

	//Generate the output sample
	MFT_OUTPUT_DATA_BUFFER mftOutputData;
	ZeroMemory(&mftOutputData, sizeof(mftOutputData));
	CHECK_HR (hr = MFCreateMemoryBuffer(mftStreamInfo.cbSize, &pBufferOut));
	CHECK_HR (hr = MFCreateSample(&pSampleOut));
	CHECK_HR (hr = pSampleOut->AddBuffer(pBufferOut));
	mftOutputData.pSample = pSampleOut;
	mftOutputData.dwStreamID = 0;
	hrRes =  m_pTransform->ProcessOutput(0, 1, &mftOutputData, &dwStatus);

	//If more input is needed there was no output to process. Return and repeat
	if(hrRes != MF_E_TRANSFORM_NEED_MORE_INPUT) {

		//Get a pointer to the memory
		CHECK_HR (hr = pBufferOut->Lock(&pDataOut, &cbTotalLength, &cbCurrentLength)); 
		
		// *nH264Size = cbCurrentLength;
		// memcpy(pH264Data, pDataOut, *nH264Size);

		//Write elementary h.264/AAC/AVC data to file
		if(m_pH264File) {
			fwrite (pDataOut , sizeof(BYTE), cbCurrentLength, m_pH264File);
		}

		CHECK_HR (hr = pBufferOut->Unlock());
		pDataOut = NULL;

		//Send Sample to MP4 StreamSink for writing
		if(m_pStreamSink) {
			hr = m_pStreamSink->ProcessSample(pSampleOut);
		}
	}

done:

	if (pDataOut && FAILED(hr))
	{
		pBufferOut->Unlock();
	}

	SAFE_RELEASE(pBufferOut);
	SAFE_RELEASE(pSampleOut);
	SAFE_RELEASE(pMediaType);
	if(hr == MF_E_TRANSFORM_NEED_MORE_INPUT) hr=S_OK;	//Do not log valid error
	return hr;
}

HRESULT CEncoderH264::Start() 
{
	HRESULT hr = S_OK;

	if (!m_pTransform)
		return E_POINTER;

	//Start MP4 SinkWriter
	if(m_pSinkWriter) {
		CHECK_HR (hr = m_pSinkWriter->BeginWriting());
	}

	//Start the encoder so it is ready to receive samples on the input
	if (SUCCEEDED(hr)) 
	{
		// CHECK_HR (hr = m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL));
		CHECK_HR (hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
		CHECK_HR (hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));
	}

	MFT_INPUT_STREAM_INFO mftInputStreamInfo;
	ZeroMemory(&mftInputStreamInfo, sizeof(MFT_INPUT_STREAM_INFO));
	CHECK_HR(hr = m_pTransform->GetInputStreamInfo(0, &mftInputStreamInfo));

done:
	return hr;
}

HRESULT CEncoderH264::Stop() 
{
	HRESULT hr = S_OK;

	if (!m_pTransform)
		return E_POINTER;

	//Stop the encoder
	if (SUCCEEDED(hr))
	{
		CHECK_HR (hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL));
		CHECK_HR (hr = m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL));
		CHECK_HR (hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL));
	}

	//Stop MP4 SinkWriter
	if(m_pSinkWriter) {
		CHECK_HR (hr = m_pSinkWriter->Finalize());
	}

done:
	return hr;
}

HRESULT CEncoderH264::CreateUncompressedVideoType(
	DWORD                fccFormat,  // FOURCC or D3DFORMAT value.     
	UINT32               width,
	UINT32               height,
	MFVideoInterlaceMode interlaceMode,
	const MFRatio&       frameRate,
	const MFRatio&       par,
	IMFMediaType         **ppType
)
{
	if (ppType == NULL)
	{
		return E_POINTER;
	}

	GUID    subtype = MFVideoFormat_Base;
	LONG    lStride = 0;
	UINT    cbImage = 0;

	IMFMediaType *pType = NULL;

	// Set the subtype GUID from the FOURCC or D3DFORMAT value.
	subtype.Data1 = fccFormat;

	HRESULT hr = MFCreateMediaType(&pType);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pType->SetGUID(MF_MT_SUBTYPE, subtype);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pType->SetUINT32(MF_MT_INTERLACE_MODE, interlaceMode);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = MFSetAttributeSize(pType, MF_MT_FRAME_SIZE, width, height);
	if (FAILED(hr))
	{
		goto done;
	}

	// Calculate the default stride value.
	hr = pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
	if (FAILED(hr))
	{
		goto done;
	}

	// Calculate the image size in bytes.
	hr = MFCalculateImageSize(subtype, width, height, &cbImage);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pType->SetUINT32(MF_MT_SAMPLE_SIZE, cbImage);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if (FAILED(hr))
	{
		goto done;
	}

	// Frame rate
	hr = MFSetAttributeRatio(pType, MF_MT_FRAME_RATE, frameRate.Numerator,
		frameRate.Denominator);
	if (FAILED(hr))
	{
		goto done;
	}

	// Pixel aspect ratio
	hr = MFSetAttributeRatio(pType, MF_MT_PIXEL_ASPECT_RATIO, par.Numerator,
		par.Denominator);
	if (FAILED(hr))
	{
		goto done;
	}

	// Return the pointer to the caller.
	*ppType = pType;
	(*ppType)->AddRef();

done:
	SafeRelease(&pType);
	return hr;
}

HRESULT CEncoderH264::CreateMFSampleFromMediaType(IMFMediaType* pMediaType, IMFSample** pSample)
{
	HRESULT hr = S_OK;
	UINT uiWidth = 0, uiHeight = 0, cbBufferSize = 0;
	IMFSample *pYUVSample = NULL;
	IMFMediaBuffer *pBuffer = NULL;

	CHECK_HR(hr = pMediaType->GetUINT32(MF_MT_SAMPLE_SIZE, &cbBufferSize));
	CHECK_HR(hr = MFCreateMemoryBuffer(cbBufferSize, &pBuffer));
	CHECK_HR(hr = MFCreateSample(&pYUVSample));
	CHECK_HR(hr = pYUVSample->AddBuffer(pBuffer));
	CHECK_HR(hr = pYUVSample->SetSampleTime(NULL));
	CHECK_HR(hr = pYUVSample->SetSampleDuration(NULL));

done:
	if (pYUVSample)
	{
		*pSample = pYUVSample;
		(*pSample)->AddRef();
	}
	SafeRelease(&pYUVSample);
	return hr;
}

HRESULT CEncoderH264::CopyAttribute(IMFAttributes *pSrc, IMFAttributes *pDest, const GUID& key)
{
	try
	{
		PROPVARIANT var;
		PropVariantInit(&var);

		HRESULT hr = S_OK;

		hr = pSrc->GetItem(key, &var);
		if (SUCCEEDED(hr))
		{
			hr = pDest->SetItem(key, var);
		}

		PropVariantClear(&var);
		return hr;
	}
	catch (...) {
		return HRESULT_FROM_WIN32(ERROR_UNHANDLED_EXCEPTION);
	}
}

void gstreamer_mft_encoder() {
	int width = 1280, height = 720;
	auto frame_rate = _MFRatio(30000, 1001);

	CEncoderH264 encoder("output_video.mp4", false, true);
	encoder.ConfigureEncoder(FCC('NV12'), width, height, frame_rate, 100, (UINT)6e6, (UINT)10e6);
	encoder.Start();

	std::ifstream fpFile;
	fpFile = std::ifstream(L"input.yuv", std::ifstream::in | std::ifstream::binary);
	_ASSERT(fpFile.is_open());

	int frame_size = (width * height * 3) / 2;
	std::unique_ptr<uint8_t[]> cache_frame;
	cache_frame.reset(new uint8_t[frame_size]);
	
	BYTE* dummy = NULL; 
	UINT32* dummy_size = NULL;

	while (true) {
		std::streamsize s_read_size = fpFile.read(reinterpret_cast<char*>(cache_frame.get()), frame_size).gcount();
		if (s_read_size == frame_size) {
			encoder.Encode(cache_frame.get(), frame_size, dummy, dummy_size, 0, 0);
		}
		else {
			encoder.Stop();
			break;
		}
	}

	if (fpFile.is_open())
	{
		fpFile.close();
	}

}