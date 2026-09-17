#include "ModelSlateElement.h"

#include "CommonRenderResources.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderResource.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

namespace
{
	TAutoConsoleVariable<bool> CVarModelWidgetDebug(
		TEXT("ui.ModelWidget.Debug"), false,
		TEXT("Log why UModelWidget has nothing to draw: which buffer snapshot failed, and when one succeeds."));
}

bool ModelWidgetDebugEnabled()
{
	return CVarModelWidgetDebug.GetValueOnAnyThread();
}

namespace
{
	class FModelWidgetVS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FModelWidgetVS);
		SHADER_USE_PARAMETER_STRUCT(FModelWidgetVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FMatrix44f, ModelViewProjection)
			SHADER_PARAMETER(FMatrix44f, NormalMatrix)
			SHADER_PARAMETER(uint32, NumTexCoords)
			SHADER_PARAMETER_SRV(Buffer<float>, Positions)
			SHADER_PARAMETER_SRV(Buffer<float4>, Tangents)
			SHADER_PARAMETER_SRV(Buffer<float2>, TexCoords)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FModelWidgetPS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FModelWidgetPS);
		SHADER_USE_PARAMETER_STRUCT(FModelWidgetPS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector3f, LightDirection)
			SHADER_PARAMETER(FLinearColor, TintColor)
			SHADER_PARAMETER(FLinearColor, TintColor2)
			SHADER_PARAMETER(float, Gamma)
			SHADER_PARAMETER(uint32, bMasked)
			SHADER_PARAMETER_TEXTURE(Texture2D, DiffuseTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, DiffuseSampler)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FModelWidgetVS, "/Plugin/ModelWidget/Private/ModelWidget.usf", "MainVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FModelWidgetPS, "/Plugin/ModelWidget/Private/ModelWidget.usf", "MainPS", SF_Pixel);

	BEGIN_SHADER_PARAMETER_STRUCT(FModelPassParameters, )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	/** Slate's gamma for an SDR back buffer; the shader encodes its linear result for it. */
	constexpr float DisplayGamma = 2.2f;

	/** View space: x right, y up, z into the screen. From the upper left, in front. */
	const FVector3f KeyLightDirection = FVector3f(-0.4f, 0.5f, -0.75f).GetSafeNormal();

	FShaderResourceViewRHIRef MakeTypedView(FRHICommandListBase& RHICmdList, FRHIBuffer* Buffer, EPixelFormat Format)
	{
		return RHICmdList.CreateShaderResourceView(Buffer,
			FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(Format));
	}

	/** Row-vector orthographic projection: x, y to [-1, 1] over the given spans, z over [-DepthExtent, DepthExtent] to [0, 1]. */
	FMatrix MakeOrthographic(double Width, double Height, double DepthExtent)
	{
		return FMatrix(
			FPlane(2.0 / Width, 0.0, 0.0, 0.0),
			FPlane(0.0, 2.0 / Height, 0.0, 0.0),
			FPlane(0.0, 0.0, 0.5 / DepthExtent, 0.0),
			FPlane(0.0, 0.0, 0.5, 1.0));
	}

	/** UE world axes (X forward, Y right, Z up) to view axes (x right, y up, z depth). */
	const FMatrix WorldToViewAxes(
		FPlane(0.0, 0.0, 1.0, 0.0),
		FPlane(1.0, 0.0, 0.0, 0.0),
		FPlane(0.0, 1.0, 0.0, 0.0),
		FPlane(0.0, 0.0, 0.0, 1.0));
}

