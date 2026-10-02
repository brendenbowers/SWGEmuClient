#include "SWGHoloUISubsystem.h"
#include "SWGHoloUISettings.h"
#include "SWGUISubsystem.h"
#include "SWGHoloMapWidget.h"
#include "SWGHoloInventoryWidget.h"
#include "SWGHoloSurveyWidget.h"
#include "SWGHoloCraftingWidget.h"
#include "Subsystems/SWGCraftingSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Under the layout (100) and windows, over the world. */
	constexpr int32 HoloZOrder = 90;
}

void USWGHoloUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The router first, so it exists to register with.
	if (USWGUISubsystem* Router = Collection.InitializeDependency<USWGUISubsystem>())
	{
		Router->RegisterPresenter(*this);
	}
}

void USWGHoloUISubsystem::Deinitialize()
{
	if (USWGUISubsystem* Router = GetRouter())
	{
		Router->UnregisterPresenter(*this);
	}
	Super::Deinitialize();
}

USWGUISubsystem* USWGHoloUISubsystem::GetRouter() const
{
	return GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<USWGUISubsystem>() : nullptr;
}

APlayerController* USWGHoloUISubsystem::GetController() const
{
	return GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
}

bool USWGHoloUISubsystem::SupportsFeature(ESWGUIFeature Feature) const
{
	return Feature == ESWGUIFeature::Inventory || Feature == ESWGUIFeature::PlanetMap
		|| Feature == ESWGUIFeature::Survey || Feature == ESWGUIFeature::Crafting;
}

bool USWGHoloUISubsystem::CanOpenFeature(ESWGUIFeature Feature) const
{
	if (Feature != ESWGUIFeature::Crafting)
	{
		return SupportsFeature(Feature);
	}
	// Holo crafting covers Draft and Assembly only; a session in any other stage keeps the window.
	UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	USWGCraftingSubsystem* Crafting = GameInstance ? GameInstance->GetSubsystem<USWGCraftingSubsystem>() : nullptr;
	return !(Crafting && Crafting->IsSessionOpen()
		&& Crafting->GetState() != ESWGCraftingSessionState::ChoosingSchematic
		&& Crafting->GetState() != ESWGCraftingSessionState::Assembling);
}

void USWGHoloUISubsystem::OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request)
{
	switch (Feature)
	{
	case ESWGUIFeature::Inventory: OpenInventory(); break;
	case ESWGUIFeature::PlanetMap: if (!HoloMap) { OpenPlanetMap(); } break;
	case ESWGUIFeature::Survey:
		// Using the tool again with the survey up just refreshes it in place.
		if (HoloSurvey) { HoloSurvey->ShowPicker(); } else { OpenSurvey(); }
		break;
	case ESWGUIFeature::Crafting: if (!HoloCrafting) { OpenCrafting(); } break;
	default: break;
	}
}

void USWGHoloUISubsystem::CloseFeature(ESWGUIFeature Feature)
{
	switch (Feature)
	{
	case ESWGUIFeature::Inventory: if (HoloInventory) { HoloInventory->Close(); } break;
	case ESWGUIFeature::PlanetMap: if (HoloMap) { HoloMap->Close(); } break;
	case ESWGUIFeature::Survey: if (HoloSurvey) { HoloSurvey->Close(); } break;
	case ESWGUIFeature::Crafting: if (HoloCrafting) { HoloCrafting->Dismiss(); } break;
	default: break;
	}
}

bool USWGHoloUISubsystem::IsFeatureOpen(ESWGUIFeature Feature) const
{
	switch (Feature)
	{
	case ESWGUIFeature::Inventory: return HoloInventory != nullptr;
	case ESWGUIFeature::PlanetMap: return HoloMap != nullptr;
	case ESWGUIFeature::Survey: return HoloSurvey != nullptr;
	case ESWGUIFeature::Crafting: return HoloCrafting != nullptr;
	default: return false;
	}
}

void USWGHoloUISubsystem::StepAsideCrafting()
{
	if (!HoloCrafting)
	{
		return;
	}
	USWGUISubsystem* Router = GetRouter();
	ISWGUIPresenter* Window = Router ? Router->GetPresenter(ESWGUIPresentation::Window) : nullptr;
	if (Window && Window->SupportsFeature(ESWGUIFeature::Crafting))
	{
		Router->SetPresentation(ESWGUIFeature::Crafting, ESWGUIPresentation::Window);
	}
	else
	{
		HoloCrafting->Dismiss();
	}
}

