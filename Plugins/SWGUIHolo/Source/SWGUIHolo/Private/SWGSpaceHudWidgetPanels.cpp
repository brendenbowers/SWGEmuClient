// The radar, target, status and warnings panels of the piloting HUD (stages 6c-6e). The widget core, reticle and gauges are in SWGSpaceHudWidget.cpp.

#include "SWGSpaceHudWidget.h"
#include "SWGHoloRadarActor.h"
#include "SWGHoloShipActor.h"
#include "SWGHoloStyle.h"
#include "SWGRetailStyle.h"
#include "SWGAudioSubsystem.h"
#include "SWGEffectsSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Common/SWGWorldScale.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarHudHoloDistance(
		TEXT("swg.Space.HudHoloDistance"), 60.f,
		TEXT("Distance in front of the pilot camera, in centimetres, at which the HUD's 3D radar and target hologram float."),
		ECVF_Default);

	FLinearColor Srgb(uint8 Red, uint8 Green, uint8 Blue) { return FLinearColor::FromSRGBColor(FColor(Red, Green, Blue)); }

	// Retail colours: ui_hud_space.inc text (#96F4FC label, #62FF15 value, #FF7417 component percentage), the missile warning
	// (#FF0000 text, #FD1F06 edge glow), the shield (#0059C6 / #1CAFFF) and armor (#F00000 / #F43E00) quadrants.
	const FLinearColor LabelColor = Srgb(0x96, 0xF4, 0xFC);
	const FLinearColor ValueColor = Srgb(0x62, 0xFF, 0x15);
	const FLinearColor PercentColor = Srgb(0xFF, 0x74, 0x17);
	const FLinearColor LowColor = Srgb(0xFF, 0x2D, 0x2D);
	const FLinearColor MediumColor = Srgb(0xFF, 0xAC, 0x00);
	const FLinearColor WarningTextColor = Srgb(0xFF, 0x00, 0x00);
	const FLinearColor MissileGlowColor = Srgb(0xFD, 0x1F, 0x06);
	const FLinearColor ShieldOuter = Srgb(0x00, 0x59, 0xC6);
	const FLinearColor ShieldInner = Srgb(0x1C, 0xAF, 0xFF);
	const FLinearColor ArmorOuter = Srgb(0xF0, 0x00, 0x00);
	const FLinearColor ArmorInner = Srgb(0xF4, 0x3E, 0x00);
	const FLinearColor RadarLineColor = Srgb(0x1C, 0xFF, 0xFF);
	const FLinearColor RadarBackColor = Srgb(0x00, 0xD6, 0xFB);

	// Design-unit layout inside each slot (one unit is a pixel of a 1080-high screen).
	const FVector2D RadarCentre(280.0, 116.0);
	constexpr float RadarSphereRadius = 96.f;
	constexpr float ArmorRadius = 111.f;
	constexpr float ArmorThickness = 14.f;
	constexpr float ShieldRadius = 116.f;
	constexpr float ShieldThickness = 8.f;
	constexpr float FlatDiscRadius = 96.f;
	constexpr int32 FlatBlipPool = 64;
	const FVector2D TargetHoloCentreLocal(190.0, 290.0);
	constexpr float TargetHoloRadius = 100.f;
	constexpr float PanelRowHeight = 22.f;
	constexpr float ComponentRowHeight = 44.f;

	/** Top and bottom quadrants as retail names them (RT, LT, RB, LB): turns from east, clockwise, begun on the vertical axis so a fill grows outward. */
	struct FQuadrant { float Begin; float End; bool bFront; };
	const FQuadrant Quadrants[4] = {{0.75f, 0.87f, true}, {0.75f, 0.63f, true}, {0.25f, 0.13f, false}, {0.25f, 0.37f, false}};

	FLinearColor ConditionColor(float Fraction)
	{
		return SWGSpaceHud::GetConditionColor(Fraction);
	}

	FText Percent(float Fraction)
	{
		return FText::Format(NSLOCTEXT("SWGEmu", "HudPercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Fraction * 100.f)));
	}

	TOptional<float> Average(const TOptional<float>& First, const TOptional<float>& Second)
	{
		if (First.IsSet() && Second.IsSet()) return (*First + *Second) * 0.5f;
		return First.IsSet() ? First : Second;
	}

	FBox2D CentredBox(const FVector2D& Centre, const FVector2D& Size) { return FBox2D(Centre - Size * 0.5, Centre + Size * 0.5); }

	FText LookupText(USWGTreSubsystem* Tre, const TCHAR* Table, const TCHAR* Key, const TCHAR* Fallback)
	{
		const FString Text = Tre ? Tre->LookupString(Table, Key) : FString();
		return Text.IsEmpty() ? FText::FromString(Fallback) : FText::FromString(Text);
	}

	/** Locks a hologram to a screen region; the actor then follows the camera itself each frame (see FSWGHoloHudAnchor). */
	FSWGHoloHudAnchor MakeAnchor(const APlayerController* Controller, const TOptional<FSWGSpaceHudView>& FixedView, const FVector2D& ViewSize, const FVector2D& ScreenCentre,
		float RadiusPixels, float NominalRadius, float CentreLift, const AActor* OrientationActor)
	{
		FSWGHoloHudAnchor Anchor;
		Anchor.ScreenCentre = ScreenCentre;
		Anchor.RadiusPixels = RadiusPixels;
		Anchor.NominalRadius = NominalRadius;
		Anchor.CentreLift = CentreLift;
		Anchor.Distance = FMath::Max(CVarHudHoloDistance.GetValueOnGameThread(), 15.f);
		Anchor.ViewSize = ViewSize;
		if (FixedView.IsSet()) Anchor.FixedView = *FixedView; else Anchor.Controller = Controller;
		Anchor.OrientationActor = OrientationActor;
		return Anchor;
	}

}

// ── Building ────────────────────────────────────────────────────────────────

UImage* USWGSpaceHudWidget::AddPanelSprite(ESWGSpaceHudSlot Region, const TCHAR* SpriteName, const FLinearColor& Tint, float Delay)
{
	FSWGSpaceHudSprite Sprite;
	if (!Atlas.Find(SpriteName, Sprite)) return nullptr;
	return AddSprite(SlotPanels[int32(Region)], Sprite, Tint, Delay);
}

FSWGSpaceHudMeter USWGSpaceHudWidget::AddPanelArc(ESWGSpaceHudSlot Region, float BeginTurns, float EndTurns, float Thickness, const FLinearColor& OuterColor,
	const FLinearColor& InnerColor, float InnerOpacity, float Delay)
{
	FSWGSpaceHudMeter Meter;
	UMaterialInstanceDynamic* Material = MakeMaterial(true, nullptr, OuterColor);
	if (!Material) return Meter;
	Material->SetScalarParameterValue(TEXT("ArcBegin"), BeginTurns);
	Material->SetScalarParameterValue(TEXT("ArcEnd"), EndTurns);
	Material->SetScalarParameterValue(TEXT("Thickness"), Thickness);
	Material->SetScalarParameterValue(TEXT("InnerOpacity"), InnerOpacity);
	Material->SetVectorParameterValue(TEXT("OuterColor"), OuterColor);
	Material->SetVectorParameterValue(TEXT("InnerColor"), InnerColor);
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetBrushFromMaterial(Material);
	Image->SetVisibility(ESlateVisibility::Collapsed);
	SlotPanels[int32(Region)]->AddChildToCanvas(Image);
	FSWGSpaceHudElement& Element = Elements.AddDefaulted_GetRef();
	Element.Image = Image; Element.Material = Material; Element.RevealDelay = Delay;
	Meter.Image = Image; Meter.Material = Material;
	return Meter;
}

FSWGSpaceHudMeter USWGSpaceHudWidget::AddPanelBar(ESWGSpaceHudSlot Region, float Delay)
{
	FSWGSpaceHudMeter Meter;
	UMaterialInstanceDynamic* Material = MakeMaterial(false, nullptr, LabelColor);
	if (!Material) return Meter;
	Material->SetScalarParameterValue(TEXT("Solid"), 1.f);
	Material->SetScalarParameterValue(TEXT("TrackAlpha"), 0.22f);
	Material->SetVectorParameterValue(TEXT("Tint"), LabelColor);
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetBrushFromMaterial(Material);
	Image->SetVisibility(ESlateVisibility::Collapsed);
	SlotPanels[int32(Region)]->AddChildToCanvas(Image);
	FSWGSpaceHudElement& Element = Elements.AddDefaulted_GetRef();
	Element.Image = Image; Element.Material = Material; Element.RevealDelay = Delay;
	Meter.Image = Image; Meter.Material = Material;
	return Meter;
}

UTextBlock* USWGSpaceHudWidget::AddPanelText(ESWGSpaceHudSlot Region, const FLinearColor& Color, ETextJustify::Type Justification, int32 FontSize, float Delay)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetJustification(Justification);
	Text->SetShadowOffset(FVector2D(1.0, 1.0));
	Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Text->SetVisibility(ESlateVisibility::Collapsed);
	SlotPanels[int32(Region)]->AddChildToCanvas(Text);
	FSWGSpaceHudPanelText& Entry = PanelTexts.AddDefaulted_GetRef();
	Entry.Text = Text; Entry.FontSize = FontSize; Entry.RevealDelay = Delay;
	return Text;
}

