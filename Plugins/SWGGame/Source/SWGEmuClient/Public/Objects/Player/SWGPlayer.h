#pragma once

#include "CoreMinimal.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGPlayer.generated.h"

class UCameraComponent;
class USWGPlayerProfileComponent;
class USWGExperienceComponent;
class USWGJournalComponent;
class USWGForceComponent;
class USWGCraftingComponent;
class USWGSocialComponent;
class USWGStomachComponent;
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

	/** Writes swg.SkylightLeaking into the follow camera's Lumen post-process settings. */
	void ApplySkylightLeaking();

	// Switches to MOVE_Walking on possession now that terrain has real
	// collision (USWGTerrainSubsystem) — the default pre-possession movement
	// mode is whatever ACharacter starts with, which isn't guaranteed to be
	// Walking.
	virtual void PossessedBy(AController* NewController) override;

	// Seeds the camera's starting orientation. This runs from ClientRestart,
	// which OnPossess calls *after* its own
	// SetControlRotation(Pawn->GetActorRotation()) — doing it in PossessedBy
	// instead is silently discarded, since the engine overwrites
	// ControlRotation one line after PossessedBy returns.
	virtual void PawnClientRestart() override;

	// Fired for the 1-9, 0, -, = action bar hotkeys with the slot index (0-11),
	// and for the gamepad's D-pad/face buttons with 0-7 (bank 0) or 8-15
	// (bank 1, after ActionBankShiftKey toggled). The HUD listens; the pawn never sees a widget.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnActionSlotHotkey, int32 /*SlotIndex*/);
	FOnActionSlotHotkey OnActionSlotHotkey;

	// Fired when ActionBankShiftKey toggles the bank, so the bar can highlight the live one.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnActionBankChanged, int32 /*BankIndex*/);
	FOnActionBankChanged OnActionBankChanged;

	int32 GetActiveActionBank() const { return bActionBankShifted ? 1 : 0; }

	// Fired by InventoryKey; the HUD opens or closes the inventory window.
	DECLARE_MULTICAST_DELEGATE(FOnToggleInventory);
	FOnToggleInventory OnToggleInventory;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	FKey InventoryKey = EKeys::I;

	// Fired by WaypointListKey; the HUD opens or closes the waypoint list window.
	DECLARE_MULTICAST_DELEGATE(FOnToggleWaypointList);
	FOnToggleWaypointList OnToggleWaypointList;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	FKey WaypointListKey = EKeys::L;

	// Gamepad layout: D-pad and face buttons are the eight action slots;
	// ActionBankShiftKey toggles to the second eight. InteractKey opens
	// the target's radial menu (the RMB-click equivalent), and holding
	// ZoomModifierKey turns right-stick Y into zoom.
	UPROPERTY(EditDefaultsOnly, Category = "Input|Gamepad")
	FKey ActionBankShiftKey = EKeys::Gamepad_LeftTrigger;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Gamepad")
	FKey InteractKey = EKeys::Gamepad_Special_Right;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Gamepad")
	FKey ZoomModifierKey = EKeys::Gamepad_RightTrigger;

	/** Opens the docked inventory; the View/Back button, the one the action layout leaves free. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Gamepad")
	FKey GamepadInventoryKey = EKeys::Gamepad_Special_Left;

protected:
	virtual void BeginPlay() override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);

	// Mouse-look, bound directly to the legacy raw MouseX/MouseY axis keys
	// rather than through an Enhanced Input action — see the .cpp's
	// SetupPlayerInputComponent comment for why.
	void LookMouseX(float Value);
	void LookMouseY(float Value);

	// Holding RMB (or pushing the right stick sideways) turns the character
	// to face the camera instead of the
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

	void OnLeftMouseButtonPressed();

	// The selectable network object under the cursor (or screen centre when the
	// cursor is hidden), or null. Shared by click-to-target and the radial menu.
	AActor* PickActorUnderCursor(FVector2D& OutScreenPosition) const;

	// How far click-to-target reaches, in Unreal units.
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Targeting")
	float TargetTraceDistance = 20000.0f;

	// RMB is both mouse-look (held and dragged) and the radial menu (clicked).
	// A release counts as a click when the cursor moved less than this, in pixels.
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Targeting")
	float RadialClickMaxDrag = 4.f;

	// Gamepad: right stick orbits the camera without needing RMB held, or
	// zooms while ZoomModifierKey is down. Axis keys fire per-frame, so the
	// value is scaled by these rates and DeltaSeconds.
	void GamepadLookX(float Value);
	void GamepadLookY(float Value);
	void GamepadZoom(float Value);

	// Stick deflection -> look speed, after the response curve below.
	UPROPERTY(EditDefaultsOnly, Category = "Input|Gamepad")
	float GamepadLookRateDegrees = 90.0f;

	// Power applied to stick deflection before the rate: >1 keeps small
	// pushes slow for fine aiming while a full push still reaches the rate.
	UPROPERTY(EditDefaultsOnly, Category = "Input|Gamepad", meta = (ClampMin = 0.5))
	float GamepadLookExponent = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Gamepad")
	float GamepadZoomRate = 600.0f;

	void ToggleActionBank();
	void ToggleInventory();
	void ToggleWaypointList();
	void OnZoomModifierPressed();
	void OnZoomModifierReleased();

	// Radial menu for the current target, targeting the nearest thing first
	// if there is none — there's no cursor to click with.
	void OnGamepadInteract();

	// Gamepad targeting has no cursor to trace under, so it works off a
	// distance-sorted list of selectable actors around the player: L3 picks
	// the nearest, the bumpers step through the list, R3 clears.
	void TargetNearest();
	void CycleTarget(int32 Direction);
	void CycleTargetNext();
	void CycleTargetPrevious();
	void ClearTarget();

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Targeting")
	float TargetCycleRadius = 6000.0f;
	// Sends the current position/orientation to the server as a
	// FDataTransformMessage, throttled by Tick — see the .cpp for the
	// send-rate/stop-detection reasoning.
	void SendDataTransformUpdate();

	class ASWGCell* ResolveCurrentCell() const;

	// Third-person: a spring arm holding the camera behind/above the
	// character, orbiting freely around it with mouse look
	// (bUsePawnControlRotation) independent of which way the body currently
	// faces, and doing its own collision test so it doesn't clip through
	// walls/terrain.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	// PLAY object state. The player object is a separate network object from the
	// creature and never spawns an actor of its own, so its baselines land on
	// these components — see FSWGPlayerBaselineHandler.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGPlayerProfileComponent> PlayerProfileComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGExperienceComponent> ExperienceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGJournalComponent> JournalComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGForceComponent> ForceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGCraftingComponent> CraftingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGSocialComponent> SocialComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGStomachComponent> StomachComponent;

	/**
	 * Tells the navigation system to keep Recast tiles generated in a radius
	 * around the player as they move — see DefaultEngine.ini's
	 * bGenerateNavigationOnlyAroundNavigationInvokers, the supported way to
	 * get runtime navmesh over a streamed world with no placed
	 * NavMeshBoundsVolume. Radii are tuned to track the terrain's own
	 * swg.TerrainLoadRadius/UnloadRadius streaming rings (see
	 * USWGTerrainSubsystem), so navmesh exists wherever there's loaded
	 * terrain to walk on and is dropped again once that terrain unloads.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<class UNavigationInvokerComponent> NavInvoker;

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

	/** The cell id last reported to the server (0 = world), so a change of room logs once rather than every send. */
	int64 LastReportedParentId = 0;
	bool bIsMouseLooking = false;
	bool bIsGamepadSteering = false;
	bool bActionBankShifted = false;
	bool bZoomModifierHeld = false;

	/** Where RMB went down, to tell a click from a look-drag on release. */
	FVector2D RightMouseDownPosition = FVector2D::ZeroVector;
};
