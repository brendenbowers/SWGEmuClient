#include "Objects/Player/SWGPlayer.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/Messages/Zone/DataTransformMessage.h"
#include "Engine/GameInstance.h"

ASWGPlayer::ASWGPlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	// Third-person: mouse look orbits the camera boom around the character
	// instead of snapping the body to match the view, and the character
	// faces whichever direction it's actually moving (standard UE
	// third-person convention — matches BP_ThirdPersonCharacter's own setup).
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	// TargetArmLength is re-derived from the capsule's real size in
	// UpdateCameraHeight (same reasoning as eye height) — a fixed 400 (tuned
	// for UE's normal ~180-unit-tall default capsule) would be wildly too
	// long once the capsule reflects this project's actual (much smaller)
	// creature-mesh scale, putting the camera far outside nearby terrain
	// features and making its own collision-test yank it in tight/underground.
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

	static ConstructorHelpers::FObjectFinder<UInputAction> LookActionFinder(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	if (LookActionFinder.Succeeded())
	{
		LookAction = LookActionFinder.Object;
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

	// Scale the boom length off the capsule's real size too — a fixed length
	// tuned for a normal ~180-unit-tall UE character would be wildly too
	// long (or too short) for whatever this creature's mesh actually decoded
	// to. ~4x full standing height is a reasonable "see the character with
	// some breathing room" default third-person distance.
	CameraBoom->TargetArmLength = FMath::Max(HalfHeight * 2.0f * 4.0f, 10.0f);
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

	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
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
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASWGPlayer::Look);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ASWGPlayer::SetupPlayerInputComponent: no EnhancedInputComponent — movement input will not work"));
	}
}

void ASWGPlayer::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller == nullptr)
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void ASWGPlayer::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller == nullptr)
	{
		return;
	}

	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

void ASWGPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsLocallyControlled())
	{
		return;
	}

	// Belt-and-suspenders alongside PossessedBy's SetMovementMode(MOVE_Flying):
	// something (baseline application arriving after possession, or the
	// initial pre-possession freefall's accumulated state) was observed
	// reverting this back to MOVE_Falling, dropping the capsule to
	// terrain-collision-doesn't-exist-yet infinity. Keep re-asserting it
	// every tick until real terrain collision lands.
	if (GetCharacterMovement()->MovementMode != MOVE_Flying)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
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
	UGameInstance* GameInstance = GetGameInstance();
	USWGNetworkSubsystem* Network = GameInstance ? GameInstance->GetSubsystem<USWGNetworkSubsystem>() : nullptr;
	if (!Network)
	{
		return;
	}

	FDataTransformMessage Transform;
	Transform.ObjectId = SWGObjectId;
	Transform.Position = GetActorLocation();
	Transform.Direction = GetActorQuat();
	Transform.TimeStamp = (uint32)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFFFFFFu);
	Transform.MovementCounter = ++TransformMovementCounter;
	Transform.Speed = GetVelocity().Size();

	Network->SendMessage(Transform.Serialize());
}
