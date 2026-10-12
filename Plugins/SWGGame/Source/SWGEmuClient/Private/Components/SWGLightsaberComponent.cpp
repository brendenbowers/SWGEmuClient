#include "Components/SWGLightsaberComponent.h"

#include "Common/SWGWorldScale.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SWGAudioSubsystem.h"
#include "SWGEffectsSubsystem.h"
#include "Subsystems/SWGEffectSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

using namespace UE::Geometry;

namespace
{
	// Cycle rate of the blade's colour frames when the shader switch names none (lightsaberblade.sht: 1/16 s).
	constexpr float DefaultFrameSeconds = 0.0625f;

	// Glow light strength in candela at full extension; the .lsb gives colour and range only.
	constexpr float GlowCandela = 400.f;
	constexpr float FlickerFloor = 0.65f;
	constexpr float DefaultFlickerSeconds = 0.06f;

	/** Two crossed unit quads: width +-0.5 around the axis, length 0..1 along +Z, UV0 (along, across: the blade textures run their length along U), UV1.x = 1. */
	FDynamicMesh3 BuildBladeMesh()
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableAttributes();
		Mesh.Attributes()->SetNumUVLayers(2);
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		FDynamicMeshUVOverlay* AcrossUVs = Mesh.Attributes()->GetUVLayer(0);
		FDynamicMeshUVOverlay* FadeUVs = Mesh.Attributes()->GetUVLayer(1);

		for (int32 Plane = 0; Plane < 2; ++Plane)
		{
			const FVector Side = Plane == 0 ? FVector::XAxisVector : FVector::YAxisVector;
			int32 Corner[4];
			int32 NormalId[4], AcrossId[4], FadeId[4];
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const float Across = (Index & 1) ? 0.5f : -0.5f;
				const float Along = (Index & 2) ? 1.f : 0.f;
				Corner[Index] = Mesh.AppendVertex(Side * Across + FVector::ZAxisVector * Along);
				NormalId[Index] = Normals->AppendElement(FVector3f::UpVector);
				AcrossId[Index] = AcrossUVs->AppendElement(FVector2f(Along, Across + 0.5f));
				FadeId[Index] = FadeUVs->AppendElement(FVector2f(1.f, 0.f));
			}
			for (const FIndex3i& Triangle : { FIndex3i(0, 1, 2), FIndex3i(1, 3, 2) })
			{
				const int32 TriangleId = Mesh.AppendTriangle(Corner[Triangle.A], Corner[Triangle.B], Corner[Triangle.C]);
				if (TriangleId >= 0)
				{
					Normals->SetTriangle(TriangleId, FIndex3i(NormalId[Triangle.A], NormalId[Triangle.B], NormalId[Triangle.C]));
					AcrossUVs->SetTriangle(TriangleId, FIndex3i(AcrossId[Triangle.A], AcrossId[Triangle.B], AcrossId[Triangle.C]));
					FadeUVs->SetTriangle(TriangleId, FIndex3i(FadeId[Triangle.A], FadeId[Triangle.B], FadeId[Triangle.C]));
				}
			}
		}
		return Mesh;
	}
}

USWGLightsaberComponent::USWGLightsaberComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

float USWGLightsaberComponent::ComputeBladeFraction(float ElapsedSeconds, float DurationSeconds, bool bInClosing)
{
	const float Progress = DurationSeconds > 0.f ? FMath::Clamp(ElapsedSeconds / DurationSeconds, 0.f, 1.f) : 1.f;
	return bInClosing ? 1.f - Progress : Progress;
}

float USWGLightsaberComponent::ComputeFlickerScale(float RandomUnit, float MinScale)
{
	return FMath::Lerp(MinScale, 1.f, FMath::Clamp(RandomUnit, 0.f, 1.f));
}

bool USWGLightsaberComponent::Setup(const FString& LsbPath, const TArray<FSWGSaberBladeAxis>& Axes, const FSWGSaberStyle& InStyle)
{
	UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (!Tre || !FSWGLightsaberReader::Read(Tre->CreateIffReader(LsbPath), Saber) || Saber.Blades.IsEmpty())
	{
		return false;
	}
	Style = InStyle;
	BladeAxes = Axes;
	if (BladeAxes.IsEmpty())
	{
		BladeAxes.Add(FSWGSaberBladeAxis());
	}

	// Every blade of a hilt shares the first one's shader, so the frame materials are built once.
	USWGEffectsSubsystem* Effects = World->GetSubsystem<USWGEffectsSubsystem>();
	TArray<UMaterialInterface*> SharedFrames;
	if (Effects)
	{
		float SecondsPerFrame = 0.f;
		Effects->GetRibbonMaterials(Saber.Blades[0].ShaderPath, SharedFrames, SecondsPerFrame);
		FrameSeconds = SecondsPerFrame > 0.f ? SecondsPerFrame : DefaultFrameSeconds;
	}
	for (UMaterialInterface* Shared : SharedFrames)
	{
		UMaterialInstanceDynamic* Frame = UMaterialInstanceDynamic::Create(Shared, this);
		Frame->SetVectorParameterValue(TEXT("Tint"), Style.Tint);
		FrameMaterials.Add(Frame);
	}

	for (int32 BladeIndex = 0; BladeIndex < Saber.Blades.Num(); ++BladeIndex)
	{
		const FSWGLightsaberBlade& Blade = Saber.Blades[BladeIndex];
		const FSWGSaberBladeAxis& Axis = BladeAxes.IsValidIndex(BladeIndex) ? BladeAxes[BladeIndex] : BladeAxes[0];

		UDynamicMeshComponent* BladeMesh = NewObject<UDynamicMeshComponent>(GetOwner());
		BladeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BladeMesh->SetCastShadow(false);
		BladeMesh->SetMesh(BuildBladeMesh());
		BladeMesh->SetRelativeRotation(FRotationMatrix::MakeFromZ(Axis.Direction).ToQuat());
		BladeMesh->SetRelativeLocation(Axis.Base);
		BladeMesh->SetRelativeScale3D(FVector(SWGToUnrealSpace(Blade.Width), SWGToUnrealSpace(Blade.Width), 0.01f));
		if (!FrameMaterials.IsEmpty())
		{
			BladeMesh->SetMaterial(0, FrameMaterials[0]);
		}
		BladeMesh->SetVisibility(false);
		BladeMesh->RegisterComponent();
		BladeMesh->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		Blades.Add(BladeMesh);
	}

	if (Saber.bHasLight && Saber.Light.RangeMax > 0.f)
	{
		GlowLight = NewObject<UPointLightComponent>(GetOwner());
		GlowLight->SetLightColor(Style.Tint);
		GlowLight->SetAttenuationRadius(SWGToUnrealSpace(Saber.Light.RangeMax));
		GlowLight->SetCastShadows(false);
		GlowLight->SetIntensity(0.f);
		GlowLight->RegisterComponent();
		GlowLight->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		GlowLight->SetRelativeLocation(BladeAxes[0].Base + BladeAxes[0].Direction * SWGToUnrealSpace(Saber.Blades[0].Length) * 0.5f);
	}

	StartHum();
	PlayStyleEffect(Style.BladeOnEffect);
	ApplyFraction();
	return true;
}

