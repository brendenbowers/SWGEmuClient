#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TRE/SWGAsteroidFieldReader.h"
#include "SWGSpaceDustComponent.generated.h"

struct FSWGSpaceTerrainData;
class ADirectionalLight;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class USWGMeshGeneratorSubsystem;
class USWGTreSubsystem;

/** Retail space dust as camera-facing streak quads, plus a sparse layer of tumbling debris inside the zone's asteroid fields. Owned by the sky actor. */
UCLASS(ClassGroup = "SWGEmu")
class SWGEMUCLIENT_API USWGSpaceDustComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** One tumbling rock. Position is absolute space; InstanceIndex is INDEX_NONE until its mesh has loaded. */
	struct FDebrisPiece
	{
		FVector Position = FVector::ZeroVector;
		FVector Drift = FVector::ZeroVector;
		FVector SpinAxis = FVector::UpVector;
		float SpinRate = 0;
		float Phase = 0;
		/** 0..1 draw between the active style's ScaleMin and ScaleMax. */
		float SizeRoll = 0;
		int32 MeshIndex = 0;
		int32 InstanceIndex = INDEX_NONE;
	};

	/** What a field's radial style table and row say about its near asteroids, already clamped to the budget. Ranges are world units around the camera. */
	struct FDebrisStyle
	{
		int32 Count = 0;
		double SpawnRange = DefaultDebrisRange;
		double DeleteRange = DefaultDebrisRange;
		/** Multiplier on the mesh's native size: the style rows' likelihood-weighted mean times the field row's. */
		float ScaleMin = 1;
		float ScaleMax = 1;
	};

	static constexpr int32 DefaultDebrisCount = 30;
	static constexpr int32 MaxDebrisCount = 96;
	static constexpr double DefaultDebrisRange = 10000.0;
	static constexpr double MinDebrisRange = 5000.0;
	static constexpr double MaxDebrisRange = 60000.0;
	/** Rock diameter limits in world units, whatever the native size and scale say. */
	static constexpr float MinDebrisSize = 100.f;
	static constexpr float MaxDebrisSize = 8000.f;

	/** Folds a field row and its style table into one style: count, ranges and scale clamped to the budget. An empty style table gives the defaults. */
	static FDebrisStyle MakeDebrisStyle(const FSWGAsteroidFieldData& Field, const TArray<FSWGAsteroidRadialStyleRow>& StyleRows);

	/** Creates the dust quads under the owner's root and seeds the particles and debris; no dust when the zone has none. False on a bad radius or asteroid table. */
	bool Setup(const FSWGSpaceTerrainData& Data, const FString& TerrainVirtualPath, USWGTreSubsystem* TreSubsystem, USWGMeshGeneratorSubsystem* MeshGenerator);

	/** Respawns particles that left the radius and rewrites every quad. Viewpoint is the camera in world space; Lights supply the glint direction. */
	void Update(const FVector& Viewpoint, const TArray<TObjectPtr<ADirectionalLight>>& Lights, float DeltaTime);

	/** Scales every streak, for flight code to lengthen them while boosting or jumping. 1 is retail's cruise streak. */
	void SetStreakBoost(float Boost);

	/** Streak boost for a ship at SpeedMeters: 1 up to MaxSpeed, a smoothstep up to MaxStreakBoost at BoostMaxSpeed. Without a known booster speed the ramp spans half of MaxSpeed; without MaxSpeed it stays 1. */
	static float ComputeStreakBoost(float SpeedMeters, float MaxSpeed, float BoostMaxSpeed);
	static constexpr float MaxStreakBoost = 3.f;

	/** The current streak scale: derived from the local ship's speed each update. */
	float GetStreakBoost() const { return StreakBoost; }

	/** World-space streak of the last update before StreakBoost: the owner's displacement capped at one native metre. */
	FVector GetStreakVector() const { return StreakWorld; }

	/** True when an absolute space position is inside one of the zone's asteroid fields. */
	bool IsInsideAsteroidField(const FVector& SpacePosition) const;

	/** Whether the debris layer is currently shown. */
	bool IsDebrisActive() const { return bDebrisActive; }

	/** Index in Fields of the first field containing a space position, or INDEX_NONE. */
	int32 FindAsteroidField(const FVector& SpacePosition) const;

	/** The style in force: the last field the camera was in, defaults before any. Rocks beyond Count are hidden. */
	const FDebrisStyle& GetActiveDebrisStyle() const { return ActiveStyle; }

	/** One style per entry of Fields, in the same order. */
	TArray<FDebrisStyle> FieldStyles;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Dust;

	/** One per debris mesh, created when the mesh has loaded. */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> DebrisMeshes;

	TArray<FSWGAsteroidFieldData> Fields;
	TArray<FDebrisPiece> Debris;
	FVector DebrisPreviousCamera = FVector::ZeroVector;

private:
	void UpdateDebris(const FTransform& Frame, const FVector& Camera, const FBox& ShipBounds, float DeltaTime);
	void ScatterDebris(const FVector& Camera, int32 FieldIndex);
	void OnDebrisMeshReady(int32 MeshIndex, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials);

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DustMaterial;
	TArray<FVector> Positions;
	/** Native diameter of each debris mesh in world units; 0 until it has loaded. */
	TArray<float> DebrisNativeSize;
	FDebrisStyle ActiveStyle;
	int32 ActiveField = INDEX_NONE;
	float DustRadius = 0;
	float StreakBoost = 1;
	FVector StreakWorld = FVector::ZeroVector;
	FVector PreviousCamera = FVector::ZeroVector;
	FVector PreviousOwner = FVector::ZeroVector;
	bool bHasPose = false;
	bool bDebrisActive = false;
	float DebrisTime = 0;
	int32 Generation = 0;
};
