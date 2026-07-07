// Copyright Epic Games, Inc. All Rights Reserved.


#include "SWGEmuClientPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "SWGEmuClient.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "UI/ULoginWidget.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void ASWGEmuClientPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController())
	{
		if(ShouldUseTouchControls())
		{
			// spawn the mobile controls widget
			MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

			if (MobileControlsWidget)
			{
				// add the controls to the player screen
				MobileControlsWidget->AddToPlayerScreen(0);

			}
			else {

				UE_LOG(LogSWGEmuClient, Error, TEXT("Could not spawn mobile controls widget."));
			}
		}

		// Ensure the layout container exists before the flow's first transition fires —
		// HandleStateChanged looks it up via USWGGameLayout::GetLayout() and pushes
		// whichever widget StateTransitionTable maps to the resulting state change.
		bool bCreated = false;
		USWGGameLayout::GetOrCreate(this, LayoutWidgetClass, bCreated);

		if (bCreated)
		{
			if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>(); FlowSubsystem->GetState() == ESWGClientState::Initialization)
			{
				FlowSubsystem->StateTransitionTable = StateTransitionTable;
				FlowSubsystem->TransitionTo(ESWGClientState::Initialization);
			}
		}
	}
}

void ASWGEmuClientPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

bool ASWGEmuClientPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
