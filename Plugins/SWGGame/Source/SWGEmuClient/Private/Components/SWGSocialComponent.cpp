#include "Components/SWGSocialComponent.h"

USWGSocialComponent::USWGSocialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSocialComponent::ApplyBase9(const FPlayerObjectBaseline& Baseline)
{
	LanguageId = Baseline.LanguageId;
	bHasBase9 = true;
}

void USWGSocialComponent::ApplyDelta9(const FPlayerObjectDelta& Delta)
{
	if (Delta.LanguageId.IsSet()) { LanguageId = *Delta.LanguageId; }

	ApplyIndexedListChanges(Delta.FriendsList, FriendsList);
	ApplyIndexedListChanges(Delta.IgnoreList, IgnoreList);
}
