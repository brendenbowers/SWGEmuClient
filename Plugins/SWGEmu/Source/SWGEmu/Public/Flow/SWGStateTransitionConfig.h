#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Flow/SWGClientState.h"
#include "CommonActivatableWidget.h"
#include "SWGStateTransitionConfig.generated.h"

USTRUCT(BlueprintType)
struct FSWGStateTransitionRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ESWGClientState OldState = ESWGClientState::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ESWGClientState NewState = ESWGClientState::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<UCommonActivatableWidget> WidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag LayerTag;  // Which layer to push to
};