#include "Objects/Player/SWGPlayer.h"
#include "Common/SWGWorldScale.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Math/RotationMatrix.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "UObject/ConstructorHelpers.h"
#include "Subsystems/SWGNetworkSubsystem.h"
//#include "Network/Messages/Zone/DataTransformMessage.h"
#include "Engine/GameInstance.h"
#include "Network/Messages/Zone/Object/DataTransform.h"
#include "Network/Messages/Zone/Object/DataTransformWithParent.h"
#include "Objects/World/SWGCell.h"
#include "Objects/World/SWGBuilding.h"
#include "Components/SWGPlayerProfileComponent.h"
#include "Components/SWGExperienceComponent.h"
#include "Components/SWGJournalComponent.h"
#include "Components/SWGForceComponent.h"
#include "Components/SWGCraftingComponent.h"
#include "Components/SWGSocialComponent.h"
#include "Components/SWGStomachComponent.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "EngineUtils.h"

ASWGPlayer::ASWGPlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	// Third-person orbit camera: mouse moves the boom freely around the
	// character (bUsePawnControlRotation) instead of turning the body, and
	// the character auto-faces whichever direction it's actually moving —
	// standard UE third-person convention (matches BP_ThirdPersonCharacter).
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	PlayerProfileComponent = CreateDefaultSubobject<USWGPlayerProfileComponent>(TEXT("PlayerProfileComponent"));
	ExperienceComponent = CreateDefaultSubobject<USWGExperienceComponent>(TEXT("ExperienceComponent"));
	JournalComponent = CreateDefaultSubobject<USWGJournalComponent>(TEXT("JournalComponent"));
	ForceComponent = CreateDefaultSubobject<USWGForceComponent>(TEXT("ForceComponent"));
	CraftingComponent = CreateDefaultSubobject<USWGCraftingComponent>(TEXT("CraftingComponent"));
	SocialComponent = CreateDefaultSubobject<USWGSocialComponent>(TEXT("SocialComponent"));
	StomachComponent = CreateDefaultSubobject<USWGStomachComponent>(TEXT("StomachComponent"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	// TargetArmLength (fixed 300, see UpdateCameraHeight) and boom offset
	// height are set there since the height still depends on the capsule's
	// real size (re-called once the actual mesh resizes it).
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// The target outline lives on the camera rather than in a post-process
	// volume, so it follows the player into every zone without each level
	// needing a volume placed in it. It reads the custom-depth stencil that
	// USWGTargetSubsystem writes on the targeted actor — see
	// r.CustomDepth=3 in DefaultEngine.ini.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OutlineMaterialFinder(
		TEXT("/Game/SWGEmu/UI/HUD/M_TargetOutline.M_TargetOutline"));
	if (OutlineMaterialFinder.Succeeded())
	{
		FollowCamera->PostProcessSettings.WeightedBlendables.Array.Emplace(1.0f, OutlineMaterialFinder.Object);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ASWGPlayer: M_TargetOutline not found — the target will have no outline"));
	}

	// Capsule starts at ACharacter's default size (88 half-height) here, before
	// any real mesh exists — USWGMeshGeneratorSubsystem resizes it to the
	// actual decoded mesh once that's ready and re-calls this to match.
	UpdateCameraHeight();

	static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionFinder(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	if (MoveActionFinder.Succeeded())
	{
		MoveAction = MoveActionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MappingContextFinder(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (MappingContextFinder.Succeeded())
	{
		DefaultMappingContext = MappingContextFinder.Object;
	}
}

void ASWGPlayer::UpdateCameraHeight()
{
	// Boom mounts around eye height (~90-95% of full standing height, capsule
	// full height = 2*HalfHeight), same reasoning as the old first-person eye
	// height — just now the *base* the camera orbits around/behind, rather
	// than the camera's own position. The root sits at the capsule's
	// *center*, not the feet, so this needs (eyeHeight - HalfHeight) above
	// the root.
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float EyeHeightAboveFeet = HalfHeight * 2.0f * 0.92f;
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, EyeHeightAboveFeet - HalfHeight));

	// Fixed rather than scaled off capsule height — the capsule-relative
	// length put the camera too far out once tested in PIE; 300 is the
	// tuned distance that reads well regardless of creature mesh size.
	CameraBoom->TargetArmLength = 300.0f;
}

void ASWGPlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Warning, TEXT("ASWGPlayer::EndPlay for %s, reason=%d (Destroyed=0, LevelTransition=1, RemovedFromWorld=2, Quit=3)"),
		*GetName(), (int32)EndPlayReason);

	Super::EndPlay(EndPlayReason);
}

void ASWGPlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// This player is spawned and possessed at runtime. Install its mapping
	// context here rather than relying solely on Blueprint controller defaults
	// that may have run before possession or may be empty on another controller.
	if (DefaultMappingContext)
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(NewController))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void ASWGPlayer::PawnClientRestart()
{
	Super::PawnClientRestart();

	// CameraBoom orbits off ControlRotation (bUsePawnControlRotation), and
	// OnPossess has just set that to this pawn's full actor rotation. Take
	// its yaw so the boom starts behind the character, and pin pitch to a
	// slight downward tilt with zero roll — from here mouse-look
	// (LookMouseX/LookMouseY) freely orbits away from this point.
	if (AController* OwningController = GetController())
	{
		OwningController->SetControlRotation(FRotator(-15.0f, GetActorRotation().Yaw, 0.0f));
	}
}

void ASWGPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASWGPlayer::Move);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ASWGPlayer::SetupPlayerInputComponent: no EnhancedInputComponent — movement input will not work"));
	}

	// Mouse-look uses the legacy raw-key axis path (BindAxisKey), not Enhanced
	// Input — Enhanced Input mouse-axis mappings produce zero Triggered events
	// in this project despite correct IMC/IA configuration; keyboard actions
	// work fine, so this is a workaround for Enhanced Input's mouse sampling.
	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &ASWGPlayer::LookMouseX);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &ASWGPlayer::LookMouseY);

	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ASWGPlayer::OnRightMouseButtonPressed);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ASWGPlayer::OnRightMouseButtonReleased);

	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ASWGPlayer::OnLeftMouseButtonPressed);

	// Same legacy-axis approach as mouse-look — MouseWheelAxis is a mouse
	// axis key like MouseX/MouseY, so it's bound the same way rather than
	// risking the same zero-events problem through an Enhanced Input action.
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ASWGPlayer::OnMouseWheel);

	// Gamepad. The left stick already drives IA_Move through IMC_Default;
	// the rest is bound here as raw keys like the mouse above. The D-pad and
	// face buttons belong to the action bar, so targeting lives on the
	// bumpers and stick clicks.
	PlayerInputComponent->BindAxisKey(EKeys::Gamepad_RightX, this, &ASWGPlayer::GamepadLookX);
	PlayerInputComponent->BindAxisKey(EKeys::Gamepad_RightY, this, &ASWGPlayer::GamepadLookY);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftThumbstick, IE_Pressed, this, &ASWGPlayer::TargetNearest);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightThumbstick, IE_Pressed, this, &ASWGPlayer::ClearTarget);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightShoulder, IE_Pressed, this, &ASWGPlayer::CycleTargetNext);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed, this, &ASWGPlayer::CycleTargetPrevious);
	PlayerInputComponent->BindKey(ActionBankShiftKey, IE_Pressed, this, &ASWGPlayer::ToggleActionBank);
	PlayerInputComponent->BindKey(ZoomModifierKey, IE_Pressed, this, &ASWGPlayer::OnZoomModifierPressed);
	PlayerInputComponent->BindKey(ZoomModifierKey, IE_Released, this, &ASWGPlayer::OnZoomModifierReleased);
	PlayerInputComponent->BindKey(InteractKey, IE_Pressed, this, &ASWGPlayer::OnGamepadInteract);
	PlayerInputComponent->BindKey(InventoryKey, IE_Pressed, this, &ASWGPlayer::ToggleInventory);
	PlayerInputComponent->BindKey(GamepadInventoryKey, IE_Pressed, this, &ASWGPlayer::ToggleInventory);

	// Action bar hotkeys: 1-9, 0, then hyphen and equals — SWG's twelve-slot
	// bank. Bound the same legacy way as the mouse keys above rather than
	// through Enhanced Input, so the HUD needs no input assets of its own.
	static const FKey SlotKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six,
		EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero, EKeys::Hyphen, EKeys::Equals
	};

	for (int32 SlotIndex = 0; SlotIndex < UE_ARRAY_COUNT(SlotKeys); ++SlotIndex)
	{
		FInputKeyBinding Binding(FInputChord(SlotKeys[SlotIndex], false, false, false, false), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, SlotIndex]()
		{
			OnActionSlotHotkey.Broadcast(SlotIndex);
		});
		PlayerInputComponent->KeyBindings.Emplace(MoveTemp(Binding));
	}

	// Gamepad slots: D-pad clockwise from up, then A B X Y. The order is the
	// same one USWGActionBarWidget::GetGamepadSlotKeyLabel labels with.
	static const FKey GamepadSlotKeys[] = {
		EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left,
		EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top
	};

	for (int32 SlotIndex = 0; SlotIndex < UE_ARRAY_COUNT(GamepadSlotKeys); ++SlotIndex)
	{
		FInputKeyBinding Binding(FInputChord(GamepadSlotKeys[SlotIndex], false, false, false, false), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, SlotIndex]()
		{
			OnActionSlotHotkey.Broadcast(SlotIndex + GetActiveActionBank() * UE_ARRAY_COUNT(GamepadSlotKeys));
		});
		PlayerInputComponent->KeyBindings.Emplace(MoveTemp(Binding));
	}
}

