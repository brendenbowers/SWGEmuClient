#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWGEffectsBridgeSubsystem.generated.h"

/** Hands SWGEffects the mesh generator (which it cannot see) so PTMH mesh particles can resolve .msh/.apt appearances and animate .sat ones. */
UCLASS()
class SWGEMUCLIENT_API USWGEffectsBridgeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