UButton* USWGSpaceHudWidget::AddPanelButton(ESWGSpaceHudSlot Region, UTextBlock*& OutLabel)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	FSlateBrush Normal = SWGHoloStyle::FrameBrush();
	Normal.TintColor = FSlateColor(FLinearColor(0.02f, 0.2f, 0.28f, 0.45f));
	FSlateBrush Hovered = Normal;
	Hovered.TintColor = FSlateColor(FLinearColor(0.05f, 0.4f, 0.5f, 0.6f));
	FSlateBrush Pressed = Normal;
	Pressed.TintColor = FSlateColor(FLinearColor(0.1f, 0.6f, 0.7f, 0.7f));
	FButtonStyle Style;
	Style.SetNormal(Normal).SetHovered(Hovered).SetPressed(Pressed).SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
	Button->SetStyle(Style);
	Button->SetVisibility(ESlateVisibility::Collapsed);
	OutLabel = WidgetTree->ConstructWidget<UTextBlock>();
	OutLabel->SetColorAndOpacity(FSlateColor(LabelColor));
	OutLabel->SetJustification(ETextJustify::Center);
	Button->AddChild(OutLabel);
	SlotPanels[int32(Region)]->AddChildToCanvas(Button);
	FSWGSpaceHudPanelText& Entry = PanelTexts.AddDefaulted_GetRef();
	Entry.Text = OutLabel; Entry.FontSize = 14; Entry.RevealDelay = 0.8f;
	return Button;
}

