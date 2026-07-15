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

	// CameraBoom orbits off ControlRotation (bUsePawnControlRotation), which
	// otherwise defaults to (0,0,0) regardless of whatever heading the
	// network's spawn quaternion actually gave this actor (see
	// HandleSceneCreateObject) — without this the boom would start facing
	// world yaw-zero instead of behind the character. A +90 yaw used to be
	// needed here because the mesh's visual forward was component +Y; now
	// that USWGMeshGeneratorSubsystem rotates the PoseableMesh -90 so it
	// faces actor +X (the standard UE convention bOrientRotationToMovement
	// assumes), the boom seeds straight off the actor's own yaw. -15 pitch
	// gives the camera its default slight downward tilt; from here on,
	// mouse-look (LookMouseX/LookMouseY) freely orbits away from this point.
	if (NewController)
	{
		NewController->SetControlRotation(GetActorRotation() + FRotator(-15.0f, 0.0f, 0.0f));
	}

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

	// Same legacy-axis approach as mouse-look — MouseWheelAxis is a mouse
	// axis key like MouseX/MouseY, so it's bound the same way rather than
	// risking the same zero-events problem through an Enhanced Input action.
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ASWGPlayer::OnMouseWheel);
}

void ASWGPlayer::LookMouseX(float Value)
{
	AddControllerYawInput(Value);
}

void ASWGPlayer::LookMouseY(float Value)
{
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

void ASWGPlayer::OnRightMouseButtonPressed()
{
	bIsTurningToCamera = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
}

void ASWGPlayer::OnRightMouseButtonReleased()
{
	bIsTurningToCamera = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
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

	if (bIsTurningToCamera && Controller)
	{
		// With the mesh now facing actor +X (see the PoseableMesh -90 yaw in
		// USWGMeshGeneratorSubsystem), facing the camera direction is simply
		// matching ControlRotation's yaw — the old -90 here compensated for
		// the mesh's pre-fix +Y facing and would now make the body strafe
		// sideways relative to its own stride whenever RMB steering is held.
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

	FDataTransform DTMessage(SWGObjectId);
	DTMessage.Position = SWGToRawSpace(GetActorLocation());
	DTMessage.Direction = GetActorQuat();
	DTMessage.TimeStamp = (uint32)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFFFFFFu);
	DTMessage.MoveCount = ++TransformMovementCounter;
	// Same raw/pre-scale conversion as Position — server compares this against
	// the character's real WalkSpeed/RunSpeed (meters/sec) in
	// PlayerManager::checkSpeedHackTests; sending raw UE cm/s here (e.g. 154.9
	// for a ~1.55 m/s walk) reads as 100x overspeed and trips the speed-hack
	// bounce back.
	DTMessage.Speed = SWGToRawSpace(GetVelocity().Size());

	Network->SendMessage(DTMessage.Serialize());


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
