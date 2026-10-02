#include "SWGUISubsystem.h"
#include "SWGUICommonSettings.h"
#include "CommonInputSubsystem.h"
#include "Subsystems/SWGSurveySubsystem.h"
#include "Subsystems/SWGCraftingSubsystem.h"
#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "Subsystems/SWGMissionSubsystem.h"
#include "Subsystems/SWGTravelSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

namespace
{
	ESWGUIPresentation Other(ESWGUIPresentation Presentation)
	{
		switch (Presentation)
		{
		case ESWGUIPresentation::Window: return ESWGUIPresentation::Holo;
		case ESWGUIPresentation::Holo: return ESWGUIPresentation::Window;
		default: return Presentation;
		}
	}
}

void USWGUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const USWGUICommonSettings& Settings = USWGUICommonSettings::Get();
	Presentations.Add(ESWGUIFeature::Inventory, Settings.InventoryPresentation);
	Presentations.Add(ESWGUIFeature::PlanetMap, Settings.PlanetMapPresentation);
	Presentations.Add(ESWGUIFeature::Survey, Settings.SurveyPresentation);
	Presentations.Add(ESWGUIFeature::Crafting, Settings.CraftingPresentation);

	UGameInstance* GameInstance = GetLocalPlayer()->GetGameInstance();
	if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
	{
		Examine->OnExamineRequested.AddDynamic(this, &USWGUISubsystem::HandleExamineRequested);
	}
	if (USWGMissionSubsystem* Missions = GameInstance->GetSubsystem<USWGMissionSubsystem>())
	{
		Missions->OnMissionWindowRequested.AddDynamic(this, &USWGUISubsystem::HandleMissionWindowRequested);
		Missions->OnMissionListChanged.AddDynamic(this, &USWGUISubsystem::HandleMissionListChanged);
	}
	if (USWGTravelSubsystem* Travel = GameInstance->GetSubsystem<USWGTravelSubsystem>())
	{
		Travel->OnTravelWindowRequested.AddDynamic(this, &USWGUISubsystem::HandleTravelWindowRequested);
	}
	if (USWGSurveySubsystem* Survey = GameInstance->GetSubsystem<USWGSurveySubsystem>())
	{
		Survey->OnSurveyWindowRequested.AddDynamic(this, &USWGUISubsystem::HandleSurveyWindowRequested);
	}
	if (USWGCraftingSubsystem* Crafting = GameInstance->GetSubsystem<USWGCraftingSubsystem>())
	{
		Crafting->OnSessionStarted.AddDynamic(this, &USWGUISubsystem::HandleCraftingSessionStarted);
	}
	if (USWGStructurePlacementSubsystem* Placement = GameInstance->GetSubsystem<USWGStructurePlacementSubsystem>())
	{
		Placement->OnPlacementStarted.AddDynamic(this, &USWGUISubsystem::HandlePlacementStarted);
	}
}

void USWGUISubsystem::Deinitialize()
{
	if (UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr)
	{
		if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
		{
			Examine->OnExamineRequested.RemoveAll(this);
		}
		if (USWGMissionSubsystem* Missions = GameInstance->GetSubsystem<USWGMissionSubsystem>())
		{
			Missions->OnMissionWindowRequested.RemoveAll(this);
			Missions->OnMissionListChanged.RemoveAll(this);
		}
		if (USWGTravelSubsystem* Travel = GameInstance->GetSubsystem<USWGTravelSubsystem>())
		{
			Travel->OnTravelWindowRequested.RemoveAll(this);
		}
		if (USWGSurveySubsystem* Survey = GameInstance->GetSubsystem<USWGSurveySubsystem>())
		{
			Survey->OnSurveyWindowRequested.RemoveAll(this);
		}
		if (USWGCraftingSubsystem* Crafting = GameInstance->GetSubsystem<USWGCraftingSubsystem>())
		{
			Crafting->OnSessionStarted.RemoveAll(this);
		}
		if (USWGStructurePlacementSubsystem* Placement = GameInstance->GetSubsystem<USWGStructurePlacementSubsystem>())
		{
			Placement->OnPlacementStarted.RemoveAll(this);
		}
	}

	Presenters.Reset();
	Super::Deinitialize();
}

