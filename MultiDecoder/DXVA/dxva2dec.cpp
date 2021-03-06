#include "dxva2dec.h"
#include <Shlwapi.h>

#include "libavcodec/dxva2.h"
#include "gpu_memcpy_sse4.h"
#include <ppl.h>
#include <assert.h>

#pragma comment ( lib, "d3d9.lib" )
#pragma comment ( lib, "d3dx9.lib" )

std::wstring WStringFromGUID(const GUID& guid)
{
	WCHAR null[128] = { 0 }, buff[128];
	StringFromGUID2(GUID_NULL, null, 127);
	return std::wstring(StringFromGUID2(guid, buff, 127) > 0 ? buff : null);
}

/*
DXVA2 Codec Mappings, as defined by VLC
*/
typedef struct {
	const char   *name;
	const GUID   *guid;
	int          codec;
} dxva2_mode_t;
/* XXX Prefered modes must come first */
static const dxva2_mode_t dxva2_modes[] = {
	/* MPEG-1/2 */
		{ "MPEG-2 variable-length decoder", &DXVA2_ModeMPEG2_VLD, AV_CODEC_ID_MPEG2VIDEO },
		{ "MPEG-2 & MPEG-1 variable-length decoder", &DXVA2_ModeMPEG2and1_VLD, AV_CODEC_ID_MPEG2VIDEO },
		{ "MPEG-2 motion compensation", &DXVA2_ModeMPEG2_MoComp, 0 },
		{ "MPEG-2 inverse discrete cosine transform", &DXVA2_ModeMPEG2_IDCT, 0 },

		{ "MPEG-1 variable-length decoder", &DXVA2_ModeMPEG1_VLD, 0 },

		/* H.264 */
		{ "H.264 variable-length decoder, film grain technology", &DXVA2_ModeH264_F, AV_CODEC_ID_H264 },
		{ "H.264 variable-length decoder, no film grain technology", &DXVA2_ModeH264_E, AV_CODEC_ID_H264 },
		{ "H.264 variable-length decoder, no film grain technology, FMO/ASO", &DXVA_ModeH264_VLD_WithFMOASO_NoFGT, AV_CODEC_ID_H264 },
		{ "H.264 variable-length decoder, no film grain technology, Flash", &DXVA_ModeH264_VLD_NoFGT_Flash, AV_CODEC_ID_H264 },

		{ "H.264 inverse discrete cosine transform, film grain technology", &DXVA2_ModeH264_D, 0 },
		{ "H.264 inverse discrete cosine transform, no film grain technology", &DXVA2_ModeH264_C, 0 },

		{ "H.264 motion compensation, film grain technology", &DXVA2_ModeH264_B, 0 },
		{ "H.264 motion compensation, no film grain technology", &DXVA2_ModeH264_A, 0 },

		/* WMV */
		{ "Windows Media Video 8 motion compensation", &DXVA2_ModeWMV8_B, 0 },
		{ "Windows Media Video 8 post processing", &DXVA2_ModeWMV8_A, 0 },

		{ "Windows Media Video 9 IDCT", &DXVA2_ModeWMV9_C, 0 },
		{ "Windows Media Video 9 motion compensation", &DXVA2_ModeWMV9_B, 0 },
		{ "Windows Media Video 9 post processing", &DXVA2_ModeWMV9_A, 0 },

		/* VC-1 */
		{ "VC-1 variable-length decoder (2010)", &DXVA2_ModeVC1_D2010, AV_CODEC_ID_VC1 },
		{ "VC-1 variable-length decoder (2010)", &DXVA2_ModeVC1_D2010, AV_CODEC_ID_WMV3 },
		{ "VC-1 variable-length decoder", &DXVA2_ModeVC1_D, AV_CODEC_ID_VC1 },
		{ "VC-1 variable-length decoder", &DXVA2_ModeVC1_D, AV_CODEC_ID_WMV3 },

		{ "VC-1 inverse discrete cosine transform", &DXVA2_ModeVC1_C, 0 },
		{ "VC-1 motion compensation", &DXVA2_ModeVC1_B, 0 },
		{ "VC-1 post processing", &DXVA2_ModeVC1_A, 0 },

		/* MPEG4-ASP */
		{ "MPEG-4 Part 2 nVidia bitstream decoder", &DXVA_nVidia_MPEG4_ASP, 0 },
		{ "MPEG-4 Part 2 variable-length decoder, Simple Profile", &DXVA_ModeMPEG4pt2_VLD_Simple, 0 },
		{ "MPEG-4 Part 2 variable-length decoder, Simple&Advanced Profile, no GMC", &DXVA_ModeMPEG4pt2_VLD_AdvSimple_NoGMC, 0 },
		{ "MPEG-4 Part 2 variable-length decoder, Simple&Advanced Profile, GMC", &DXVA_ModeMPEG4pt2_VLD_AdvSimple_GMC, 0 },
		{ "MPEG-4 Part 2 variable-length decoder, Simple&Advanced Profile, Avivo", &DXVA_ModeMPEG4pt2_VLD_AdvSimple_Avivo, 0 },

		/* H.264 MVC */
		{ "H.264 MVC variable-length decoder, stereo, progressive", &DXVA_ModeH264_VLD_Stereo_Progressive_NoFGT, 0 },
		{ "H.264 MVC variable-length decoder, stereo", &DXVA_ModeH264_VLD_Stereo_NoFGT, 0 },
		{ "H.264 MVC variable-length decoder, multiview", &DXVA_ModeH264_VLD_Multiview_NoFGT, 0 },

		/* H.264 SVC */
		{ "H.264 SVC variable-length decoder, baseline", &DXVA_ModeH264_VLD_SVC_Scalable_Baseline, 0 },
		{ "H.264 SVC variable-length decoder, constrained baseline", &DXVA_ModeH264_VLD_SVC_Restricted_Scalable_Baseline, 0 },
		{ "H.264 SVC variable-length decoder, high", &DXVA_ModeH264_VLD_SVC_Scalable_High, 0 },
		{ "H.264 SVC variable-length decoder, constrained high progressive", &DXVA_ModeH264_VLD_SVC_Restricted_Scalable_High_Progressive, 0 },

		/* HEVC / H.265 */
		{ "HEVC / H.265 variable-length decoder, main", &DXVA_ModeHEVC_VLD_Main, AV_CODEC_ID_HEVC },
		{ "HEVC / H.265 variable-length decoder, main10", &DXVA_ModeHEVC_VLD_Main10, AV_CODEC_ID_HEVC },

		/* Intel specific modes (only useful on older GPUs) */
		{ "H.264 variable-length decoder, no film grain technology (Intel ClearVideo)", &DXVADDI_Intel_ModeH264_E, AV_CODEC_ID_H264 },
		{ "H.264 inverse discrete cosine transform, no film grain technology (Intel)", &DXVADDI_Intel_ModeH264_C, 0 },
		{ "H.264 motion compensation, no film grain technology (Intel)", &DXVADDI_Intel_ModeH264_A, 0 },
		{ "VC-1 variable-length decoder 2 (Intel)", &DXVA_Intel_VC1_ClearVideo_2, 0 },
		{ "VC-1 variable-length decoder (Intel)", &DXVA_Intel_VC1_ClearVideo, 0 },

		{ nullptr, nullptr, 0 }
};

