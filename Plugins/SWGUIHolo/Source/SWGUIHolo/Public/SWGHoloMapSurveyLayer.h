#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/SWGSurveySubsystem.h"
#include "SWGHoloMapSurveyLayer.generated.h"

class UDynamicMeshComponent;

/** Survey concentration bands attached to a holo map's baked terrain. */
UCLASS()
class SWGUIHOLO_API USWGHoloMapSurveyLayer : public UActorComponent
{
	GENERATED_BODY()

public:
	void SetSurveyField(const FSWGSurveyResult& Result);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	void RebuildSurveyField();
	UPROPERTY() TArray<TObjectPtr<UDynamicMeshComponent>> SurveyBands;
	FSWGSurveyResult SurveyField;
};