// ── Presenters and features ──────────────────────────────────────────────────

void USWGUISubsystem::RegisterPresenter(ISWGUIPresenter& Presenter)
{
	Presenters.Add(Presenter.GetPresentation(), &Presenter);
}

void USWGUISubsystem::UnregisterPresenter(ISWGUIPresenter& Presenter)
{
	if (Presenters.FindRef(Presenter.GetPresentation()) == &Presenter)
	{
		Presenters.Remove(Presenter.GetPresentation());
	}
}

ESWGUIPresentation USWGUISubsystem::GetPresentation(ESWGUIFeature Feature) const
{
	const ESWGUIPresentation* Found = Presentations.Find(Feature);
	if (Found)
	{
		return *Found;
	}
	// The HUD has one form; the rest default to windows.
	return Feature == ESWGUIFeature::Hud ? ESWGUIPresentation::Core : ESWGUIPresentation::Window;
}

ISWGUIPresenter* USWGUISubsystem::FindOpenPresenter(ESWGUIFeature Feature) const
{
	for (const TPair<ESWGUIPresentation, ISWGUIPresenter*>& Entry : Presenters)
	{
		if (Entry.Value && Entry.Value->SupportsFeature(Feature) && Entry.Value->IsFeatureOpen(Feature))
		{
			return Entry.Value;
		}
	}
	return nullptr;
}

ISWGUIPresenter* USWGUISubsystem::ResolvePresenter(ESWGUIFeature Feature) const
{
	// The wanted presentation if it exists and can open the feature now, otherwise the other one: a project with
	// only one visual plugin enabled still gets every feature that plugin provides.
	const ESWGUIPresentation Wanted = GetPresentation(Feature);
	for (const ESWGUIPresentation Candidate : { Wanted, Other(Wanted) })
	{
		ISWGUIPresenter* Presenter = Presenters.FindRef(Candidate);
		if (Presenter && Presenter->SupportsFeature(Feature) && Presenter->CanOpenFeature(Feature))
		{
			return Presenter;
		}
	}
	return nullptr;
}

void USWGUISubsystem::OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request)
{
	// A tool used again with its view up refreshes that view in place, whichever presentation it is in.
	ISWGUIPresenter* Presenter = FindOpenPresenter(Feature);
	if (!Presenter)
	{
		Presenter = ResolvePresenter(Feature);
	}
	if (!Presenter)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no presenter can open %s (is its UI plugin enabled?)"),
			*UEnum::GetValueAsString(Feature));
		return;
	}
	Presenter->OpenFeature(Feature, Request);
	NotifyFeatureStateChanged(Feature);
}

void USWGUISubsystem::ToggleFeature(ESWGUIFeature Feature)
{
	ISWGUIPresenter* Presenter = FindOpenPresenter(Feature);
	if (!Presenter)
	{
		Presenter = ResolvePresenter(Feature);
	}
	if (Presenter)
	{
		Presenter->ToggleFeature(Feature);
		NotifyFeatureStateChanged(Feature);
	}
}

void USWGUISubsystem::CloseFeature(ESWGUIFeature Feature)
{
	for (const TPair<ESWGUIPresentation, ISWGUIPresenter*>& Entry : Presenters)
	{
		if (Entry.Value && Entry.Value->SupportsFeature(Feature))
		{
			Entry.Value->CloseFeature(Feature);
		}
	}
	NotifyFeatureStateChanged(Feature);
}

bool USWGUISubsystem::IsFeatureOpen(ESWGUIFeature Feature) const
{
	return FindOpenPresenter(Feature) != nullptr;
}

void USWGUISubsystem::SetPresentation(ESWGUIFeature Feature, ESWGUIPresentation Presentation)
{
	ISWGUIPresenter* Target = Presenters.FindRef(Presentation);
	if (!Target || !Target->SupportsFeature(Feature))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: %s has no %s presentation (that plugin is not enabled)"),
			*UEnum::GetValueAsString(Feature), *UEnum::GetValueAsString(Presentation));
		return;
	}
	if (!Target->CanOpenFeature(Feature))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: %s cannot show %s right now; keeping the current form."),
			*UEnum::GetValueAsString(Presentation), *UEnum::GetValueAsString(Feature));
		return;
	}

	Presentations.Add(Feature, Presentation);
	bool bWasOpen = false;
	for (const TPair<ESWGUIPresentation, ISWGUIPresenter*>& Entry : Presenters)
	{
		if (Entry.Value && Entry.Key != Presentation && Entry.Value->SupportsFeature(Feature) && Entry.Value->IsFeatureOpen(Feature))
		{
			bWasOpen = true;
			Entry.Value->CloseFeature(Feature);
		}
	}
	if (bWasOpen && !Target->IsFeatureOpen(Feature))
	{
		Target->OpenFeature(Feature, FSWGUIFeatureRequest());
	}
	NotifyFeatureStateChanged(Feature);
}