void ASWGPlayer::ToggleInventory()
{
	OnToggleInventory.Broadcast();
}

void ASWGPlayer::ToggleActionBank()
{
	bActionBankShifted = !bActionBankShifted;
	OnActionBankChanged.Broadcast(GetActiveActionBank());
}

void ASWGPlayer::OnZoomModifierPressed()
{
	bZoomModifierHeld = true;
}

void ASWGPlayer::OnZoomModifierReleased()
{
	bZoomModifierHeld = false;
}

void ASWGPlayer::OnGamepadInteract()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGTargetSubsystem* TargetSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
	if (!TargetSubsystem)
	{
		return;
	}

	if (!TargetSubsystem->HasTarget())
	{
		TargetNearest();
	}

	ISWGNetworkObjectInterface* NetworkObject = Cast<ISWGNetworkObjectInterface>(TargetSubsystem->GetTargetActor());
	if (!NetworkObject)
	{
		return;
	}

	// Anchor the menu on the target, or the screen centre if it's off-screen.
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	FVector2D ScreenPosition;
	if (!PlayerController || !PlayerController->ProjectWorldLocationToScreen(TargetSubsystem->GetTargetActor()->GetActorLocation(), ScreenPosition))
	{
		int32 ViewportX = 0;
		int32 ViewportY = 0;
		if (PlayerController)
		{
			PlayerController->GetViewportSize(ViewportX, ViewportY);
		}
		ScreenPosition = FVector2D(ViewportX * 0.5f, ViewportY * 0.5f);
	}

	if (USWGRadialMenuSubsystem* RadialMenu = GameInstance->GetSubsystem<USWGRadialMenuSubsystem>())
	{
		RadialMenu->RequestMenu(NetworkObject->GetObjectId(), ScreenPosition);
	}
}

void ASWGPlayer::LookMouseX(float Value)
{
	if (!bIsMouseLooking)
	{
		return;
	}

	AddControllerYawInput(Value);
}

void ASWGPlayer::LookMouseY(float Value)
{
	if (!bIsMouseLooking)
	{
		return;
	}

	AddControllerPitchInput(Value);
}

void ASWGPlayer::OnMouseWheel(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	constexpr float ZoomStep = 50.0f;
	constexpr float MinArmLength = 100.0f;
	constexpr float MaxArmLength = 1000.0f;

	CameraBoom->TargetArmLength = FMath::Clamp(
		CameraBoom->TargetArmLength - Value * ZoomStep, MinArmLength, MaxArmLength);
}

