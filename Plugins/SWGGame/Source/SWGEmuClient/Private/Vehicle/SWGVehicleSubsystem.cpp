#include "Vehicle/SWGVehicleSubsystem.h"

#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Objects/Creature/SWGCreature.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGRegionSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Vehicle/SWGVehicleDynamicsComponent.h"
#include "Vehicle/SWGVehicleEngine.h"

namespace
{
	constexpr float ScanIntervalSeconds = 0.5f;
}

bool USWGVehicleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld());
}

void USWGVehicleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		GameInstance->GetSubsystem<USWGRegionSubsystem>();
	}
}

TStatId USWGVehicleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGVehicleSubsystem, STATGROUP_Tickables);
}

TArray<USWGVehicleDynamicsComponent*> USWGVehicleSubsystem::GetDynamics() const
{
	TArray<USWGVehicleDynamicsComponent*> Result;
	for (TActorIterator<ASWGCreature> It(GetWorld()); It; ++It)
	{
		if (USWGVehicleDynamicsComponent* Dynamics = It->FindComponentByClass<USWGVehicleDynamicsComponent>())
		{
			Result.Add(Dynamics);
		}
	}
	return Result;
}

TSharedPtr<const FSWGClientDataFile> USWGVehicleSubsystem::FindClientData(uint32 TemplateCrc, FString& OutPath)
{
	if (const FVehicleData* Cached = DataByCrc.Find(TemplateCrc))
	{
		OutPath = Cached->Path;
		return Cached->Data;
	}

	FVehicleData& Entry = DataByCrc.Add(TemplateCrc);
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (!Tre)
	{
		DataByCrc.Remove(TemplateCrc);
		return nullptr;
	}

	if (!bTriedTable)
	{
		bTriedTable = true;
		FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/mount/saddle_appearance_map.iff")), SaddleAppearanceMap);
	}

	FString Appearance;
	const FString TemplatePath = Tre->ResolveTemplatePath(TemplateCrc);
	if (!TemplatePath.IsEmpty() && Tre->FindTemplateFormStringParam(TemplatePath, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("appearanceFilename"), Appearance))
	{
		Entry.Path = FSWGVehicleEngine::FindClientDataPath(SaddleAppearanceMap, Appearance);
	}
	if (!Entry.Path.IsEmpty())
	{
		TSharedRef<FSWGClientDataFile> Data = MakeShared<FSWGClientDataFile>();
		if (FSWGClientDataFileReader::Read(Tre->CreateIffReader(Entry.Path), *Data) && !Data->Thrusts.IsEmpty())
		{
			Entry.Data = Data;
		}
	}
	OutPath = Entry.Path;
	return Entry.Data;
}

void USWGVehicleSubsystem::AdoptVehicles()
{
	for (TActorIterator<ASWGCreature> It(GetWorld()); It; ++It)
	{
		ASWGCreature* Creature = *It;
		if (Creature->SWGObjectCRC == 0 || Creature->FindComponentByClass<USWGVehicleDynamicsComponent>()
			|| Creature->GetComponentsByTag(UPrimitiveComponent::StaticClass(), USWGMeshGeneratorSubsystem::VehicleBodyTag).IsEmpty())
		{
			continue;
		}

		FString Path;
		const TSharedPtr<const FSWGClientDataFile> Data = FindClientData(Creature->SWGObjectCRC, Path);
		if (!Data)
		{
			continue;
		}

		USWGVehicleDynamicsComponent* Dynamics = NewObject<USWGVehicleDynamicsComponent>(Creature, TEXT("VehicleDynamics"));
		Dynamics->Setup(Path, *Data);
		Dynamics->RegisterComponent();
		UE_LOG(LogTemp, Log, TEXT("USWGVehicleSubsystem: %s hover dynamics + engine sound from %s (%d thrust level(s))"),
			*Creature->GetName(), *Path, Data->Thrusts.Num());
	}
}

void USWGVehicleSubsystem::Tick(float DeltaTime)
{
	SinceScanSeconds += DeltaTime;
	if (SinceScanSeconds >= ScanIntervalSeconds)
	{
		SinceScanSeconds = 0.f;
		AdoptVehicles();
	}
}