void USWGLightsaberComponent::Retract()
{
	if (bClosing)
	{
		return;
	}
	bClosing = true;
	Elapsed = 0.f;
	PlayStyleEffect(Style.BladeOffEffect);
}

float USWGLightsaberComponent::GetCloseSeconds() const
{
	float Seconds = 0.f;
	for (const FSWGLightsaberBlade& Blade : Saber.Blades)
	{
		Seconds = FMath::Max(Seconds, Blade.CloseSeconds);
	}
	return Seconds;
}

void USWGLightsaberComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Saber.Blades.IsEmpty())
	{
		return;
	}

	Elapsed += DeltaTime;
	const FSWGLightsaberBlade& Lead = Saber.Blades[0];
	Fraction = ComputeBladeFraction(Elapsed, bClosing ? Lead.CloseSeconds : Lead.OpenSeconds, bClosing);

	NextFlickerSeconds -= DeltaTime;
	if (NextFlickerSeconds <= 0.f)
	{
		FlickerScale = ComputeFlickerScale(FMath::FRand(), FlickerFloor);
		const float Low = Saber.Light.TimeMin > 0.f ? Saber.Light.TimeMin : DefaultFlickerSeconds;
		const float High = FMath::Max(Saber.Light.TimeMax, Low);
		NextFlickerSeconds = FMath::FRandRange(Low, High);
	}

	if (FrameMaterials.Num() > 1 && GetWorld())
	{
		const int32 Frame = FMath::FloorToInt32(GetWorld()->GetTimeSeconds() / FrameSeconds) % FrameMaterials.Num();
		if (Frame != ShownFrame)
		{
			ShownFrame = Frame;
			for (UDynamicMeshComponent* Blade : Blades)
			{
				Blade->SetMaterial(0, FrameMaterials[Frame]);
			}
		}
	}

	ApplyFraction();
	if (bClosing && Fraction <= 0.f)
	{
		DestroyComponent();
	}
}

void USWGLightsaberComponent::ApplyFraction()
{
	for (int32 BladeIndex = 0; BladeIndex < Blades.Num(); ++BladeIndex)
	{
		const FSWGLightsaberBlade& Blade = Saber.Blades[BladeIndex];
		UDynamicMeshComponent* BladeMesh = Blades[BladeIndex];
		const float LengthCm = SWGToUnrealSpace(Blade.Length) * Fraction;
		BladeMesh->SetRelativeScale3D(FVector(SWGToUnrealSpace(Blade.Width), SWGToUnrealSpace(Blade.Width), FMath::Max(LengthCm, 0.01f)));
		BladeMesh->SetVisibility(Fraction > 0.001f);
	}
	if (GlowLight)
	{
		GlowLight->SetIntensity(GlowCandela * Fraction * FlickerScale);
	}
}

void USWGLightsaberComponent::StartHum()
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	USWGAudioSubsystem* Audio = GameInstance ? GameInstance->GetSubsystem<USWGAudioSubsystem>() : nullptr;
	if (Audio && !Saber.HumSoundPath.IsEmpty())
	{
		Hum = Audio->PlaySoundAttached(Saber.HumSoundPath, this);
	}
}

void USWGLightsaberComponent::StopHum()
{
	Hum.Stop(0.2f);
	Hum = FSWGSoundHandle();
}

void USWGLightsaberComponent::PlayStyleEffect(const FString& Effect)
{
	if (Effect.IsEmpty())
	{
		return;
	}
	if (USWGEffectSubsystem* Effects = USWGEffectSubsystem::Get(this))
	{
		Effects->PlayClientEffectAttached(Effect, this);
	}
}

void USWGLightsaberComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	StopHum();
	for (UDynamicMeshComponent* Blade : Blades)
	{
		if (Blade)
		{
			Blade->DestroyComponent();
		}
	}
	Blades.Reset();
	if (GlowLight)
	{
		GlowLight->DestroyComponent();
		GlowLight = nullptr;
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
