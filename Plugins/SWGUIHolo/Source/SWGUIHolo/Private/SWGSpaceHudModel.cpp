#include "SWGSpaceHudModel.h"
#include "Common/SWGWorldScale.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Objects/Creature/SWGCreature.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/World/SWGShip.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarHudProjectileSpeed(
		TEXT("swg.Space.HudProjectileSpeed"), 0.f,
		TEXT("Projectile speed in m/s used to place the HUD's lead indicator. 0 (default) means unknown: the client has no weapon projectile data, so the indicator marks the target's position."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarHudRadarRange(
		TEXT("swg.Space.HudRadarRange"), 2048.f,
		TEXT("Radar range in metres. Neither the retail HUD data nor Core3 defines one; the default is the server's close-object range for ships (ZoneServer SPACECLOSEOBJECTRANGE), which is as far as it tells the client about ships."),
		ECVF_Default);

	// Baseline-1 scalar fields in Core3's ShipObjectMessage1 order, minus the first field (17) that OwnerValues starts at.
	constexpr int32 CapacitorMaxIndex = 2;
	constexpr int32 BoosterMaxIndex = 14;
	constexpr int32 BoosterMaxSpeedIndex = 18;
	// Baseline-1 float maps: index 4 is the component max-damage map, which only weapons have entries in.
	constexpr int32 MaxDamageMapIndex = 4;
	// AmmoMaps: field 14 current, 15 max, 16 class, each keyed by weapon slot.
	constexpr int32 AmmoCurrentMap = 0, AmmoMaxMap = 1;
	// Core3 SceneObjectType: a missile launcher and a countermeasure launcher component.
	constexpr int32 MissileLauncherType = 0x4000000F;
	constexpr int32 CountermeasureLauncherType = 0x40000010;
	constexpr int32 MaxBlips = 96;

	float FindMapValue(const TArray<TPair<uint32, float>>& Items, uint32 Slot, bool& bFound)
	{
		for (const TPair<uint32, float>& Item : Items)
		{
			if (Item.Key == Slot) { bFound = true; return Item.Value; }
		}
		bFound = false;
		return 0.f;
	}

	TOptional<float> Fraction(float Current, float Max)
	{
		return Max > 0.f ? TOptional<float>(FMath::Clamp(Current / Max, 0.f, 1.f)) : TOptional<float>();
	}

	/** Current over max of one slot in a pair of keyed maps; unset unless both have the slot and max is positive. */
	TOptional<float> SlotFraction(const TArray<TPair<uint32, float>>& Current, const TArray<TPair<uint32, float>>& Max, int32 Slot)
	{
		bool bHasCurrent = false, bHasMax = false;
		const float CurrentValue = FindMapValue(Current, uint32(Slot), bHasCurrent);
		const float MaxValue = FindMapValue(Max, uint32(Slot), bHasMax);
		return bHasCurrent && bHasMax ? Fraction(CurrentValue, MaxValue) : TOptional<float>();
	}

	int32 FindAmmoValue(const TArray<TPair<uint32, uint32>>& Items, int32 Slot)
	{
		for (const TPair<uint32, uint32>& Item : Items)
		{
			if (Item.Key == uint32(Slot)) return int32(Item.Value);
		}
		return 0;
	}

	/** Hitpoint fraction summed over the slots Predicate accepts; unset when none has both a current and a positive max. */
	template <typename PredicateType>
	TOptional<float> HitpointFraction(const FShipObjectBaseline& State, PredicateType Predicate)
	{
		float CurrentSum = 0.f, MaxSum = 0.f;
		for (const TPair<uint32, float>& Item : State.HitpointsMax.Items)
		{
			if (!Predicate(int32(Item.Key)) || Item.Value <= 0.f) continue;
			bool bHasCurrent = false;
			const float Current = FindMapValue(State.HitpointsCurrent.Items, Item.Key, bHasCurrent);
			if (!bHasCurrent) continue;
			CurrentSum += FMath::Clamp(Current, 0.f, Item.Value);
			MaxSum += Item.Value;
		}
		return Fraction(CurrentSum, MaxSum);
	}
}

