// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Viewport.cpp: D3D viewport RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"
#include "RenderCore.h"

#ifndef D3D11_WITH_DWMAPI
#if WINVER > 0x502		// Windows XP doesn't support DWM
	#define D3D11_WITH_DWMAPI	1
#else
	#define D3D11_WITH_DWMAPI	0
#endif
#endif

#if D3D11_WITH_DWMAPI
	#include "AllowWindowsPlatformTypes.h"
		#include <dwmapi.h>
#endif	//D3D11_WITH_DWMAPI

/**
 * RHI console variables used by viewports.
 */
namespace RHIConsoleVariables
{
	int32 bSyncWithDWM = 0;
	static FAutoConsoleVariableRef CVarSyncWithDWM(
		TEXT("RHI.SyncWithDWM"),
		bSyncWithDWM,
		TEXT("If true, synchronize with the desktop window manager for vblank."),
		ECVF_RenderThreadSafe
		);

	float RefreshPercentageBeforePresent = 1.0f;
	static FAutoConsoleVariableRef CVarRefreshPercentageBeforePresent(
		TEXT("RHI.RefreshPercentageBeforePresent"),
		RefreshPercentageBeforePresent,
		TEXT("The percentage of the refresh period to wait before presenting."),
		ECVF_RenderThreadSafe
		);

	int32 bForceThirtyHz = 1;
	static FAutoConsoleVariableRef CVarForceThirtyHz(
		TEXT("RHI.ForceThirtyHz"),
		bForceThirtyHz,
		TEXT("If true, the display will never update more often than 30Hz."),
		ECVF_RenderThreadSafe
		);

	int32 SyncInterval = 1;
	static FAutoConsoleVariableRef CVarSyncInterval(
		TEXT("RHI.SyncInterval"),
		SyncInterval,
		TEXT("When synchronizing with D3D, specifies the interval at which to refresh."),
		ECVF_RenderThreadSafe
		);

	float SyncRefreshThreshold = 1.05f;
	static FAutoConsoleVariableRef CVarSyncRefreshThreshold(
		TEXT("RHI.SyncRefreshThreshold"),
		SyncRefreshThreshold,
		TEXT("Threshold for time above which vsync will be disabled as a percentage of the refresh rate."),
		ECVF_RenderThreadSafe
		);

	int32 MaxSyncCounter = 8;
	static FAutoConsoleVariableRef CVarMaxSyncCounter(
		TEXT("RHI.MaxSyncCounter"),
		MaxSyncCounter,
		TEXT("Maximum sync counter to smooth out vsync transitions."),
		ECVF_RenderThreadSafe
		);

	int32 SyncThreshold = 7;
	static FAutoConsoleVariableRef CVarSyncThreshold(
		TEXT("RHI.SyncThreshold"),
		SyncThreshold,
		TEXT("Number of consecutive 'fast' frames before vsync is enabled."),
		ECVF_RenderThreadSafe
		);

	int32 MaximumFrameLatency = 3;
	static FAutoConsoleVariableRef CVarMaximumFrameLatency(
		TEXT("RHI.MaximumFrameLatency"),
		MaximumFrameLatency,
		TEXT("Number of frames that can be queued for render."),
		ECVF_RenderThreadSafe
		);

};

extern void D3D11TextureAllocated2D( FD3D11Texture2D& Texture );

/**
 * Creates a FD3D11Surface to represent a swap chain's back buffer.
 */
