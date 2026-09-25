#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGHoloView.h"
#include "SWGInventoryQuery.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "SWGHoloInventoryWidget.generated.h"

class ASWGHoloFigureActor;
class UButton;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;
class USWGHoloCharacterSheetWidget;
class USWGHoloDetailCardWidget;
class USWGHoloLabelWidget;

DECLARE_MULTICAST_DELEGATE(FSWGOnHoloInventoryClosed);
DECLARE_MULTICAST_DELEGATE(FSWGOnHoloInventorySwitchToWindow);

/** What the pane beside the figure shows. */
UENUM(BlueprintType)
enum class ESWGHoloInventoryPane : uint8
{
	Inventory,
	Character,
};

/**
 * The holographic inventory: projects a hologram of the player in front of
 * them (ASWGHoloFigureActor), swings the camera close over their shoulder and
 * holds the figure left of centre, and turns the screen into its controls.
 * Each equipped item gets a
 * marker on the figure and a leader line out to its name in a column either
 * side. Hovering a name lights the item up and unfolds its examine details
 * beside it; clicking the name pins them open; right-clicking opens its radial
 * menu. The bag's contents fill a list of models and names beside the figure
 * (ASWGHoloShelfActor), a few columns wide, scrolled a row at a time with the
 * wheel, and answer hover and clicks the same way. Dragging turns the figure. Closing puts the camera and
 * controls back.
 *
 * WBP_HoloInventory lays out the overlay, binding every piece below by name;
 * this positions them over the scene each frame and fills them.
 */