void USWGHoloUISubsystem::OpenInventory()
{
	StepAsideCrafting();
	APlayerController* PlayerController = GetController();
	if (!PlayerController || HoloInventory)
	{
		return;
	}
	// Both take over the camera; only one hologram at a time.
	if (HoloMap)
	{
		HoloMap->Close();
	}
	if (HoloSurvey)
	{
		HoloSurvey->Close();
	}
	TSubclassOf<USWGHoloInventoryWidget> HoloInventoryClass = USWGHoloUISettings::Get().HoloInventoryClass.LoadSynchronous();
	if (!HoloInventoryClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGHoloUISubsystem: HoloInventoryClass is unset (Project Settings > SWG UI (Holo)); no holo inventory."));
		return;
	}
	HoloInventory = CreateWidget<USWGHoloInventoryWidget>(PlayerController, HoloInventoryClass);
	HoloInventory->OnClosed.AddUObject(this, &USWGHoloUISubsystem::HandleHoloInventoryClosed);
	HoloInventory->OnSwitchToWindow.AddWeakLambda(this, [this]()
	{
		if (USWGUISubsystem* Router = GetRouter()) { Router->SetPresentation(ESWGUIFeature::Inventory, ESWGUIPresentation::Window); }
	});
	HoloInventory->AddToPlayerScreen(HoloZOrder);
}

void USWGHoloUISubsystem::HandleHoloInventoryClosed()
{
	HoloInventory = nullptr;
}

void USWGHoloUISubsystem::OpenPlanetMap()
{
	APlayerController* PlayerController = GetController();
	if (!PlayerController)
	{
		return;
	}
	TSubclassOf<USWGHoloMapWidget> HoloMapClass = USWGHoloUISettings::Get().HoloMapClass.LoadSynchronous();
	if (HoloInventory)
	{
		HoloInventory->Close();
	}
	// A scan in progress steps aside for the map and comes back when it closes; the map draws the scan meanwhile.
	if (HoloSurvey)
	{
		HoloSurvey->SetSuspended(true);
	}
	if (HoloCrafting) { HoloCrafting->SetSuspended(true); }
	HoloMap = CreateWidget<USWGHoloMapWidget>(PlayerController, HoloMapClass ? HoloMapClass.Get() : USWGHoloMapWidget::StaticClass());
	HoloMap->OnClosed.AddUObject(this, &USWGHoloUISubsystem::HandleHoloMapClosed);
	HoloMap->OnSwitchToWindow.AddWeakLambda(this, [this]()
	{
		if (USWGUISubsystem* Router = GetRouter()) { Router->SetPresentation(ESWGUIFeature::PlanetMap, ESWGUIPresentation::Window); }
	});
	HoloMap->AddToPlayerScreen(HoloZOrder);
}

void USWGHoloUISubsystem::HandleHoloMapClosed()
{
	HoloMap = nullptr;
	if (HoloSurvey)
	{
		HoloSurvey->SetSuspended(false);
	}
	if (HoloCrafting) { HoloCrafting->SetSuspended(false); }
}

void USWGHoloUISubsystem::OpenSurvey()
{
	APlayerController* PlayerController = GetController();
	if (!PlayerController)
	{
		return;
	}
	StepAsideCrafting();
	// Every hologram takes over the camera; only one at a time.
	if (HoloMap)
	{
		HoloMap->Close();
	}
	if (HoloInventory)
	{
		HoloInventory->Close();
	}
	TSubclassOf<USWGHoloSurveyWidget> HoloSurveyClass = USWGHoloUISettings::Get().HoloSurveyClass.LoadSynchronous();
	HoloSurvey = CreateWidget<USWGHoloSurveyWidget>(PlayerController, HoloSurveyClass ? HoloSurveyClass.Get() : USWGHoloSurveyWidget::StaticClass());
	HoloSurvey->OnClosed.AddWeakLambda(this, [this]()
	{
		HoloSurvey = nullptr;
		if (USWGUISubsystem* Router = GetRouter()) { Router->NotifyFeatureStateChanged(ESWGUIFeature::Survey); }
	});
	HoloSurvey->OnSwitchToWindow.AddWeakLambda(this, [this]()
	{
		if (USWGUISubsystem* Router = GetRouter()) { Router->SetPresentation(ESWGUIFeature::Survey, ESWGUIPresentation::Window); }
	});
	HoloSurvey->AddToPlayerScreen(HoloZOrder);
}

void USWGHoloUISubsystem::OpenCrafting()
{
	APlayerController* PlayerController = GetController();
	if (!PlayerController) { return; }
	if (HoloMap) { HoloMap->Close(); }
	if (HoloInventory) { HoloInventory->Close(); }
	if (HoloSurvey) { HoloSurvey->Close(); }
	TSubclassOf<USWGHoloCraftingWidget> HoloClass = USWGHoloUISettings::Get().HoloCraftingClass.LoadSynchronous();
	HoloCrafting = CreateWidget<USWGHoloCraftingWidget>(PlayerController, HoloClass ? HoloClass.Get() : USWGHoloCraftingWidget::StaticClass());
	HoloCrafting->OnClosed.AddWeakLambda(this, [this]() { HoloCrafting = nullptr; });
	HoloCrafting->OnSwitchToWindow.AddWeakLambda(this, [this]()
	{
		if (USWGUISubsystem* Router = GetRouter()) { Router->SetPresentation(ESWGUIFeature::Crafting, ESWGUIPresentation::Window); }
	});
	HoloCrafting->AddToPlayerScreen(HoloZOrder);
}
