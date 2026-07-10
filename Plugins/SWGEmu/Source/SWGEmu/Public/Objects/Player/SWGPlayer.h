#pragma once

#include "CoreMinimal.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGPlayer.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
struct FInputActionValue;

/**
 * The local player's own CREO. Wire-wise it's spawned from the same SCOT
 * template class as any other creature (see world-object-plan.html
 * "Template FORM types"), so the CRC->actor-class map alone can't tell it
 * apart from an NPC — spawning this subclass for the player specifically
 * (instead of plain ASWGCreature) is a special case the object graph
 * subsystem will need once it knows which ObjectId is "us" (from character
 * select / zone-in), not yet wired up.
 *
 * Exists as a distinct place for PLAY-layer state (profile, quests,
 * abilities, vitals, presence — deferred per the "moveable player with
 * health" milestone scope) to attach later as components, without every NPC
 * ASWGCreature carrying player-only data it'll never use.
 *
 * Also owns the first-person camera and movement input — this is the actor
 * USWGObjectGraphSubsystem::HandleSceneEndBaselines possesses once the local
 * player's own CREO is revealed, replacing the editor's default free-fly
 * pawn. Movement is client-authoritative here (standard UCharacterMovementComponent,
 * same as any UE character): input moves the capsule locally and immediately,
 * then Tick reports the resulting position/orientation to the server via
 * periodic FDataTransformMessage sends — matching the same wire format
 * FSWGInWorldState::Enter's one-shot "stationary" report already uses.
 */
UCLASS()
class SWGEMU_API ASWGPlayer : public ASWGCreature
{
	GENERATED_BODY()

public:
	ASWGPlayer(const FObjectInitializer& ObjectInitializer);

	// Temporary diagnostic: the local player's ASWGPlayer instance has been
	// observed to vanish (no longer resolvable in the world) minutes after a
	// successful spawn/reveal, with zero explanation anywhere else in the
	// log — no crash, no relogin/zone-reload, no explicit Destroy() call
	// traced. Logging every EndPlay reason here should catch which of those
	// (if any) is actually happening, since nothing else does.
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void Tick(float DeltaTime) override;

	// Re-derives CameraBoom's height above the capsule from the capsule's
	// *current* size. Called once in the constructor (default 88 half-height,
	// before any mesh exists) and again by USWGMeshGeneratorSubsystem once
	// this player's real mesh has resized the capsule to match — otherwise
	// the camera stays permanently based on the default human-sized capsule
	// even for a differently-sized character.
	void UpdateCameraHeight();

	// Terrain has no collision yet (USWGTerrainSubsystem's landscape renders
	// but collision generation is a separate deferred task), so MOVE_Walking
	// would free-fall forever. Switch to Flying on possession as a stand-in
	// until real terrain collision exists.
	virtual void PossessedBy(AController* NewController) override;

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	// Sends the current position/orientation to the server as a
	// FDataTransformMessage, throttled by Tick — see the .cpp for the
	// send-rate/stop-detection reasoning.
	void SendDataTransformUpdate();

	// Third-person: a spring arm holding the camera behind/above the
	// character, orbiting with mouse look (bUsePawnControlRotation) and doing
	// its own collision test so it doesn't clip through walls/terrain.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	// Same Enhanced Input actions ASWGEmuClientCharacter's third-person
	// Blueprint uses (/Game/Input/Actions/IA_Move, IA_Look) — the project's
	// existing IMC_Default/IMC_MouseLook mapping contexts already route
	// WASD/mouse to these, so binding the same assets here means input keeps
	// working across the swap from the default free-fly pawn to this one
	// without touching any input assets.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

private:
	float TimeSinceLastTransformSend = 0.0f;
	int32 TransformMovementCounter = 0;
	bool bWasMovingLastSend = false;
};