FSWGSpaceHudVitals FSWGSpaceHudVitals::FromBaseline(const FShipObjectBaseline& State)
{
	FSWGSpaceHudVitals Vitals;
	Vitals.FrontShield = Fraction(State.FrontShield, State.MaxFrontShield);
	Vitals.RearShield = Fraction(State.RearShield, State.MaxRearShield);
	Vitals.FrontArmor = SlotFraction(State.ArmorCurrent.Items, State.ArmorMax.Items, SWGShipSlot::Armor0);
	Vitals.RearArmor = SlotFraction(State.ArmorCurrent.Items, State.ArmorMax.Items, SWGShipSlot::Armor1);
	Vitals.System = HitpointFraction(State, [](int32) { return true; });
	Vitals.Chassis = Fraction(State.ChassisHealth, State.ChassisMaxHealth);
	return Vitals;
}

TOptional<float> FSWGSpaceHudData::ComponentCondition(const FShipObjectBaseline& State, ESWGSpaceHudComponent Component)
{
	switch (Component)
	{
	case ESWGSpaceHudComponent::Reactor: return HitpointFraction(State, [](int32 Slot) { return Slot == SWGShipSlot::Reactor; });
	case ESWGSpaceHudComponent::Engine: return HitpointFraction(State, [](int32 Slot) { return Slot == SWGShipSlot::Engine; });
	case ESWGSpaceHudComponent::Shield: return HitpointFraction(State, [](int32 Slot) { return Slot == SWGShipSlot::Shield0 || Slot == SWGShipSlot::Shield1; });
	case ESWGSpaceHudComponent::Armor: return HitpointFraction(State, [](int32 Slot) { return Slot == SWGShipSlot::Armor0 || Slot == SWGShipSlot::Armor1; });
	case ESWGSpaceHudComponent::Capacitor: return HitpointFraction(State, [](int32 Slot) { return Slot == SWGShipSlot::Capacitor; });
	case ESWGSpaceHudComponent::Weapon: return HitpointFraction(State, [](int32 Slot) { return Slot >= SWGShipSlot::WeaponStart; });
	default: return TOptional<float>();
	}
}