void USWGSpaceHudWidget::BuildRadar()
{
	constexpr ESWGSpaceHudSlot Region = ESWGSpaceHudSlot::Radar;
	// Armor inside, shield on the outer rim, as in retail's two ring pages.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ArmorArcs[Index] = AddPanelArc(Region, Quadrants[Index].Begin, Quadrants[Index].End, ArmorThickness / ArmorRadius, ArmorOuter, ArmorInner, 0.5f, 0.3f);
		ShieldArcs[Index] = AddPanelArc(Region, Quadrants[Index].Begin, Quadrants[Index].End, ShieldThickness / ShieldRadius, ShieldOuter, ShieldInner, 0.9f, 0.35f);
	}
	// The flat disc: retail radarGlow in its two tints, three range rings, a cross and the north tick.
	for (const FLinearColor& Tint : {RadarBackColor, RadarLineColor})
	{
		if (UImage* Glow = AddPanelSprite(Region, TEXT("RadarGlow"), Tint, 0.4f))
		{
			if (FSWGSpaceHudElement* Element = FindElement(Glow)) Element->Material->SetScalarParameterValue(TEXT("Opacity"), Tint == RadarLineColor ? 0.35f : 0.25f);
			Glow->SetVisibility(ESlateVisibility::Collapsed);
			FlatRadarPieces.Add(Glow); FlatGlows.Add(Glow);
		}
	}
	for (int32 RingIndex = 0; RingIndex < 3; ++RingIndex)
	{
		FSWGSpaceHudMeter Ring = AddPanelArc(Region, 0.f, 1.f, 0.012f, RadarLineColor, RadarLineColor, 1.f, 0.45f);
		if (!Ring.Image) continue;
		Ring.Material->SetScalarParameterValue(TEXT("Opacity"), RingIndex == 0 ? 0.9f : 0.5f);
		FlatRadarPieces.Add(Ring.Image); FlatRings.Add(Ring.Image);
	}
	for (int32 SpokeIndex = 0; SpokeIndex < 2; ++SpokeIndex)
	{
		FSWGSpaceHudMeter Spoke = AddPanelBar(Region, 0.5f);
		if (!Spoke.Image) continue;
		Spoke.Material->SetScalarParameterValue(TEXT("Opacity"), 0.45f);
		Spoke.Material->SetScalarParameterValue(TEXT("TrackAlpha"), 1.f);
		FlatRadarPieces.Add(Spoke.Image); FlatSpokes.Add(Spoke.Image);
	}
	FSWGSpaceHudMeter NorthTick = AddPanelBar(Region, 0.6f);
	if (NorthTick.Image)
	{
		NorthTick.Material->SetVectorParameterValue(TEXT("Tint"), MediumColor);
		NorthTick.Material->SetScalarParameterValue(TEXT("TrackAlpha"), 1.f);
		FlatNorthTick = NorthTick.Image;
	}
	// Blips: one pooled sprite each, retargeted every frame.
	for (int32 BlipIndex = 0; BlipIndex < FlatBlipPool; ++BlipIndex)
	{
		FSWGSpaceHudFlatBlip& Blip = FlatBlips.AddDefaulted_GetRef();
		Blip.Image = AddPanelSprite(Region, TEXT("BlipDefault"), LabelColor, 0.6f);
		if (!Blip.Image) { FlatBlips.Pop(); break; }
		Blip.Material = FindElement(Blip.Image)->Material;
		Blip.Image->SetVisibility(ESlateVisibility::Collapsed);
	}
	RangeText = AddPanelText(Region, LabelColor, ETextJustify::Center, 14, 0.7f);
}

void USWGSpaceHudWidget::BuildTargetPanel()
{
	constexpr ESWGSpaceHudSlot Region = ESWGSpaceHudSlot::Target;
	TargetName = AddPanelText(Region, LabelColor, ETextJustify::Left, 16, 0.6f);
	TargetType = AddPanelText(Region, LabelColor, ETextJustify::Left, 15, 0.65f);
	TargetDifficulty = AddPanelText(Region, LabelColor, ETextJustify::Left, 15, 0.7f);
	const TCHAR* const Keys[4] = {TEXT("shield"), TEXT("armor"), TEXT("system"), TEXT("chassis")};
	const TCHAR* const Fallbacks[4] = {TEXT("Shields"), TEXT("Armor"), TEXT("Systems"), TEXT("Chassis")};
	for (int32 RowIndex = 0; RowIndex < 4; ++RowIndex)
	{
		TargetRowLabels[RowIndex] = AddPanelText(Region, LabelColor, ETextJustify::Left, 15, 0.7f + 0.04f * RowIndex);
		TargetRowLabels[RowIndex]->SetText(LookupText(Tre, TEXT("ui_spacehud"), Keys[RowIndex], Fallbacks[RowIndex]));
		TargetRowValues[RowIndex] = AddPanelText(Region, ValueColor, ETextJustify::Left, 15, 0.7f + 0.04f * RowIndex);
	}
	// The badge is a coloured texture, drawn through the sprite material (its alpha channel as the shape).
	FactionBadge = AddPanelSprite(Region, TEXT("BlipDefault"), LabelColor, 0.7f);
	if (FactionBadge)
	{
		FactionBadgeMaterial = FindElement(FactionBadge)->Material;
		FactionBadge->SetVisibility(ESlateVisibility::Collapsed);
	}
	// Flat mode's stand-in for the hologram: the retail target bracket, large.
	FlatTargetIcon = AddPanelSprite(Region, TEXT("BlipBracket"), LabelColor, 0.75f);
	if (FlatTargetIcon) FlatTargetIcon->SetVisibility(ESlateVisibility::Collapsed);
	UTextBlock* CommText = nullptr;
	CommButton = AddPanelButton(Region, CommText);
	CommLabel = CommText;
	CommLabel->SetText(LookupText(Tre, TEXT("ui_spacehud"), TEXT("comm"), TEXT("Comm Target")));
	CommButton->OnClicked.AddDynamic(this, &USWGSpaceHudWidget::OnCommClicked);
}

