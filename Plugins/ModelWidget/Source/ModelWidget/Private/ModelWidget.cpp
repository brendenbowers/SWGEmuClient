#include "ModelWidget.h"
#include "ModelSlateElement.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "RawIndexBuffer.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshResources.h"
#include "TextureResource.h"
#include "Widgets/SNullWidget.h"

// ── Slate widget ─────────────────────────────────────────────────────────────

void SModelWidget::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
	ViewRotation = InArgs._ViewRotation;
	Fill = InArgs._Fill;
	RotateSpeed = InArgs._RotateSpeed;
	SetCanTick(true);
}

void SModelWidget::SetModelSource(TFunction<TSharedPtr<FModelRenderData>()> InSource)
{
	Source = MoveTemp(InSource);
	Model.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SModelWidget::SetDesiredSize(FVector2D InSize)
{
	DesiredSize = InSize;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SModelWidget::SetViewRotation(FRotator InRotation)
{
	ViewRotation = InRotation;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SModelWidget::SetFill(float InFill)
{
	Fill = InFill;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SModelWidget::SetRotateSpeed(float InSpeed)
{
	RotateSpeed = InSpeed;
}

void SModelWidget::SetYaw(float InYaw)
{
	Yaw = InYaw;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SModelWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!Model && Source)
	{
		Model = Source();
		if (Model)
		{
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		if (ModelWidgetDebugEnabled())
		{
			UE_LOG(LogTemp, Log, TEXT("ModelWidget tick: source %s"), Model ? TEXT("ready") : TEXT("not ready"));
		}
	}

	if (Model && RotateSpeed != 0.f)
	{
		Yaw = FMath::Fmod(Yaw + RotateSpeed * InDeltaTime, 360.f);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 SModelWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!Model || !Model->IsValid())
	{
		return LayerId;
	}

	// Absolute coordinates are window pixels; the element adds the offset of
	// the back buffer within the window itself.
	const FSlateRect Bounds = AllottedGeometry.GetRenderBoundingRect();
	const FSlateRect Clip = Bounds.IntersectionWith(MyCullingRect);
	if (!Clip.IsValid() || Clip.GetSize().X <= 0.f || Clip.GetSize().Y <= 0.f)
	{
		return LayerId;
	}

	TSharedPtr<FModelSlateElement> Element = MakeShared<FModelSlateElement>();
	Element->Model = Model;
	Element->ViewRect = FIntRect(FMath::RoundToInt(Bounds.Left), FMath::RoundToInt(Bounds.Top), FMath::RoundToInt(Bounds.Right), FMath::RoundToInt(Bounds.Bottom));
	Element->ClipRect = FIntRect(FMath::FloorToInt(Clip.Left), FMath::FloorToInt(Clip.Top), FMath::CeilToInt(Clip.Right), FMath::CeilToInt(Clip.Bottom));
	Element->ModelRotation = FRotator(0.f, Yaw, 0.f);
	Element->ViewRotation = ViewRotation;
	Element->Fill = Fill;

	FModelSlateElement::Retain(Element);

	FSlateDrawElement::MakeCustom(OutDrawElements, LayerId, Element);
	return LayerId + 1;
}

// ── UMG widget ───────────────────────────────────────────────────────────────

namespace
{
	/** What one material contributes to a section: its diffuse and the tint pair, read from the named parameters. */
	void DescribeMaterial(const UMaterialInterface* Material, FName DiffuseParameter, FName TintParameter, FName SecondTintParameter, FModelSection& OutSection)
	{
		if (!Material)
		{
			return;
		}

		UTexture* Diffuse = nullptr;
		if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(DiffuseParameter), Diffuse) && Diffuse)
		{
			OutSection.Diffuse = Diffuse->GetResource();
		}
		Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TintParameter), OutSection.TintColor);
		Material->GetVectorParameterValue(FHashedMaterialParameterInfo(SecondTintParameter), OutSection.TintColor2);
		OutSection.bMasked = Material->GetBlendMode() == BLEND_Masked;
	}

	bool ReadVertexBuffers(const FPositionVertexBuffer& Positions, const FStaticMeshVertexBuffer& Attributes, FModelRenderData& OutData)
	{
		OutData.PositionBuffer = Positions.VertexBufferRHI;
		OutData.TangentBuffer = Attributes.TangentsVertexBuffer.VertexBufferRHI;
		OutData.TexCoordBuffer = Attributes.TexCoordVertexBuffer.VertexBufferRHI;
		OutData.NumVertices = Positions.GetNumVertices();
		OutData.NumTexCoords = FMath::Max<uint32>(Attributes.GetNumTexCoords(), 1);
		OutData.bHighPrecisionTangents = Attributes.GetUseHighPrecisionTangentBasis();
		OutData.bFullPrecisionUVs = Attributes.GetUseFullPrecisionUVs();
		return OutData.PositionBuffer && OutData.TangentBuffer && OutData.TexCoordBuffer;
	}
}