AActor* ASWGPlayer::PickActorUnderCursor(FVector2D& OutScreenPosition) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController)
	{
		return nullptr;
	}

	FVector2D AimPoint;
	if (!PlayerController->bShowMouseCursor || !PlayerController->GetMousePosition(AimPoint.X, AimPoint.Y))
	{
		int32 ViewportX = 0;
		int32 ViewportY = 0;
		PlayerController->GetViewportSize(ViewportX, ViewportY);
		AimPoint = FVector2D(ViewportX * 0.5f, ViewportY * 0.5f);
	}
	OutScreenPosition = AimPoint;

	FVector TraceStart;
	FVector TraceDirection;
	if (!PlayerController->DeprojectScreenPositionToWorld(AimPoint.X, AimPoint.Y, TraceStart, TraceDirection))
	{
		return nullptr;
	}

	// bTraceComplex so generated item meshes are picked per-triangle: their
	// collision is complex-as-simple (see USWGTargetSubsystem::HandleMeshReady),
	// which means the render triangles *are* the query geometry.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SWGClickToTarget), /*bTraceComplex*/ true, this);

	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, TraceStart, TraceStart + TraceDirection * TargetTraceDistance,
		USWGTargetSubsystem::SelectionChannel, QueryParams);

	// Walk up from whatever primitive was hit: a creature's mesh can sit on a
	// child actor, and only the owning network object carries an ObjectId.
	// IsSelectable is what enforces "anything with a tangible component" —
	// creatures, items, buildings and installations qualify; cells, doors and
	// static props are scenery and fall through to a miss.
	AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
	while (HitActor && !USWGTargetSubsystem::IsSelectable(HitActor))
	{
		HitActor = HitActor->GetParentActor();
	}
	return HitActor;
}

void ASWGPlayer::OnLeftMouseButtonPressed()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGTargetSubsystem* TargetSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
	if (!TargetSubsystem)
	{
		return;
	}

	// A miss clears the target, matching the retail client's click-off-to-deselect.
	FVector2D ScreenPosition;
	TargetSubsystem->SetTargetActor(PickActorUnderCursor(ScreenPosition));
}

namespace
{
	// Stick deflection through a power curve, sign preserved.
	float ShapeStick(float Value, float Exponent)
	{
		return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), Exponent);
	}
}

void ASWGPlayer::GamepadLookX(float Value)
{
	// Axis bindings fire every frame, zero included, so this is a clean per-frame
	// "is the stick pushed sideways" that Tick reads to turn the body with the camera.
	bIsGamepadSteering = !FMath::IsNearlyZero(Value);
	if (bIsGamepadSteering)
	{
		AddControllerYawInput(ShapeStick(Value, GamepadLookExponent) * GamepadLookRateDegrees * GetWorld()->GetDeltaSeconds());
	}
}

void ASWGPlayer::GamepadLookY(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	if (bZoomModifierHeld)
	{
		GamepadZoom(Value);
		return;
	}

	// Stick up tilts the camera up, the opposite of mouse-Y's push-forward-to-look-down.
	AddControllerPitchInput(-ShapeStick(Value, GamepadLookExponent) * GamepadLookRateDegrees * GetWorld()->GetDeltaSeconds());
}

void ASWGPlayer::GamepadZoom(float Value)
{
	// Same clamp as OnMouseWheel; the wheel's per-notch step becomes a per-second rate.
	CameraBoom->TargetArmLength = FMath::Clamp(
		CameraBoom->TargetArmLength - Value * GamepadZoomRate * GetWorld()->GetDeltaSeconds(), 100.0f, 1000.0f);
}

void ASWGPlayer::TargetNearest()
{
	// Cycling from "no target" lands on the nearest; from a target it steps
	// past it, so repeated presses walk outward like the bumper does.
	CycleTarget(1);
}

void ASWGPlayer::CycleTargetNext() { CycleTarget(1); }
void ASWGPlayer::CycleTargetPrevious() { CycleTarget(-1); }

void ASWGPlayer::ClearTarget()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (USWGTargetSubsystem* TargetSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr)
	{
		TargetSubsystem->ClearTarget();
	}
}