void USWGSpaceHudWidget::BuildStatusPanel()
{
	constexpr ESWGSpaceHudSlot Region = ESWGSpaceHudSlot::Status;
	EnergyText = AddPanelText(Region, LabelColor, ETextJustify::Left, 15, 0.6f);
	const TCHAR* const AmmoNames[3] = {TEXT("m1:"), TEXT("m2:"), TEXT("cm:")};
	for (int32 AmmoIndex = 0; AmmoIndex < 3; ++AmmoIndex)
	{
		AmmoLabels[AmmoIndex] = AddPanelText(Region, LabelColor, ETextJustify::Left, 15, 0.65f + 0.03f * AmmoIndex);
		AmmoLabels[AmmoIndex]->SetText(FText::FromString(AmmoNames[AmmoIndex]));
		AmmoValues[AmmoIndex] = AddPanelText(Region, ValueColor, ETextJustify::Left, 15, 0.65f + 0.03f * AmmoIndex);
	}
	const TCHAR* const IconNames[int32(ESWGSpaceHudComponent::Count)] = {TEXT("ComponentReactor"), TEXT("ComponentEngine"), TEXT("ComponentWeapon"),
		TEXT("ComponentCapacitor"), TEXT("ComponentShield"), TEXT("ComponentArmor")};
	const TCHAR* const FlyoutKeys[int32(ESWGSpaceHudComponent::Count)] = {TEXT("flyout_reactor"), TEXT("flyout_engine"), TEXT("flyout_weapon"),
		TEXT("flyout_capacitor"), TEXT("flyout_shield"), TEXT("flyout_armor")};
	const TCHAR* const Fallbacks[int32(ESWGSpaceHudComponent::Count)] = {TEXT("Reactor Status"), TEXT("Engine Status"), TEXT("Weapon Status"),
		TEXT("Capacitor Status"), TEXT("Shield Status"), TEXT("Armor Status")};
	for (int32 ComponentIndex = 0; ComponentIndex < int32(ESWGSpaceHudComponent::Count); ++ComponentIndex)
	{
		FSWGSpaceHudComponentRow& Row = ComponentRows[ComponentIndex];
		const float Delay = 0.7f + 0.05f * ComponentIndex;
		Row.Icon = AddPanelSprite(Region, IconNames[ComponentIndex], LabelColor, Delay);
		if (Row.Icon) Row.Icon->SetVisibility(ESlateVisibility::Collapsed);
		Row.Label = AddPanelText(Region, LabelColor, ETextJustify::Left, 14, Delay);
		Row.Label->SetText(LookupText(Tre, TEXT("ui_spacehud"), FlyoutKeys[ComponentIndex], Fallbacks[ComponentIndex]));
		Row.Percent = AddPanelText(Region, PercentColor, ETextJustify::Right, 15, Delay);
		Row.Bar = AddPanelBar(Region, Delay);
	}
}

void USWGSpaceHudWidget::BuildWarnings()
{
	constexpr ESWGSpaceHudSlot Region = ESWGSpaceHudSlot::Warnings;
	IncomingMissileText = AddPanelText(Region, WarningTextColor, ETextJustify::Center, 26, 0.5f);
	IncomingMissileText->SetText(LookupText(Tre, TEXT("ui_spacehud"), TEXT("incoming_missile"), TEXT("Incoming Missile")));
	// The red edge glows sit on the whole screen, not in the slot.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FSWGSpaceHudSprite Sprite;
		if (!Atlas.Find(Side == 0 ? TEXT("MissileGlowLeft") : TEXT("MissileGlowRight"), Sprite)) continue;
		MissileGlows[Side] = AddSprite(RootCanvas, Sprite, MissileGlowColor, 0.5f);
		if (MissileGlows[Side]) MissileGlows[Side]->SetVisibility(ESlateVisibility::Collapsed);
	}
	IncapText = AddPanelText(Region, LabelColor, ETextJustify::Center, 15, 0.6f);
	IncapText->SetText(FText::FromString(Tre ? Tre->LookupString(TEXT("base_player"), TEXT("victim_incapacitated")) : FString()));
	IncapBar = AddPanelBar(Region, 0.6f);
	if (IncapBar.Material) IncapBar.Material->SetVectorParameterValue(TEXT("Tint"), WarningTextColor);
	UTextBlock* EjectText = nullptr;
	EjectButton = AddPanelButton(Region, EjectText);
	EjectLabel = EjectText;
	EjectLabel->SetText(LookupText(Tre, TEXT("ui_spacehud"), TEXT("eject"), TEXT("EJECT")));
	EjectLabel->SetColorAndOpacity(FSlateColor(LowColor));
	EjectButton->OnClicked.AddDynamic(this, &USWGSpaceHudWidget::OnEjectClicked);
}

// ── Layout ──────────────────────────────────────────────────────────────────

void USWGSpaceHudWidget::PlacePanelChild(UWidget* Child, ESWGSpaceHudSlot Region, const FVector2D& Position, const FVector2D& Size)
{
	if (!Child) return;
	const double Unit = Layout.Unit;
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Child->Slot))
	{
		CanvasSlot->SetPosition(Position * Unit);
		CanvasSlot->SetSize(Size * Unit);
	}
	if (UImage* Image = Cast<UImage>(Child))
	{
		if (FSWGSpaceHudElement* Element = FindElement(Image))
		{
			const FVector2D Origin = Layout.GetSlot(Region).Min;
			SetElementRegion(*Element, FBox2D(Origin + Position * Unit, Origin + (Position + Size) * Unit));
		}
	}
}