bool FSWGSpaceHudData::Gather(const USWGObjectGraphSubsystem& Graph, FSWGSpaceHudData& Out)
{
	const ASWGShip* Ship = Graph.GetLocalPlayerShip();
	const ASWGPlayer* Pilot = Cast<ASWGPlayer>(Graph.FindActor(Graph.GetLocalPlayerObjectId()));
	if (!Ship || !Pilot || !Graph.IsLocalPlayerPiloting()) return false;
	const FShipObjectBaseline& State = Ship->ShipState;
	USWGTreSubsystem* Tre = Graph.GetGameInstance() ? Graph.GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	Out = FSWGSpaceHudData();

	Out.Speed = Ship->SpaceVelocity.Size() / SWGWorldScale;
	if (State.Flight.MaxSpeed > 0.f) Out.MaxSpeed = State.Flight.MaxSpeed;
	if (State.bHasBase1 && State.OwnerValues[BoosterMaxSpeedIndex] > State.Flight.MaxSpeed && State.Flight.MaxSpeed > 0.f)
	{
		Out.BoostMaxSpeed = State.OwnerValues[BoosterMaxSpeedIndex];
	}
	Out.Throttle = FMath::Clamp(Pilot->GetShipThrottle(), 0.f, 1.f);
	if (State.bHasBase4 && State.bHasBase1)
	{
		if (State.OwnerValues[CapacitorMaxIndex] > 0.f)
		{
			Out.Capacitor = State.CapacitorEnergy;
			Out.CapacitorMax = State.OwnerValues[CapacitorMaxIndex];
		}
		if (State.OwnerValues[BoosterMaxIndex] > 0.f)
		{
			Out.Booster = State.BoosterEnergy;
			Out.BoosterMax = State.OwnerValues[BoosterMaxIndex];
		}
	}
	if (State.bHasBase1 && State.bHasBase3)
	{
		for (const TPair<uint32, float>& Weapon : State.OwnerComponentFloatMaps[MaxDamageMapIndex].Items)
		{
			bool bHasCurrent = false, bHasMax = false;
			const float Current = FindMapValue(State.HitpointsCurrent.Items, Weapon.Key, bHasCurrent);
			const float Max = FindMapValue(State.HitpointsMax.Items, Weapon.Key, bHasMax);
			if (!bHasCurrent || !bHasMax || Max <= 0.f) continue;
			Out.Weapons.Add({int32(Weapon.Key), FMath::Clamp(Current / Max, 0.f, 1.f)});
		}
		Out.Weapons.Sort([](const FSWGSpaceHudWeapon& Left, const FSWGSpaceHudWeapon& Right) { return Left.Slot < Right.Slot; });
	}

	Out.ShipActor = const_cast<ASWGShip*>(Ship);
	Out.ShipLocation = Ship->GetActorLocation();
	Out.ShipForward = Ship->GetActorForwardVector();
	if (State.bHasBase3)
	{
		Out.Vitals = FSWGSpaceHudVitals::FromBaseline(State);
		for (int32 Index = 0; Index < int32(ESWGSpaceHudComponent::Count); ++Index)
		{
			Out.Components[Index] = ComponentCondition(State, ESWGSpaceHudComponent(Index));
		}
	}

	// Launchers: the weapon slots whose template is a missile or countermeasure launcher, with their ammunition.
	if (State.bHasBase1 && Tre)
	{
		for (const TPair<uint32, uint32>& Component : State.ComponentCrcs.Items)
		{
			int32 GameObjectType = 0;
			const FString TemplatePath = Tre->ResolveTemplatePath(Component.Value);
			if (TemplatePath.IsEmpty() || !Tre->FindTemplateIntParam(TemplatePath, TEXT("gameObjectType"), GameObjectType)) continue;
			// The client weapon templates name only countermeasure launchers (wpn_std_countermeasure); a missile launcher is a weapon that holds ammunition.
			const int32 AmmoMax = FindAmmoValue(State.AmmoMaps[AmmoMaxMap].Items, int32(Component.Key));
			const bool bCountermeasure = GameObjectType == CountermeasureLauncherType || TemplatePath.Contains(TEXT("countermeasure"));
			if (!bCountermeasure && GameObjectType != MissileLauncherType && AmmoMax <= 0) continue;
			FSWGSpaceHudLauncher& Launcher = Out.Launchers.AddDefaulted_GetRef();
			Launcher.Slot = int32(Component.Key);
			Launcher.Ammo = FindAmmoValue(State.AmmoMaps[AmmoCurrentMap].Items, Launcher.Slot);
			Launcher.AmmoMax = AmmoMax;
			Launcher.bCountermeasure = bCountermeasure;
		}
		Out.Launchers.Sort([](const FSWGSpaceHudLauncher& Left, const FSWGSpaceHudLauncher& Right) { return Left.Slot < Right.Slot; });
	}

	Out.IncomingMissiles = Graph.CountIncomingMissiles(Ship->GetObjectId(), FPlatformTime::Seconds());
	Out.bPilotIncapacitated = Pilot->CombatStateComponent && Pilot->CombatStateComponent->IsIncapacitated();
	if (Ship->bHasSpaceStamp) Out.NorthInShip = Ship->SpacePose.GetRotation().UnrotateVector(FVector::ForwardVector);

	// Radar: everything the graph holds in space within range, nearest first.
	Out.RadarRange = FMath::Max(CVarHudRadarRange.GetValueOnGameThread(), 1.f) * SWGWorldScale;
	const AActor* TargetActor = State.TargetId ? Graph.FindActor(int64(State.TargetId)) : nullptr;
	TArray<AActor*> SpaceActors;
	Graph.GetSpaceObjectActors(SpaceActors);
	for (const AActor* Actor : SpaceActors)
	{
		if (Actor == Ship) continue;
		const FVector Relative = Ship->GetActorQuat().UnrotateVector(Actor->GetActorLocation() - Ship->GetActorLocation());
		if (Relative.Size() > Out.RadarRange) continue;
		FSWGSpaceHudBlip& Blip = Out.Blips.AddDefaulted_GetRef();
		const ISWGNetworkObjectInterface* NetworkObject = Cast<ISWGNetworkObjectInterface>(Actor);
		Blip.ObjectId = NetworkObject ? NetworkObject->GetObjectId() : 0;
		Blip.Relative = Relative;
		Blip.bTarget = Actor == TargetActor;
		if (const ASWGShip* OtherShip = Cast<ASWGShip>(Actor))
		{
			Blip.Kind = ESWGSpaceHudBlipKind::Ship;
			Blip.Relation = SWGSpaceHud::ClassifyRelation(State.Faction, OtherShip->ShipState.Faction);
		}
		else
		{
			Blip.Kind = ESWGSpaceHudBlipKind::Structure;
		}
	}
	Out.Blips.Sort([](const FSWGSpaceHudBlip& Left, const FSWGSpaceHudBlip& Right) { return Left.Relative.SizeSquared() < Right.Relative.SizeSquared(); });
	if (Out.Blips.Num() > MaxBlips) Out.Blips.SetNum(MaxBlips);

	if (TargetActor && TargetActor != Ship)
	{
		Out.bHasTarget = true;
		Out.TargetLocation = TargetActor->GetActorLocation();
		const ASWGShip* TargetShip = Cast<ASWGShip>(TargetActor);
		const FVector TargetWorldVelocity = TargetShip ? TargetShip->SpaceVelocity : FVector::ZeroVector;
		Out.TargetRelativeVelocity = Ship->SpacePose.GetRotation().UnrotateVector(TargetWorldVelocity - Ship->SpaceVelocity);

		Out.Target.ObjectId = int64(State.TargetId);
		Out.Target.Actor = const_cast<AActor*>(TargetActor);
		Out.Target.bIsShip = TargetShip != nullptr;
		if (const USWGTangibleComponent* Tangible = TargetActor->FindComponentByClass<USWGTangibleComponent>()) Out.Target.Name = Tangible->GetDisplayName();
		if (TargetShip)
		{
			const FShipObjectBaseline& TargetState = TargetShip->ShipState;
			if (Out.Target.Name.IsEmpty()) Out.Target.Name = TargetState.WingName;
			Out.Target.TypeName = TargetState.TypeName;
			Out.Target.Difficulty = TargetState.Difficulty;
			Out.Target.Faction = TargetState.Faction;
			Out.Target.Relation = SWGSpaceHud::ClassifyRelation(State.Faction, TargetState.Faction);
			Out.Target.Vitals = FSWGSpaceHudVitals::FromBaseline(TargetState);
		}
	}
	if (const float ProjectileSpeed = CVarHudProjectileSpeed.GetValueOnGameThread(); ProjectileSpeed > 0.f) Out.ProjectileSpeed = ProjectileSpeed;
	return true;
}

