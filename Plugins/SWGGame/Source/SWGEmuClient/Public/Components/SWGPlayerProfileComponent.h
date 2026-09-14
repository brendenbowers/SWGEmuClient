#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGPlayerProfileComponent.generated.h"

/**
 * PLAY base3/6 — account-level identity and flags. Every player in range sends
 * these two slots, so this lands on other players' creatures as well as the
 * local one (slots 8/9 are owner-only and live on ASWGPlayer's components).
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGPlayerProfileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGPlayerProfileComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString Title;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 BirthDate = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 TotalPlayedTime = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 Status = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	uint8 PrivilegeFlag = 0;

	TArray<uint32> PlayerBitmasks;

	bool bHasBase3 = false;
	bool bHasBase6 = false;

	/**
	 * The component on Actor, created and registered if it has none. Remote
	 * players are plain ASWGCreatures, which don't carry one until their player
	 * object actually turns up.
	 */
	static USWGPlayerProfileComponent* FindOrAdd(AActor& Actor);

	void ApplyBase3(const FPlayerObjectBaseline& Baseline);
	void ApplyBase6(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta3(const FPlayerObjectDelta& Delta);
	void ApplyDelta6(const FPlayerObjectDelta& Delta);
};