TSharedRef<SWidget> UModelWidget::RebuildWidget()
{
	SlateWidget = SNew(SModelWidget)
		.DesiredSize(DesiredSize)
		.ViewRotation(ViewRotation)
		.Fill(Fill)
		.RotateSpeed(RotateSpeed);

	if (Mesh)
	{
		SlateWidget->SetModelSource(MakeModelSource());
	}
	return SlateWidget.ToSharedRef();
}

void UModelWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (SlateWidget)
	{
		SlateWidget->SetDesiredSize(DesiredSize);
		SlateWidget->SetViewRotation(ViewRotation);
		SlateWidget->SetFill(Fill);
		SlateWidget->SetRotateSpeed(RotateSpeed);
	}
}

void UModelWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SlateWidget.Reset();
}

namespace
{
	FModelProvider ModelProvider;
}

void UModelWidget::SetModelProvider(FModelProvider InProvider)
{
	ModelProvider = MoveTemp(InProvider);
}

void UModelWidget::SetObject(int64 ObjectId)
{
	PendingObjectId = ObjectId;
	ClearModel();

	UWorld* World = GetWorld();
	if (!ModelProvider || !World || ObjectId == 0)
	{
		return;
	}

	TWeakObjectPtr<UModelWidget> WeakThis(this);
	ModelProvider(World, ObjectId, [WeakThis, ObjectId](UObject* InMesh, const TArray<UMaterialInterface*>& InMaterials)
	{
		if (UModelWidget* Self = WeakThis.Get(); Self && Self->PendingObjectId == ObjectId)
		{
			Self->HandleModelReady(InMesh, InMaterials);
		}
	});
}

void UModelWidget::HandleModelReady(UObject* InMesh, const TArray<UMaterialInterface*>& InMaterials)
{
	SetMesh(InMesh, InMaterials);
}

void UModelWidget::SetMesh(UObject* InMesh, const TArray<UMaterialInterface*>& InMaterials)
{
	Mesh = InMesh;
	Materials.Reset();
	for (UMaterialInterface* Material : InMaterials)
	{
		Materials.Add(Material);
	}

	if (SlateWidget)
	{
		SlateWidget->SetModelSource(Mesh ? MakeModelSource() : nullptr);
	}
}

void UModelWidget::ClearModel()
{
	Mesh = nullptr;
	Materials.Reset();
	if (SlateWidget)
	{
		SlateWidget->SetModelSource(nullptr);
	}
}

void UModelWidget::SetRotateSpeed(float DegreesPerSecond)
{
	RotateSpeed = DegreesPerSecond;
	if (SlateWidget)
	{
		SlateWidget->SetRotateSpeed(RotateSpeed);
	}
}

TFunction<TSharedPtr<FModelRenderData>()> UModelWidget::MakeModelSource() const
{
	// Weak: the Slate widget can tick once more after this object is gone.
	TWeakObjectPtr<const UModelWidget> WeakThis(this);
	return [WeakThis]() -> TSharedPtr<FModelRenderData>
	{
		const UModelWidget* Self = WeakThis.Get();
		return Self ? Self->BuildRenderData() : nullptr;
	};
}

