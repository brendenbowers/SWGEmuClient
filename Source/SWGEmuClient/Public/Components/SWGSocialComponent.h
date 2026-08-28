#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGSocialComponent.generated.h"

/**
 * PLAY base9 — friends, ignore list and spoken language. Core3 sends the two
 * lists as empty placeholders in the baseline and only ever fills them by
 * delta, so an empty list here doesn't mean the player has none.
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGSocialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGSocialComponent();

	TSWGBaselineList<FString> FriendsList;
	TSWGBaselineList<FString> IgnoreList;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 LanguageId = 0;

	bool bHasBase9 = false;

	void ApplyBase9(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta9(const FPlayerObjectDelta& Delta);
};