void USWGSpaceHudWidget::LayoutPanels()
{
	if (!bBuilt || Layout.Unit <= 0.f) return;
	const double Unit = Layout.Unit;
	for (FSWGSpaceHudPanelText& Entry : PanelTexts)
	{
		if (Entry.Text) Entry.Text->SetFont(SWGRetailStyle::Font(FMath::Max(8, FMath::RoundToInt(Entry.FontSize * Unit)), true));
	}

	// Radar: arcs round the disc, the flat disc inside them, the range text under.
	constexpr ESWGSpaceHudSlot Radar = ESWGSpaceHudSlot::Radar;
	const FVector2D RadarOrigin = Layout.GetSlot(Radar).Min;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		PlacePanelChild(ArmorArcs[Index].Image, Radar, RadarCentre - FVector2D(ArmorRadius, ArmorRadius), FVector2D(ArmorRadius, ArmorRadius) * 2.0);
		PlacePanelChild(ShieldArcs[Index].Image, Radar, RadarCentre - FVector2D(ShieldRadius, ShieldRadius), FVector2D(ShieldRadius, ShieldRadius) * 2.0);
	}
	for (UImage* Glow : FlatGlows)
	{
		PlacePanelChild(Glow, Radar, RadarCentre - FVector2D(FlatDiscRadius, FlatDiscRadius), FVector2D(FlatDiscRadius, FlatDiscRadius) * 2.0);
	}
	for (int32 RingIndex = 0; RingIndex < FlatRings.Num(); ++RingIndex)
	{
		const float RingRadius = FlatDiscRadius * (1.f - RingIndex / 3.f);
		PlacePanelChild(FlatRings[RingIndex], Radar, RadarCentre - FVector2D(RingRadius, RingRadius), FVector2D(RingRadius, RingRadius) * 2.0);
	}
	// The spokes are a cross: a thin bar through the centre and the same bar turned to the vertical.
	for (int32 SpokeIndex = 0; SpokeIndex < FlatSpokes.Num(); ++SpokeIndex)
	{
		PlacePanelChild(FlatSpokes[SpokeIndex], Radar, RadarCentre - FVector2D(FlatDiscRadius, 0.75), FVector2D(FlatDiscRadius * 2.0, 1.5));
		FlatSpokes[SpokeIndex]->SetRenderTransformPivot(FVector2D(0.5, 0.5));
		FlatSpokes[SpokeIndex]->SetRenderTransformAngle(SpokeIndex == 0 ? 0.f : 90.f);
	}
	PlacePanelChild(RangeText, Radar, FVector2D(RadarCentre.X - 70.0, RadarCentre.Y + 122.0), FVector2D(140.0, 20.0));
	RadarSphereCentre = RadarOrigin + RadarCentre * Unit;
	RadarSpherePixels = RadarSphereRadius * float(Unit);

	// Target: text column, badge, then the hologram's region and the comm button.
	constexpr ESWGSpaceHudSlot Target = ESWGSpaceHudSlot::Target;
	PlacePanelChild(TargetName, Target, FVector2D(0.0, 0.0), FVector2D(330.0, PanelRowHeight));
	PlacePanelChild(TargetType, Target, FVector2D(0.0, PanelRowHeight), FVector2D(330.0, PanelRowHeight));
	PlacePanelChild(TargetDifficulty, Target, FVector2D(0.0, PanelRowHeight * 2.0), FVector2D(330.0, PanelRowHeight));
	PlacePanelChild(FactionBadge, Target, FVector2D(338.0, 0.0), FVector2D(56.0, 56.0));
	for (int32 RowIndex = 0; RowIndex < 4; ++RowIndex)
	{
		const double RowY = 84.0 + RowIndex * PanelRowHeight;
		PlacePanelChild(TargetRowLabels[RowIndex], Target, FVector2D(0.0, RowY), FVector2D(160.0, PanelRowHeight));
		PlacePanelChild(TargetRowValues[RowIndex], Target, FVector2D(170.0, RowY), FVector2D(90.0, PanelRowHeight));
	}
	PlacePanelChild(FlatTargetIcon, Target, TargetHoloCentreLocal - FVector2D(32.0, 32.0), FVector2D(64.0, 64.0));
	PlacePanelChild(CommButton, Target, FVector2D(0.0, 440.0), FVector2D(150.0, 30.0));
	TargetHoloCentre = Layout.GetSlot(Target).Min + TargetHoloCentreLocal * Unit;
	TargetHoloPixels = TargetHoloRadius * float(Unit) * 0.9f;

	// Status: energy, ammunition, then the six component rows.
	constexpr ESWGSpaceHudSlot Status = ESWGSpaceHudSlot::Status;
	PlacePanelChild(EnergyText, Status, FVector2D(0.0, 0.0), FVector2D(260.0, PanelRowHeight));
	for (int32 AmmoIndex = 0; AmmoIndex < 3; ++AmmoIndex)
	{
		const double RowY = 28.0 + AmmoIndex * PanelRowHeight;
		PlacePanelChild(AmmoLabels[AmmoIndex], Status, FVector2D(0.0, RowY), FVector2D(54.0, PanelRowHeight));
		PlacePanelChild(AmmoValues[AmmoIndex], Status, FVector2D(58.0, RowY), FVector2D(90.0, PanelRowHeight));
	}
	for (int32 ComponentIndex = 0; ComponentIndex < int32(ESWGSpaceHudComponent::Count); ++ComponentIndex)
	{
		const FSWGSpaceHudComponentRow& Row = ComponentRows[ComponentIndex];
		const double RowY = 120.0 + ComponentIndex * ComponentRowHeight;
		PlacePanelChild(Row.Icon, Status, FVector2D(0.0, RowY + 6.0), FVector2D(28.0, 28.0));
		PlacePanelChild(Row.Label, Status, FVector2D(38.0, RowY), FVector2D(220.0, 20.0));
		PlacePanelChild(Row.Bar.Image, Status, FVector2D(38.0, RowY + 24.0), FVector2D(220.0, 8.0));
		PlacePanelChild(Row.Percent, Status, FVector2D(264.0, RowY + 8.0), FVector2D(72.0, 24.0));
	}

	// Warnings: the missile text on top, the incapacitation page under it, the glows on the screen edges.
	constexpr ESWGSpaceHudSlot Warnings = ESWGSpaceHudSlot::Warnings;
	PlacePanelChild(IncomingMissileText, Warnings, FVector2D(0.0, 0.0), FVector2D(720.0, 36.0));
	PlacePanelChild(IncapText, Warnings, FVector2D(0.0, 44.0), FVector2D(720.0, 22.0));
	PlacePanelChild(IncapBar.Image, Warnings, FVector2D(230.0, 70.0), FVector2D(260.0, 7.0));
	PlacePanelChild(EjectButton, Warnings, FVector2D(296.0, 80.0), FVector2D(127.0, 28.0));
	const FVector2D GlowSize = FVector2D(150.0, 224.0) * Unit;
	if (MissileGlows[0])
	{
		const FVector2D Centre(GlowSize.X * 0.5 - 16.0 * Unit, Layout.ViewSize.Y * 0.5);
		if (UCanvasPanelSlot* GlowSlot = Cast<UCanvasPanelSlot>(MissileGlows[0]->Slot)) { GlowSlot->SetPosition(Centre - GlowSize * 0.5); GlowSlot->SetSize(GlowSize); }
		if (FSWGSpaceHudElement* Element = FindElement(MissileGlows[0])) SetElementRegion(*Element, CentredBox(Centre, GlowSize));
	}
	if (MissileGlows[1])
	{
		const FVector2D Centre(Layout.ViewSize.X - GlowSize.X * 0.5 + 16.0 * Unit, Layout.ViewSize.Y * 0.5);
		if (UCanvasPanelSlot* GlowSlot = Cast<UCanvasPanelSlot>(MissileGlows[1]->Slot)) { GlowSlot->SetPosition(Centre - GlowSize * 0.5); GlowSlot->SetSize(GlowSize); }
		if (FSWGSpaceHudElement* Element = FindElement(MissileGlows[1])) SetElementRegion(*Element, CentredBox(Centre, GlowSize));
	}
}

