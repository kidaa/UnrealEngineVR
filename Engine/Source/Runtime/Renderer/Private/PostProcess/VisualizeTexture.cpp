// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VisualizeTexture.cpp: Post processing visualize texture.
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "SceneFilterRendering.h"
#include "VisualizeTexture.h"
#include "PostProcessing.h"
#include "PostProcessWeightedSampleSum.h"
#include "SceneUtils.h"


/** A pixel shader which filters a texture. */
// @param TextureType 0:Cube, 1:1D(not yet supported), 2:2D, 3:3D, 4:Cube[], 5:2D MSAA
template<uint32 TextureType>
class VisualizeTexturePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(VisualizeTexturePS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("TEXTURE_TYPE"), TextureType);
	}

	/** Default constructor. */
	VisualizeTexturePS() {}

	/** Initialization constructor. */
	VisualizeTexturePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
	FGlobalShader(Initializer)
	{
		VisualizeTexture2D.Bind(Initializer.ParameterMap,TEXT("VisualizeTexture2D"));
		VisualizeDepthStencilTexture.Bind(Initializer.ParameterMap, TEXT("VisualizeDepthStencilTexture"));

		VisualizeTexture2DSampler.Bind(Initializer.ParameterMap,TEXT("VisualizeTexture2DSampler"));
		VisualizeTexture2DMS.Bind(Initializer.ParameterMap,TEXT("VisualizeTexture2DMS"));
		VisualizeTexture3D.Bind(Initializer.ParameterMap,TEXT("VisualizeTexture3D"));
		VisualizeTexture3DSampler.Bind(Initializer.ParameterMap,TEXT("VisualizeTexture3DSampler"));
		VisualizeTextureCube.Bind(Initializer.ParameterMap,TEXT("VisualizeTextureCube"));
		VisualizeTextureCubeSampler.Bind(Initializer.ParameterMap,TEXT("VisualizeTextureCubeSampler"));
		VisualizeTextureCubeArray.Bind(Initializer.ParameterMap,TEXT("VisualizeTextureCubeArray"));
		VisualizeTextureCubeArraySampler.Bind(Initializer.ParameterMap,TEXT("VisualizeTextureCubeArraySampler"));
		VisualizeParam.Bind(Initializer.ParameterMap,TEXT("VisualizeParam"));
		TextureExtent.Bind(Initializer.ParameterMap,TEXT("TextureExtent"));
	}

	/** Serializer */
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << VisualizeTexture2D;
		Ar << VisualizeDepthStencilTexture;
		Ar << VisualizeTexture2DSampler;
		Ar << VisualizeTexture2DMS;
		Ar << VisualizeTexture3D;
		Ar << VisualizeTexture3DSampler;
		Ar << VisualizeTextureCube;
		Ar << VisualizeTextureCubeSampler;
		Ar << VisualizeTextureCubeArray;
		Ar << VisualizeTextureCubeArraySampler;
		Ar << VisualizeParam;
		Ar << TextureExtent;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(FRHICommandList& RHICmdList, const FVisualizeTextureData& Data)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		
		{
			// alternates between 0 and 1 with a short pause
			const float FracTimeScale = 2.0f;
			float FracTime = FApp::GetCurrentTime() * FracTimeScale - floor(FApp::GetCurrentTime() * FracTimeScale);
			float BlinkState = (FracTime > 0.5f) ? 1.0f : 0.0f;

			FVector4 VisualizeParamValue[3];

			float Add = 0.0f;
			float FracScale = 1.0f;

			// w * almost_1 to avoid frac(1) => 0
			VisualizeParamValue[0] = FVector4(Data.RGBMul, Data.AMul, Add, FracScale * 0.9999f);
			VisualizeParamValue[1] = FVector4(BlinkState, Data.bSaturateInsteadOfFrac ? 1.0f : 0.0f, Data.ArrayIndex, Data.CustomMip);
			VisualizeParamValue[2] = FVector4(Data.InputValueMapping, 0, 0,0);

			SetShaderValueArray(RHICmdList, ShaderRHI, VisualizeParam, VisualizeParamValue, 3);
		}

		{
			FVector4 TextureExtentValue(Data.Desc.Extent.X, Data.Desc.Extent.Y, Data.Desc.Depth, 0);

			SetShaderValue(RHICmdList, ShaderRHI, TextureExtent, TextureExtentValue);
		}
		
		
		SetSRVParameter(RHICmdList, ShaderRHI, VisualizeDepthStencilTexture, Data.StencilSRV );		
		SetTextureParameter(RHICmdList, ShaderRHI, VisualizeTexture2D, VisualizeTexture2DSampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), (FTextureRHIRef&)Data.RenderTargetItem.ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, VisualizeTexture2DMS, (FTextureRHIRef&)Data.RenderTargetItem.TargetableTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, VisualizeTexture3D, VisualizeTexture3DSampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), (FTextureRHIRef&)Data.RenderTargetItem.ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, VisualizeTextureCube, VisualizeTextureCubeSampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), (FTextureRHIRef&)Data.RenderTargetItem.ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, VisualizeTextureCubeArray, VisualizeTextureCubeArraySampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), (FTextureRHIRef&)Data.RenderTargetItem.ShaderResourceTexture);
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("VisualizeTexture");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("VisualizeTexturePS");
	}

