#pragma once

#include "CoreMinimal.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGPlayer.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
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
class SWGEMUCLIENT_API ASWGPlayer : public ASWGCreature
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

	// Switches to MOVE_Walking on possession now that terrain has real
	// collision (USWGTerrainSubsystem) — the default pre-possession movement
	// mode is whatever ACharacter starts with, which isn't guaranteed to be
	// Walking.
	virtual void PossessedBy(AController* NewController) override;

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);

	// Mouse-look, bound directly to the legacy raw MouseX/MouseY axis keys
	// rather than through an Enhanced Input action — see the .cpp's
	// SetupPlayerInputComponent comment for why.
	void LookMouseX(float Value);
	void LookMouseY(float Value);

	// Holding RMB turns the character to face the camera instead of the
	// default auto-face-movement behavior (bOrientRotationToMovement) —
	// released, it reverts back. Implemented manually in Tick() rather than
	// via bUseControllerRotationYaw because the actor's yaw needs the same
	// -90 correction CameraBoom's own alignment requires (see the .cpp's
	// PossessedBy comment) — the engine's built-in controller-rotation-yaw
	// behavior has no way to inject that offset.
	void OnRightMouseButtonPressed();
	void OnRightMouseButtonReleased();

	// Mouse wheel zooms the camera by adjusting CameraBoom's TargetArmLength,
	// clamped so it can't zoom through the character or out to absurd range.
	void OnMouseWheel(float Value);

	// Sends the current position/orientation to the server as a
	// FDataTransformMessage, throttled by Tick — see the .cpp for the
	// send-rate/stop-detection reasoning.
	void SendDataTransformUpdate();

	// Third-person: a spring arm holding the camera behind/above the
	// character, orbiting freely around it with mouse look
	// (bUsePawnControlRotation) independent of which way the body currently
	// faces, and doing its own collision test so it doesn't clip through
	// walls/terrain.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	// IA_Move is mapped to both WASD and the left gamepad stick by IMC_Default.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	// Installed on runtime possession so input does not depend on a particular
	// PlayerController Blueprint having populated its mapping-context arrays.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

private:
	float TimeSinceLastTransformSend = 0.0f;
	int32 TransformMovementCounter = 0;
	bool bWasMovingLastSend = false;
	bool bIsTurningToCamera = false;
};
