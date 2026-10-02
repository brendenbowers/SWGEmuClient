#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TRE/SWGFootprintReader.h"
#include "SWGStructurePlacementSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGCommandSubsystem;
class USWGTreSubsystem;
class USWGMeshGeneratorSubsystem;
class USWGTerrainSubsystem;
class USWGObjectGraphSubsystem;
class ASWGStructurePlacementPreview;
struct FSWGNetMessage;

UENUM(BlueprintType)
enum class ESWGPlacementVerdict : uint8 { Invalid, Uncertain, Valid };

USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGPlacementValidation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="SWGEmu|Placement")
	ESWGPlacementVerdict Verdict = ESWGPlacementVerdict::Invalid;

	UPROPERTY(BlueprintReadOnly, Category="SWGEmu|Placement")
	FString ReasonStringId;

	UPROPERTY(BlueprintReadOnly, Category="SWGEmu|Placement")
	FString ReasonParam;
};

/** A nearby object that can block placement: its server-math footprint rect and/or no-build radius. */
struct FSWGPlacementNeighbour
{
	TWeakObjectPtr<const AActor> Actor;
	FString Path;
	FVector2D Center = FVector2D::ZeroVector;
	FBox2D Rect = FBox2D(ForceInit);
	float DistanceSq = 0.f;
	float NoBuildRadius = 0.f;
	bool bHasRect = false;
	bool bBlocking = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnPlacementChanged);

/** Placement session opened by EnterStructurePlacementMode; a cancelled session sends nothing. */
UCLASS()
class SWGEMUCLIENT_API USWGStructurePlacementSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return IsActive() || !PendingMarkers.IsEmpty(); }

	UPROPERTY(BlueprintAssignable, Category="SWGEmu|Placement") FSWGOnPlacementChanged OnPlacementStarted;
	UPROPERTY(BlueprintAssignable, Category="SWGEmu|Placement") FSWGOnPlacementChanged OnPlacementUpdated;
	UPROPERTY(BlueprintAssignable, Category="SWGEmu|Placement") FSWGOnPlacementChanged OnPlacementEnded;

	bool BeginFromServer(uint64 InDeedId, const FString& InTemplatePath);
	bool BeginFake(const FString& InTemplatePath);
	UFUNCTION(BlueprintCallable, Category="SWGEmu|Placement") void SetCandidate(FVector2D Raw);
	UFUNCTION(BlueprintCallable, Category="SWGEmu|Placement") void Rotate(int32 Steps);
	UFUNCTION(BlueprintCallable, Category="SWGEmu|Placement") bool Confirm();
	bool ConfirmWithProjector(AActor* Projector);
	UFUNCTION(BlueprintCallable, Category="SWGEmu|Placement") void Cancel();
	UFUNCTION(BlueprintPure, Category="SWGEmu|Placement") bool IsActive() const { return DeedId != 0; }
	UFUNCTION(BlueprintPure, Category="SWGEmu|Placement") FVector2D GetCandidate() const { return Candidate; }
	UFUNCTION(BlueprintPure, Category="SWGEmu|Placement") int32 GetRotation() const { return Rotation; }
	UFUNCTION(BlueprintPure, Category="SWGEmu|Placement") FSWGPlacementValidation GetValidation() const { return Validation; }
	const FSWGStructureFootprint& GetFootprint() const { return Footprint; }
	const FString& GetTemplatePath() const { return TemplatePath; }
	const ASWGStructurePlacementPreview* GetPreview() const { return Preview; }
	ASWGStructurePlacementPreview* GetPreview() { return Preview; }
	/** Hides the world preview while the holo map shows the placement. */
	void SetPreviewHidden(bool bHidden);
	float GetClearFloraRadius() const { return ClearFloraRadius; }
	bool GetSnapToTerrain() const { return bSnapToTerrain; }
	const TArray<FSWGPlacementNeighbour>& GetNeighbours() const { return Neighbours; }

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	void Validate();
	void CollectNeighbours();
	void TickPendingMarkers(float DeltaTime);
	void ClearPendingMarkers();
	void HandleObjectReady(int64 ObjectId);
	FString NeighbourName(const FSWGPlacementNeighbour& Neighbour) const;
	void LoadPlanetRules();
	const FSWGStructureFootprint* FootprintForTemplate(const FString& Path);
	void HandleObjectDestroyed(int64 ObjectId);

	UPROPERTY() TObjectPtr<USWGNetworkSubsystem> Network;
	UPROPERTY() TObjectPtr<USWGCommandSubsystem> Commands;
	UPROPERTY() TObjectPtr<USWGTreSubsystem> Tre;
	UPROPERTY() TObjectPtr<USWGMeshGeneratorSubsystem> Meshes;
	UPROPERTY() TObjectPtr<USWGTerrainSubsystem> Terrain;
	UPROPERTY() TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;
	UPROPERTY() TObjectPtr<ASWGStructurePlacementPreview> Preview;
	FDelegateHandle MessageHandle;
	FDelegateHandle ObjectDestroyedHandle;
	uint64 DeedId = 0;
	bool bFake = false;
	bool bFakeGrounded = false;
	bool bPlanetKnown = false;
	bool bPlanetAllowed = false;
	bool bRuleDataReady = false;
	FString TemplatePath;
	FSWGStructureFootprint Footprint;
	float ClearFloraRadius = 0.f;
	bool bSnapToTerrain = false;
	FVector2D Candidate = FVector2D::ZeroVector;
	int32 Rotation = 0;
	FSWGPlacementValidation Validation;
	FString RulesPlanet;
	TArray<FVector2D> ClientPois;
	struct FCityCircle { FVector2D Center; float Radius = 0; FString Name; };
	TArray<FCityCircle> StaticCities;
	TMap<FString, float> NoBuildRadii;
	TMap<FString, FSWGStructureFootprint> FootprintsByTemplate;
	TArray<FSWGPlacementNeighbour> Neighbours;

	/** A confirmed placement whose footprint stays on screen until the server has built the structure. */
	struct FPendingMarker
	{
		TWeakObjectPtr<ASWGStructurePlacementPreview> Marker;
		TWeakObjectPtr<AActor> Projector;
		TWeakObjectPtr<AActor> Barricade;
		FVector2D Center = FVector2D::ZeroVector;
		FString TemplatePath;
		float Elapsed = 0.f;
		float PollTimer = 0.f;
		float PrintElapsed = 0.f;
		// ponytail: Core3 uses lots * 3 s, but lot size is absent from the client template and placement message. Use a generic reveal until the finished structure arrives.
		float PrintDuration = 6.f;
		bool bConstructionSeen = false;
	};
	TArray<FPendingMarker> PendingMarkers;

	/** Footprint areas for construction barricades in view (templates flagged useStructureFootprintOutline), keyed by object id. */
	TMap<int64, TWeakObjectPtr<ASWGStructurePlacementPreview>> ConstructionOutlines;
	FDelegateHandle ObjectReadyHandle;
};