protected:
	FShaderResourceParameter VisualizeTexture2D;
	FShaderResourceParameter VisualizeDepthStencilTexture;
	FShaderResourceParameter VisualizeTexture2DSampler;
	FShaderResourceParameter VisualizeTexture2DMS;
	FShaderResourceParameter VisualizeTexture3D;
	FShaderResourceParameter VisualizeTexture3DSampler;
	FShaderResourceParameter VisualizeTextureCube;
	FShaderResourceParameter VisualizeTextureCubeSampler;
	FShaderResourceParameter VisualizeTextureCubeArray;
	FShaderResourceParameter VisualizeTextureCubeArraySampler;
	FShaderParameter VisualizeParam;
	FShaderParameter TextureExtent;
};

// #define avoids a lot of code duplication
#define VARIATION1(A) typedef VisualizeTexturePS<A> VisualizeTexturePS##A; \
	IMPLEMENT_SHADER_TYPE2(VisualizeTexturePS##A, SF_Pixel);

VARIATION1(0)			VARIATION1(2)			VARIATION1(3)			VARIATION1(4)			VARIATION1(5)
#undef VARIATION1


	/** Encapsulates a simple copy pixel shader. */
class FVisualizeTexturePresentPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FVisualizeTexturePresentPS, Global);

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	/** Default constructor. */
	FVisualizeTexturePresentPS() {}

public:
	FShaderResourceParameter VisualizeTexture2D;
	FShaderResourceParameter VisualizeTexture2DSampler;

	/** Initialization constructor. */
	FVisualizeTexturePresentPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		VisualizeTexture2D.Bind(Initializer.ParameterMap,TEXT("VisualizeTexture2D"));
		VisualizeTexture2DSampler.Bind(Initializer.ParameterMap,TEXT("VisualizeTexture2DSampler"));
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << VisualizeTexture2D << VisualizeTexture2DSampler;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const IPooledRenderTarget& Src)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters(RHICmdList, ShaderRHI, View);
		SetTextureParameter(RHICmdList, ShaderRHI, VisualizeTexture2D, VisualizeTexture2DSampler, 
			TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), (FTextureRHIRef&)Src.GetRenderTargetItem().ShaderResourceTexture);
	}
};

IMPLEMENT_SHADER_TYPE(,FVisualizeTexturePresentPS,TEXT("VisualizeTexture"),TEXT("PresentPS"),SF_Pixel);


