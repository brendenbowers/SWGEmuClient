#include "Subsystems/SWGEffectsBridgeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Common/SWGSatLoopSource.h"
#include "SWGEffectsSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"

bool USWGEffectsBridgeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void USWGEffectsBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	USWGEffectsSubsystem* Effects = Collection.InitializeDependency<USWGEffectsSubsystem>();
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!Effects || !MeshGenerator)
	{
		return;
	}

	TWeakObjectPtr<USWGMeshGeneratorSubsystem> WeakGenerator(MeshGenerator);
	TWeakObjectPtr<USWGTerrainSubsystem> WeakTerrain(GameInstance->GetSubsystem<USWGTerrainSubsystem>());
	Effects->SetTerrainHeightProvider([WeakTerrain](const FVector& Position, float& Height)
	{
		USWGTerrainSubsystem* Terrain = WeakTerrain.Get();
		if (!Terrain || !Terrain->IsTerrainLoaded()) return false;
		// Sim Y is up; the terrain sampler takes raw X east / Y north, metres.
		// shortcut: Terrain height only; add swept/floor collision when effects need building contacts.
		Height = Terrain->GetHeightAt(Position.X, Position.Z);
		return FMath::IsFinite(Height);
	});
	Effects->SetStaticMeshProvider([WeakGenerator](const FString& AppearancePath, TFunction<void(UStaticMesh*, const TArray<UMaterialInterface*>&)> OnComplete)
	{
		if (USWGMeshGeneratorSubsystem* Generator = WeakGenerator.Get())
		{
			Generator->RequestAppearanceMesh(AppearancePath, MoveTemp(OnComplete));
		}
		else
		{
			OnComplete(nullptr, TArray<UMaterialInterface*>());
		}
	});

	// Ambient animals (pt_ambient_*.sat) animate their .sat root loop, as flags do.
	Effects->SetSkeletalMeshProvider([WeakGenerator](const FString& SatPath, USkeletalMeshComponent& Component, TFunction<void(bool)> OnReady)
	{
		USWGMeshGeneratorSubsystem* Generator = WeakGenerator.Get();
		FSWGSatLoopSource Source;
		if (!Generator || !Generator->ResolveSatLoopSource(SatPath, Source))
		{
			OnReady(false);
			return;
		}
		const FString Clip = Source.ClipPaths.IsEmpty() ? FString() : Source.ClipPaths[0];
		Generator->PlayLoopOnComponent(Component, Source, Clip, TMap<FString, int32>(), MoveTemp(OnReady));
	});
}