TSharedPtr<FModelRenderData> UModelWidget::BuildRenderData() const
{
	const auto Abandon = [this](const TCHAR* Reason) -> TSharedPtr<FModelRenderData>
	{
		if (ModelWidgetDebugEnabled())
		{
			UE_LOG(LogTemp, Log, TEXT("ModelWidget snapshot of %s: %s"), Mesh ? *Mesh->GetName() : TEXT("(null)"), Reason);
		}
		return nullptr;
	};

	TSharedPtr<FModelRenderData> Data = MakeShared<FModelRenderData>();

	// The RHI refs are assigned on the render thread after the mesh's
	// resources are queued, so a mesh built this frame reads back null until
	// then; the Slate widget keeps asking each tick.
	if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
	{
		const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
		if (!RenderData || RenderData->LODResources.IsEmpty())
		{
			return Abandon(TEXT("static mesh has no render data"));
		}
		const FStaticMeshLODResources& Lod = RenderData->LODResources[0];
		Data->IndexBuffer = Lod.IndexBuffer.IndexBufferRHI;
		if (!Data->IndexBuffer || !ReadVertexBuffers(Lod.VertexBuffers.PositionVertexBuffer, Lod.VertexBuffers.StaticMeshVertexBuffer, *Data))
		{
			return Abandon(TEXT("static mesh buffers not on the GPU yet"));
		}
		for (const FStaticMeshSection& Section : Lod.Sections)
		{
			FModelSection& Out = Data->Sections.AddDefaulted_GetRef();
			Out.FirstIndex = Section.FirstIndex;
			Out.NumTriangles = Section.NumTriangles;
			const UMaterialInterface* Material = Materials.IsValidIndex(Section.MaterialIndex) ? Materials[Section.MaterialIndex].Get() : nullptr;
			if (!Material && StaticMesh->GetStaticMaterials().IsValidIndex(Section.MaterialIndex))
			{
				Material = StaticMesh->GetStaticMaterials()[Section.MaterialIndex].MaterialInterface;
			}
			DescribeMaterial(Material, DiffuseParameter, TintParameter, SecondTintParameter, Out);
		}
		const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
		Data->BoundsOrigin = FVector3f(Bounds.Origin);
		Data->BoundsRadius = static_cast<float>(Bounds.SphereRadius);
		Data->BoundsExtent = FVector3f(Bounds.BoxExtent);
	}
	else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
	{
		// Bind pose — fine for an item on a turntable.
		const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
		if (!RenderData || RenderData->LODRenderData.IsEmpty())
		{
			return Abandon(TEXT("skeletal mesh has no render data"));
		}
		const FSkeletalMeshLODRenderData& Lod = RenderData->LODRenderData[0];
		const FRawStaticIndexBuffer16or32Interface* IndexBuffer = Lod.MultiSizeIndexContainer.IsIndexBufferValid() ? Lod.MultiSizeIndexContainer.GetIndexBuffer() : nullptr;
		Data->IndexBuffer = IndexBuffer ? IndexBuffer->IndexBufferRHI : nullptr;
		if (!Data->IndexBuffer || !ReadVertexBuffers(Lod.StaticVertexBuffers.PositionVertexBuffer, Lod.StaticVertexBuffers.StaticMeshVertexBuffer, *Data))
		{
			return Abandon(TEXT("skeletal mesh buffers not on the GPU yet"));
		}
		for (const FSkelMeshRenderSection& Section : Lod.RenderSections)
		{
			FModelSection& Out = Data->Sections.AddDefaulted_GetRef();
			Out.FirstIndex = Section.BaseIndex;
			Out.NumTriangles = Section.NumTriangles;
			const UMaterialInterface* Material = Materials.IsValidIndex(Section.MaterialIndex) ? Materials[Section.MaterialIndex].Get() : nullptr;
			if (!Material && SkeletalMesh->GetMaterials().IsValidIndex(Section.MaterialIndex))
			{
				Material = SkeletalMesh->GetMaterials()[Section.MaterialIndex].MaterialInterface;
			}
			DescribeMaterial(Material, DiffuseParameter, TintParameter, SecondTintParameter, Out);
		}
		const FBoxSphereBounds Bounds = SkeletalMesh->GetBounds();
		Data->BoundsOrigin = FVector3f(Bounds.Origin);
		Data->BoundsRadius = static_cast<float>(Bounds.SphereRadius);
		Data->BoundsExtent = FVector3f(Bounds.BoxExtent);
	}
	else
	{
		return Abandon(Mesh ? TEXT("not a static or skeletal mesh") : TEXT("no mesh"));
	}

	// A section past the end of the buffer would assert in the RHI draw.
	const uint32 IndexCount = Data->IndexBuffer->GetStride() > 0 ? Data->IndexBuffer->GetSize() / Data->IndexBuffer->GetStride() : 0;
	for (int32 SectionIndex = Data->Sections.Num() - 1; SectionIndex >= 0; --SectionIndex)
	{
		const FModelSection& Section = Data->Sections[SectionIndex];
		if (Section.FirstIndex + Section.NumTriangles * 3 > IndexCount)
		{
			UE_LOG(LogTemp, Warning, TEXT("UModelWidget: %s section %d spans indices %u..%u of %u — skipped"),
				*Mesh->GetName(), SectionIndex, Section.FirstIndex, Section.FirstIndex + Section.NumTriangles * 3, IndexCount);
			Data->Sections.RemoveAt(SectionIndex);
		}
	}

	if (ModelWidgetDebugEnabled() && Data->IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("ModelWidget snapshot of %s: %d section(s), %u vertices, %u indices"), *Mesh->GetName(), Data->Sections.Num(), Data->NumVertices, IndexCount);
	}
	return Data->IsValid() ? Data : Abandon(TEXT("no sections"));
}

void UModelWidget::SetYaw(float InYaw)
{
	if (SlateWidget)
	{
		SlateWidget->SetYaw(InYaw);
	}
}

float UModelWidget::GetYaw() const
{
	return SlateWidget ? SlateWidget->GetYaw() : 0.f;
}

void UModelWidget::SetViewRotation(FRotator InRotation)
{
	ViewRotation = InRotation;
	ViewRotation.Pitch = FMath::Clamp(ViewRotation.Pitch, -85.f, 85.f);
	if (SlateWidget)
	{
		SlateWidget->SetViewRotation(ViewRotation);
	}
}