void FModelSlateElement::Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs)
{
	if (!Model || !Model->IsValid() || !Inputs.OutputTexture || ViewRect.IsEmpty())
	{
		return;
	}

	const FIntPoint Extent = Inputs.OutputTexture->Desc.Extent;
	const FIntPoint Offset(FMath::RoundToInt(Inputs.ElementsOffset.X), FMath::RoundToInt(Inputs.ElementsOffset.Y));

	FIntRect PixelRect = ViewRect + Offset;
	// D3D12 insists the scissor sits inside the viewport, and the viewport
	// inside the target.
	PixelRect.Clip(FIntRect(FIntPoint::ZeroValue, Extent));
	FIntRect Scissor = (ClipRect + Offset);
	Scissor.Clip(PixelRect);
	if (PixelRect.IsEmpty() || Scissor.IsEmpty())
	{
		return;
	}

	// Turntable: spin the model about its centre, then look at it from the
	// fixed view angle, then fit its bounding sphere to the rect.
	const double Radius = FMath::Max(static_cast<double>(Model->BoundsRadius), 1.0);
	const double Span = 2.0 * FMath::Max(static_cast<double>(Model->BoundsExtent.GetMax()), 1.0) / FMath::Max(Fill, 0.1f);
	const double Aspect = static_cast<double>(PixelRect.Width()) / FMath::Max(PixelRect.Height(), 1);
	const FMatrix ModelMatrix = FTranslationMatrix(-FVector(Model->BoundsOrigin)) * FRotationMatrix(ModelRotation);
	const FMatrix ViewMatrix = FInverseRotationMatrix(ViewRotation) * WorldToViewAxes;
	const FMatrix ModelView = ModelMatrix * ViewMatrix;
	const FMatrix Projection = MakeOrthographic(Span * FMath::Max(1.0, Aspect), Span * FMath::Max(1.0, 1.0 / Aspect), Radius * 2.0);

	FModelWidgetVS::FParameters VertexParameters;
	VertexParameters.ModelViewProjection = FMatrix44f(ModelView * Projection);
	VertexParameters.NormalMatrix = FMatrix44f(ModelView.RemoveTranslation());
	VertexParameters.NumTexCoords = Model->NumTexCoords;

	// Our own depth: Slate's target has none, and a mesh needs one to occlude
	// itself. Pooled by the graph, so one texture serves every element.
	FRDGTextureRef Depth = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Extent, PF_DepthStencil, FClearValueBinding::DepthOne, TexCreate_DepthStencilTargetable),
		TEXT("ModelWidgetDepth"));

	FModelPassParameters* PassParameters = GraphBuilder.AllocParameters<FModelPassParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.OutputTexture, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Depth, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	const bool bHDR = Inputs.bOutputIsHDRDisplay;
	TSharedPtr<FModelRenderData> Mesh = Model;

	GraphBuilder.AddPass(RDG_EVENT_NAME("ModelWidget"), PassParameters, ERDGPassFlags::Raster,
		[Mesh, VertexParameters, PixelRect, Scissor, bHDR](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			if (!Mesh->PositionView)
			{
				Mesh->PositionView = MakeTypedView(RHICmdList, Mesh->PositionBuffer, PF_R32_FLOAT);
				Mesh->TangentView = MakeTypedView(RHICmdList, Mesh->TangentBuffer, Mesh->bHighPrecisionTangents ? PF_R16G16B16A16_SNORM : PF_R8G8B8A8_SNORM);
				Mesh->TexCoordView = MakeTypedView(RHICmdList, Mesh->TexCoordBuffer, Mesh->bFullPrecisionUVs ? PF_G32R32F : PF_G16R16F);
			}

			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FModelWidgetVS> VertexShader(ShaderMap);
			TShaderMapRef<FModelWidgetPS> PixelShader(ShaderMap);

			RHICmdList.SetViewport(PixelRect.Min.X, PixelRect.Min.Y, 0.f, PixelRect.Max.X, PixelRect.Max.Y, 1.f);
			RHICmdList.SetScissorRect(true, Scissor.Min.X, Scissor.Min.Y, Scissor.Max.X, Scissor.Max.Y);

			FGraphicsPipelineStateInitializer PipelineState;
			RHICmdList.ApplyCachedRenderTargets(PipelineState);
			PipelineState.BlendState = TStaticBlendState<>::GetRHI();
			// Two-sided, so meshes with inconsistent winding still draw whole.
			PipelineState.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			PipelineState.DepthStencilState = TStaticDepthStencilState<true, CF_LessEqual>::GetRHI();
			PipelineState.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			PipelineState.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			PipelineState.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			PipelineState.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, PipelineState, 0);

			FModelWidgetVS::FParameters VertexParametersWithViews = VertexParameters;
			VertexParametersWithViews.Positions = Mesh->PositionView;
			VertexParametersWithViews.Tangents = Mesh->TangentView;
			VertexParametersWithViews.TexCoords = Mesh->TexCoordView;
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VertexParametersWithViews);

			for (const FModelSection& Section : Mesh->Sections)
			{
				FRHITexture* Texture = Section.Diffuse ? Section.Diffuse->TextureRHI.GetReference() : nullptr;
				if (!Texture)
				{
					Texture = GWhiteTexture->TextureRHI;
				}

				FModelWidgetPS::FParameters PixelParameters;
				PixelParameters.LightDirection = KeyLightDirection;
				PixelParameters.TintColor = Section.TintColor;
				PixelParameters.TintColor2 = Section.TintColor2;
				PixelParameters.Gamma = bHDR ? 1.f : DisplayGamma;
				PixelParameters.bMasked = Section.bMasked ? 1 : 0;
				PixelParameters.DiffuseTexture = Texture;
				PixelParameters.DiffuseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PixelParameters);

				RHICmdList.DrawIndexedPrimitive(Mesh->IndexBuffer, 0, 0, Mesh->NumVertices, Section.FirstIndex, Section.NumTriangles, 1);
			}

			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		});
}

void FModelSlateElement::Retain(const TSharedPtr<FModelSlateElement>& Element)
{
	// Sized for many rows painting every frame with the render thread two
	// frames behind; the RHI references it pins are a handful of meshes.
	constexpr int32 RingSize = 512;
	static TArray<TSharedPtr<FModelSlateElement>> Ring;
	static int32 Next = 0;

	check(IsInGameThread());
	if (Ring.Num() < RingSize)
	{
		Ring.Add(Element);
		return;
	}
	Ring[Next] = Element;
	Next = (Next + 1) % RingSize;
}
