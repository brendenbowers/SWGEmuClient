#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGForceComponent.generated.h"

/** PLAY base8/9 — force pool and jedi progression. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGForceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGForceComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 ForcePower = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 ForcePowerMax = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 JediState = 0;

	bool bHasBase8 = false;
	bool bHasBase9 = false;

	void ApplyBase8(const FPlayerObjectBaseline& Baseline);
	void ApplyBase9(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta8(const FPlayerObjectDelta& Delta);
	void ApplyDelta9(const FPlayerObjectDelta& Delta);
};
