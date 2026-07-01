// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ULoginWidget.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Misc/ScopeExit.h"


void ULoginWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ConnectButton)
    {
        ConnectButton->OnClicked.AddDynamic(this, &ULoginWidget::OnConnectClicked);
    }

	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->OnStatus.AddDynamic(this, &ULoginWidget::HandleStatusUdpate);
		FlowSubsystem->OnError.AddDynamic(this, &ULoginWidget::HandleStatusUdpate);
	}

}

void ULoginWidget::NativeDestruct()
{
	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->OnStatus.RemoveAll(this);
		FlowSubsystem->OnError.RemoveAll(this);
	}
}

void ULoginWidget::HandleStatusUdpate(FText Status)
{
	StatusText->SetText(Status);
}

void ULoginWidget::OnConnectClicked()
{
	StatusText->SetText(FText::GetEmpty());
	ConnectButton->SetIsEnabled(false);

	FString Host = ServerAddressBox->GetText().ToString();
	FString Username = UsernameBox->GetText().ToString();
	FString Password = PasswordBox->GetText().ToString();


	USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>();

	// TODO: Switch to actions/commands
	FlowSubsystem->BeginLogin(Host, Username, Password);


	ConnectButton->SetIsEnabled(true);


	//if (Host.IsEmpty() || Username.IsEmpty() || Password.IsEmpty())
	//{
	//	StatusText->SetText(FText::FromString(TEXT("Please fill in all fields.")));
	//	ConnectButton->SetIsEnabled(true);
	//	return;
	//}

	//StatusText->SetText(FText::FromString(TEXT("Connecting...")));

	//auto* Net = GetGameInstance()->GetSubsystem<USWGNetworkSubsystem>();
	//Net->ConnectAsync(Host, 44453).Next([this](const TResult<void>& Result)
	//{
	//	if (Result.IsSuccess())
	//	{
	//		StatusText->SetText(FText::FromString(TEXT("Connected! Logging in...")));
	//		// TODO: Send LoginIDMessage here
	//	}
	//	else
	//	{
	//		StatusText->SetText(FText::FromString(Result.GetError()));
	//		ConnectButton->SetIsEnabled(true);
	//	}
	//});
}