FD3D11Texture2D* GetSwapChainSurface(FD3D11DynamicRHI* D3DRHI,IDXGISwapChain* SwapChain)
{
	// Grab the back buffer
	TRefCountPtr<ID3D11Texture2D> BackBufferResource;
	VERIFYD3D11RESULT_EX(SwapChain->GetBuffer(0,IID_ID3D11Texture2D,(void**)BackBufferResource.GetInitReference()), D3DRHI->GetDevice());

	// create the render target view
	TRefCountPtr<ID3D11RenderTargetView> BackBufferRenderTargetView;
	D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
	RTVDesc.Format = DXGI_FORMAT_UNKNOWN;
	RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	RTVDesc.Texture2D.MipSlice = 0;
	VERIFYD3D11RESULT(D3DRHI->GetDevice()->CreateRenderTargetView(BackBufferResource,&RTVDesc,BackBufferRenderTargetView.GetInitReference()));

	D3D11_TEXTURE2D_DESC TextureDesc;
	BackBufferResource->GetDesc(&TextureDesc);

	TArray<TRefCountPtr<ID3D11RenderTargetView> > RenderTargetViews;
	RenderTargetViews.Add(BackBufferRenderTargetView);
	
	// create a shader resource view to allow using the backbuffer as a texture
	TRefCountPtr<ID3D11ShaderResourceView> BackBufferShaderResourceView;
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;
	VERIFYD3D11RESULT(D3DRHI->GetDevice()->CreateShaderResourceView(BackBufferResource,&SRVDesc,BackBufferShaderResourceView.GetInitReference()));

	FD3D11Texture2D* NewTexture = new FD3D11Texture2D(
		D3DRHI,
		BackBufferResource,
		BackBufferShaderResourceView,
		false,
		1,
		RenderTargetViews,
		NULL,
		TextureDesc.Width,
		TextureDesc.Height,
		1,
		1,
		1,
		PF_A2B10G10R10,
		false,
		false,
		false
		);

	D3D11TextureAllocated2D(*NewTexture);

	NewTexture->DoNoDeferDelete();

	return NewTexture;
}

FD3D11Viewport::~FD3D11Viewport()
{
	check(IsInRenderingThread());

	// If the swap chain was in fullscreen mode, switch back to windowed before releasing the swap chain.
	// DXGI throws an error otherwise.
	VERIFYD3D11RESULT(SwapChain->SetFullscreenState(false,NULL));

	FrameSyncEvent.ReleaseResource();

	D3DRHI->Viewports.Remove(this);
}

DXGI_MODE_DESC FD3D11Viewport::SetupDXGI_MODE_DESC() const
{
	DXGI_MODE_DESC Ret;

	Ret.Width = SizeX;
	Ret.Height = SizeY;
	Ret.RefreshRate.Numerator = 0;	// illamas: use 0 to avoid a potential mismatch with hw
	Ret.RefreshRate.Denominator = 0;	// illamas: ditto
	Ret.Format = (DXGI_FORMAT)FD3D11Viewport::GetBackBufferFormat();
	Ret.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	Ret.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	return Ret;
}

void FD3D11Viewport::Resize(uint32 InSizeX,uint32 InSizeY,bool bInIsFullscreen)
{
	// Unbind any dangling references to resources
	D3DRHI->ClearState();

	if (IsValidRef(CustomPresent))
	{
		CustomPresent->OnBackBufferResize();
	}

	// Release our backbuffer reference, as required by DXGI before calling ResizeBuffers.
	if (IsValidRef(BackBuffer))
	{
		check(BackBuffer->GetRefCount() == 1);

		checkComRefCount(BackBuffer->GetResource(),1);
		checkComRefCount(BackBuffer->GetRenderTargetView(0, -1),1);
		checkComRefCount(BackBuffer->GetShaderResourceView(),1);
	}
	BackBuffer.SafeRelease();

	if(SizeX != InSizeX || SizeY != InSizeY)
	{
		SizeX = InSizeX;
		SizeY = InSizeY;

		check(SizeX > 0);
		check(SizeY > 0);

		// Resize the swap chain.
		VERIFYD3D11RESULT_EX(SwapChain->ResizeBuffers(1,SizeX,SizeY,(DXGI_FORMAT)FD3D11Viewport::GetBackBufferFormat(),DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH), D3DRHI->GetDevice());

		if(bInIsFullscreen)
		{
			DXGI_MODE_DESC BufferDesc = SetupDXGI_MODE_DESC();

			if (FAILED(SwapChain->ResizeTarget(&BufferDesc)))
			{
				ConditionalResetSwapChain(true);
			}
		}
	}

	if(bIsFullscreen != bInIsFullscreen)
	{
		bIsFullscreen = bInIsFullscreen;
		bIsValid = false;

		// Use ConditionalResetSwapChain to call SetFullscreenState, to handle the failure case.
		// Ignore the viewport's focus state; since Resize is called as the result of a user action we assume authority without waiting for Focus.
		ConditionalResetSwapChain(true);
	}

	// Create a RHI surface to represent the viewport's back buffer.
	BackBuffer = GetSwapChainSurface(D3DRHI,SwapChain);
}

