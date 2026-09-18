#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGFloatingTextWidget.generated.h"

class UCanvasPanel;
class UTextBlock;
struct FSWGCombatSpamEvent;

/**
 * Damage numbers: a full-screen, click-through layer that floats "25" or
 * "miss" up from whoever a combat log line was about. Builds its own canvas,
 * so it needs no Blueprint; the HUD puts one on the player screen under the
 * layout for as long as it is up.
 */
UCLASS(Abstract)
class SWGUI_API USWGFloatingTextWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Floats Text over Anchor. Ignored when the actor is gone or off-screen. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|FloatingText")
	void ShowText(AActor* Anchor, const FString& Text, FLinearColor Color, bool bEmphasis = false);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|FloatingText")
	float Lifetime = 1.4f;

	/** How far a line drifts upward over its life, in screen pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|FloatingText")
	float RiseDistance = 70.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|FloatingText")
	int32 FontSize = 22;

	/** Font size for the local player's own hits and the damage they take. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|FloatingText")
	int32 EmphasisFontSize = 28;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|FloatingText")
	FLinearColor DealtColor = FLinearColor(0.55f, 1.f, 0.4f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|FloatingText")
	FLinearColor TakenColor = FLinearColor(1.f, 0.3f, 0.25f);

	/** Fights we're only watching. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|FloatingText")
	FLinearColor BystanderColor = FLinearColor(0.85f, 0.85f, 0.85f);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	struct FEntry
	{
		TWeakObjectPtr<AActor> Anchor;
		/** Head height above the actor origin, measured once so a ragdolling or resizing mesh can't jitter the text. */
		float HeadOffset = 0.f;
		/** Sideways scatter so simultaneous lines on one target don't overprint. */
		FVector2D Scatter = FVector2D::ZeroVector;
		float Age = 0.f;
		TObjectPtr<UTextBlock> Text;
	};

	UFUNCTION()
	void HandleCombatSpam(const FSWGCombatSpamEvent& Event);

	/** What a log line puts on screen — a number, a word, or nothing for lines that aren't a swing's outcome. */
	bool DescribeSpam(const FSWGCombatSpamEvent& Event, FString& OutText) const;

	FLinearColor ColorFor(const FSWGCombatSpamEvent& Event) const;

	/** Full-screen canvas the labels are placed on (WBP_FloatingText). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> Canvas;

	TArray<FEntry> Entries;

	FRandomStream ScatterRandom;
};
