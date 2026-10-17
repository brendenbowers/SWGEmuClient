#include "SWGShipLaunchWidget.h"
#include "SWGRetailStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"

void USWGShipLaunchWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = RootCanvas;
	Frame = WidgetTree->ConstructWidget<USizeBox>();
	Frame->SetWidthOverride(440); Frame->SetHeightOverride(240);
	RootCanvas->AddChildToCanvas(Frame)->SetAutoSize(true);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(.02f,.07f,.09f,.96f)); Panel->SetPadding(FMargin(8)); Frame->AddChild(Panel);
	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(); Panel->AddChild(Body);
	Caption = WidgetTree->ConstructWidget<UBorder>(); Caption->SetPadding(FMargin(6));
	Caption->SetBrushColor(FLinearColor(.02f,.25f,.30f,1)); Body->AddChild(Caption);
	UHorizontalBox* Heading = WidgetTree->ConstructWidget<UHorizontalBox>(); Caption->AddChild(Heading);
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(); TitleText->SetFont(SWGRetailStyle::Font(13)); Heading->AddChild(TitleText);
	CloseButton = WidgetTree->ConstructWidget<UButton>(); Heading->AddChild(CloseButton);
	UTextBlock* CloseText = WidgetTree->ConstructWidget<UTextBlock>(); CloseText->SetText(FText::FromString(TEXT("X"))); CloseButton->AddChild(CloseText);
	UVerticalBox* Controls = WidgetTree->ConstructWidget<UVerticalBox>(); Content = Controls; Body->AddChild(Controls);
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(); StatusText->SetAutoWrapText(true);
	StatusText->SetFont(SWGRetailStyle::Font(13)); Controls->AddChild(StatusText);
	Selector = WidgetTree->ConstructWidget<UComboBoxString>(); Controls->AddChild(Selector);
	LaunchButton = WidgetTree->ConstructWidget<UButton>(); Controls->AddChild(LaunchButton);
	UTextBlock* LaunchText = WidgetTree->ConstructWidget<UTextBlock>();
	LaunchText->SetText(NSLOCTEXT("SWGEmu", "LaunchIntoSpace", "Launch into space")); LaunchButton->AddChild(LaunchText);
	UTextBlock* Grip = WidgetTree->ConstructWidget<UTextBlock>(); Grip->SetText(FText::FromString(TEXT("/")));
	Grip->SetJustification(ETextJustify::Right); Body->AddChild(Grip); ResizeGrip = Grip;
}
void USWGShipLaunchWidget::NativeConstruct()
{
	check(RootCanvas && Frame && Caption && TitleText && CloseButton && Content && ResizeGrip);
	Super::NativeConstruct(); SetTitle(NSLOCTEXT("SWGEmu", "ShipLaunchTitle", "Launch into space"));
	LaunchSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGShipLaunchSubsystem>() : nullptr;
	if (LaunchSystem) LaunchSystem->OnDataChanged.AddUniqueDynamic(this, &USWGShipLaunchWidget::Refresh);
	Selector->OnSelectionChanged.AddUniqueDynamic(this, &USWGShipLaunchWidget::Selected);
	LaunchButton->OnClicked.AddUniqueDynamic(this, &USWGShipLaunchWidget::LaunchClicked);
	Refresh();
}
void USWGShipLaunchWidget::NativeDestruct()
{
	if (LaunchSystem) LaunchSystem->OnDataChanged.RemoveDynamic(this, &USWGShipLaunchWidget::Refresh);
	Super::NativeDestruct();
}
void USWGShipLaunchWidget::Refresh()
{
	if (!LaunchSystem) return;
	if (LaunchSystem->GetStatus().IsEmpty() && !LaunchSystem->IsWaiting()) { Close(); return; }
	const int32 OldIndex = Selector->GetSelectedIndex();
	const int64 OldShip = Ships.IsValidIndex(OldIndex) ? Ships[OldIndex].ShipId : 0;
	Ships = LaunchSystem->GetShips();
	bRefreshing = true; Selector->ClearOptions();
	int32 Pick = INDEX_NONE;
	for (int32 Index = 0; Index < Ships.Num(); ++Index)
	{
		const auto& Ship = Ships[Index];
		Selector->AddOption(FString::Printf(TEXT("%d. %s (%s)%s"), Index+1, *Ship.Name, *Ship.ParkingLocation,
			Ship.bCanLaunch ? TEXT("") : TEXT(" - unavailable")));
		if (Ship.ShipId == OldShip || (Pick == INDEX_NONE && Ship.bCanLaunch)) Pick = Index;
	}
	Selector->SetSelectedIndex(Pick); bRefreshing = false;
	StatusText->SetText(FText::FromString(LaunchSystem->GetStatus()));
	Selected(FString(), ESelectInfo::Direct);
}
void USWGShipLaunchWidget::Selected(FString Value, ESelectInfo::Type Type)
{
	if (bRefreshing) return;
	const int32 Index = Selector->GetSelectedIndex();
	LaunchButton->SetIsEnabled(LaunchSystem && !LaunchSystem->IsWaiting() && Ships.IsValidIndex(Index) && Ships[Index].bCanLaunch);
}
void USWGShipLaunchWidget::LaunchClicked()
{
	const int32 Index = Selector->GetSelectedIndex();
	if (LaunchSystem && Ships.IsValidIndex(Index) && LaunchSystem->Launch(Ships[Index].ShipId)) Close();
	else { Refresh(); StatusText->SetText(NSLOCTEXT("SWGEmu", "ShipNotReady", "This ship is not ready to launch.")); }
}