void ASWGPlayer::CycleTarget(int32 Direction)
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGTargetSubsystem* TargetSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
	if (!TargetSubsystem)
	{
		return;
	}

	struct FCandidate
	{
		AActor* Actor;
		float DistanceSquared;
	};
	TArray<FCandidate> Candidates;

	const FVector Origin = GetActorLocation();
	const float RadiusSquared = FMath::Square(TargetCycleRadius);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == this || Actor->IsHidden() || !USWGTargetSubsystem::IsSelectable(Actor))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(Origin, Actor->GetActorLocation());
		if (DistanceSquared <= RadiusSquared)
		{
			Candidates.Add({ Actor, DistanceSquared });
		}
	}

	if (Candidates.IsEmpty())
	{
		return;
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistanceSquared < B.DistanceSquared; });

	// Step from the current target's slot in the list, wrapping at either
	// end; with no current target (or one that has left the radius) start
	// from the nearest going forward, the farthest going back.
	AActor* CurrentTarget = TargetSubsystem->GetTargetActor();
	int32 CurrentIndex = CurrentTarget ? Candidates.IndexOfByPredicate([CurrentTarget](const FCandidate& C) { return C.Actor == CurrentTarget; }) : INDEX_NONE;
	int32 NextIndex;
	if (CurrentIndex == INDEX_NONE)
	{
		NextIndex = Direction >= 0 ? 0 : Candidates.Num() - 1;
	}
	else
	{
		NextIndex = (CurrentIndex + Direction + Candidates.Num()) % Candidates.Num();
	}

	TargetSubsystem->SetTargetActor(Candidates[NextIndex].Actor);
}

void ASWGPlayer::OnRightMouseButtonPressed()
{
	bIsMouseLooking = true;

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->GetMousePosition(RightMouseDownPosition.X, RightMouseDownPosition.Y))
	{
		RightMouseDownPosition = FVector2D(-1.f, -1.f);
	}
}

void ASWGPlayer::OnRightMouseButtonReleased()
{
	bIsMouseLooking = false;

	// A click (no drag) on an object opens its radial menu; a drag was mouse-look.
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	FVector2D ReleasePosition;
	if (!PlayerController || RightMouseDownPosition.X < 0.f || !PlayerController->GetMousePosition(ReleasePosition.X, ReleasePosition.Y)
		|| FVector2D::Distance(ReleasePosition, RightMouseDownPosition) > RadialClickMaxDrag)
	{
		return;
	}

	FVector2D ScreenPosition;
	AActor* HitActor = PickActorUnderCursor(ScreenPosition);
	ISWGNetworkObjectInterface* NetworkObject = Cast<ISWGNetworkObjectInterface>(HitActor);
	if (!NetworkObject)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (USWGRadialMenuSubsystem* RadialMenu = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr)
	{
		RadialMenu->RequestMenu(NetworkObject->GetObjectId(), ScreenPosition);
	}
}

void ASWGPlayer::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (!Controller)
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void ASWGPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsLocallyControlled())
	{
		return;
	}

	// Steering: RMB held, or the right stick deflected sideways this frame.
	const bool bSteering = bIsMouseLooking || bIsGamepadSteering;
	GetCharacterMovement()->bOrientRotationToMovement = !bSteering;
	if (bSteering && Controller)
	{
		// The mesh faces actor +X (SWG's +z forward lands there, see
		// SWGWorldScale.h), so facing the camera is just matching
		// ControlRotation's yaw.
		FRotator NewRotation = GetActorRotation();
		NewRotation.Yaw = Controller->GetControlRotation().Yaw;
		SetActorRotation(NewRotation);
	}

	// Real SWG clients report position ~10/sec while moving and send one
	// final "stopped" report — matches FSWGInWorldState::Enter's own
	// one-shot stationary report, which is what first established this wire
	// format/field layout works against Core3's DataTransformCallback.
	constexpr float TransformSendInterval = 0.1f;

	const float Speed = GetVelocity().Size();
	const bool bIsMoving = Speed > KINDA_SMALL_NUMBER;

	TimeSinceLastTransformSend += DeltaTime;

	if ((bIsMoving && TimeSinceLastTransformSend >= TransformSendInterval)
		|| (bWasMovingLastSend && !bIsMoving))
	{
		SendDataTransformUpdate();
		TimeSinceLastTransformSend = 0.0f;
	}

	bWasMovingLastSend = bIsMoving;
}