void USWGUISubsystem::NotifyFeatureStateChanged(ESWGUIFeature Feature)
{
	if (Feature == ESWGUIFeature::Survey)
	{
		RefreshSurveyToolActive();
	}
}

void USWGUISubsystem::RefreshSurveyToolActive()
{
	UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	if (USWGSurveySubsystem* Survey = GameInstance ? GameInstance->GetSubsystem<USWGSurveySubsystem>() : nullptr)
	{
		Survey->SetToolActive(IsFeatureOpen(ESWGUIFeature::Survey));
	}
}

bool USWGUISubsystem::IsGamepadActive() const
{
	const UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetLocalPlayer());
	return CommonInput && CommonInput->GetCurrentInputType() == ECommonInputType::Gamepad;
}

void USWGUISubsystem::OpenExamine(int64 ObjectId)
{
	FSWGUIFeatureRequest Request;
	Request.ObjectId = ObjectId;
	OpenFeature(ESWGUIFeature::Examine, Request);
}

// ── Gameplay events → features ───────────────────────────────────────────────

void USWGUISubsystem::HandleExamineRequested(int64 ObjectId)
{
	OpenExamine(ObjectId);
}

void USWGUISubsystem::HandleMissionWindowRequested(int64 TerminalObjectId)
{
	FSWGUIFeatureRequest Request;
	Request.ObjectId = TerminalObjectId;
	OpenFeature(ESWGUIFeature::MissionBrowser, Request);
}

void USWGUISubsystem::HandleMissionListChanged()
{
	for (const TPair<ESWGUIPresentation, ISWGUIPresenter*>& Entry : Presenters)
	{
		if (Entry.Value)
		{
			Entry.Value->NotifyDataChanged(ESWGUIFeature::MissionBrowser);
		}
	}
}

void USWGUISubsystem::HandleTravelWindowRequested()
{
	OpenFeature(ESWGUIFeature::Travel, FSWGUIFeatureRequest());
}

void USWGUISubsystem::HandleSurveyWindowRequested()
{
	FSWGUIFeatureRequest Request;
	Request.bFromTool = true;
	OpenFeature(ESWGUIFeature::Survey, Request);
}

void USWGUISubsystem::HandleCraftingSessionStarted()
{
	FSWGUIFeatureRequest Request;
	Request.bFromTool = true;
	OpenFeature(ESWGUIFeature::Crafting, Request);
}

void USWGUISubsystem::HandlePlacementStarted()
{
	// Placement takes over the camera and the screen, and the deed was most likely used from the inventory.
	CloseFeature(ESWGUIFeature::Inventory);
	OpenFeature(ESWGUIFeature::StructurePlacement, FSWGUIFeatureRequest());
}

// ── Presenter-owned state ────────────────────────────────────────────────────

void USWGUISubsystem::NotifyRadialMenuClosed()
{
	for (const TPair<ESWGUIPresentation, ISWGUIPresenter*>& Entry : Presenters)
	{
		if (Entry.Value)
		{
			Entry.Value->NotifyRadialMenuClosed();
		}
	}
}

float USWGUISubsystem::GetHudOpacity() const
{
	const ISWGUIPresenter* Presenter = Presenters.FindRef(ESWGUIPresentation::Core);
	return Presenter ? Presenter->GetHudOpacity() : 1.f;
}

void USWGUISubsystem::SetHudOpacity(float Opacity)
{
	if (ISWGUIPresenter* Presenter = Presenters.FindRef(ESWGUIPresentation::Core))
	{
		Presenter->SetHudOpacity(Opacity);
	}
}

