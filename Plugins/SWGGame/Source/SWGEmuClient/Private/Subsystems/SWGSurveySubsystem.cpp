#include "Subsystems/SWGSurveySubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Components/SWGSkillComponent.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ObjectMenuSelectMessage.h"
#include "Network/Messages/Zone/SurveyMessages.h"
#include "TRE/SWGResourceClassRow.h"
#include "Common/SWGWorldScale.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGSurvey, Log, All);

namespace
{
	/** Core3 SurveyToolImplementation::handleObjectMenuSelect: 20 uses, 133 is "Survey Range". */
	constexpr int32 RadialUse = 20;
	constexpr int32 RadialSurveyRange = 133;
}

float FSWGSurveyResult::SampleDensity(const FVector2D& RawPosition) const
{
	if (GridSize < 2 || Samples.Num() != GridSize * GridSize || Range <= 0.f)
	{
		return 0.f;
	}
	const float Spacing = Range / (GridSize - 1);
	const FVector2D NorthWest = Samples[0].Position;
	const float Column = (RawPosition.X - NorthWest.X) / Spacing;
	const float Row = (NorthWest.Y - RawPosition.Y) / Spacing;
	const float Last = GridSize - 1;
	if (Column < 0.f || Row < 0.f || Column > Last || Row > Last)
	{
		return 0.f;
	}
	const int32 Column0 = FMath::Min(FMath::FloorToInt(Column), GridSize - 2);
	const int32 Row0 = FMath::Min(FMath::FloorToInt(Row), GridSize - 2);
	const float Across = Column - Column0;
	const float Down = Row - Row0;
	auto At = [this](int32 GridRow, int32 GridColumn) { return Samples[GridRow * GridSize + GridColumn].Density; };
	const float Top = FMath::Lerp(At(Row0, Column0), At(Row0, Column0 + 1), Across);
	const float Bottom = FMath::Lerp(At(Row0 + 1, Column0), At(Row0 + 1, Column0 + 1), Across);
	return FMath::Lerp(Top, Bottom, Down);
}

void USWGSurveySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Commands = Collection.InitializeDependency<USWGCommandSubsystem>();
	ObjectGraph = Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();
	Radial = Collection.InitializeDependency<USWGRadialMenuSubsystem>();
	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGSurveySubsystem::HandleMessageReceived);
	}
	if (Radial)
	{
		RadialHandle = Radial->OnServerOptionSelected.AddUObject(this, &USWGSurveySubsystem::HandleServerOptionSelected);
	}
}

void USWGSurveySubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	if (Radial)
	{
		Radial->OnServerOptionSelected.Remove(RadialHandle);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(PendingTimeout);
	}
	Super::Deinitialize();
}

bool USWGSurveySubsystem::IsSurveyTool(int64 ObjectId) const
{
	const uint32 Crc = ObjectGraph ? ObjectGraph->FindObjectCrc(ObjectId) : 0;
	return Crc != 0 && Tre && Tre->ResolveTemplatePath(Crc).Contains(TEXT("object/tangible/survey_tool/"));
}

void USWGSurveySubsystem::HandleServerOptionSelected(int64 ObjectId, int32 RadialId)
{
	if (RadialId == RadialUse && IsSurveyTool(ObjectId))
	{
		LastUsedToolId = ObjectId;
	}
}

void USWGSurveySubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message)
	{
		return;
	}
	switch (static_cast<ESWGMessageOp>(Message->Opcode))
	{
		case ESWGMessageOp::ResourceListForSurvey:
		{
			const FResourceListForSurveyMessage& List = *static_cast<const FResourceListForSurveyMessage*>(Message.Get());
			TArray<FSWGSurveyResource> NewResources;
			for (const FSWGSurveyResourceEntry& Entry : List.Resources)
			{
				FSWGSurveyResource& Resource = NewResources.AddDefaulted_GetRef();
				Resource.Name = Entry.Name;
				Resource.ObjectId = static_cast<int64>(Entry.ObjectId);
				Resource.Type = Entry.Type;
			}
			ToolObjectId = LastUsedToolId;
			UE_LOG(LogSWGSurvey, Log, TEXT("resource list: %d %s resource(s), tool %lld"), NewResources.Num(), *List.SurveyType, ToolObjectId);
			InjectResources(NewResources, List.SurveyType);
			break;
		}
		case ESWGMessageOp::SurveyMessage:
		{
			const FSurveyMessage& Survey = *static_cast<const FSurveyMessage*>(Message.Get());
			TArray<FVector2D> Positions;
			TArray<float> Densities;
			for (const FSWGSurveyPoint& Point : Survey.Points)
			{
				Positions.Add(FVector2D(Point.X, Point.Y));
				Densities.Add(Point.Density);
			}
			const FString ResourceName = PendingResource.IsEmpty() ? SelectedResource : PendingResource;
			const FSWGSurveyResult Result = BuildResult(ResourceName, Positions, Densities);
			UE_LOG(LogSWGSurvey, Log, TEXT("survey result for '%s': %d point(s), best %.0f%% at (%.1f, %.1f)"), *ResourceName, Survey.Points.Num(),
				Result.IsValid() ? Result.GetBest().Density * 100.f : 0.f, Result.IsValid() ? Result.GetBest().Position.X : 0.f, Result.IsValid() ? Result.GetBest().Position.Y : 0.f);
			InjectResult(Result);
			break;
		}
		default:
			break;
	}
}

