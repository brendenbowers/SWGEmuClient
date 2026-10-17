#include "Objects/World/SWGShip.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"
#include "Subsystems/SWGDeltaHandlerRegistry.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"
#include "TRE/SWGInteriorLayoutReader.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Common/SWGWorldScale.h"

void ASWGShip::IntegrateFlight(float DeltaTime, float Throttle, const FVector& Steering)
{
	if (!bHasFlightBaseline || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0
		|| !FMath::IsFinite(Throttle) || Steering.ContainsNaN() || !FMath::IsFinite(ShipState.Slip)) return;
	const FShipFlightLimits& Limits = ShipState.Flight;
	const float Step = FMath::Min(DeltaTime, 0.25f);
	const float TargetSpeed = FMath::Clamp(Throttle, 0.f, 1.f) * FMath::Clamp(Limits.MaxSpeed, 0.f, 512.f) * SWGWorldScale;
	// Retail updates the control rates once, then rotates and advances using the new velocity.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Target = FMath::Clamp(Steering[Axis], -1., 1.) * FMath::Clamp(Limits.AngularRates[Axis], 0., UE_PI / 2.);
		FlightAngularRates[Axis] = FMath::FInterpConstantTo(FlightAngularRates[Axis], Target, Step,
			FMath::Max(0., Limits.AngularAcceleration[Axis]));
	}
	const double OldSpeed = FlightSpeed;
	const double Accel = FMath::Max(0.f, TargetSpeed > OldSpeed ? Limits.Acceleration : Limits.Deceleration) * SWGWorldScale;
	const double Speed = FMath::FInterpConstantTo(OldSpeed, double(TargetSpeed), Step, Accel);
	FlightSpeed = Speed;
	// Retail 0x11e5640 rotates local yaw, then pitch, then roll (native Y, X, Z).
	const FQuat Rotation = (SpacePose.GetRotation() * FQuat(FVector::UpVector, FlightAngularRates.Y * Step)
		* FQuat(FVector::RightVector, -FlightAngularRates.X * Step)
		* FQuat(FVector::ForwardVector, FlightAngularRates.Z * Step)).GetNormalized();
	const FVector ForwardVelocity = Rotation.GetForwardVector() * Speed;
	// Retail 0x11e5640: zero slip follows heading; otherwise add thrust then cap velocity to engine speed.
	SpaceVelocity = ShipState.Slip <= 0 ? ForwardVelocity
		: (SpaceVelocity + ForwardVelocity * ShipState.Slip * Step).GetClampedToMaxSize(Speed);
	const FVector Position = SpacePose.GetLocation() + SpaceVelocity * Step;
	SpacePose = FTransform(Rotation, Position.BoundToCube(8000.f * SWGWorldScale));
}

class FSWGShipBaselineHandler : public ISWGBaselineHandler
{
public:
	bool CanHandleBaseline(const AActor& Actor, const FBaselinesMessage&) const override { return Actor.IsA<ASWGShip>(); }
	bool HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments&) override
	{
		ASWGShip& Ship = *CastChecked<ASWGShip>(&Actor);
		FSWGPacket Packet = Msg.AsPayloadPacket();
		bool Parsed = false;
		switch (Msg.BaselineType)
		{
		case 1: Parsed = SWGShipBaselineParser::ParseBase1(Packet, Ship.ShipState); break;
		case 3: Parsed = SWGShipBaselineParser::ParseBase3(Packet, Ship.ShipState); break;
		case 4: Parsed = SWGShipBaselineParser::ParseBase4(Packet, Ship.ShipState); break;
		case 6: Parsed = SWGShipBaselineParser::ParseBase6(Packet, Ship.ShipState); break;
		}
		if (!Parsed) return false;
		if (Msg.BaselineType == 3)
		{
			Ship.TangibleComponent->ApplyBase3(Ship.ShipState.Tangible);
			Ship.ConditionComponent->ApplyBase3(Ship.ShipState.Tangible);
		}
		if (Msg.BaselineType == 6)
		{
			Ship.bHasFlightBaseline = true;
			Ship.DefenderComponent->ApplyBase6(Ship.ShipState.Tangible);
		}
		return true;
	}
};
REGISTER_SWG_BASELINE_HANDLER(FSWGShipBaselineHandler, ESWGObjectType::SHIP)

class FSWGShipDeltaHandler : public ISWGDeltaHandler
{
public:
	bool CanHandleDelta(const AActor& Actor, const FDeltasMessage&) const override { return Actor.IsA<ASWGShip>(); }
	bool HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments&) override
	{
		ASWGShip& Ship = *CastChecked<ASWGShip>(&Actor);
		FSWGPacket Packet = Msg.AsPayloadPacket();
		if (!SWGShipBaselineParser::ApplyDelta(Packet, Ship.ShipState, Msg.DeltaType, Msg.UpdateCount)) return false;
		if (Msg.DeltaType == 3)
		{
			Ship.TangibleComponent->ApplyBase3(Ship.ShipState.Tangible);
			Ship.ConditionComponent->ApplyBase3(Ship.ShipState.Tangible);
		}
		if (Msg.DeltaType == 6) Ship.DefenderComponent->ApplyBase6(Ship.ShipState.Tangible);
		return true;
	}
};
REGISTER_SWG_DELTA_HANDLER(FSWGShipDeltaHandler, ESWGObjectType::SHIP)

class FSWGShipSpawnHandler : public ISWGActorSpawnHandler
{
	FSWGBuildingSpawnHandler PobHandler;
public:
	bool HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& Info) override
	{
		ASWGShip* Ship = Cast<ASWGShip>(&Actor);
		UGameInstance* Game = Actor.GetWorld() ? Actor.GetWorld()->GetGameInstance() : nullptr;
		USWGTreSubsystem* Tre = Game ? Game->GetSubsystem<USWGTreSubsystem>() : nullptr;
		USWGMeshGeneratorSubsystem* Mesh = Game ? Game->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
		if (!Ship || !Tre || !Mesh) return false;
		const FString Template = Info.TemplateName.IsEmpty() ? Tre->ResolveTemplatePath(Info.TemplateCrc) : Info.TemplateName;
		if (Template.IsEmpty()) return false;
		Ship->CockpitData.Reset();
		FString CockpitPath;
		if (Tre->FindTemplateFormStringParam(Template, SWG_IFF_TAG('S','S','H','P'), TEXT("cockpitFilename"), CockpitPath))
		{
			FSWGCockpitData Data;
			if (FSWGCockpitReader::Read(Tre->CreateIffReader(CockpitPath), Data)) Ship->CockpitData = MoveTemp(Data);
			else UE_LOG(LogTemp, Warning, TEXT("Ship cockpit configuration could not be parsed: %s"), *CockpitPath);
		}
		FString Pob;
		if (Mesh->ResolvePortalLayoutPath(Template, Pob))
		{
			return PobHandler.HandleActorSpawn(Actor, Info);
		}
		Mesh->RequestMeshForTemplatePath(Ship, Template);
		return true;
	}
};
REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGShipSpawnHandler, ASWGShip)
