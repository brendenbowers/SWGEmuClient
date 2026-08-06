#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Creature/EquiptmentItem.h"
#include "SWGEquipmentComponent.generated.h"

struct FSWGPacket;
struct FSWGMeshData;

/** CREO base6 — equipped items + composite appearance override. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGEquipmentComponent();

	TSWGBaselineList<FEquiptmentItem> EquipmentList;
	FString AlternateAppearance;
	bool bHasBase6 = false;

	void ApplyBase6(FSWGPacket& Packet);
	void ApplyDelta6(FSWGPacket& Packet);

protected:
	void BuildEquipmentVisuals(const TConstArrayView<FEquiptmentItem> EquipmentList);

	/**
	 * RetryCount: the character's own base skeletal mesh is a separate,
	 * independently-queued async build (see
	 * USWGMeshGeneratorSubsystem::ProcessNextRequest) — it can still be
	 * mid-flight when this item's mesh finishes. 
	 */
	void AttachMeshToHardpoint(UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials, int32 RetryCount = 0);
};