FSWGSurveyResult USWGSurveySubsystem::BuildResult(const FString& ResourceName, const TArray<FVector2D>& Positions, const TArray<float>& Densities)
{
	FSWGSurveyResult Result;
	Result.ResourceName = ResourceName;
	const int32 Count = FMath::Min(Positions.Num(), Densities.Num());
	FVector2D Sum = FVector2D::ZeroVector;
	float BestDensity = -1.f;
	for (int32 PointIndex = 0; PointIndex < Count; ++PointIndex)
	{
		FSWGSurveySample& Sample = Result.Samples.AddDefaulted_GetRef();
		Sample.Position = Positions[PointIndex];
		Sample.Density = Densities[PointIndex];
		Sum += Sample.Position;
		// Strictly greater, as Core3 picks the waypoint point.
		if (Sample.Density > BestDensity)
		{
			BestDensity = Sample.Density;
			Result.BestIndex = PointIndex;
		}
	}
	if (Count == 0)
	{
		return Result;
	}
	Result.Center = Sum / Count;
	Result.GridSize = FMath::RoundToInt(FMath::Sqrt(static_cast<float>(Count)));
	if (Result.GridSize * Result.GridSize != Count)
	{
		Result.GridSize = 0;
	}
	else if (Result.GridSize > 1)
	{
		Result.Range = FMath::Abs(Result.Samples[Result.GridSize - 1].Position.X - Result.Samples[0].Position.X);
	}
	return Result;
}

void USWGSurveySubsystem::InjectResources(const TArray<FSWGSurveyResource>& InResources, const FString& InSurveyType)
{
	Resources = InResources;
	SurveyType = InSurveyType;
	for (FSWGSurveyResource& Resource : Resources)
	{
		if (!ResolveResourceClass(Resource.Type, Resource.ClassName, Resource.ClassPath))
		{
			Resource.ClassName = Resource.Type;
		}
	}
	Resources.Sort([](const FSWGSurveyResource& A, const FSWGSurveyResource& B)
	{
		return A.ClassName != B.ClassName ? A.ClassName < B.ClassName : A.Name < B.Name;
	});
	if (!Resources.ContainsByPredicate([this](const FSWGSurveyResource& Resource) { return Resource.Name == SelectedResource; }))
	{
		SelectedResource = Resources.IsEmpty() ? FString() : Resources[0].Name;
	}
	OnResourcesChanged.Broadcast();
	OnSurveyWindowRequested.Broadcast();
}

void USWGSurveySubsystem::InjectResult(const FSWGSurveyResult& Result)
{
	ClearPending();
	LastResult = Result;
	OnSurveyResultReceived.Broadcast();
	OnSurveyStateChanged.Broadcast();
}

void USWGSurveySubsystem::SetToolActive(bool bActive)
{
	if (bToolActive != bActive)
	{
		bToolActive = bActive;
		OnSurveyStateChanged.Broadcast();
	}
}

void USWGSurveySubsystem::SetSelectedResource(const FString& ResourceName)
{
	if (SelectedResource != ResourceName)
	{
		SelectedResource = ResourceName;
		OnSurveyStateChanged.Broadcast();
	}
}

bool USWGSurveySubsystem::RequestSurvey()
{
	if (!Commands || SelectedResource.IsEmpty() || bSurveyPending)
	{
		return false;
	}
	// RequestSurveyCommand passes the whole argument string on as the resource name.
	if (Commands->SendCommand(TEXT("requestSurvey"), 0, SelectedResource) == 0)
	{
		return false;
	}
	PendingResource = SelectedResource;
	bSurveyPending = true;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().SetTimer(PendingTimeout, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogSWGSurvey, Log, TEXT("survey for '%s' got no reply"), *PendingResource);
			ClearPending();
			OnSurveyStateChanged.Broadcast();
		}), SurveyTimeoutSeconds, false);
	}
	OnSurveyStateChanged.Broadcast();
	return true;
}