namespace SWGSpaceHud
{
	FSWGSpaceHudLayout ComputeLayout(const FVector2D& ViewSize)
	{
		FSWGSpaceHudLayout Layout;
		Layout.ViewSize = ViewSize;
		Layout.Unit = float(ViewSize.Y / 1080.0);
		const double Unit = Layout.Unit;
		const double Margin = ViewSize.Y * 0.05;
		Layout.Safe = FBox2D(FVector2D(Margin, Margin), ViewSize - FVector2D(Margin, Margin));
		const FVector2D Centre = ViewSize * 0.5;

		auto Place = [&](ESWGSpaceHudSlot Slot, const FVector2D& Min, const FVector2D& Size)
		{
			Layout.Slots[int32(Slot)] = FBox2D(Min, Min + Size);
		};
		// The reticle is retail's page scaled up for a modern screen (1.25x, leaving the bottom for the radar), centred on the view.
		const double ReticleScale = 1.25 * Unit;
		const FVector2D ReticleSize(RetailReticleWidth * ReticleScale, RetailReticleHeight * ReticleScale);
		Place(ESWGSpaceHudSlot::Reticle, Centre - ReticleSize * 0.5, ReticleSize);
		const FVector2D RadarSize(560.0 * Unit, 300.0 * Unit);
		Place(ESWGSpaceHudSlot::Radar, FVector2D(Centre.X - RadarSize.X * 0.5, Layout.Safe.Max.Y - RadarSize.Y), RadarSize);
		const FVector2D SidePanelSize(400.0 * Unit, 480.0 * Unit);
		Place(ESWGSpaceHudSlot::Target, FVector2D(Layout.Safe.Max.X - SidePanelSize.X, Centre.Y - SidePanelSize.Y * 0.5), SidePanelSize);
		Place(ESWGSpaceHudSlot::Status, FVector2D(Layout.Safe.Min.X, Centre.Y - SidePanelSize.Y * 0.5), SidePanelSize);
		const FVector2D WarningSize(720.0 * Unit, 110.0 * Unit);
		Place(ESWGSpaceHudSlot::Warnings, FVector2D(Centre.X - WarningSize.X * 0.5, Layout.Safe.Min.Y), WarningSize);
		return Layout;
	}

	EConditionBand ClassifyCondition(float Fraction)
	{
		return Fraction < 0.25f ? EConditionBand::Critical : (Fraction < 0.5f ? EConditionBand::Warning : EConditionBand::Good);
	}

	FLinearColor GetConditionColor(float Fraction)
	{
		static const FLinearColor Colors[3] = {
			FLinearColor::FromSRGBColor(FColor(0x62, 0xFF, 0x15)), FLinearColor::FromSRGBColor(FColor(0xFF, 0xAC, 0x00)), FLinearColor::FromSRGBColor(FColor(0xFF, 0x2D, 0x2D))};
		return Colors[int32(ClassifyCondition(Fraction))];
	}

