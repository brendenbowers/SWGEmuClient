#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGPerformanceComponent.generated.h"

struct FSWGPacket;

/** CREO base6 — entertainer dance/music performance state. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMU_API USWGPerformanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGPerformanceComponent();

	FString PerformanceAnimation;
	FString MoodString;
	uint8   MoodId = 0;
	int32   PerformanceStartTime = 0;
	int32   PerformanceType = 0;
	bool bHasBase6 = false;

	// Split: PerformanceAnimation/MoodString come early in CREO base6; MoodId/
	// PerformanceStartTime/PerformanceType come much later (after TargetId) —
	// see SWGCreatureBaselineParser::ParseBase6.
	void ApplyBase6Part1(FSWGPacket& Packet); // PerformanceAnimation, MoodString
	void ApplyBase6Part2(FSWGPacket& Packet); // MoodId, PerformanceStartTime, PerformanceType
};
