// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ListView.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "CommonActivatableWidget.h"
#include "UCharacterSelectWidget.generated.h"

/**
 * Character select screen widget.
 *
 * Lists the available characters (via CharacterListView) and lets the player pick one.
 * Binds to BindWidget properties: CharacterListView, StatusText, BackButton, NextButton,
 * CharacterNamePreviewText.
 *
 * Rows only display character data (see UCharacterListEntryWidget) - selecting a row
 * highlights it via the list's own selection, and NextButton confirms the highlighted
 * character by calling SelectCharacter on the flow subsystem.
 */
UCLASS()
class SWGEMUCLIENT_API UCharacterSelectWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Repopulates CharacterListView from the flow subsystem's current character list. */
	UFUNCTION(BlueprintCallable)
	void RefreshCharacterList();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnNextClicked();

	void OnCharacterSelectionChanged(UObject* Item);

	/**
	 * Deprojects a point within CharacterPreviewPanel (PanelAnchor in normalized
	 * 0-1 panel space, X: left-right, Y: top-bottom) into a world-space ray from
	 * whatever camera is actually active, then intersects that ray with the
	 * horizontal ground plane at GroundZ.
	 **/
	UFUNCTION(BlueprintCallable, Category = "Character Preview")
	bool GetCharacterPreviewWorldLocation(float GroundZ, FVector2D PanelAnchor, FVector& OutLocation) const;

	UPROPERTY(meta = (BindWidget))
	UListView* CharacterListView;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
	UPROPERTY(meta = (BindWidget))
	UButton* BackButton;
	UPROPERTY(meta = (BindWidget))
	UButton* NextButton;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* CharacterNamePreviewText;
	UPROPERTY(meta = (BindWidget))
	UBorder* CharacterPreviewPanel;

	/** World Z the preview spawn point's ground/feet plane sits at (see CharacterPreviewSpawnPoint in L_Startup, which is authored at Z=0). */
	UPROPERTY(EditDefaultsOnly, Category = "Character Preview")
	float CharacterPreviewGroundZ = 0.f;

	/**
	 * Fallback distance to walk along the view ray if it doesn't actually
	 * point at CharacterPreviewGroundZ (e.g. the camera is below ground with
	 * nothing to stand on in an otherwise-empty level, or looking level/up).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Character Preview")
	float CharacterPreviewFallbackDistance = 300.f;

	/** Actor tag used to find the level's preview spawn point (see CharacterPreviewSpawnPoint in L_Startup). */
	UPROPERTY(EditDefaultsOnly, Category = "Character Preview")
	FName CharacterPreviewSpawnPointTag = "CharacterPreviewSpawnPoint";

	/**
	 * Where in CharacterPreviewPanel the spawn point's location (the character's
	 * feet, per the spawn transform convention used in L_Startup) should land.
	 * Normalized 0-1 panel space: X 0=left/1=right, Y 0=top/1=bottom. Defaults
	 * to bottom-center so feet sit on the bottom edge of the panel rather than
	 * floating mid-frame.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Character Preview")
	FVector2D CharacterPreviewFeetAnchor = FVector2D(0.5f, 1.0f);

private:
	/**
	 * Moves the level's CharacterPreviewSpawnPoint (found by tag) to wherever
	 * CharacterPreviewPanel is actually pointing
	 */
	bool PositionCharacterPreviewSpawnPoint();

	bool bPositionedPreviewSpawn = false;

	/** Viewport size the spawn point was last derived for - see NativeTick. */
	FVector2D LastViewportSize = FVector2D::ZeroVector;

	UPROPERTY()
	TArray<UObject*> CharacterEntries;
};