	FSWGSpaceHudProjection Project(const FSWGSpaceHudView& View, const FVector& World)
	{
		FSWGSpaceHudProjection Result;
		const FVector Local = View.Rotation.UnrotateVector(World - View.Location);
		const FVector2D Raw(Local.Y, -Local.Z);
		if (Raw.SizeSquared() > UE_SMALL_NUMBER) Result.Direction = Raw.GetSafeNormal();
		else if (Local.X < 0.0) Result.Direction = FVector2D(0.0, 1.0);
		if (Local.X <= 1.0) return Result;

		const double HalfTangent = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(View.HorizontalFovDegrees, 5.f, 170.f) * 0.5f));
		const FVector2D Centre = View.Size * 0.5;
		const double PixelsPerTangent = Centre.X / HalfTangent;
		Result.Position = Centre + FVector2D(Local.Y, -Local.Z) / Local.X * PixelsPerTangent;
		Result.bOnScreen = Result.Position.X >= 0.0 && Result.Position.X <= View.Size.X && Result.Position.Y >= 0.0 && Result.Position.Y <= View.Size.Y;
		return Result;
	}

	FVector RadarSpherePosition(const FVector& Relative, float Range)
	{
		if (Range <= UE_SMALL_NUMBER) return FVector::ZeroVector;
		const FVector Scaled = Relative / Range;
		return Scaled.SizeSquared() > 1.0 ? Scaled.GetSafeNormal() : Scaled;
	}

	FVector2D RadarDiscPosition(const FVector& Relative, float Range)
	{
		const FVector Sphere = RadarSpherePosition(Relative, Range);
		return FVector2D(Sphere.Y, -Sphere.X);
	}

	FVector Deproject(const FSWGSpaceHudView& View, const FVector2D& ScreenPoint, double Distance)
	{
		const double HalfTangent = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(View.HorizontalFovDegrees, 5.f, 170.f) * 0.5f));
		const FVector2D Centre = View.Size * 0.5;
		const double PixelsPerTangent = Centre.X / HalfTangent;
		const FVector2D Offset = (ScreenPoint - Centre) / PixelsPerTangent;
		return View.Location + View.Rotation.RotateVector(FVector(1.0, Offset.X, -Offset.Y) * Distance);
	}

	double WorldSizeForPixels(const FSWGSpaceHudView& View, double Pixels, double Distance)
	{
		const double HalfTangent = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(View.HorizontalFovDegrees, 5.f, 170.f) * 0.5f));
		return Pixels / (View.Size.X * 0.5 / HalfTangent) * Distance;
	}

	ESWGSpaceHudRelation ClassifyRelation(const FString& OwnFaction, const FString& OtherFaction)
	{
		auto IsNeutral = [](const FString& Faction) { return Faction.IsEmpty() || Faction.Equals(TEXT("neutral"), ESearchCase::IgnoreCase); };
		if (IsNeutral(OtherFaction)) return ESWGSpaceHudRelation::Neutral;
		return !IsNeutral(OwnFaction) && OwnFaction.Equals(OtherFaction, ESearchCase::IgnoreCase) ? ESWGSpaceHudRelation::Friendly : ESWGSpaceHudRelation::Hostile;
	}

	FVector InterceptPoint(const FVector& TargetRelativeLocation, const FVector& TargetVelocity, float ProjectileSpeed)
	{
		if (ProjectileSpeed <= 0.f) return TargetRelativeLocation;
		const double Quadratic = TargetVelocity.SizeSquared() - double(ProjectileSpeed) * ProjectileSpeed;
		const double Linear = 2.0 * FVector::DotProduct(TargetRelativeLocation, TargetVelocity);
		const double Constant = TargetRelativeLocation.SizeSquared();
		double Time = -1.0;
		if (FMath::Abs(Quadratic) < UE_KINDA_SMALL_NUMBER)
		{
			if (Linear < 0.0) Time = -Constant / Linear;
		}
		else if (const double Discriminant = Linear * Linear - 4.0 * Quadratic * Constant; Discriminant >= 0.0)
		{
			const double Root = FMath::Sqrt(Discriminant);
			const double First = (-Linear - Root) / (2.0 * Quadratic);
			const double Second = (-Linear + Root) / (2.0 * Quadratic);
			const double Low = FMath::Min(First, Second), High = FMath::Max(First, Second);
			Time = Low > 0.0 ? Low : High;
		}
		return Time > 0.0 ? TargetRelativeLocation + TargetVelocity * Time : TargetRelativeLocation;
	}
}