static const dxva2_mode_t *DXVA2FindMode(const GUID *guid)
{
	for (unsigned i = 0; dxva2_modes[i].name; i++) {
		if (IsEqualGUID(*dxva2_modes[i].guid, *guid))
			return &dxva2_modes[i];
	}
	return nullptr;
}

// List of PCI Device ID of ATI cards with UVD or UVD+ decoding block.
static DWORD UVDDeviceID[] = 
{
	0x94C7, // ATI Radeon HD 2350
	0x94C1, // ATI Radeon HD 2400 XT
	0x94CC, // ATI Radeon HD 2400 Series
	0x958A, // ATI Radeon HD 2600 X2 Series
	0x9588, // ATI Radeon HD 2600 XT
	0x9405, // ATI Radeon HD 2900 GT
	0x9400, // ATI Radeon HD 2900 XT
	0x9611, // ATI Radeon 3100 Graphics
	0x9610, // ATI Radeon HD 3200 Graphics
	0x9614, // ATI Radeon HD 3300 Graphics
	0x95C0, // ATI Radeon HD 3400 Series (and others)
	0x95C5, // ATI Radeon HD 3400 Series (and others)
	0x95C4, // ATI Radeon HD 3400 Series (and others)
	0x94C3, // ATI Radeon HD 3410
	0x9589, // ATI Radeon HD 3600 Series (and others)
	0x9598, // ATI Radeon HD 3600 Series (and others)
	0x9591, // ATI Radeon HD 3600 Series (and others)
	0x9501, // ATI Radeon HD 3800 Series (and others)
	0x9505, // ATI Radeon HD 3800 Series (and others)
	0x9507, // ATI Radeon HD 3830
	0x9513, // ATI Radeon HD 3850 X2
	0x950F, // ATI Radeon HD 3850 X2
	0x0000
};

