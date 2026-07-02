// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UZoneLoadingWidget.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void UZoneLoadingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Loading zone...")));
	}

	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->OnStatus.AddDynamic(this, &UZoneLoadingWidget::HandleStatusUpdate);
		FlowSubsystem->OnError.AddDynamic(this, &UZoneLoadingWidget::HandleStatusUpdate);
	}
}

void UZoneLoadingWidget::NativeDestruct()
{
	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->OnStatus.RemoveAll(this);
		FlowSubsystem->OnError.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void UZoneLoadingWidget::HandleStatusUpdate(FText Status)
{
	if (StatusText && !Status.IsEmpty())
	{
		StatusText->SetText(Status);
	}
}