void ASWGPlayer::SendDataTransformUpdate()
{

	// todo: move thos to be more generic, a component registered with the network system to update or 
	// something similar

	UGameInstance* GameInstance = GetGameInstance();
	USWGNetworkSubsystem* Network = GameInstance ? GameInstance->GetSubsystem<USWGNetworkSubsystem>() : nullptr;
	if (!Network)
	{
		return;
	}

	const FVector RawPosition = SWGToRawSpace(GetActorLocation());
	const FQuat RawDirection = SWGCharacterHeadingToNativeRotation(GetActorRotation());
	const uint32 RawTimeStamp = (uint32)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFFFFFFu);
	const int32 RawMoveCount = ++TransformMovementCounter;
	// Same raw/pre-scale conversion as Position — server compares this against
	// the character's real WalkSpeed/RunSpeed (meters/sec) in
	// PlayerManager::checkSpeedHackTests; sending raw UE cm/s here (e.g. 154.9
	// for a ~1.55 m/s walk) reads as 100x overspeed and trips the speed-hack
	// bounce back.
	const float RawSpeed = SWGToRawSpace(GetVelocity().Size());

	const ASWGCell* CurrentCell = ResolveCurrentCell();

	const int64 ParentId = CurrentCell ? CurrentCell->GetObjectId() : 0;
	if (ParentId != LastReportedParentId)
	{
		UE_LOG(LogTemp, Log, TEXT("ASWGPlayer: reporting position relative to %s (cell %lld, building %s)"),
			CurrentCell ? *CurrentCell->GetName() : TEXT("the world"), ParentId,
			CurrentCell && CurrentCell->OwningBuilding.IsValid() ? *CurrentCell->OwningBuilding->GetName() : TEXT("none"));
		LastReportedParentId = ParentId;
	}

	if (CurrentCell != nullptr)
	{
		const ASWGBuilding* OwningBuilding = CurrentCell->OwningBuilding.Get();
		const FVector BuildingLocalPosition = OwningBuilding
			? SWGToRawSpace(OwningBuilding->GetActorTransform().InverseTransformPosition(GetActorLocation()))
			: RawPosition;

		FDataTransformWithParent DTMessage(SWGObjectId);
		DTMessage.ParentId = (uint64)CurrentCell->GetObjectId();
		DTMessage.Position = BuildingLocalPosition;
		DTMessage.Direction = RawDirection;
		DTMessage.TimeStamp = RawTimeStamp;
		DTMessage.MoveCount = RawMoveCount;
		DTMessage.Speed = RawSpeed;

		Network->SendMessage(DTMessage.Serialize());
	}
	else
	{
		FDataTransform DTMessage(SWGObjectId);
		DTMessage.Position = RawPosition;
		DTMessage.Direction = RawDirection;
		DTMessage.TimeStamp = RawTimeStamp;
		DTMessage.MoveCount = RawMoveCount;
		DTMessage.Speed = RawSpeed;

		Network->SendMessage(DTMessage.Serialize());
	}


	//FDataTransformMessage Transform;
	//Transform.ObjectId = SWGObjectId;
	//// The server expects raw (pre-scale) wire-space coordinates, same as
	//// every position it sends us — convert back before sending our own.
	//Transform.Position = SWGToRawSpace(GetActorLocation());
	//Transform.Direction = GetActorQuat();
	//Transform.TimeStamp = (uint32)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFFFFFFu);
	//Transform.MovementCounter = ++TransformMovementCounter;
	//Transform.Speed = GetVelocity().Size();

	//Network->SendMessage(Transform.Serialize());
}

void ASWGPlayer::BeginPlay()
{
	Super::BeginPlay();

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);

	}
}

ASWGCell* ASWGPlayer::ResolveCurrentCell() const
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return nullptr;
	}
	TArray<AActor*> OverlappingActors;
	Capsule->GetOverlappingActors(OverlappingActors, ASWGCell::StaticClass());

	for (AActor* OverlappingActor : OverlappingActors)
	{
		if (ASWGCell* Cell = Cast<ASWGCell>(OverlappingActor))
		{
			return Cell;
		}
	}

	return nullptr;
}