UCLASS(Abstract, Blueprintable)
class SWGUI_API USWGHoloInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void Close();

	/** Asks the owner to replace this with the inventory window. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void SwitchToWindow();

	/** Pins the next (Direction > 0) or previous item's details, down the left column then the right; how a gamepad picks. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void SelectNext(int32 Direction);

	/** Pins the next or previous bag item in reading order, scrolling it into view. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void SelectNextInBag(int32 Direction);

	/** Scrolls the bag's list by whole rows. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void ScrollBag(int32 Rows);

	/** Turns the page beside the figure to the bag or the character sheet: the droid scans the old page away and the new one in. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void ShowPane(ESWGHoloInventoryPane Pane);

	/** The next (Direction > 0) or previous page, wrapping. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void TurnPage(int32 Direction);

	ESWGHoloInventoryPane GetPane() const { return Pane; }

	/** Seconds for each of the scan's two passes: down wiping the old page, back up drawing the new. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float ScanSeconds = 0.3f;

	/** Brightness of the droid's rays to the scan line's ends. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float ScanRayBrightness = 0.6f;

	FSWGOnHoloInventoryClosed OnClosed;
	FSWGOnHoloInventorySwitchToWindow OnSwitchToWindow;

	/** World units in front of the player the projector stands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	float ProjectorDistance = 200.f;

	/** World units to the player's right the projector stands; straight ahead, the player faces it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	float ProjectorSideOffset = 0.f;

	/** World units above the ground the projector's disc floats: about table height, as the holo map's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	float ProjectorHeight = 95.f;

	/** Size of the figure against the real character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	float FigureScale = 0.65f;

	/** Over-the-shoulder camera as the holo map places it: relative to the projector in the player's frame (back, right, up), looking down past the player's right shoulder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	FVector CameraOffset = FVector(-320.f, 60.f, 235.f);

	/** How far up the figure the camera aims, 0 at the feet to 1 at the crown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory", meta = (ClampMin = "0", ClampMax = "1"))
	float CameraAimHeight = 0.5f;

	/** Where on screen that aim point is held, as fractions of the width and height; left of centre leaves the right for the bag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	FVector2D FigureScreenPosition = FVector2D(0.36f, 0.54f);

	/**
	 * Where on screen the player's own head is held, as fractions of the width
	 * and height: low on the left, so they are seen looking over at their
	 * hologram, as with the holo map. The camera slides to hold it while it
	 * turns to hold the figure.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	FVector2D PlayerScreenPosition = FVector2D(0.1f, 0.86f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	float CameraFieldOfView = 60.f;

	/** How much of the HUD stays visible while the hologram is up, 0-1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory", meta = (ClampMin = "0", ClampMax = "1"))
	float HudOpacity = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloInventory")
	float CameraBlendSeconds = 0.6f;

	/** Closes on this as well as Escape, so the key that opened it also shuts it. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	FKey ToggleKey = EKeys::I;

	/** Turn the page forward and back; Tab turns forward too, and the gamepad's triggers back and forward. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	FKey NextPageKey = EKeys::E;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	FKey PreviousPageKey = EKeys::Q;

	/** Opens the character sheet, as retail's C key does; again goes back to the bag. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	FKey CharacterKey = EKeys::C;

	/** Gives a Blueprint's bound hint text, headings and panels the holo fonts, colours and brushes; off leaves them as designed. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	bool bApplyHoloStyle = true;

	/** Containment changes arrive as separate messages with no "done"; poll instead of chasing each. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float RefreshInterval = 0.5f;

	/** Clearance between the figure's widest marker and each column of names, slate units. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float LabelColumnMargin = 45.f;

	/** Gap between stacked names, slate units. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float LabelSpacing = 4.f;

	/** The part of the screen the bag's list fills: left, top, right, bottom, as fractions of the width and height. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	FVector4 BagArea = FVector4(0.62f, 0.14f, 0.97f, 0.86f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory", meta = (ClampMin = "1"))
	int32 BagColumns = 2;

	/** Height of one row of the list, slate units; the model takes most of it, the name sits beside. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float BagRowHeight = 58.f;

	/** How deep the bag's list floats, as a fraction of the camera's distance to the figure: near 1 it stands beside the figure, projected by the same droid. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float BagDepth = 0.9f;

	/** Brightness of the droid's rays to the list's corners, and to the hovered or selected bag item, against the hologram's own. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float BagRayBrightness = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float BagItemRayBrightness = 0.35f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> WindowButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

private:
	/** One equipped item's marker, line and name. Screen positions are this widget's local units, refreshed every tick. */
	struct FItemMarker
	{
		FSWGInventoryEntry Entry;
		TWeakObjectPtr<UPrimitiveComponent> Visual;
		int32 LabelIndex = INDEX_NONE;
		/** Which column; chosen once, from where the item first showed, so turning the figure doesn't swap names about. */
		bool bLeft = false;
		bool bSidePlaced = false;
		bool bOnScreen = false;
		FVector2D Anchor = FVector2D::ZeroVector;
		FVector2D Elbow = FVector2D::ZeroVector;
		FVector2D LabelEdge = FVector2D::ZeroVector;
	};

	void Project();
	void RestoreView();
	void RefreshItems();
	void RebuildMarkers();
	void LayoutMarkers(const FGeometry& MyGeometry);
	UPrimitiveComponent* FindItemVisual(int64 ObjectId) const;

	void HandleLabelHovered(int64 ObjectId, bool bHovered);
	void HandleLabelPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition);

	UFUNCTION() void HandleWindowClicked();
	UFUNCTION() void HandleCloseClicked();

	TArray<FSWGInventoryEntry> Equipped;
	TArray<FSWGInventoryEntry> Contents;
	TArray<FItemMarker> Markers;

	UPROPERTY()
	TArray<TObjectPtr<USWGHoloLabelWidget>> Labels;

	/** The root canvas the names are laid out on. */
	UPROPERTY()
	TObjectPtr<UCanvasPanel> LabelLayer;

	int64 HoveredObjectId = 0;
	double NextRefreshTime = 0.0;

	/** The card follows the hovered name, else the pinned one, lingering briefly after the pointer leaves. */
	void UpdateCard();
	void ShowCard(int64 ObjectId);
	void PlaceCard(const FGeometry& MyGeometry);

	UFUNCTION()
	void HandleExamineInfo(const FSWGExamineInfo& Info);

	UPROPERTY()
	TObjectPtr<USWGHoloDetailCardWidget> Card;

	UPROPERTY()
	TObjectPtr<USWGExamineSubsystem> Examine;

	/** Each item's details as last heard, so hovering back and forth doesn't ask the server again. */
	TMap<int64, FSWGExamineInfo> ExamineCache;
	int64 CardObjectId = 0;
	int64 PinnedObjectId = 0;
	double HoverLostTime = 0.0;

	UPROPERTY()
	TObjectPtr<ASWGHoloFigureActor> Figure;

	UPROPERTY()
	TObjectPtr<class ASWGHoloShelfActor> Shelf;

	/** Once the camera has arrived, turns it until the figure sits at FigureScreenPosition. */
	void SteerCamera(const FGeometry& MyGeometry);
	/** Lays the bag's list over BagArea: the models in the world, the names and headings on screen. */
	void LayoutBag(const FGeometry& MyGeometry);
	/** The bag item whose row cell holds a point on this widget, or zero. */
	int64 PickShelfItem(const FVector2D& LocalPosition) const;
	void RebuildBagLabels();

	/** Names beside the bag's models, one per item. */
	UPROPERTY()
	TMap<int64, TObjectPtr<USWGHoloLabelWidget>> BagLabels;

	/** Each showing bag item's cell on screen, from the last layout: where it can be picked, and where its card goes. */
	TMap<int64, FBox2D> BagCells;
	int32 LastBagRows = 0;

	/*
	 * The pieces laid over the scene each frame. A Blueprint may supply any of
	 * them by name, as direct children of its root canvas (they are positioned
	 * through their canvas slots); whatever it leaves out is built here.
	 */

	/** A faint frame round the list, heading included; the droid's rays land on its corners. Sized to the list. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> BagFrame;

	/** The frame on screen from the last layout; invalid while the bag is empty. */
	FBox2D BagFrameRect = FBox2D(ForceInit);

	/** The bag's heading text, and what holds it (itself, or a panel round it) in the canvas. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BagCaption;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> BagCaptionPanel;

	/** Heads the worn items' names, over the figure, as BagCaption heads the bag. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> EquippedCaption;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> EquippedCaptionPanel;

	/** "▲ n more" above the list and "▼ n more" below it. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BagMoreBefore;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BagMoreAfter;

	/** Under the page: "‹ ● ○ ›", which page this is and arrows to turn it. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> PageIndicator;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PageDots;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PagePrevious;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PageNext;

	/**
	 * Along the bottom of the frame, as ui_pda_inventory's capacity and money
	 * blocks: the bag's used / limit over a meter, then cash and bank. Sized
	 * to the frame's width.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> BagFooter;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CapacityText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> CapacityBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CashText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BankText;

	/** The character page (WBP_HoloCharacterSheet), inside PageClip; takes the list's place while it's up. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USWGHoloCharacterSheetWidget> CharacterSheet;

	/** The footer's labels, given retail's strings (ui.stf). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CapacityLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CashLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BankLabel;

	/** Clips the character sheet (its child) to below the scan line; a canvas panel clipping to its bounds. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> PageClip;

	/** The holo fonts, colours and brushes, and retail's strings, over what the Blueprint set. */
	void ApplyHoloStyle();
	void RefreshFooter();

	UFUNCTION() void HandlePagePreviousClicked();
	UFUNCTION() void HandlePageNextClicked();

	ESWGHoloInventoryPane Pane = ESWGHoloInventoryPane::Inventory;

	/** The page being turned to, once the scan has wiped the current one. */
	ESWGHoloInventoryPane PendingPane = ESWGHoloInventoryPane::Inventory;

	/** Makes Pane current: drops the bag's focus when it goes, fills the sheet when it comes. */
	void ApplyPane();

	enum class EScanPhase : uint8 { None, Wipe, Draw };
	EScanPhase ScanPhase = EScanPhase::None;
	/** When the current pass began, on ScanClock; the line is worked out from it so the shelf's tick and this widget's agree. */
	double ScanPhaseStart = 0.0;
	/** The world's real time: one value for the whole frame, however far apart in it the shelf and this widget tick. */
	double ScanClock() const;
	void UpdateScan();
	float ScanProgress() const;
	/** Where the line is now, from the page's extent at the last layout. */
	float CurrentScanLine() const;
	float ScanPageTop = 0.f;
	float ScanPageBottom = 0.f;

	/** The scan line from the last layout, local units; page content above it isn't shown. Far above the page when no scan runs. */
	float ScanLineY = -FLT_MAX;
	float ScanLineLeft = 0.f;
	float ScanLineRight = 0.f;

	int64 HoveredShelfId = 0;

	FSWGHoloView View;

	/** The blend onto the camera has finished; SteerCamera may move it from here on. */
	bool bCameraArrived = false;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> ButtonTextTints;

	/** Right stick X, applied every tick to turn the figure. */
	float TurnStick = 0.f;
	bool bTurning = false;
};