template<uint32 TextureType> void VisualizeTextureForTextureType(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, const FVisualizeTextureData& Data)
{
	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<VisualizeTexturePS<TextureType> > PixelShader(ShaderMap);

	static FGlobalBoundShaderState BoundShaderState;
	

	SetGlobalBoundShaderState(RHICmdList, FeatureLevel, BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	PixelShader->SetParameters(RHICmdList, Data);

	DrawRectangle(
		RHICmdList,
		// XY
		0, 0,
		// SizeXY
		GSceneRenderTargets.GetBufferSizeXY().X, GSceneRenderTargets.GetBufferSizeXY().Y,
		// UV
		Data.Tex00.X, Data.Tex00.Y,
		// SizeUV
		Data.Tex11.X - Data.Tex00.X, Data.Tex11.Y - Data.Tex00.Y,
		// TargetSize
		GSceneRenderTargets.GetBufferSizeXY(),
		// TextureSize
		FIntPoint(1, 1),
		*VertexShader,
		EDRF_UseTriangleOptimization);
}

void RenderVisualizeTexture(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, const FVisualizeTextureData& Data)
{
	if(Data.Desc.Is2DTexture())
	{
		// 2D
		if(Data.Desc.NumSamples > 1)
		{
			// MSAA
			VisualizeTextureForTextureType<5>(RHICmdList, FeatureLevel, Data);
		}
		else
		{
			// non MSAA
			VisualizeTextureForTextureType<2>(RHICmdList, FeatureLevel, Data);
		}
	}
	else if(Data.Desc.Is3DTexture())
	{
		// Volume
		VisualizeTextureForTextureType<3>(RHICmdList, FeatureLevel, Data);
	}
	else if(Data.Desc.IsCubemap())
	{
		if(Data.Desc.IsArray())
		{
			// Cube[]
			VisualizeTextureForTextureType<4>(RHICmdList, FeatureLevel, Data);
		}
		else
		{
			// Cube
			VisualizeTextureForTextureType<0>(RHICmdList, FeatureLevel, Data);
		}
	}
}

FVisualizeTexture::FVisualizeTexture()
{
	Mode = 0;
	RGBMul = 1.0f;
	AMul = 0.0f;
	UVInputMapping = 3;
	Flags = 0;
	ObservedDebugNameReusedGoal = 0xffffffff;
	ArrayIndex = 0;
	CustomMip = 0;
	bSaveBitmap = false;
	bOutputStencil = false;
	bFullList = false;
	SortOrder = -1;
	bEnabled = true;
}

FIntRect FVisualizeTexture::ComputeVisualizeTextureRect(FIntPoint InputTextureSize) const
{
	FIntRect ret = ViewRect;
	FIntPoint ViewExtent = ViewRect.Size();

	// set ViewRect
	switch(UVInputMapping)
	{
		// pixel perfect centered (not yet for volume textures)
		case 2:
		{
			FIntPoint Center = ViewExtent / 2;
			FIntPoint HalfMin = InputTextureSize / 2;
			FIntPoint HalfMax = InputTextureSize - HalfMin;

			ret = FIntRect(Center - HalfMin, Center + HalfMax);
			break;
		}

		// whole texture in PIP
		case 3:
		{
			int32 LeftOffset = AspectRatioConstrainedViewRect.Min.X;
			int32 BottomOffset = AspectRatioConstrainedViewRect.Max.Y - ViewRect.Max.Y;

			ret = FIntRect(LeftOffset + 80, ViewExtent.Y - ViewExtent.Y / 3 - 10 + BottomOffset, ViewExtent.X / 3 + 10, ViewExtent.Y - 10 + BottomOffset) + ViewRect.Min;
			break;
		}

		default:
		{
			break;
		}
	}

	return ret;
}

void FVisualizeTexture::GenerateContent(FRHICommandListImmediate& RHICmdList, const FSceneRenderTargetItem& RenderTargetItem, const FPooledRenderTargetDesc& Desc)
{
	// otherwise StartFrame() wasn't called
	check(ViewRect != FIntRect(0, 0, 0, 0))


	FTexture2DRHIRef VisTexture = (FTexture2DRHIRef&)RenderTargetItem.ShaderResourceTexture;

	if(!IsValidRef(VisTexture) || !Desc.IsValid())
	{
		// todo: improve
		return;
	}

	FIntRect VisualizeTextureRect = ComputeVisualizeTextureRect(Desc.Extent);

	FIntPoint Size = VisualizeTextureRect.Size();

	// clamp to reasonable value to prevent crash
	Size.X = FMath::Max(Size.X, 1);
	Size.Y = FMath::Max(Size.Y, 1);

	FPooledRenderTargetDesc OutputDesc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_B8G8R8A8, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
	
	GRenderTargetPool.FindFreeElement(OutputDesc, VisualizeTextureContent, TEXT("VisualizeTexture"));

	if(!VisualizeTextureContent)
	{
		return;
	}

	const FSceneRenderTargetItem& DestRenderTarget = VisualizeTextureContent->GetRenderTargetItem();

	SetRenderTarget(RHICmdList, DestRenderTarget.TargetableTexture, FTextureRHIRef());
	RHICmdList.Clear(true, FLinearColor(1, 1, 0, 1), false, 0.0f, false, 0, FIntRect());
	RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());
	RHICmdList.SetRasterizerState(TStaticRasterizerState<>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	FIntPoint RTExtent = GSceneRenderTargets.GetBufferSizeXY();

	FVector2D Tex00 = FVector2D(0, 0);
	FVector2D Tex11 = FVector2D(1, 1);

	uint32 LocalVisualizeTextureInputMapping = UVInputMapping;

	if(!Desc.Is2DTexture())
	{
		LocalVisualizeTextureInputMapping = 1;
	}

	// set UV
	switch(LocalVisualizeTextureInputMapping)
	{
		// UV in left top
		case 0:
			Tex11 = FVector2D((float)ViewRect.Width() / RTExtent.X, (float)ViewRect.Height() / RTExtent.Y);
			break;

		// whole texture
		default:
			break;
	}

	bool bIsDefault = StencilSRVSrc == GBlackTexture->TextureRHI;
	bool bDepthStencil = Desc.Is2DTexture() && Desc.Format == PF_DepthStencil;

	//clear if this is a new different Stencil buffer, or it's not a stencil buffer and we haven't switched to the default yet.
	bool bNeedsClear = bDepthStencil && (StencilSRVSrc != RenderTargetItem.TargetableTexture);
	bNeedsClear |= !bDepthStencil && !bIsDefault;
	if (bNeedsClear)
	{
		StencilSRVSrc = nullptr;
		StencilSRV.SafeRelease();
	}

	//always set something into the StencilSRV slot for platforms that require a full resource binding, even if
	//dynamic branching will cause them not to be used.	
	if(bDepthStencil && !StencilSRVSrc)
	{
		StencilSRVSrc = RenderTargetItem.TargetableTexture;
		StencilSRV = RHICreateShaderResourceView((FTexture2DRHIRef&) RenderTargetItem.TargetableTexture, 0, 1, PF_X24_G8);
	}
	else if(!StencilSRVSrc)
	{
		StencilSRVSrc = GBlackTexture->TextureRHI;
		StencilSRV = RHICreateShaderResourceView((FTexture2DRHIRef&) GBlackTexture->TextureRHI, 0, 1, PF_B8G8R8A8);
	}	

	FVisualizeTextureData VisualizeTextureData(RenderTargetItem, Desc);

	bool bDepthTexture = (Desc.TargetableFlags & TexCreate_DepthStencilTargetable) != 0;
	
	VisualizeTextureData.RGBMul = RGBMul;
	VisualizeTextureData.AMul = AMul;
	VisualizeTextureData.Tex00 = Tex00;
	VisualizeTextureData.Tex11 = Tex11;
	VisualizeTextureData.bSaturateInsteadOfFrac = (Flags & 1) != 0;
	VisualizeTextureData.InputValueMapping = bDepthTexture ? 1 : 0;
	VisualizeTextureData.ArrayIndex = ArrayIndex;
	VisualizeTextureData.CustomMip = CustomMip;
	VisualizeTextureData.StencilSRV = StencilSRV;

	if(!(Desc.Flags & TexCreate_CPUReadback))		// We cannot make a texture lookup on such elements
	{	
		SCOPED_DRAW_EVENT(RHICmdList, VisualizeTexture);
		// continue rendering to HDR if necessary
		RenderVisualizeTexture(RHICmdList, FeatureLevel, VisualizeTextureData);
	}

	{
		SCOPED_DRAW_EVENT(RHICmdList, VisCopy);
		RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, false, FResolveParams());
	}

	VisualizeTextureDesc = Desc;

	// save to disk
	if(bSaveBitmap)
	{
		bSaveBitmap = false;

		uint32 MipAdjustedExtentX = FMath::Clamp(Desc.Extent.X >> CustomMip, 0, Desc.Extent.X);
		uint32 MipAdjustedExtentY = FMath::Clamp(Desc.Extent.Y >> CustomMip, 0, Desc.Extent.Y);
		FIntPoint Extent(MipAdjustedExtentX, MipAdjustedExtentY);

		FReadSurfaceDataFlags ReadDataFlags;
		ReadDataFlags.SetLinearToGamma(false);
		ReadDataFlags.SetOutputStencil(bOutputStencil);		
		ReadDataFlags.SetMip(CustomMip);

		FTextureRHIRef Texture = RenderTargetItem.TargetableTexture ? RenderTargetItem.TargetableTexture : RenderTargetItem.ShaderResourceTexture;

		check(Texture);

		TArray<FColor> Bitmap;
		
		
		
		RHICmdList.ReadSurfaceData(Texture, FIntRect(0, 0, Extent.X, Extent.Y), Bitmap, ReadDataFlags);

		// if the format and texture type is supported
		if(Bitmap.Num())
		{
			// Create screenshot folder if not already present.
			IFileManager::Get().MakeDirectory(*FPaths::ScreenShotDir(), true);

			const FString ScreenFileName(FPaths::ScreenShotDir() / TEXT("VisualizeTexture"));

			uint32 ExtendXWithMSAA = Bitmap.Num() / Extent.Y;

			// Save the contents of the array to a bitmap file. (24bit only so alpha channel is dropped)
			FFileHelper::CreateBitmap(*ScreenFileName, ExtendXWithMSAA, Extent.Y, Bitmap.GetData());	

			UE_LOG(LogConsoleResponse, Display, TEXT("Content was saved to \"%s\""), *FPaths::ScreenShotDir());
		}
		else
		{
			UE_LOG(LogConsoleResponse, Error, TEXT("Failed to save BMP for VisualizeTexture, format or texture type is not supported"));
		}
	}
}

