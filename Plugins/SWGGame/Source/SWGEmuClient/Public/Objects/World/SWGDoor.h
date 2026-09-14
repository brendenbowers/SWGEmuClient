

#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGDoor.generated.h"

class ASWGCell;
class USphereComponent;
class UPrimitiveComponent;
struct FSWGDoorStyleRow;

UCLASS()
class SWGEMUCLIENT_API ASWGDoor : public ASWGObject
{
    GENERATED_BODY()
public:
    ASWGDoor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    int32 PortalNumber;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    TObjectPtr<ASWGCell> CellA;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    TObjectPtr<ASWGCell> CellB;   // nullptr if the other side is cell 0

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    bool bIsOpen = false;

    /** FSWGDoorStyleRow::MoveOffset — local-space translation from closed to fully open. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    FVector OpenOffset = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    float OpenTime = 0.5f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    float CloseTime = 0.5f;

    /** No documented physical meaning found in Core3 or the mesh data — stored for future tuning, not currently used by the move interpolation (see Tick). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    float Springiness = 1.0f;

    /** Shapes the open/close ease curve — see Tick. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    float Smoothness = 1.0f;

    UPROPERTY(VisibleAnywhere, Category = "SWGEmu")
    TObjectPtr<USphereComponent> TriggerVolume;

    void InitializeDoorStyle(const FSWGDoorStyleRow* StyleRow);

    UFUNCTION(BlueprintCallable, Category = "SWGEmu")
    void Open();

    UFUNCTION(BlueprintCallable, Category = "SWGEmu")
    void Close();

    UFUNCTION(BlueprintCallable, Category = "SWGEmu")
    void ToggleOpen();

    virtual void Tick(float DeltaTime) override;

private:
    UFUNCTION()
    void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    FTransform ClosedRelativeTransform;

    /** 0 = fully closed, 1 = fully open — moves toward 1 while bIsOpen, toward 0 otherwise (see Tick). */
    float MoveAlpha = 0.0f;
    bool bMoving = false;

    /** How many pawns are currently inside TriggerVolume — Open() while > 0, Close() when it returns to 0, so overlapping pawns leaving one at a time doesn't close on the first one out. */
    int32 OverlappingPawnCount = 0;
};