static int IsAMDUVD(DWORD dwDeviceId)
{
	for (int i = 0; UVDDeviceID[i] != 0; i++) {
		if (UVDDeviceID[i] == dwDeviceId)
			return 1;
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
// DXVA2 decoder implementation
////////////////////////////////////////////////////////////////////////////////

CopyFrameProc CopyFrameNV12 = NULL;
CopyFrameProc CopyFrameYUV420P = NULL;

static void CopyFrameNV12_fallback(const BYTE *pSourceData, BYTE *pY, BYTE *pUV, size_t surfaceHeight, size_t imageHeight, size_t pitch)
{
	const size_t size = imageHeight * pitch;
	memcpy(pY, pSourceData, size);
	memcpy(pUV, pSourceData + (surfaceHeight * pitch), size >> 1);
}

static void CopyFrameNV12_fallback_MT(const BYTE *pSourceData, BYTE *pY, BYTE *pUV, size_t surfaceHeight, size_t imageHeight, size_t pitch)
{
	const size_t halfSize = (imageHeight * pitch) >> 1;
	Concurrency::parallel_for(0, 3, [&](int i) {
		if (i < 2)
			memcpy(pY + (halfSize * i), pSourceData + (halfSize * i), halfSize);
		else
			memcpy(pUV, pSourceData + (surfaceHeight * pitch), halfSize);
	});
}

static void CopyFrameNV12_SSE4(const BYTE *pSourceData, BYTE *pY, BYTE *pUV, size_t surfaceHeight, size_t imageHeight, size_t pitch)
{
	const size_t size = imageHeight * pitch;
	gpu_memcpy(pY, pSourceData, size);
	gpu_memcpy(pUV, pSourceData + (surfaceHeight * pitch), size >> 1);
}

static void CopyFrameNV12_SSE4_MT(const BYTE *pSourceData, BYTE *pY, BYTE *pUV, size_t surfaceHeight, size_t imageHeight, size_t pitch)
{
	const size_t halfSize = (imageHeight * pitch) >> 1;
	Concurrency::parallel_for(0, 3, [&](int i) 
	{
		if (i < 2)
			gpu_memcpy(pY + (halfSize * i), pSourceData + (halfSize * i), halfSize);
		else
			gpu_memcpy(pUV, pSourceData + (surfaceHeight * pitch), halfSize);
	});
}
void CopyPlane(uint8_t *dst, size_t dst_pitch,
					const uint8_t *src, size_t src_pitch,
					unsigned width, unsigned height)
{
	unsigned int y;
	for (y = 0; y < height; y++) 
	{
		memcpy(dst, src, width);
		src += src_pitch;
		dst += dst_pitch;
	}
}

CDXVA2Decode::CDXVA2Decode(void)
{
	ZeroMemory(&m_nVtableAddr, sizeof(CDXVA2Decode) - offsetof(CDXVA2Decode,m_nVtableAddr));
	m_DecoderPixelFormat = AV_PIX_FMT_NONE;
	m_guidDecoderDevice = GUID_NULL;
	m_DisplayDelay = DXVA2_QUEUE_SURFACES;
}

// 把NV12格式的图像复制到dstFrame中
// bool CDecDXVA2::CopyFrame( AVFrame *dstFrame,AVFrame *srcFrame)
// {
// 	LPDIRECT3DSURFACE9 pSurface = (LPDIRECT3DSURFACE9)(uintptr_t)srcFrame->data[3];
// 
// 	D3DSURFACE_DESC surfaceDesc;
// 	pSurface->GetDesc(&surfaceDesc);
// 	dstFrame->width = srcFrame->width;
// 	dstFrame->height = srcFrame->height;
// 	av_frame_copy_props(dstFrame, srcFrame);
// 	dstFrame->format = AV_PIX_FMT_NV12;
// 	D3DLOCKED_RECT LockedRect;
// 	HRESULT hr = pSurface->LockRect(&LockedRect, nullptr, D3DLOCK_READONLY);
// 	if (FAILED(hr)) 
// 	{
// 		DxTraceMsg("pSurface->LockRect failed (hr: %X)\n", hr);
// 		return false;
// 	}
// 	
// 	// Y分量图像
// 	uint8_t *pY	 = (uint8_t*)LockedRect.pBits;
// 	// UV分量图像
// 	uint8_t *pUV = (uint8_t*)LockedRect.pBits + LockedRect.Pitch * surfaceDesc.Height;	
// 
// 	CopyPlane(dstFrame->data[0], dstFrame->linesize[0], pY, LockedRect.Pitch, srcFrame->width, srcFrame->height);
// 	CopyPlane(dstFrame->data[1], dstFrame->linesize[1], pUV, LockedRect.Pitch, srcFrame->width, srcFrame->height / 2);
// 	
// 	pSurface->UnlockRect();
// 	return true;
// }

CDXVA2Decode::~CDXVA2Decode(void)
{
	DestroyDXVADecoder(true);

}

STDMETHODIMP CDXVA2Decode::DestroyDXVADecoder(bool bFull, bool bNoAVCodec)
{
	//m_pCallback->ReleaseAllDXVAResources();	// 释放最后一帧
	for (int i = 0; i < m_NumSurfaces; i++) 
	{
		SafeRelease(m_pSurfaces[i].d3d);
	}
	m_NumSurfaces = 0;

	SafeRelease(m_pDecoder);

	if (!bNoAVCodec) 
	{
		DestroyDecoder();
	}

	if (bFull) 
	{
		FreeD3DResources();
	}

	return S_OK;
}


STDMETHODIMP CDXVA2Decode::FreeD3DResources()
{
	SafeRelease(m_pDXVADecoderService);
	if (m_pD3DDevMngr && m_hDevice != INVALID_HANDLE_VALUE)
		m_pD3DDevMngr->CloseDeviceHandle(m_hDevice);
	m_hDevice = INVALID_HANDLE_VALUE;
	SafeRelease(m_pD3DDevMngr);
	SafeRelease(m_pD3DDev);
	SafeRelease(m_pD3D);

	if (m_dxva.dxva2lib) {
		FreeLibrary(m_dxva.dxva2lib);
		m_dxva.dxva2lib = nullptr;
	}

	return S_OK;
}


STDMETHODIMP CDXVA2Decode::LoadDXVA2Functions()
{
	// Load DXVA2 library
	//dx.dxva2lib = LoadLibraryW("E://DXVA//TDxvaWin32//Debug//dxva2.dll.\n");
	m_dxva.dxva2lib = ::LoadLibraryA("dxva2.dll");
	if (m_dxva.dxva2lib == nullptr) {
		DxTraceMsg( "-> Loading dxva2.dll failed.\n");
		return E_FAIL;
	}

	m_dxva.createDeviceManager = (pCreateDeviceManager9 *)GetProcAddress(m_dxva.dxva2lib, "DXVA2CreateDirect3DDeviceManager9");
	if (m_dxva.createDeviceManager == nullptr) {
		DxTraceMsg( "-> DXVA2CreateDirect3DDeviceManager9 unavailable.\n");
		return E_FAIL;
	}

	return S_OK;
}

#define VEND_ID_ATI     0x1002
#define VEND_ID_NVIDIA  0x10DE
#define VEND_ID_INTEL   0x8086

static const struct {
	unsigned id;
	char     name[32];
} vendors[] = {
		{ VEND_ID_ATI, "ATI" },
		{ VEND_ID_NVIDIA, "NVIDIA" },
		{ VEND_ID_INTEL, "Intel" },
		{ 0, "" }
};

HRESULT CDXVA2Decode::CreateD3DDeviceManager(IDirect3DDevice9Ex *pDevice, UINT *pReset, IDirect3DDeviceManager9 **ppManager)
{
	UINT resetToken = 0;
	IDirect3DDeviceManager9 *pD3DManager = nullptr;
	HRESULT hr = m_dxva.createDeviceManager(&resetToken, &pD3DManager);
	if (FAILED(hr)) {
		DxTraceMsg("-> DXVA2CreateDirect3DDeviceManager9 failed.\n");
		goto done;
	}

	hr = pD3DManager->ResetDevice(pDevice, resetToken);
	if (FAILED(hr)) {
		DxTraceMsg( "-> ResetDevice failed.\n");
		goto done;
	}

	*ppManager = pD3DManager;
	(*ppManager)->AddRef();

	*pReset = resetToken;
done:
	SafeRelease(pD3DManager);
	return hr;
}

HRESULT CDXVA2Decode::CreateDXVAVideoService(IDirect3DDeviceManager9 *pManager, IDirectXVideoDecoderService **ppService)
{
	HRESULT hr = S_OK;

	IDirectXVideoDecoderService *pService = nullptr;

	hr = pManager->OpenDeviceHandle(&m_hDevice);
	if (FAILED(hr)) {
		m_hDevice = INVALID_HANDLE_VALUE;
		DxTraceMsg( "-> OpenDeviceHandle failed.\n");
		goto done;
	}

	hr = pManager->GetVideoService(m_hDevice, IID_IDirectXVideoDecoderService, (void**)&pService);
	if (FAILED(hr)) {
		DxTraceMsg( "-> Acquiring VideoDecoderService failed.\n");
		goto done;
	}

	(*ppService) = pService;

done:
	return hr;
}

HRESULT CDXVA2Decode::FindVideoServiceConversion(AVCodecID codec, bool bHighBitdepth, GUID *input, D3DFORMAT *output)
{
	HRESULT hr = S_OK;

	UINT count = 0;
	GUID *input_list = nullptr;

	/* Gather the format supported by the decoder */
	hr = m_pDXVADecoderService->GetDecoderDeviceGuids(&count, &input_list);
	if (FAILED(hr)) {
		DxTraceMsg("-> GetDecoderDeviceGuids failed with hr: %X\n", hr);
		goto done;
	}

	DxTraceMsg("-> Enumerating supported DXVA2 modes (count: %d)\n", count);
	for (unsigned i = 0; i < count; i++) {
		const GUID *g = &input_list[i];
		const dxva2_mode_t *mode = DXVA2FindMode(g);
		if (mode) {
			DxTraceMsg( "  -> %S\n", mode->name);
		}
		else {
			DxTraceMsg("  -> Unknown GUID (%s)\n", WStringFromGUID(*g).c_str());
		}
	}

	/* Iterate over our priority list */
	for (unsigned i = 0; dxva2_modes[i].name; i++) {
		const dxva2_mode_t *mode = &dxva2_modes[i];
		if (!mode->codec || mode->codec != codec)
			continue;

		BOOL supported = FALSE;
		for (const GUID *g = &input_list[0]; !supported && g < &input_list[count]; g++) {
			supported = IsEqualGUID(*mode->guid, *g);

			if (codec == AV_CODEC_ID_HEVC && bHighBitdepth && !IsEqualGUID(*g, DXVA_ModeHEVC_VLD_Main10))
				supported = false;
			else if (codec == AV_CODEC_ID_HEVC && !bHighBitdepth && IsEqualGUID(*g, DXVA_ModeHEVC_VLD_Main10))
				supported = false;
		}
		if (!supported)
			continue;

		DxTraceMsg( "-> Trying to use '%S'\n", mode->name);
		UINT out_count = 0;
		D3DFORMAT *out_list = nullptr;
		hr = m_pDXVADecoderService->GetDecoderRenderTargets(*mode->guid, &out_count, &out_list);
		if (FAILED(hr)) {
			DxTraceMsg("-> Retrieving render targets failed with hr: %X\n", hr);
			continue;
		}

		BOOL matchingFormat = FALSE;
		D3DFORMAT format = D3DFMT_UNKNOWN;
		DxTraceMsg( "-> Enumerating render targets (count: %d)\n", out_count);
		for (unsigned j = 0; j < out_count; j++) {
			const D3DFORMAT f = out_list[j];
			DxTraceMsg("  -> %d is supported (%4.4S)\n", f, (const char *)&f);
			if (bHighBitdepth && (f == FOURCC_P010 || f == FOURCC_P016)) {
				matchingFormat = TRUE;
				format = f;
			}
			else if (!bHighBitdepth && f == FOURCC_NV12) {
				matchingFormat = TRUE;
				format = f;
			}
		}
		if (matchingFormat) {
			DxTraceMsg(( "-> Found matching output format, finished setup.\n"));
			*input = *mode->guid;
			*output = format;

			SAFE_CO_FREE(out_list);
			SAFE_CO_FREE(input_list);

			return S_OK;
		}

		SAFE_CO_FREE(out_list);
	}

done:
	SAFE_CO_FREE(input_list);
	return E_FAIL;
}

/**
 * This function is only called in non-native mode
 * Its responsibility is to initialize D3D, create a device and a device manager
 * and call SetD3DDeviceManager with it.
 */
HRESULT CDXVA2Decode::InitD3D(UINT &nAdapter)
{
	HRESULT hr = S_OK;
	if (FAILED(hr = LoadDXVA2Functions())) 
	{
		DxTraceMsg(( "%s Failed to load DXVA2 DLL functions.\n"),__FUNCTION__);
		return E_FAIL;
	}
	
	hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &m_pD3D);
	if (FAILED(hr))
	{
		DxTraceMsg("%s Failed to acquire IDirect3D9Ex.\n",__FUNCTION__);
		return E_FAIL;
	}

retry_default:
	D3DADAPTER_IDENTIFIER9 d3dai = { 0 };
	hr = m_pD3D->GetAdapterIdentifier(nAdapter, 0, &d3dai);
	if (FAILED(hr)) 
	{
		// retry if the adapter is invalid
		if (nAdapter != D3DADAPTER_DEFAULT) 
		{
			nAdapter = D3DADAPTER_DEFAULT;
			goto retry_default;
		}
		DxTraceMsg( "%s Querying of adapter identifier failed with hr: %X.\n",__FUNCTION__, hr);
		return E_FAIL;
	}

	const char *vendor = "Unknown";
	for (int i = 0; vendors[i].id != 0; i++) 
	{
		if (vendors[i].id == d3dai.VendorId) 
		{
			vendor = vendors[i].name;
			break;
		}
	}

	DxTraceMsg("%s Running on adapter %d, %S, vendor 0x%04X(%S), device 0x%04X.\n",__FUNCTION__, nAdapter, d3dai.Description, d3dai.VendorId, vendor, d3dai.DeviceId);
	m_dwVendorId = d3dai.VendorId;
	m_dwDeviceId = d3dai.DeviceId;

	ZeroMemory(&m_d3dpp, sizeof(m_d3dpp));

	m_d3dpp.Windowed		 = TRUE;
	m_d3dpp.BackBufferWidth	 = 640/*GetSystemMetrics(SM_CXSCREEN)*/;
	m_d3dpp.BackBufferHeight = 480/* GetSystemMetrics(SM_CYSCREEN)*/;
	m_d3dpp.BackBufferCount	 = 0;
	m_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN/*d3ddm.Format*/;
	m_d3dpp.SwapEffect		 = D3DSWAPEFFECT_DISCARD;
	m_d3dpp.Flags			 = D3DPRESENTFLAG_VIDEO | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	
	hr = m_pD3D->CreateDeviceEx(nAdapter, 
								D3DDEVTYPE_HAL, 
								GetShellWindow(), 
								D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
								&m_d3dpp, 
								NULL,
								&m_pD3DDev);
	if (FAILED(hr)) 
	{
		DxTraceMsg("%s Creation of device failed with hr: %X.\n",__FUNCTION__, hr);
		return E_FAIL;
	}
	
	hr = CreateD3DDeviceManager(m_pD3DDev, &m_pD3DResetToken, &m_pD3DDevMngr);
	if (FAILED(hr)) 
	{
		DxTraceMsg("%s Creation of Device manager failed with hr: %X.\n", __FUNCTION__,hr);
		return E_FAIL;
	}

	hr = SetD3DDeviceManager(m_pD3DDevMngr);
	if (FAILED(hr)) 
	{
		DxTraceMsg( "%s SetD3DDeviceManager failed with hr: %X.\n",__FUNCTION__, hr);
		return E_FAIL;
	}

	// 需要编写CopyFrameYUV420P_SSE4_MT等函数
	if (CopyFrameNV12 == nullptr ||
		CopyFrameYUV420P == nullptr) 
	{
		int cpu_flags = av_get_cpu_flags();
		if (cpu_flags & AV_CPU_FLAG_SSE4) 
		{
			DxTraceMsg("%s Using SSE4 frame copy.\n",__FUNCTION__);
			if (m_dwVendorId == VEND_ID_INTEL)
			{
				CopyFrameNV12 = CopyFrameNV12_SSE4_MT;
				CopyFrameYUV420P = nullptr;
			}
			else
			{
				CopyFrameNV12 = CopyFrameNV12_SSE4;
				CopyFrameYUV420P = nullptr;
			}
		}
		else 
		{
			DxTraceMsg("%s Using fallback frame copy.\n",__FUNCTION__);
			if (m_dwVendorId == VEND_ID_INTEL)
			{
				CopyFrameNV12 = CopyFrameNV12_fallback_MT;
				CopyFrameYUV420P = nullptr;
			}
			else
			{
				CopyFrameNV12 = CopyFrameNV12_fallback;
				CopyFrameYUV420P = nullptr;
			}
		}
	}

	return S_OK;
}

HRESULT CDXVA2Decode::RetrieveVendorId(IDirect3DDeviceManager9 *pDevManager)
{
	HANDLE hDevice = 0;
	IDirect3D9 *pD3D = nullptr;
	IDirect3DDevice9 *pDevice = nullptr;

	HRESULT hr = pDevManager->OpenDeviceHandle(&hDevice);
	if (FAILED(hr)) {
		DxTraceMsg("-> Failed to open device handle with hr: %X.\n", hr);
		goto done;
	}

	hr = pDevManager->LockDevice(hDevice, &pDevice, TRUE);
	if (FAILED(hr)) {
		DxTraceMsg("-> Failed to lock device with hr: %X.\n", hr);
		goto done;
	}

	hr = pDevice->GetDirect3D(&pD3D);
	if (FAILED(hr)) {
		DxTraceMsg("-> Failed to get D3D object hr: %X.\n", hr);
		goto done;
	}

	D3DDEVICE_CREATION_PARAMETERS devParams;
	hr = pDevice->GetCreationParameters(&devParams);
	if (FAILED(hr)) {
		DxTraceMsg( "-> Failed to get device creation params hr: %X.\n", hr);
		goto done;
	}

	D3DADAPTER_IDENTIFIER9 adIdentifier;
	hr = pD3D->GetAdapterIdentifier(devParams.AdapterOrdinal, 0, &adIdentifier);
	if (FAILED(hr)) {
		DxTraceMsg("-> Failed to get adapter identified hr: %X.\n", hr);
		goto done;
	}

	m_dwVendorId = adIdentifier.VendorId;
	m_dwDeviceId = adIdentifier.DeviceId;

done:
	SafeRelease(pD3D);
	SafeRelease(pDevice);
	if (hDevice && hDevice != INVALID_HANDLE_VALUE) {
		pDevManager->UnlockDevice(hDevice, FALSE);
		pDevManager->CloseDeviceHandle(hDevice);
	}
	return hr;
}

HRESULT CDXVA2Decode::CheckHWCompatConditions(GUID decoderGuid)
{
	if (m_dwSurfaceWidth == 0 || m_dwSurfaceHeight == 0)
		return E_UNEXPECTED;

	int width_mbs = m_dwSurfaceWidth / 16;
	int height_mbs = m_dwSurfaceHeight / 16;
	int max_ref_frames_dpb41 = min(11, 32768 / (width_mbs * height_mbs));
	if (m_dwVendorId == VEND_ID_ATI) 
	{
		if (IsAMDUVD(m_dwDeviceId)) 
		{
			if (m_pAVCtx->codec_id == AV_CODEC_ID_H264 && m_pAVCtx->refs > max_ref_frames_dpb41) 
			{
				DxTraceMsg(( "-> Too many reference frames for AMD UVD/UVD+ H.264 decoder.\n"));
				return E_FAIL;
			}
			else if ((m_pAVCtx->codec_id == AV_CODEC_ID_VC1 || m_pAVCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO) && (m_dwSurfaceWidth > 1920 || m_dwSurfaceHeight > 1200)) 
			{
				DxTraceMsg(("-> VC-1 Resolutions above FullHD are not supported by the UVD/UVD+ decoder.\n"));
				return E_FAIL;
			}
			else if (m_pAVCtx->codec_id == AV_CODEC_ID_WMV3) 
			{
				DxTraceMsg(("-> AMD UVD/UVD+ is currently not compatible with WMV3.\n"));
				return E_FAIL;
			}
		}
	}
	else if (m_dwVendorId == VEND_ID_INTEL) 
	{
		if (decoderGuid == DXVADDI_Intel_ModeH264_E && m_pAVCtx->codec_id == AV_CODEC_ID_H264 && m_pAVCtx->refs > max_ref_frames_dpb41) 
		{
			DxTraceMsg(("-> Too many reference frames for Intel H.264 decoder implementation.\n"));
			return E_FAIL;
		}
	}
	return S_OK;
}

/**
 * Called from both native and non-native mode
 * Initialize all the common DXVA2 interfaces and device handles
 */
HRESULT CDXVA2Decode::SetD3DDeviceManager(IDirect3DDeviceManager9 *pDevManager)
{
	HRESULT hr = S_OK;
	assert(pDevManager != nullptr);

	m_pD3DDevMngr = pDevManager;

	RetrieveVendorId(pDevManager);

	// This should really be null here, but since we're overwriting it, make sure its actually released
	SafeRelease(m_pDXVADecoderService);

	hr = CreateDXVAVideoService(m_pD3DDevMngr, &m_pDXVADecoderService);
	if (FAILED(hr)) {
		DxTraceMsg("-> Creation of DXVA2 Decoder Service failed with hr: %X.\n", hr);
		goto done;
	}

	// If the decoder was initialized already, check if we can use this device
	if (m_pAVCtx) {
		DxTraceMsg("-> Checking hardware for format support...\n");

		GUID input = GUID_NULL;
		D3DFORMAT output;
		bool bHighBitdepth = (m_pAVCtx->codec_id == AV_CODEC_ID_HEVC && (m_pAVCtx->sw_pix_fmt == AV_PIX_FMT_YUV420P10 || m_pAVCtx->profile == FF_PROFILE_HEVC_MAIN_10));
		hr = FindVideoServiceConversion(m_pAVCtx->codec_id, bHighBitdepth, &input, &output);
		if (FAILED(hr)) {
			DxTraceMsg( "-> No decoder device available that can decode codec '%S' to a matching output.\n", avcodec_get_name(m_pAVCtx->codec_id));
			goto done;
		}

		m_eSurfaceFormat = output;

		if (FAILED(CheckHWCompatConditions(input))) {
			hr = E_FAIL;
			goto done;
		}

		DXVA2_VideoDesc desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.SampleWidth = m_dwSurfaceWidth;
		desc.SampleHeight = m_dwSurfaceHeight;
		desc.Format = output;

		DXVA2_ConfigPictureDecode config;
		hr = FindDecoderConfiguration(input, &desc, &config);
		if (FAILED(hr)) {
			DxTraceMsg("-> No decoder configuration available for codec '%S'.\n", avcodec_get_name(m_pAVCtx->codec_id));
			goto done;
		}

		LPDIRECT3DSURFACE9 pSurfaces[DXVA2_MAX_SURFACES] = { 0 };
		UINT numSurfaces = max(config.ConfigMinRenderTargetBuffCount, 1);
		hr = m_pDXVADecoderService->CreateSurface(m_dwSurfaceWidth, m_dwSurfaceHeight, numSurfaces, output, D3DPOOL_DEFAULT, 0, DXVA2_VideoDecoderRenderTarget, pSurfaces, nullptr);
		if (FAILED(hr)) {
			DxTraceMsg("-> Creation of surfaces failed with hr: %X.\n", hr);
			goto done;
		}

		IDirectXVideoDecoder *decoder = nullptr;
		hr = m_pDXVADecoderService->CreateVideoDecoder(input, &desc, &config, pSurfaces, numSurfaces, &decoder);

		// Release resources, decoder and surfaces
		SafeRelease(decoder);
		int i = DXVA2_MAX_SURFACES;
		while (i > 0) {
			SafeRelease(pSurfaces[--i]);
		}

		if (FAILED(hr)) {
			DxTraceMsg("-> Creation of decoder failed with hr: %X.\n", hr);
			goto done;
		}
	}

done:
	return hr;
}


DWORD CDXVA2Decode::GetAlignedDimension(DWORD dim)
{
	int align = DXVA2_SURFACE_BASE_ALIGN;

	// MPEG-2 needs higher alignment on Intel cards, and it doesn't seem to harm anything to do it for all cards.
	if (m_nCodecId == AV_CODEC_ID_MPEG2VIDEO)
		align <<= 1;
	else if (m_nCodecId == AV_CODEC_ID_HEVC)
		align = 32;

	return FFALIGN(dim, align);
	return dim;
}

#define H264_CHECK_PROFILE(profile) \
  (((profile) & ~FF_PROFILE_H264_CONSTRAINED) <= FF_PROFILE_H264_HIGH)

#define HEVC_CHECK_PROFILE(dec, profile) \
  (( (profile) <= FF_PROFILE_HEVC_MAIN) || ((profile) <= FF_PROFILE_HEVC_MAIN_10))

// 检测系统硬件是否支持当前码流格式 
// 必须在初始化解码器后调用
STDMETHODIMP CDXVA2Decode::CodecIsSupported(AVCodecID codec)
{
	HRESULT hr = S_OK;	

	// If we have a DXVA Decoder, check if its capable
	// If we don't have one yet, it may be handed to us later, and compat is checked at that point
	GUID input = GUID_NULL;
	D3DFORMAT output = D3DFMT_UNKNOWN;
	bool bHighBitdepth = (m_pAVCtx->codec_id == AV_CODEC_ID_HEVC && (m_pAVCtx->sw_pix_fmt == AV_PIX_FMT_YUV420P10 || m_pAVCtx->profile == FF_PROFILE_HEVC_MAIN_10));
	if (m_pDXVADecoderService)
	{
		hr = FindVideoServiceConversion(codec, bHighBitdepth, &input, &output);
		if (FAILED(hr)) {
			DxTraceMsg("-> No decoder device available that can decode codec '%S' to a matching output.\n", avcodec_get_name(codec));
			return E_FAIL;
		}
	}
	else
	{
		if (bHighBitdepth)
			output = (D3DFORMAT)FOURCC_P010;
		else
			output = (D3DFORMAT)FOURCC_NV12;
	}

	if (((codec == AV_CODEC_ID_H264 || codec == AV_CODEC_ID_MPEG2VIDEO) && m_pAVCtx->pix_fmt != AV_PIX_FMT_YUV420P && m_pAVCtx->pix_fmt != AV_PIX_FMT_YUVJ420P && m_pAVCtx->pix_fmt != AV_PIX_FMT_DXVA2_VLD && m_pAVCtx->pix_fmt != AV_PIX_FMT_NONE)
		|| (codec == AV_CODEC_ID_H264 && m_pAVCtx->profile != FF_PROFILE_UNKNOWN && !H264_CHECK_PROFILE(m_pAVCtx->profile))
		|| ((codec == AV_CODEC_ID_WMV3 || codec == AV_CODEC_ID_VC1) && m_pAVCtx->profile == FF_PROFILE_VC1_COMPLEX)
		|| (codec == AV_CODEC_ID_HEVC && (!HEVC_CHECK_PROFILE(this, m_pAVCtx->profile) || (m_pAVCtx->pix_fmt != AV_PIX_FMT_YUV420P && m_pAVCtx->pix_fmt != AV_PIX_FMT_YUVJ420P && m_pAVCtx->pix_fmt != AV_PIX_FMT_YUV420P10 && m_pAVCtx->pix_fmt != AV_PIX_FMT_DXVA2_VLD && m_pAVCtx->pix_fmt != AV_PIX_FMT_NONE)))) {
		DxTraceMsg(("-> Incompatible profile detected, falling back to software decoding.\n"));
		return E_FAIL;
	}

	m_dwSurfaceWidth = GetAlignedDimension(m_pAVCtx->coded_width);
	m_dwSurfaceHeight = GetAlignedDimension(m_pAVCtx->coded_height);
	m_eSurfaceFormat = output;

	if (FAILED(CheckHWCompatConditions(input))) {
		return E_FAIL;
	}

	return S_OK;
}



HRESULT CDXVA2Decode::FindDecoderConfiguration(const GUID &input, const DXVA2_VideoDesc *pDesc, DXVA2_ConfigPictureDecode *pConfig)
{
	CheckPointer(pConfig, E_INVALIDARG);
	CheckPointer(pDesc, E_INVALIDARG);
	HRESULT hr = S_OK;
	UINT cfg_count = 0;
	DXVA2_ConfigPictureDecode *cfg_list = nullptr;
	hr = m_pDXVADecoderService->GetDecoderConfigurations(input, pDesc, nullptr, &cfg_count, &cfg_list);
	if (FAILED(hr)) {
		DxTraceMsg("-> GetDecoderConfigurations failed with hr: %X.\n", hr);
		return E_FAIL;
	}

	DxTraceMsg("-> We got %d decoder configurations", cfg_count);
	int best_score = 0;
	DXVA2_ConfigPictureDecode best_cfg;
	for (unsigned i = 0; i < cfg_count; i++) {
		DXVA2_ConfigPictureDecode *cfg = &cfg_list[i];

		int score;
		if (cfg->ConfigBitstreamRaw == 1)
			score = 1;
		else if (m_pAVCtx->codec_id == AV_CODEC_ID_H264 && cfg->ConfigBitstreamRaw == 2)
			score = 2;
		else
			continue;
		if (IsEqualGUID(cfg->guidConfigBitstreamEncryption, DXVA2_NoEncrypt))
			score += 16;
		if (score > best_score) {
			best_score = score;
			best_cfg = *cfg;
		}
	}
	SAFE_CO_FREE(cfg_list);
	if (best_score <= 0) {
		DxTraceMsg(("-> No matching configuration available.\n"));
		return E_FAIL;
	}

	*pConfig = best_cfg;
	return S_OK;
}

HRESULT CDXVA2Decode::CreateDXVA2Decoder(int nSurfaces, IDirect3DSurface9 **ppSurfaces)
{
	DxTraceMsg(( "-> CDecDXVA2::CreateDXVA2Decoder.\n"));
	HRESULT hr = S_OK;
	LPDIRECT3DSURFACE9 pSurfaces[DXVA2_MAX_SURFACES];

	if (!m_pDXVADecoderService)
		return E_FAIL;

	DestroyDXVADecoder(false,true);

	GUID input = GUID_NULL;
	bool bHighBitdepth = (m_pAVCtx->codec_id == AV_CODEC_ID_HEVC && (m_pAVCtx->sw_pix_fmt == AV_PIX_FMT_YUV420P10 || m_pAVCtx->profile == FF_PROFILE_HEVC_MAIN_10));
	D3DFORMAT output;
	FindVideoServiceConversion(m_pAVCtx->codec_id, bHighBitdepth, &input, &output);

	if (!nSurfaces) 
	{
  		m_dwSurfaceWidth = GetAlignedDimension(m_pAVCtx->coded_width);
  		m_dwSurfaceHeight = GetAlignedDimension(m_pAVCtx->coded_height);
//		m_dwSurfaceWidth = m_pAVCtx->coded_width;
//		m_dwSurfaceHeight = m_pAVCtx->coded_height;

		m_eSurfaceFormat = output;
		m_DecoderPixelFormat = m_pAVCtx->sw_pix_fmt;

		m_NumSurfaces = GetBufferCount();
		hr = m_pDXVADecoderService->CreateSurface(m_dwSurfaceWidth, m_dwSurfaceHeight, m_NumSurfaces - 1, output, D3DPOOL_DEFAULT, 0, DXVA2_VideoDecoderRenderTarget, pSurfaces, nullptr);
		if (FAILED(hr)) 
		{
			DxTraceMsg( "%s Creation of surfaces failed with hr: %X.\n",__FUNCTION__, hr);
			m_NumSurfaces = 0;
			return E_FAIL;
		}
		ppSurfaces = pSurfaces;
	}
	else 
	{
		m_NumSurfaces = nSurfaces;
		for (int i = 0; i < m_NumSurfaces; i++) 
		{
			ppSurfaces[i]->AddRef();
		}
	}

	if (m_NumSurfaces <= 0) {
		DxTraceMsg( "%s No surfaces? No good!.\n",__FUNCTION__);
		return E_FAIL;
	}

	// get the device, for ColorFill() to init the surfaces in black
	IDirect3DDevice9 *pDev = nullptr;
	ppSurfaces[0]->GetDevice(&pDev);

	for (int i = 0; i < m_NumSurfaces; i++) 
	{
		m_pSurfaces[i].index = i;
		m_pSurfaces[i].d3d = ppSurfaces[i];
		m_pSurfaces[i].age = UINT64_MAX;
		m_pSurfaces[i].used = false;

		// fill the surface in black, to avoid the "green screen" in case the first frame fails to decode.
		if (pDev) pDev->ColorFill(ppSurfaces[i], NULL, D3DCOLOR_XYUV(0, 128, 128));
	}

	// and done with the device
	SafeRelease(pDev);

	DxTraceMsg("%s Successfully created %d surfaces (%dx%d).\n",__FUNCTION__, m_NumSurfaces, m_dwSurfaceWidth, m_dwSurfaceHeight);

	DXVA2_VideoDesc desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.SampleWidth = m_dwSurfaceWidth;
	desc.SampleHeight = m_dwSurfaceHeight;
	desc.Format = output;

	hr = FindDecoderConfiguration(input, &desc, &m_DXVAVideoDecoderConfig);
	if (FAILED(hr)) {
		DxTraceMsg("-> FindDecoderConfiguration failed with hr: %X.\n", hr);
		return hr;
	}

	IDirectXVideoDecoder *decoder = nullptr;
	hr = m_pDXVADecoderService->CreateVideoDecoder(input, &desc, &m_DXVAVideoDecoderConfig, ppSurfaces, m_NumSurfaces, &decoder);
	if (FAILED(hr)) {
		DxTraceMsg( "-> CreateVideoDecoder failed with hr: %X.\n", hr);
		return E_FAIL;
	}
	m_pDecoder = decoder;
	m_guidDecoderDevice = input;

	/* fill hwaccel_context */
	FillHWContext((dxva_context *)m_pAVCtx->hwaccel_context);

	memset(m_pRawSurface, 0, sizeof(m_pRawSurface));
	for (int i = 0; i < m_NumSurfaces; i++) {
		m_pRawSurface[i] = m_pSurfaces[i].d3d;
	}

	return S_OK;
}

HRESULT CDXVA2Decode::FillHWContext(dxva_context *ctx)
{
	ctx->cfg = &m_DXVAVideoDecoderConfig;
	ctx->decoder = m_pDecoder;
	ctx->surface = m_pRawSurface;
	ctx->surface_count = m_NumSurfaces;

	if (m_dwVendorId == VEND_ID_INTEL && m_guidDecoderDevice == DXVADDI_Intel_ModeH264_E)
		ctx->workaround = FF_DXVA2_WORKAROUND_INTEL_CLEARVIDEO;
	else if (m_dwVendorId == VEND_ID_ATI && IsAMDUVD(m_dwDeviceId))
		ctx->workaround = FF_DXVA2_WORKAROUND_SCALING_LIST_ZIGZAG;
	else
		ctx->workaround = 0;

	return S_OK;
}

enum AVPixelFormat CDXVA2Decode::get_dxva2_format(struct AVCodecContext *c, const enum AVPixelFormat * pix_fmts)
{
	CDXVA2Decode *pDec = (CDXVA2Decode *)c->opaque;
	const enum AVPixelFormat *p;
	for (p = pix_fmts; *p != -1; p++) 
	{
		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);

		if (!desc || !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
			break;

		if (*p == AV_PIX_FMT_DXVA2_VLD) 
		{
			HRESULT hr = pDec->ReInitDXVA2Decoder(c);
			if (FAILED(hr)) 
				continue;
			else 
				break;
		}
	}
	return *p;
}

typedef struct SurfaceWrapper {
	LPDIRECT3DSURFACE9 surface;
	IMediaSample *sample;
	CDXVA2Decode *pDec;
	IDirectXVideoDecoder *pDXDecoder;
} SurfaceWrapper;

void CDXVA2Decode::free_dxva2_buffer(void *opaque, uint8_t *data)
{
	SurfaceWrapper *sw = (SurfaceWrapper *)opaque;	
	CDXVA2Decode *pDec = sw->pDec;
	
	LPDIRECT3DSURFACE9 pSurface = sw->surface;
	for (int i = 0; i < pDec->m_NumSurfaces; i++) 
	{
		if (pDec->m_pSurfaces[i].d3d == pSurface) 
		{
			pDec->m_pSurfaces[i].used = false;
			break;
		}
	}
	SafeRelease(pSurface);
	SafeRelease(sw->pDXDecoder);
	SafeRelease(sw->sample);
	delete sw;
}

HRESULT CDXVA2Decode::ReInitDXVA2Decoder(AVCodecContext *c)
{
	HRESULT hr = S_OK;

	// Don't allow decoder creation during first init
	if (m_bInInit)
		return S_FALSE;

	if (!m_pDecoder || GetAlignedDimension(c->coded_width) != m_dwSurfaceWidth || GetAlignedDimension(c->coded_height) != m_dwSurfaceHeight || m_DecoderPixelFormat != c->sw_pix_fmt) 
	{
		DxTraceMsg(("No DXVA2 Decoder or image dimensions changed -> Re-Allocating resources.\n"));				
		hr = CreateDXVA2Decoder();		
	}

	return hr;
}

int CDXVA2Decode::get_dxva2_buffer(struct AVCodecContext *c, AVFrame *pic, int flags)
{
	CDXVA2Decode *pDec = (CDXVA2Decode *)c->opaque;
	HRESULT hr = S_OK;

	if (pic->format != AV_PIX_FMT_DXVA2_VLD || 
		(c->codec_id == AV_CODEC_ID_H264 && !H264_CHECK_PROFILE(c->profile)) ||
		(c->codec_id == AV_CODEC_ID_HEVC && !HEVC_CHECK_PROFILE(pDec, c->profile))) 
	{
		DxTraceMsg(( "DXVA2 buffer request, but not dxva2 pixfmt or unsupported profile.\n"));
		
		return -1;
	}

	hr = pDec->ReInitDXVA2Decoder(c);
	if (FAILED(hr)) {
		
		return -1;
	}

	if (FAILED(pDec->m_pD3DDevMngr->TestDevice(pDec->m_hDevice))) 
	{
		DxTraceMsg(("Device Lost.\n.\n"));
	}

	int i;
	{
		int old, old_unused;
		for (i = 0, old = 0, old_unused = -1; i < pDec->m_NumSurfaces; i++) 
		{
			d3d_surface_t *surface = &pDec->m_pSurfaces[i];
			if (!surface->used && (old_unused == -1 || surface->age < pDec->m_pSurfaces[old_unused].age))
				old_unused = i;
			if (surface->age < pDec->m_pSurfaces[old].age)
				old = i;
		}
		if (old_unused == -1) {
			DxTraceMsg(("No free surface, using oldest.\n"));
			i = old;
		}
		else {
			i = old_unused;
		}
	}

	LPDIRECT3DSURFACE9 pSurface = pDec->m_pSurfaces[i].d3d;
	if (!pSurface) 
	{
		DxTraceMsg(("There is a sample, but no D3D Surace? WTF?.\n"));
		return -1;
	}

	pDec->m_pSurfaces[i].age = pDec->m_CurrentSurfaceAge++;
	pDec->m_pSurfaces[i].used = true;

	memset(pic->data, 0, sizeof(pic->data));
	memset(pic->linesize, 0, sizeof(pic->linesize));
	memset(pic->buf, 0, sizeof(pic->buf));

	pic->data[0] = pic->data[3] = (uint8_t *)pSurface;	
	SurfaceWrapper *surfaceWrapper = new SurfaceWrapper();
	surfaceWrapper->pDec			 = pDec;
	surfaceWrapper->surface		 = pSurface;
	surfaceWrapper->surface->AddRef();
	surfaceWrapper->pDXDecoder	 = pDec->m_pDecoder;
	surfaceWrapper->pDXDecoder->AddRef();
	pic->buf[0]						 = av_buffer_create(nullptr, 0, free_dxva2_buffer, surfaceWrapper, 0);

	return 0;
}

HRESULT CDXVA2Decode::AdditionaDecoderInit()
{
	/* Create ffmpeg dxva_context, but only fill it if we have a decoder already. */
	dxva_context *ctx = (dxva_context *)av_mallocz(sizeof(dxva_context));

	if (m_pDecoder) {
		FillHWContext(ctx);
	}

	m_pAVCtx->flags |= CODEC_CAP_DR1;
	m_pAVCtx->flags |= CODEC_CAP_HWACCEL;
	m_pAVCtx->thread_count = 1;
	m_pAVCtx->hwaccel_context = ctx;
	m_pAVCtx->get_format = get_dxva2_format;
	m_pAVCtx->get_buffer2 = get_dxva2_buffer;
	m_pAVCtx->opaque = this;
	m_pAVCtx->slice_flags |= SLICE_FLAG_ALLOW_FIELD;

	return S_OK;
}