/** Returns true if desktop composition is enabled. */
static bool IsCompositionEnabled()
{
	BOOL bDwmEnabled = false;
#if D3D11_WITH_DWMAPI
	DwmIsCompositionEnabled(&bDwmEnabled);
#endif	//D3D11_WITH_DWMAPI
	return !!bDwmEnabled;
}

/** Presents the swap chain checking the return result. */
bool FD3D11Viewport::PresentChecked(int32 SyncInterval)
{
	HRESULT Result = S_OK;
	bool bNeedNativePresent = true;
	if (IsValidRef(CustomPresent))
	{
		bNeedNativePresent = CustomPresent->Present(SyncInterval);
	}
	if (bNeedNativePresent)
	{
		// Present the back buffer to the viewport window.
		Result = SwapChain->Present(SyncInterval, 0);
	}

	// Detect a lost device.
	if(Result == DXGI_ERROR_DEVICE_REMOVED || Result == DXGI_ERROR_DEVICE_RESET || Result == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
	{
		// This variable is checked periodically by the main thread.
		D3DRHI->bDeviceRemoved = true;
	}
	else
	{
		VERIFYD3D11RESULT(Result);
	}
	return bNeedNativePresent;
}

/** Blocks the CPU to synchronize with vblank by communicating with DWM. */
void FD3D11Viewport::PresentWithVsyncDWM()
{
#if D3D11_WITH_DWMAPI
	LARGE_INTEGER Cycles;
	DWM_TIMING_INFO TimingInfo;

	// Find out how long since we last flipped and query DWM for timing information.
	QueryPerformanceCounter(&Cycles);
	FMemory::MemZero(TimingInfo);
	TimingInfo.cbSize = sizeof(DWM_TIMING_INFO);
	DwmGetCompositionTimingInfo(WindowHandle, &TimingInfo);

	uint64 QpcAtFlip = Cycles.QuadPart;
	uint64 CyclesSinceLastFlip = Cycles.QuadPart - LastFlipTime;
	float CPUTime = FPlatformTime::ToMilliseconds(CyclesSinceLastFlip);
	float GPUTime = FPlatformTime::ToMilliseconds(TimingInfo.qpcFrameComplete - LastCompleteTime);
	float DisplayRefreshPeriod = FPlatformTime::ToMilliseconds(TimingInfo.qpcRefreshPeriod);

	// Find the smallest multiple of the refresh rate that is >= 33ms, our target frame rate.
	float RefreshPeriod = DisplayRefreshPeriod;
	if (RHIConsoleVariables::bForceThirtyHz && RefreshPeriod > 1.0f)
	{
		while (RefreshPeriod - (1000.0f / 30.0f) < -1.0f)
		{
			RefreshPeriod *= 2.0f;
		}
	}

	// If the last frame hasn't completed yet, we don't know how long the GPU took.
	bool bValidGPUTime = (TimingInfo.cFrameComplete > LastFrameComplete);
	if (bValidGPUTime)
	{
		GPUTime /= (float)(TimingInfo.cFrameComplete - LastFrameComplete);
	}

	// Update the sync counter depending on how much time it took to complete the previous frame.
	float FrameTime = FMath::Max<float>(CPUTime, GPUTime);
	if (FrameTime >= RHIConsoleVariables::SyncRefreshThreshold * RefreshPeriod)
	{
		SyncCounter--;
	}
	else if (bValidGPUTime)
	{
		SyncCounter++;
	}
	SyncCounter = FMath::Clamp<int32>(SyncCounter, 0, RHIConsoleVariables::MaxSyncCounter);

	// If frames are being completed quickly enough, block for vsync.
	bool bSync = (SyncCounter >= RHIConsoleVariables::SyncThreshold);
	if (bSync)
	{
		// This flushes the previous present call and blocks until it is made available to DWM.
		D3DRHI->GetDeviceContext()->Flush();
		DwmFlush();

		// We sleep a percentage of the remaining time. The trick is to get the
		// present call in after the vblank we just synced for but with time to
		// spare for the next vblank.
		float MinFrameTime = RefreshPeriod * RHIConsoleVariables::RefreshPercentageBeforePresent;
		float TimeToSleep;
		do 
		{
			QueryPerformanceCounter(&Cycles);
			float TimeSinceFlip = FPlatformTime::ToMilliseconds(Cycles.QuadPart - LastFlipTime);
			TimeToSleep = (MinFrameTime - TimeSinceFlip);
			if (TimeToSleep > 0.0f)
			{
				FPlatformProcess::Sleep(TimeToSleep * 0.001f);
			}
		}
		while (TimeToSleep > 0.0f);
	}

	// Present.
	PresentChecked(/*SyncInterval=*/ 0);

	// If we are forcing <= 30Hz, block the CPU an additional amount of time if needed.
	// This second block is only needed when RefreshPercentageBeforePresent < 1.0.
	if (bSync)
	{
		LARGE_INTEGER LocalCycles;
		float TimeToSleep;
		bool bSaveCycles = false;
		do 
		{
			QueryPerformanceCounter(&LocalCycles);
			float TimeSinceFlip = FPlatformTime::ToMilliseconds(LocalCycles.QuadPart - LastFlipTime);
			TimeToSleep = (RefreshPeriod - TimeSinceFlip);
			if (TimeToSleep > 0.0f)
			{
				bSaveCycles = true;
				FPlatformProcess::Sleep(TimeToSleep * 0.001f);
			}
		}
		while (TimeToSleep > 0.0f);

		if (bSaveCycles)
		{
			Cycles = LocalCycles;
		}
	}

	// If we are dropping vsync reset the counter. This provides a debounce time
	// before which we try to vsync again.
	if (!bSync && bSyncedLastFrame)
	{
		SyncCounter = 0;
	}

	if (bSync != bSyncedLastFrame || UE_LOG_ACTIVE(LogRHI,VeryVerbose))
	{
		UE_LOG(LogRHI,Verbose,TEXT("BlockForVsync[%d]: CPUTime:%.2fms GPUTime[%d]:%.2fms Blocked:%.2fms Pending/Complete:%d/%d"),
			bSync,
			CPUTime,
			bValidGPUTime,
			GPUTime,
			FPlatformTime::ToMilliseconds(Cycles.QuadPart - QpcAtFlip),
			TimingInfo.cFramePending,
			TimingInfo.cFrameComplete);
	}

	// Remember if we synced, when the frame completed, etc.
	bSyncedLastFrame = bSync;
	LastFlipTime = Cycles.QuadPart;
	LastFrameComplete = TimingInfo.cFrameComplete;
	LastCompleteTime = TimingInfo.qpcFrameComplete;
#endif	//D3D11_WITH_DWMAPI
}

bool FD3D11Viewport::Present(bool bLockToVsync)
{
	bool bNativelyPresented = true;
#if	D3D11_WITH_DWMAPI
	// We can't call Present if !bIsValid, as it waits a window message to be processed, but the main thread may not be pumping the message handler.
	if(bIsValid)
	{
		// Check if the viewport's swap chain has been invalidated by DXGI.
		BOOL bSwapChainFullscreenState;
		TRefCountPtr<IDXGIOutput> SwapChainOutput;
		VERIFYD3D11RESULT(SwapChain->GetFullscreenState(&bSwapChainFullscreenState,SwapChainOutput.GetInitReference()));
		// Can't compare BOOL with bool...
		if ( (!!bSwapChainFullscreenState)  != bIsFullscreen )
		{
			bIsValid = false;
			
			// Minimize the window.
			// use SW_FORCEMINIMIZE if the messaging thread is likely to be blocked for a sizeable period.
			// SW_FORCEMINIMIZE also prevents the minimize animation from playing.
			::ShowWindow(WindowHandle,SW_MINIMIZE);
		}
	}

	if (MaximumFrameLatency != RHIConsoleVariables::MaximumFrameLatency)
	{
		MaximumFrameLatency = RHIConsoleVariables::MaximumFrameLatency;	
		TRefCountPtr<IDXGIDevice1> DXGIDevice;
		VERIFYD3D11RESULT(D3DRHI->GetDevice()->QueryInterface(IID_IDXGIDevice, (void**)DXGIDevice.GetInitReference()));
		DXGIDevice->SetMaximumFrameLatency(MaximumFrameLatency);
	}

	// When desktop composition is enabled, locking to vsync via the Present
	// call is unreliable. Instead, communicate with the desktop window manager
	// directly to enable vsync.
	const bool bSyncWithDWM = bLockToVsync && !bIsFullscreen && RHIConsoleVariables::bSyncWithDWM && IsCompositionEnabled();
	if (bSyncWithDWM)
	{
		PresentWithVsyncDWM();
	}
	else
#endif	//D3D11_WITH_DWMAPI
	{
		// Present the back buffer to the viewport window.
		bNativelyPresented = PresentChecked(bLockToVsync ? RHIConsoleVariables::SyncInterval : 0);
	}
	return bNativelyPresented;
}

/*=============================================================================
 *	The following RHI functions must be called from the main thread.
 *=============================================================================*/
FViewportRHIRef FD3D11DynamicRHI::RHICreateViewport(void* WindowHandle,uint32 SizeX,uint32 SizeY,bool bIsFullscreen)
{
	check( IsInGameThread() );
	return new FD3D11Viewport(this,(HWND)WindowHandle,SizeX,SizeY,bIsFullscreen);
}

void FD3D11DynamicRHI::RHIResizeViewport(FViewportRHIParamRef ViewportRHI,uint32 SizeX,uint32 SizeY,bool bIsFullscreen)
{
	DYNAMIC_CAST_D3D11RESOURCE(Viewport,Viewport);

	check( IsInGameThread() );
	Viewport->Resize(SizeX,SizeY,bIsFullscreen);
}

void FD3D11DynamicRHI::RHITick( float DeltaTime )
{
	check( IsInGameThread() );

	// Check to see if the device has been removed.
	if ( bDeviceRemoved )
	{
		InitD3DDevice();
	}

	// Check if any swap chains have been invalidated.
	for(int32 ViewportIndex = 0;ViewportIndex < Viewports.Num();ViewportIndex++)
	{
		Viewports[ViewportIndex]->ConditionalResetSwapChain(false);
	}
}

/*=============================================================================
 *	Viewport functions.
 *=============================================================================*/

void FD3D11DynamicRHI::RHIBeginDrawingViewport(FViewportRHIParamRef ViewportRHI, FTextureRHIParamRef RenderTarget)
{
	DYNAMIC_CAST_D3D11RESOURCE(Viewport,Viewport);

	SCOPE_CYCLE_COUNTER(STAT_D3D11PresentTime);

	check(!DrawingViewport);
	DrawingViewport = Viewport;

	// Set the render target and viewport.
	if( RenderTarget == NULL )
	{
		RenderTarget = Viewport->GetBackBuffer();
	}
	FRHIRenderTargetView View(RenderTarget);
	RHISetRenderTargets(1,&View,FTextureRHIRef(),0,NULL);

	// Set an initially disabled scissor rect.
	RHISetScissorRect(false,0,0,0,0);
}

void FD3D11DynamicRHI::RHIEndDrawingViewport(FViewportRHIParamRef ViewportRHI,bool bPresent,bool bLockToVsync)
{
	DYNAMIC_CAST_D3D11RESOURCE(Viewport,Viewport);

	SCOPE_CYCLE_COUNTER(STAT_D3D11PresentTime);

	check(DrawingViewport.GetReference() == Viewport);
	DrawingViewport = NULL;

	// Clear references the device might have to resources.
	CurrentDepthTexture = NULL;
	CurrentDepthStencilTarget = NULL;
	CurrentDSVAccessType = DSAT_Writable;
	CurrentRenderTargets[0] = NULL;
	for(uint32 RenderTargetIndex = 1;RenderTargetIndex < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++RenderTargetIndex)
	{
		CurrentRenderTargets[RenderTargetIndex] = NULL;
	}

	ClearAllShaderResources();

	CommitRenderTargetsAndUAVs();

	StateCache.SetVertexShader(nullptr);

	for(uint32 StreamIndex = 0;StreamIndex < 16;StreamIndex++)
	{
		StateCache.SetStreamSource(nullptr, StreamIndex, 0, 0);
	}

	StateCache.SetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
	StateCache.SetPixelShader(nullptr);
	StateCache.SetHullShader(nullptr);
	StateCache.SetDomainShader(nullptr);
	StateCache.SetGeometryShader(nullptr);
	// Compute Shader is set to NULL after each Dispatch call, so no need to clear it here

	bool bNativelyPresented = Viewport->Present(bLockToVsync);

	// Don't wait on the GPU when using SLI, let the driver determine how many frames behind the GPU should be allowed to get
	if (GNumActiveGPUsForRendering == 1)
	{
		if (bNativelyPresented)
		{ 
			static const auto CFinishFrameVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.FinishCurrentFrame"));
			if (!CFinishFrameVar->GetValueOnRenderThread())
			{
				// Wait for the GPU to finish rendering the previous frame before finishing this frame.
				Viewport->WaitForFrameEventCompletion();
				Viewport->IssueFrameEvent();
			}
			else
			{
				// Finish current frame immediately to reduce latency
				Viewport->IssueFrameEvent();
				Viewport->WaitForFrameEventCompletion();
			}
		}

		// If the input latency timer has been triggered, block until the GPU is completely
		// finished displaying this frame and calculate the delta time.
		if ( GInputLatencyTimer.RenderThreadTrigger )
		{
			Viewport->WaitForFrameEventCompletion();
			uint32 EndTime = FPlatformTime::Cycles();
			GInputLatencyTimer.DeltaTime = EndTime - GInputLatencyTimer.StartTime;
			GInputLatencyTimer.RenderThreadTrigger = false;
		}
	}
}

/**
 * Determine if currently drawing the viewport
 *
 * @return true if currently within a BeginDrawingViewport/EndDrawingViewport block
 */
bool FD3D11DynamicRHI::RHIIsDrawingViewport()
{
	return DrawingViewport != NULL;
}

#if PLATFORM_SUPPORTS_RHI_THREAD
void FD3D11DynamicRHI::RHIAdvanceFrameForGetViewportBackBuffer()
{
}
#endif

FTexture2DRHIRef FD3D11DynamicRHI::RHIGetViewportBackBuffer(FViewportRHIParamRef ViewportRHI)
{
	DYNAMIC_CAST_D3D11RESOURCE(Viewport,Viewport);

	return Viewport->GetBackBuffer();
}

#if D3D11_WITH_DWMAPI
	#include "HideWindowsPlatformTypes.h"
#endif	//D3D11_WITH_DWMAPI