// ── Data ────────────────────────────────────────────────────────────────────

void USWGSpaceHudWidget::SetMeterFill(FSWGSpaceHudMeter& Meter, float Fill, bool bBar)
{
	if (!Meter.Material) return;
	Fill = FMath::Clamp(Fill, 0.f, 1.f);
	Meter.Material->SetScalarParameterValue(bBar ? TEXT("FillX") : TEXT("Fill"), Fill);
	// A drop of a few percent (a hit) flashes the meter; steady drift does not.
	if (Meter.LastFill >= 0.f && Meter.LastFill - Fill > 0.04f) PulseElement(Meter.Image);
	Meter.LastFill = Fill;
}

void USWGSpaceHudWidget::ApplyRadar(const FSWGSpaceHudData& Data)
{
	const bool bHasRadar = Data.RadarRange > 0.f;
	const bool bFlat = IsFlat();
	auto ShowArc = [this](FSWGSpaceHudMeter& Meter, const TOptional<float>& Fraction)
	{
		if (!Meter.Image) return;
		Meter.Image->SetVisibility(Fraction.IsSet() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (Fraction.IsSet()) SetMeterFill(Meter, *Fraction, false);
		else Meter.LastFill = -1.f;
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bFront = Quadrants[Index].bFront;
		ShowArc(ShieldArcs[Index], bFront ? Data.Vitals.FrontShield : Data.Vitals.RearShield);
		ShowArc(ArmorArcs[Index], bFront ? Data.Vitals.FrontArmor : Data.Vitals.RearArmor);
	}
	if (RangeText)
	{
		RangeText->SetVisibility(bHasRadar ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bHasRadar) RangeText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "HudRadarRange", "{0} m"), FText::AsNumber(FMath::RoundToInt(Data.RadarRange / SWGWorldScale))));
	}

	// Flat disc and blips.
	for (UImage* Piece : FlatRadarPieces) Piece->SetVisibility(bFlat && bHasRadar ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (FlatNorthTick)
	{
		const FVector FlatNorth = Data.NorthInShip.IsSet() ? FVector(Data.NorthInShip->X, Data.NorthInShip->Y, 0.f).GetSafeNormal() : FVector::ZeroVector;
		const bool bShowNorth = bFlat && bHasRadar && !FlatNorth.IsNearlyZero();
		FlatNorthTick->SetVisibility(bShowNorth ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShowNorth)
		{
			const FVector2D Local = RadarCentre + FVector2D(FlatNorth.Y, -FlatNorth.X) * (FlatDiscRadius + 5.f);
			PlacePanelChild(FlatNorthTick, ESWGSpaceHudSlot::Radar, Local - FVector2D(3.0, 3.0), FVector2D(6.0, 6.0));
		}
	}
	for (int32 BlipIndex = 0; BlipIndex < FlatBlips.Num(); ++BlipIndex)
	{
		FSWGSpaceHudFlatBlip& Pooled = FlatBlips[BlipIndex];
		if (!bFlat || !Data.Blips.IsValidIndex(BlipIndex))
		{
			Pooled.Image->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FSWGSpaceHudBlip& Blip = Data.Blips[BlipIndex];
		const TCHAR* SpriteName = Blip.bTarget ? TEXT("BlipBracket") : (Blip.Kind == ESWGSpaceHudBlipKind::Structure ? TEXT("BlipStructure") : TEXT("BlipDefault"));
		FSWGSpaceHudSprite Sprite;
		if (!Atlas.Find(SpriteName, Sprite)) continue;
		const FVector4f Uv = Atlas.GetUvRect(Sprite);
		Pooled.Material->SetScalarParameterValue(TEXT("U0"), Uv.X); Pooled.Material->SetScalarParameterValue(TEXT("V0"), Uv.Y);
		Pooled.Material->SetScalarParameterValue(TEXT("U1"), Uv.Z); Pooled.Material->SetScalarParameterValue(TEXT("V1"), Uv.W);
		FLinearColor Tint = LabelColor;
		if (Blip.bTarget) Tint = MediumColor;
		else if (Blip.Kind == ESWGSpaceHudBlipKind::Structure) Tint = Srgb(0xB8, 0xD4, 0xFF);
		else if (Blip.Relation == ESWGSpaceHudRelation::Friendly) Tint = ValueColor;
		else if (Blip.Relation == ESWGSpaceHudRelation::Hostile) Tint = LowColor;
		Pooled.Material->SetVectorParameterValue(TEXT("Tint"), Tint);
		const FVector2D DiscOffset = SWGSpaceHud::RadarDiscPosition(Blip.Relative, Data.RadarRange);
		const FVector2D Size = FVector2D(Sprite.Rect.Width(), Sprite.Rect.Height()) * (Blip.bTarget ? 2.6 : 2.0);
		const FVector2D Local = RadarCentre + DiscOffset * FlatDiscRadius;
		Pooled.Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		PlacePanelChild(Pooled.Image, ESWGSpaceHudSlot::Radar, Local - Size * 0.5, Size);
	}

	// The 3D radar.
	EnsureHolograms(!bFlat);
	if (RadarActor)
	{
		RadarActor->SetActorHiddenInGame(!bHasRadar || !RadarActor->HasHudAnchor());
		if (bHasRadar)
		{
			RadarActor->SetShip(Data.ShipActor.Get());
			RadarActor->SetContents(Data.Blips, Data.RadarRange, Data.NorthInShip);
		}
	}
}

void USWGSpaceHudWidget::ApplyTargetPanel(const FSWGSpaceHudData& Data)
{
	const bool bTarget = Data.bHasTarget;
	const bool bFlat = IsFlat();
	auto SetText = [bTarget](UTextBlock* Text, const FString& Value)
	{
		if (!Text) return;
		const bool bShow = bTarget && !Value.IsEmpty();
		Text->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShow && !Text->GetText().ToString().Equals(Value)) Text->SetText(FText::FromString(Value));
	};
	SetText(TargetName, Data.Target.Name);
	SetText(TargetType, Data.Target.TypeName);
	SetText(TargetDifficulty, Data.Target.Difficulty);

	// One percentage per row: the shield and armor rows average the front and rear values the baselines carry.
	const TOptional<float> RowValues[4] = {Average(Data.Target.Vitals.FrontShield, Data.Target.Vitals.RearShield),
		Average(Data.Target.Vitals.FrontArmor, Data.Target.Vitals.RearArmor), Data.Target.Vitals.System, Data.Target.Vitals.Chassis};
	for (int32 RowIndex = 0; RowIndex < 4; ++RowIndex)
	{
		const bool bShow = bTarget && RowValues[RowIndex].IsSet();
		const ESlateVisibility Shown = bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		TargetRowLabels[RowIndex]->SetVisibility(Shown);
		TargetRowValues[RowIndex]->SetVisibility(Shown);
		if (!bShow) continue;
		TargetRowValues[RowIndex]->SetText(Percent(*RowValues[RowIndex]));
		TargetRowValues[RowIndex]->SetColorAndOpacity(FSlateColor(ConditionColor(*RowValues[RowIndex])));
	}

	FSWGSpaceHudSprite Badge;
	const bool bHasBadge = bTarget && FactionBadge && Atlas.FindFactionBadge(Data.Target.Faction, Badge);
	if (FactionBadge) FactionBadge->SetVisibility(bHasBadge ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bHasBadge && FactionBadgeMaterial)
	{
		const FVector4f Uv = Atlas.GetUvRect(Badge);
		FactionBadgeMaterial->SetTextureParameterValue(TEXT("Sprite"), Atlas.LoadTexture(Badge));
		FactionBadgeMaterial->SetScalarParameterValue(TEXT("U0"), Uv.X); FactionBadgeMaterial->SetScalarParameterValue(TEXT("V0"), Uv.Y);
		FactionBadgeMaterial->SetScalarParameterValue(TEXT("U1"), Uv.Z); FactionBadgeMaterial->SetScalarParameterValue(TEXT("V1"), Uv.W);
	}
	if (FlatTargetIcon) FlatTargetIcon->SetVisibility(bTarget && bFlat ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// The Comm button: the server handles the comm command; the other four bottom-bar commands it does not, so they are not drawn.
	TargetCommId = Data.Target.ObjectId;
	const bool bCanComm = bTarget && Data.Target.bIsShip;
	CommButton->SetVisibility(bCanComm ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	EnsureHolograms(!bFlat);
	if (TargetHologram)
	{
		TargetHologram->SetActorHiddenInGame(!bTarget || !TargetHologram->HasHudAnchor());
		TargetHologram->SetSource(bTarget ? Data.Target.Actor.Get() : nullptr);
	}
	UpdateTargetBracket(Data);
}

void USWGSpaceHudWidget::UpdateTargetBracket(const FSWGSpaceHudData& Data)
{
	AActor* TargetActor = Data.bHasTarget ? Data.Target.Actor.Get() : nullptr;
	if (!TargetActor || Data.Target.ObjectId == 0)
	{
		StopTargetBracket();
		return;
	}
	if (Data.Target.ObjectId == BracketTargetId && TargetBracket.IsValid()) return;
	StopTargetBracket();
	USWGEffectsSubsystem* Effects = GetWorld() ? GetWorld()->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	if (!Effects || !TargetActor->GetRootComponent()) return;
	// The effect is authored about 4 m across; it grows with the target's hull.
	const float RadiusMetres = TargetActor->GetComponentsBoundingBox().GetExtent().Size() / SWGWorldScale;
	const float Scale = FMath::Clamp(RadiusMetres * 0.5f, 0.5f, 50.f);
	const bool bEnemy = Data.Target.Relation == ESWGSpaceHudRelation::Hostile;
	TargetBracket = Effects->PlayParticleAttached(bEnemy ? TEXT("pt_ui_target_ship_enemy") : TEXT("pt_ui_target_ship"), TargetActor->GetRootComponent(), NAME_None, Scale);
	BracketTargetId = TargetBracket.IsValid() ? Data.Target.ObjectId : 0;
}

void USWGSpaceHudWidget::StopTargetBracket()
{
	if (TargetBracket.IsValid()) TargetBracket.Stop(true);
	TargetBracket = FSWGEffectHandle();
	BracketTargetId = 0;
}

void USWGSpaceHudWidget::ApplyStatus(const FSWGSpaceHudData& Data)
{
	const bool bHasEnergy = Data.Capacitor.IsSet() && Data.CapacitorMax.IsSet() && *Data.CapacitorMax > 0.f;
	EnergyText->SetVisibility(bHasEnergy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bHasEnergy)
	{
		EnergyText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "HudEnergy", "{0}/{1}"), FText::AsNumber(FMath::RoundToInt(*Data.Capacitor)), FText::AsNumber(FMath::RoundToInt(*Data.CapacitorMax))));
	}

	// Rows m1, m2 for the first two missile launchers, cm for the first countermeasure launcher.
	const FSWGSpaceHudLauncher* Rows[3] = {nullptr, nullptr, nullptr};
	int32 MissileCount = 0;
	for (const FSWGSpaceHudLauncher& Launcher : Data.Launchers)
	{
		if (Launcher.bCountermeasure) { if (!Rows[2]) Rows[2] = &Launcher; }
		else if (MissileCount < 2) Rows[MissileCount++] = &Launcher;
	}
	for (int32 AmmoIndex = 0; AmmoIndex < 3; ++AmmoIndex)
	{
		const ESlateVisibility Shown = Rows[AmmoIndex] ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		AmmoLabels[AmmoIndex]->SetVisibility(Shown);
		AmmoValues[AmmoIndex]->SetVisibility(Shown);
		if (!Rows[AmmoIndex]) continue;
		AmmoValues[AmmoIndex]->SetText(FText::AsNumber(Rows[AmmoIndex]->Ammo));
		AmmoValues[AmmoIndex]->SetColorAndOpacity(FSlateColor(Rows[AmmoIndex]->Ammo <= 0 ? LowColor : ValueColor));
	}

	for (int32 ComponentIndex = 0; ComponentIndex < int32(ESWGSpaceHudComponent::Count); ++ComponentIndex)
	{
		FSWGSpaceHudComponentRow& Row = ComponentRows[ComponentIndex];
		const TOptional<float>& Condition = Data.Components[ComponentIndex];
		const ESlateVisibility Shown = Condition.IsSet() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (Row.Icon) Row.Icon->SetVisibility(Shown);
		Row.Label->SetVisibility(Shown);
		Row.Percent->SetVisibility(Shown);
		if (Row.Bar.Image) Row.Bar.Image->SetVisibility(Shown);
		if (!Condition.IsSet()) { Row.Bar.LastFill = -1.f; continue; }
		Row.Percent->SetText(Percent(*Condition));
		Row.Percent->SetColorAndOpacity(FSlateColor(ConditionColor(*Condition)));
		if (Row.Bar.Material) Row.Bar.Material->SetVectorParameterValue(TEXT("Tint"), ConditionColor(*Condition));
		SetMeterFill(Row.Bar, *Condition, true);
	}
}

