#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGTerminalComponent.generated.h"

/**
 * What kind of terminal an actor's template is — read from the actual
 * gameObjectType field every SharedObjectTemplate carries (retail's own
 * SceneObjectType enum, values 0x4000-0x40xx for the terminal family), not
 * guessed from the template's file name. See FSWGTerminalSpawnHandler, which
 * classifies and attaches this; add a case there (not just here) for a new
 * terminal kind.
 */
UENUM(BlueprintType)
enum class ESWGTerminalType : uint8
{
	/** In the terminal family (gameObjectType 0x4000-0x40xx) but not one anything acts on yet — a city or guild terminal, say. */
	Other,
	Mission,
	Bazaar,
	Bank,
};

/**
 * Marks an ASWGItem as a terminal (gameObjectType in the 0x4000-0x40xx
 * range) and says which kind. FSWGTerminalSpawnHandler attaches this once,
 * at spawn time — so anything that needs to know "is this a mission
 * terminal" (USWGRadialMenuSubsystem::IsMissionTerminal) checks this
 * component instead of re-resolving the template on every call.
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGTerminalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Terminal")
	ESWGTerminalType TerminalType = ESWGTerminalType::Other;

	/** The template's raw gameObjectType value (SceneObjectType.h), for anything that wants a kind this enum doesn't cover yet. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Terminal")
	int32 GameObjectType = 0;
};