void USWGSurveySubsystem::ClearPending()
{
	bSurveyPending = false;
	PendingResource.Reset();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(PendingTimeout);
	}
}

bool USWGSurveySubsystem::RequestSample()
{
	if (!Commands || SelectedResource.IsEmpty())
	{
		return false;
	}
	return Commands->SendCommand(TEXT("requestCoreSample"), 0, SelectedResource) != 0;
}

bool USWGSurveySubsystem::RequestRangeSettings()
{
	if (!Network || ToolObjectId == 0)
	{
		return false;
	}
	FObjectMenuSelectMessage Select;
	Select.ObjectId = ToolObjectId;
	Select.RadialId = static_cast<uint8>(RadialSurveyRange);
	Network->SendMessage(Select.Serialize());
	return true;
}

int32 USWGSurveySubsystem::GetSkillRange() const
{
	const AActor* Player = ObjectGraph ? ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
	const USWGSkillComponent* Skills = Player ? Player->FindComponentByClass<USWGSkillComponent>() : nullptr;
	if (!Skills || !Skills->bHasBase4)
	{
		return 0;
	}
	const FSkillModifier* Mod = Skills->SkillMods.Items.FindByPredicate([](const FSkillModifier& Candidate)
	{
		return Candidate.SkillModString == TEXT("surveying");
	});
	const int32 Surveying = Mod ? Mod->BaseValue + Mod->Modifier : 0;
	// Core3 SurveyToolImplementation::getSkillBasedRange.
	return Surveying >= 120 ? 384 : Surveying >= 100 ? 320 : Surveying >= 75 ? 256
		: Surveying >= 55 ? 192 : Surveying >= 35 ? 128 : Surveying >= 20 ? 64 : 0;
}

int32 USWGSurveySubsystem::GetCurrentRange() const
{
	return LastResult.Range > 0.f ? FMath::RoundToInt(LastResult.Range) : GetSkillRange();
}

bool USWGSurveySubsystem::ResolveResourceClass(const FString& Type, FString& OutClassName, TArray<FString>& OutClassPath)
{
	if (!bTriedResourceTree)
	{
		bTriedResourceTree = true;
		LoadResourceTree();
	}
	const FResourceClass* Found = ResourceClasses.Find(Type.ToLower());
	if (!Found)
	{
		return false;
	}
	OutClassName = Found->Name;
	OutClassPath = Found->Path;
	return true;
}

void USWGSurveySubsystem::LoadResourceTree()
{
	// Built from resource_tree.iff at startup by SWGInitializationState.
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, *SWGResourceClass::DataTablePath);
	if (!Table)
	{
		UE_LOG(LogSWGSurvey, Warning, TEXT("%s not built; resources show their type names"), *SWGResourceClass::DataTablePath);
		return;
	}
	for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
	{
		const FSWGResourceClassRow& ClassRow = *reinterpret_cast<const FSWGResourceClassRow*>(Row.Value);
		FResourceClass& Entry = ResourceClasses.Add(Row.Key.ToString().ToLower());
		Entry.Name = ClassRow.DisplayName;
		FString Parent = ClassRow.ParentClass;
		for (int32 Depth = 0; Depth < 16 && !Parent.IsEmpty(); ++Depth)
		{
			const FSWGResourceClassRow* ParentRow = Table->FindRow<FSWGResourceClassRow>(FName(*Parent), TEXT("Survey"), false);
			if (!ParentRow)
			{
				break;
			}
			Entry.Path.Insert(ParentRow->DisplayName, 0);
			Parent = ParentRow->ParentClass;
		}
	}
}

FText USWGSurveySubsystem::GetSurveyTypeDisplayName() const
{
	FString ClassName;
	TArray<FString> ClassPath;
	if (const_cast<USWGSurveySubsystem*>(this)->ResolveResourceClass(SurveyType, ClassName, ClassPath))
	{
		return FText::FromString(ClassName);
	}
	FString Display = SurveyType.Replace(TEXT("_"), TEXT(" "));
	if (!Display.IsEmpty())
	{
		Display[0] = FChar::ToUpper(Display[0]);
	}
	return FText::FromString(Display);
}