void USWGSpaceHudWidget::ApplyWarnings(const FSWGSpaceHudData& Data)
{
	const bool bLocked = Data.IncomingMissiles > 0;
	IncomingMissileText->SetVisibility(bLocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	for (UImage* Glow : MissileGlows)
	{
		if (Glow) Glow->SetVisibility(bLocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	// The incapacitation page. The client gets no countdown for it, so the bar is full and the page says only that you are down.
	const ESlateVisibility IncapShown = Data.bPilotIncapacitated ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	IncapText->SetVisibility(IncapShown);
	if (IncapBar.Image) IncapBar.Image->SetVisibility(IncapShown);
	if (IncapBar.Material) IncapBar.Material->SetScalarParameterValue(TEXT("FillX"), 1.f);
	EjectButton->SetVisibility(Data.bPilotIncapacitated && Data.bCanEject ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

// ── Holograms ───────────────────────────────────────────────────────────────

void USWGSpaceHudWidget::EnsureHolograms(bool bWanted)
{
	UWorld* World = GetWorld();
	if (!bWanted || !World)
	{
		if (RadarActor) { RadarActor->Destroy(); RadarActor = nullptr; }
		if (TargetHologram) { TargetHologram->Destroy(); TargetHologram = nullptr; }
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!RadarActor && LastData.RadarRange > 0.f && LoadObject<UMaterialInterface>(nullptr, RadarMaterialPath))
	{
		RadarActor = World->SpawnActor<ASWGHoloRadarActor>(FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
		if (RadarActor) RadarActor->SetActorHiddenInGame(true);
	}
	if (!TargetHologram && LastData.bHasTarget)
	{
		TargetHologram = World->SpawnActor<ASWGHoloShipActor>(FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
		if (TargetHologram) TargetHologram->SetActorHiddenInGame(true);
	}
}

void USWGSpaceHudWidget::UpdateHolograms(float DeltaSeconds)
{
	FSWGSpaceHudView View;
	if (!BuildView(View) || Layout.Unit <= 0.f) return;
	const APlayerController* Controller = GetOwningPlayer();
	if (RadarActor && LastData.RadarRange > 0.f)
	{
		RadarActor->SetHudAnchor(MakeAnchor(Controller, ViewOverride, Layout.ViewSize, RadarSphereCentre, RadarSpherePixels,
			ASWGHoloRadarActor::NominalRadius, ASWGHoloRadarActor::CentreLift, LastData.ShipActor.Get()));
		RadarActor->SetActorHiddenInGame(false);
	}
	if (TargetHologram && LastData.bHasTarget)
	{
		// Upright to the camera: the droid hovers on the far side.
		TargetHologram->SetHudAnchor(MakeAnchor(Controller, ViewOverride, Layout.ViewSize, TargetHoloCentre, TargetHoloPixels,
			ASWGHoloShipActor::NominalRadius, ASWGHoloShipActor::NominalRadius * 1.1f + 5.f, nullptr));
		TargetHologram->SetActorHiddenInGame(false);
	}
}

void USWGSpaceHudWidget::UpdateWarnings(float DeltaSeconds)
{
	WarningSeconds += DeltaSeconds;
	const bool bLocked = LastData.IncomingMissiles > 0;
	if (!bLocked) return;
	// A pulsing red: the text and glows swell and fade together.
	const float Pulse = 0.65f + 0.35f * FMath::Sin(WarningSeconds * 10.f);
	IncomingMissileText->SetRenderOpacity(Pulse);
	for (UImage* Glow : MissileGlows)
	{
		if (Glow) Glow->SetRenderOpacity(Pulse);
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastAlarmTime < 1.6) return;
	LastAlarmTime = Now;
	if (UGameInstance* Game = GetGameInstance())
	{
		if (USWGAudioSubsystem* Audio = Game->GetSubsystem<USWGAudioSubsystem>()) Audio->PlaySound2D(TEXT("sound/cbt_msl_alarm_incoming.snd"));
	}
}

void USWGSpaceHudWidget::SendCommandNamed(const TCHAR* CommandName, int64 TargetId)
{
	UGameInstance* Game = GetGameInstance();
	if (USWGCommandSubsystem* Commands = Game ? Game->GetSubsystem<USWGCommandSubsystem>() : nullptr) Commands->SendCommand(CommandName, TargetId);
}

void USWGSpaceHudWidget::OnCommClicked()
{
	if (TargetCommId != 0) SendCommandNamed(TEXT("comm"), TargetCommId);
}

void USWGSpaceHudWidget::OnEjectClicked()
{
	// Core3 arms the escape pod on the first escapePod command and launches it on the second.
	SendCommandNamed(TEXT("escapePod"), 0);
}