void FVisualizeTexture::PresentContent(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	if(Mode != 0)
	{
		// old mode is used, lets copy the specified texture to do it similar to the new system
		FPooledRenderTarget* Element = GRenderTargetPool.GetElementById(Mode - 1);
		if(Element)
		{
			GenerateContent(RHICmdList, Element->GetRenderTargetItem(), Element->GetDesc());
		}
	}

	const FTexture2DRHIRef& RenderTargetTexture = View.Family->RenderTarget->GetRenderTargetTexture();

	if(!VisualizeTextureContent 
		|| !IsValidRef(RenderTargetTexture)
		|| !bEnabled)
	{
		// visualize feature is deactivated
		return;
	}

	FPooledRenderTargetDesc Desc = VisualizeTextureDesc;

	auto& RenderTarget = View.Family->RenderTarget->GetRenderTargetTexture();
	SetRenderTarget(RHICmdList, RenderTarget, FTextureRHIRef());
	RHICmdList.SetViewport(0, 0, 0.0f, RenderTarget->GetSizeX(), RenderTarget->GetSizeY(), 1.0f);

	RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());
	RHICmdList.SetRasterizerState(TStaticRasterizerState<>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	auto ShaderMap = View.ShaderMap;
	TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
	TShaderMapRef<FVisualizeTexturePresentPS> PixelShader(ShaderMap);

	static FGlobalBoundShaderState BoundShaderState;
	

	SetGlobalBoundShaderState(RHICmdList, FeatureLevel, BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

	VertexShader->SetParameters(RHICmdList, View);
	PixelShader->SetParameters(RHICmdList, View, *VisualizeTextureContent);

	FIntRect DestRect = View.ViewRect;

	FIntRect VisualizeTextureRect = ComputeVisualizeTextureRect(Desc.Extent);

	{
		SCOPED_DRAW_EVENT(RHICmdList, VisCopyToMain);
		// Draw a quad mapping scene color to the view's render target
		DrawRectangle(
			RHICmdList,
			VisualizeTextureRect.Min.X, VisualizeTextureRect.Min.Y,
			VisualizeTextureRect.Width(), VisualizeTextureRect.Height(),
			0, 0,
			VisualizeTextureRect.Width(), VisualizeTextureRect.Height(),
			FIntPoint(RenderTarget->GetSizeX(), RenderTarget->GetSizeY()),
			VisualizeTextureRect.Size(),
			*VertexShader,
			EDRF_Default);
	}

	// this is a helper class for FCanvas to be able to get screen size
	class FRenderTargetTemp : public FRenderTarget
	{
	public:
		const FSceneView& View;

		FRenderTargetTemp(const FSceneView& InView) : View(InView)
		{
		}
		virtual FIntPoint GetSizeXY() const
		{
			return View.UnscaledViewRect.Size();
		};
		virtual const FTexture2DRHIRef& GetRenderTargetTexture() const
		{
			return View.Family->RenderTarget->GetRenderTargetTexture();
		}
	} TempRenderTarget(View);

	FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, View.Family->CurrentWorldTime, View.Family->DeltaWorldTime, View.GetFeatureLevel());

	float X = 100 + View.ViewRect.Min.X;
	float Y = 160 + View.ViewRect.Min.Y;
	float YStep = 14;

	{
		uint32 ReuseCount = ObservedDebugNameReusedCurrent;

		FString ExtendedName;
		if(ReuseCount)
		{
			uint32 ReuseGoal = FMath::Min(ReuseCount - 1, ObservedDebugNameReusedGoal);

			// was reused this frame
			ExtendedName = FString::Printf(TEXT("%s@%d @0..%d"), Desc.DebugName, ReuseGoal, ReuseCount - 1);
		}
		else
		{
			// was not reused this frame but can be referenced
			ExtendedName = FString::Printf(TEXT("%s"), Desc.DebugName);
		}

		FString Line = FString::Printf(TEXT("VisualizeTexture: %d \"%s\" RGB*%g+A*%g UV%d"),
			Mode,
			*ExtendedName,
			RGBMul,
			AMul,
			UVInputMapping);
		Canvas.DrawShadowedString( X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
	}
	{
		FString Line = FString::Printf(TEXT("   TextureInfoString(): %s"), *(Desc.GenerateInfoString()));
		Canvas.DrawShadowedString( X + 10, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
	}
	{
		FString Line = FString::Printf(TEXT("  BufferSize:(%d,%d)"), GSceneRenderTargets.GetBufferSizeXY().X, GSceneRenderTargets.GetBufferSizeXY().Y);
		Canvas.DrawShadowedString( X + 10, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
	}

	const FSceneViewFamily& ViewFamily = *View.Family;

	for(int32 ViewId = 0; ViewId < ViewFamily.Views.Num(); ++ViewId)
	{
		const FSceneView& ViewIt = *ViewFamily.Views[ViewId];
		FString Line = FString::Printf(TEXT("   View #%d: (%d,%d)-(%d,%d)"), ViewId + 1,
			ViewIt.ViewRect.Min.X, ViewIt.ViewRect.Min.Y, ViewIt.ViewRect.Max.X, ViewIt.ViewRect.Max.Y);
		Canvas.DrawShadowedString( X + 10, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
	}

	X += 40;

	if(Desc.Flags & TexCreate_CPUReadback)
	{
		Canvas.DrawShadowedString( X, Y += YStep, TEXT("Content cannot be visualized on the GPU (TexCreate_CPUReadback)"), GetStatsFont(), FLinearColor(1,1,0));
	}
	else
	{
		Canvas.DrawShadowedString( X, Y += YStep, TEXT("Blinking Red: <0"), GetStatsFont(), FLinearColor(1,0,0));
		Canvas.DrawShadowedString( X, Y += YStep, TEXT("Blinking Blue: NAN or Inf"), GetStatsFont(), FLinearColor(0,0,1));
	}

	Canvas.Flush_RenderThread(RHICmdList);
}


void FVisualizeTexture::SetObserveTarget(const FString& InObservedDebugName, uint32 InObservedDebugNameReusedGoal)
{
	ObservedDebugName = InObservedDebugName;
	ObservedDebugNameReusedGoal = InObservedDebugNameReusedGoal;
}

void FVisualizeTexture::SetCheckPoint(FRHICommandList& RHICmdList, const IPooledRenderTarget* PooledRenderTarget)
{

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	check(IsInRenderingThread());
	if (!PooledRenderTarget || !bEnabled)
	{
		// Don't checkpoint on ES2 to avoid TMap alloc/reallocations
		return;
	}

	const FSceneRenderTargetItem& RenderTargetItem = PooledRenderTarget->GetRenderTargetItem();
	const FPooledRenderTargetDesc& Desc = PooledRenderTarget->GetDesc();
	const TCHAR* DebugName = Desc.DebugName;

	uint32* UsageCountPtr = VisualizeTextureCheckpoints.Find(DebugName);

	if(!UsageCountPtr)
	{
		// create a new element with count 0
		UsageCountPtr = &VisualizeTextureCheckpoints.Add(DebugName, 0);
	}

	// is this is the name we are observing with visualize texture?
	if(ObservedDebugName == DebugName)
	{
		// if multiple times reused during the frame, is that the one we want to look at?
		if(*UsageCountPtr == ObservedDebugNameReusedGoal || ObservedDebugNameReusedGoal == 0xffffffff)
		{
			FRHICommandListImmediate& RHICmdListIm = FRHICommandListExecutor::GetImmediateCommandList();
			if (RHICmdListIm.IsExecuting())
			{
				UE_LOG(LogConsoleResponse, Fatal, TEXT("We can't create a checkpoint because that requires the immediate commandlist, which is currently executing. You might try disabling parallel rendering."));
			}
			else
			{
				if (&RHICmdList != &RHICmdListIm)
				{
					UE_LOG(LogConsoleResponse, Warning, TEXT("Attempt to checkpoint a render target from a non-immediate command list. We will flush it and hope that works. If it doesn't you might try disabling parallel rendering."));
					RHICmdList.Flush();
				}
				GenerateContent(RHICmdListIm, RenderTargetItem, Desc);
				if (&RHICmdList != &RHICmdListIm)
				{
					RHICmdListIm.Flush();
				}
			}
		}
	}
	// only needed for VisualizeTexture (todo: optimize out when possible)
	*UsageCountPtr = *UsageCountPtr + 1;
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}


struct FSortedLines
{
	FString Line;
	int32 SortIndex;
	uint32 PoolIndex;

	FORCEINLINE bool operator<( const FSortedLines &B ) const
	{
		// first large ones
		if(SortIndex < B.SortIndex)
		{
			return true; 
		}
		if(SortIndex > B.SortIndex)
		{
			return false; 
		}

		return Line < B.Line;
	}
};

void FVisualizeTexture::DebugLog(bool bExtended)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		TArray<FSortedLines> SortedLines;

		for(uint32 i = 0, Num = GRenderTargetPool.GetElementCount(); i < Num; ++i)
		{
			FPooledRenderTarget* RT = GRenderTargetPool.GetElementById(i);

			if(!RT)
			{
				continue;
			}

			FPooledRenderTargetDesc Desc = RT->GetDesc();

			if(bFullList || (Desc.Flags & TexCreate_HideInVisualizeTexture) == 0)
			{
				uint32 SizeInKB = (RT->ComputeMemorySize() + 1023) / 1024;

				FString UnusedStr;

				if(RT->GetUnusedForNFrames() > 0)
				{
					if(!bFullList)
					{
						continue;
					}

					UnusedStr = FString::Printf(TEXT(" unused(%d)"), RT->GetUnusedForNFrames());
				}

				FSortedLines Element;

				Element.PoolIndex = i;
				
				// sort by index
				Element.SortIndex = i;
				
				if(SortOrder == -1)
				{
					// sort by index
					Element.Line = FString::Printf(TEXT("%s %s %d KB%s"), *Desc.GenerateInfoString(), Desc.DebugName, SizeInKB, *UnusedStr);
				}
				else if(SortOrder == 0)
				{
					// sort by name
					Element.Line = FString::Printf(TEXT("%s %s %d KB%s"), Desc.DebugName, *Desc.GenerateInfoString(), SizeInKB, *UnusedStr);
					Element.SortIndex = 0;
				}
				else if(SortOrder == 1)
				{
					// sort by size (large ones first)
					Element.Line = FString::Printf(TEXT("%d KB %s %s%s"), SizeInKB, *Desc.GenerateInfoString(), Desc.DebugName, *UnusedStr);
					Element.SortIndex = -(int32)SizeInKB;
				}
				else
				{
					check(0);
				}

				if(Desc.Flags & TexCreate_FastVRAM)
				{
					FRHIResourceInfo Info;

					FTextureRHIRef Texture = RT->GetRenderTargetItem().ShaderResourceTexture;

					if(!IsValidRef(Texture))
					{
						Texture = RT->GetRenderTargetItem().TargetableTexture;
					}

					if(IsValidRef(Texture))
					{
						RHIGetResourceInfo(Texture, Info);
					}

					if(Info.VRamAllocation.AllocationSize)
					{
						// note we do KB for more readable numbers but this can cause quantization loss
						Element.Line += FString::Printf(TEXT(" VRamInKB(Start/Size):%d/%d"), 
							Info.VRamAllocation.AllocationStart / 1024, 
							(Info.VRamAllocation.AllocationSize + 1023) / 1024);
					}
					else
					{
						Element.Line += TEXT(" VRamInKB(Start/Size):<NONE>");
					}
				}

				SortedLines.Add(Element);
			}
		}

		SortedLines.Sort();

		{
			for(int32 Index = 0; Index < SortedLines.Num(); Index++)
			{
				const FSortedLines& Entry = SortedLines[Index];

				UE_LOG(LogConsoleResponse, Log, TEXT("   %3d = %s"), Entry.PoolIndex + 1, *Entry.Line);
			}
		}

		// clean flags for next use
		bFullList = false;
		SortOrder = -1;
	}
						
	UE_LOG(LogConsoleResponse, Log, TEXT(""));

	// log names (alternative method to look at the rendertargets)
	if(bExtended)
	{
		UE_LOG(LogConsoleResponse, Log, TEXT("CheckpointName (what was rendered this frame, use <Name>@<Number> to get intermediate versions):"));

		TArray<FString> Entries;
				
		// sorted by pointer for efficiency, now we want to print sorted alphabetically
		for (TMap<FString, uint32>:: TIterator It(GRenderTargetPool.VisualizeTexture.VisualizeTextureCheckpoints); It; ++It)
		{
			const FString& Key = It.Key();
			uint32 Value = It.Value();

/*					if(Value)
			{
				// was reused this frame
				Entries.Add(FString::Printf(TEXT("%s @0..%d"), *Key.GetPlainNameString(), Value - 1));
			}
			else
*/					{
				// was not reused this frame but can be referenced
				Entries.Add(Key);
			}
		}

		Entries.Sort();
				
		// print them sorted, if possible multiple in a line
		{
			FString Line;
			FString Separator = " ,    ";

			for(int32 Index=0; Index < Entries.Num(); Index++ )
			{
				const FString& Entry = *Entries[Index];

				if(Line.Len() + 2 + Entry.Len() > 80)
				{
					UE_LOG(LogConsoleResponse, Log, TEXT("   %s"), *Line);
					Line.Empty();
				}

				Line += Entry;
				Line += Separator;
			}

			if(!Line.IsEmpty())
			{	
				// remove separator in the end
				Line = Line.Left(Line.Len() - Separator.Len());
				UE_LOG(LogConsoleResponse, Log, TEXT("   %s"), *Line);
			}
		}
	}

	{
		uint32 WholeCount;
		uint32 WholePoolInKB;
		uint32 UsedInKB;

		GRenderTargetPool.GetStats(WholeCount, WholePoolInKB, UsedInKB);

		UE_LOG(LogConsoleResponse, Log, TEXT("Pool: %d/%d MB (referenced/allocated)"), (UsedInKB + 1023) / 1024, (WholePoolInKB + 1023) / 1024);
	}
#endif
}

// @return 0 if not found
IPooledRenderTarget* FVisualizeTexture::GetObservedElement() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	IPooledRenderTarget* RT = VisualizeTextureContent.GetReference();

	if(!RT && Mode >= 0)
	{
		uint32 Id = Mode - 1;

		RT = GRenderTargetPool.GetElementById(Id);
	}

	return RT;
#else
	return 0;
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

void FVisualizeTexture::OnStartFrame(const FSceneView& View)
{
	FeatureLevel = View.GetFeatureLevel();
	bEnabled = true;
	ViewRect = View.UnscaledViewRect;
	AspectRatioConstrainedViewRect = View.Family->EngineShowFlags.CameraAspectRatioBars ? View.CameraConstrainedViewRect : ViewRect;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// VisualizeTexture observed render target is set each frame
	VisualizeTextureContent.SafeRelease();
	VisualizeTextureDesc = FPooledRenderTargetDesc();
	VisualizeTextureDesc.DebugName = TEXT("VisualizeTexture");

	ObservedDebugNameReusedCurrent = 0;

	// only needed for VisualizeTexture (todo: optimize out when possible)
	{
		for (TMap<FString, uint32>:: TIterator It(VisualizeTextureCheckpoints); It; ++It)
		{
			uint32& Value = It.Value();

			// 0 as it was not used this frame yet
			Value = 0;
		}
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

void FVisualizeTexture::QueryInfo( FQueryVisualizeTexureInfo& Out )
{
	for(uint32 i = 0, Num = GRenderTargetPool.GetElementCount(); i < Num; ++i)
	{
		FPooledRenderTarget* RT = GRenderTargetPool.GetElementById(i);

		if(!RT)
		{
			continue;
		}

		FPooledRenderTargetDesc Desc = RT->GetDesc();
		uint32 SizeInKB = (RT->ComputeMemorySize() + 1023) / 1024;
		FString Entry = FString::Printf(TEXT("%s %d %s %d"),
				*Desc.GenerateInfoString(),
				i + 1,
				Desc.DebugName ? Desc.DebugName : TEXT("<Unnamed>"),
				SizeInKB);
		Out.Entries.Add(Entry);
	}
}
