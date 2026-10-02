#pragma once

#include "SWGHoloProjectorActor.h"
#include "SWGHoloCraftingActor.generated.h"

class UMeshComponent;
class UTextRenderComponent;

/**
 * The droid's crafting projection. Draft: a carousel of the chosen
 * section's craftable items (SetItems/FEntry). Assembly: the prototype off
 * to one side (SetPrototypeObject), and — laid on the holo floor — one row
 * of candidates per required slot, the selected slot's row pulled to the
 * front and the rest receding behind it (SetCandidateRows). The slot list
 * itself isn't drawn here — see USWGHoloCraftingWidget's card, tracked
 * on-screen beside the prototype, styled like the holo inventory's detail
 * card.
 */
UCLASS(NotPlaceable)
class SWGUIHOLO_API ASWGHoloCraftingActor : public ASWGHoloProjectorActor
{
	GENERATED_BODY()

public:
	ASWGHoloCraftingActor();

	struct FEntry
	{
		FString Name;
		/** Draft: the crafted item's own template, for a mesh with no live object yet (USWGMeshGeneratorSubsystem::RequestItemMesh). */
		uint32 CraftedTemplateCrc = 0;
		/** Assembly: a bag item's live object id (USWGItemIconSubsystem::RequestItemModel) — takes precedence over CraftedTemplateCrc when non-zero. */
		int64 ObjectId = 0;
	};

	void SetItems(const TArray<FEntry>& InItems);
	void SetAssemblyMode(bool bEnabled);
	void RedrawStage();
	void SetComponentPanelRayTargets(const TArray<FVector>& WorldTargets);
	void SetSelectedIndex(int32 Index);
	int32 GetSelectedIndex() const { return SelectedIndex; }
	bool GetItemLocation(int32 Index, FVector& OutWorld) const;
	FVector GetFocusLocation() const;

	/** The item under construction, off to one side in Assembly mode — a live object id (MSCO prototype), not a template. Zero clears it. */
	void SetPrototypeObject(int64 ObjectId);
	/** Where the prototype currently sits, world space — for the widget's card, tracked on screen beside it. */
	FVector GetPrototypeLocation() const;

	/**
	 * How "real" the prototype should look, 0-1: 0 is a pure hologram (the
	 * glow overlay dominant, as if nothing exists yet), 1 is fully tangible
	 * (the real model's own materials read clearly). Meant to track filled
	 * slots / required slots, so the item visibly solidifies as resources
	 * go in. No effect before a prototype model has resolved.
	 */
	void SetPrototypeTangibility(float Progress);

	/**
	 * The Assembly floor: RowsBySlot[i] is slot i's candidate resources
	 * (already filtered/capped by the widget). Rebuilds every row's models
	 * — call only when the candidate set itself changes, not on a plain
	 * slot-focus change (see SetSelectedSlotForCandidates for that).
	 */
	void SetCandidateRows(const TArray<TArray<FEntry>>& RowsBySlot);
	/** Filled assembly slots drive beams from their floor lanes into the prototype. */
	void SetSlotFillCounts(const TArray<int32>& Counts);
	void SetContributionOrigins(const TArray<FVector>& WorldOrigins);
	/** Which slot's row is pulled to the front; a cheap reposition, no model reload. */
	void SetSelectedSlotForCandidates(int32 SlotIndex);
	/** Which candidate within the front row is highlighted/would be placed. */
	void SetSelectedCandidateColumn(int32 Column);
	int32 GetSelectedCandidateColumn() const { return SelectedCandidateColumn; }
	bool GetCandidateLocation(int32 SlotIndex, int32 Column, FVector& OutWorld) const;

	/** Where the Assembly camera should look. */
	FVector GetAssemblyFocusLocation() const;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetDroidHover() const override;
	virtual float GetFadeRadius() const override { return 320.f; }

private:
	struct FProjectedItem
	{
		FString Name;
		uint32 TemplateCrc = 0;
		int64 ObjectId = 0;
		TObjectPtr<USceneComponent> Pivot;
		TObjectPtr<UMeshComponent> Model;
		TObjectPtr<UMeshComponent> HoloOverlay;
		TObjectPtr<UTextRenderComponent> Label;
		float ModelExtent = 0.f;
		FVector ModelOrigin = FVector::ZeroVector;
	};

	void AttachModel(int32 Index, int32 Revision, UObject* Mesh, const TArray<UMaterialInterface*>& Materials);
	FVector ItemLocation(int32 Index) const;

	TArray<FProjectedItem> Items;
	UPROPERTY() TArray<TObjectPtr<UObject>> ItemObjects;
	int32 SelectedIndex = 0;
	int32 Revision = 0;

	void AttachPrototypeModel(int32 RequestedRevision, UObject* Mesh, const TArray<UMaterialInterface*>& Materials);

	TObjectPtr<USceneComponent> PrototypePivot;
	TObjectPtr<UMeshComponent> PrototypeModel;
	TObjectPtr<UMeshComponent> PrototypeHoloOverlay;
	UPROPERTY() TArray<TObjectPtr<UObject>> PrototypeObjects;
	int64 PrototypeObjectId = 0;
	int32 PrototypeRevision = 0;
	float PrototypeExtent = 0.f;
	FVector PrototypeOrigin = FVector::ZeroVector;
	/** SetPrototypeTangibility's last value; applied (and re-applied on model attach) in Tick. */
	float PrototypeTangibility = 0.f;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> AssemblyPan;
	bool bAssemblyMode = false;
	float PanTransition = 0.f;
	TArray<FVector> ComponentPanelRayTargets;

	struct FProjectedCandidate
	{
		FString Name;
		int64 ObjectId = 0;
		int32 SlotIndex = 0;
		int32 Column = 0;
		int32 RowColumnCount = 1;
		TObjectPtr<USceneComponent> Pivot;
		TObjectPtr<UMeshComponent> Model;
		TObjectPtr<UMeshComponent> HoloOverlay;
		TObjectPtr<UTextRenderComponent> Label;
		float ModelExtent = 0.f;
		FVector ModelOrigin = FVector::ZeroVector;
		/** This item's own slow idle spin (yaw, on top of the row's face angle) — accumulated in Tick rather than added via AddLocalRotation, since the face angle now shares the same rotation. */
		float SpinYaw = 0.f;
	};

	void AttachCandidateModel(int32 Index, int32 RequestedRevision, UObject* Mesh, const TArray<UMaterialInterface*>& Materials);
	int32 SlotRowDepthIndex(int32 SlotIndex) const;
	/** Degrees a slot's row is yawed away from the camera-facing front row — 0 for the selected slot, increasing with distance from it (see CandidateLocation). */
	FVector CandidateLocation(const FProjectedCandidate& Candidate) const;

	TArray<FProjectedCandidate> Candidates;
	UPROPERTY() TArray<TObjectPtr<UObject>> CandidateObjects;
	int32 CandidateRevision = 0;
	int32 SelectedSlotForCandidates = 0;
	int32 SelectedCandidateColumn = 0;
	TArray<int32> SlotFillCounts;
	TArray<FVector> ContributionOrigins;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LaneLines;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ContributionLines;
};
