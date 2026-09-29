#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGSurveySubsystem.generated.h"

class USWGCommandSubsystem;
class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGRadialMenuSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;

/** A resource spawn the survey tool in use can find. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGSurveyResource
{
	GENERATED_BODY()

	/** The spawn's name ("Aqabuo"), what the survey and sample commands take. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	int64 ObjectId = 0;

	/** resource_tree.iff ENUM ("copper_desh"). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	FString Type;

	/** The type's class name from resource_tree.iff ("Desh Copper"). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	FString ClassName;

	/** Ancestors from the root down, excluding ClassName ("Inorganic", "Mineral", "Metal", ...). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	TArray<FString> ClassPath;
};

USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGSurveySample
{
	GENERATED_BODY()

	/** Raw metres, x east, y north. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	FVector2D Position = FVector2D::ZeroVector;

	/** 0-1; the server shows it as a percentage. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	float Density = 0.f;
};

/** One survey's grid, as Core3 ResourceSpawner::sendSurvey laid it out. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGSurveyResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	FString ResourceName;

	/** Row-major, north row first, west to east within a row. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	TArray<FSWGSurveySample> Samples;

	/** Points per side (3-5). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	int32 GridSize = 0;

	/** Side length of the surveyed square in metres (the tool's range). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	float Range = 0.f;

	/** Where the player stood. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	FVector2D Center = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Survey")
	int32 BestIndex = INDEX_NONE;

	/** Core3 only drops a waypoint on the best point when it reaches this density. */
	static constexpr float WaypointDensity = 0.1f;

	bool IsValid() const { return Samples.IsValidIndex(BestIndex); }
	const FSWGSurveySample& GetBest() const { return Samples[BestIndex]; }

	/** Bilinear density at a raw point; 0 outside the grid. */
	float SampleDensity(const FVector2D& RawPosition) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnSurveyEvent);

/**
 * Owns the survey tool protocol. Using a tool (ObjectMenuSelect ITEM_USE)
 * makes Core3 send ResourceListForSurvey, already filtered to the tool's type;
 * that opens the survey window. RequestSurvey sends "requestsurvey <name>"
 * and ~3 s later the grid arrives as SurveyMessage, together with the server's
 * "Resource Survey" waypoint on the best point (Core3 SurveyTask) — the
 * waypoint reaches the client as an ordinary PLAY waypoint delta.
 */
UCLASS()
class SWGEMUCLIENT_API USWGSurveySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** A survey tool was used and its resource list arrived. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Survey")
	FSWGOnSurveyEvent OnSurveyWindowRequested;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Survey")
	FSWGOnSurveyEvent OnResourcesChanged;

	/** A survey was sent, answered or timed out. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Survey")
	FSWGOnSurveyEvent OnSurveyStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Survey")
	FSWGOnSurveyEvent OnSurveyResultReceived;

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	const TArray<FSWGSurveyResource>& GetResources() const { return Resources; }

	/** The tool template's surveyType ("mineral", "flora_resources", ...). */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	const FString& GetSurveyType() const { return SurveyType; }

	/** The survey type as a window heading ("Mineral Resources"). */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	FText GetSurveyTypeDisplayName() const;

	/** The tool last used, or 0 if the list arrived without a known use. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	int64 GetToolObjectId() const { return ToolObjectId; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	const FString& GetSelectedResource() const { return SelectedResource; }

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Survey")
	void SetSelectedResource(const FString& ResourceName);

	/** Surveys for the selected resource. False when nothing is selected or one is in flight. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Survey")
	bool RequestSurvey();

	/** Takes a core sample of the selected resource where the player stands. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Survey")
	bool RequestSample();

	/** Opens the server's tool range list (radial "Tool Options > Survey Range"). */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Survey")
	bool RequestRangeSettings();

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	bool IsSurveyPending() const { return bSurveyPending; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	bool HasResult() const { return LastResult.IsValid(); }

	/** Set by the UI while a survey window or hologram is up; maps show the last scan only then. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Survey")
	void SetToolActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	bool IsToolActive() const { return bToolActive; }

	/** The scan a map should draw: the last result while the tool is up. */
	bool ShouldShowScan() const { return bToolActive && LastResult.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	const FSWGSurveyResult& GetLastResult() const { return LastResult; }

	/** Largest range the player's "surveying" skill mod allows (Core3 SurveyTool::getSkillBasedRange); 0 if unknown. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	int32 GetSkillRange() const;

	/** The range last surveyed with, else the skill range. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Survey")
	int32 GetCurrentRange() const;

	/** Class name and ancestry for a resource_tree.iff ENUM; false if unknown. */
	bool ResolveResourceClass(const FString& Type, FString& OutClassName, TArray<FString>& OutClassPath);

	/** Feeds a list/result as if the server sent it, for UI work without a tool (swg.Survey.Fake). */
	void InjectResources(const TArray<FSWGSurveyResource>& InResources, const FString& InSurveyType);
	void InjectResult(const FSWGSurveyResult& Result);

	/** Builds a result from Core3's flat point list; exposed for tests. */
	static FSWGSurveyResult BuildResult(const FString& ResourceName, const TArray<FVector2D>& Positions, const TArray<float>& Densities);

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	void HandleServerOptionSelected(int64 ObjectId, int32 RadialId);
	void LoadResourceTree();
	bool IsSurveyTool(int64 ObjectId) const;
	void ClearPending();

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGRadialMenuSubsystem> Radial;

	FDelegateHandle MessageHandle;
	FDelegateHandle RadialHandle;
	FTimerHandle PendingTimeout;

	TArray<FSWGSurveyResource> Resources;
	FString SurveyType;
	FString SelectedResource;
	FString PendingResource;
	int64 ToolObjectId = 0;
	/** Last survey tool a Use was sent for, claimed when the list arrives. */
	int64 LastUsedToolId = 0;
	bool bSurveyPending = false;
	bool bToolActive = false;
	FSWGSurveyResult LastResult;

	/** ENUM -> (name, ancestry), loaded on first use. */
	struct FResourceClass
	{
		FString Name;
		TArray<FString> Path;
	};
	TMap<FString, FResourceClass> ResourceClasses;
	bool bTriedResourceTree = false;

	/** Core3 SurveyTask runs 3 s after the command; allow for queue and lag. */
	static constexpr float SurveyTimeoutSeconds = 12.f;
};
