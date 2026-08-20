

#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGDoor.generated.h"

class ASWGCell;

/**
 * 
 */
UCLASS()
class SWGEMUCLIENT_API ASWGDoor : public ASWGObject
{
    GENERATED_BODY()
public:

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    int32 PortalNumber;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    TObjectPtr<ASWGCell> CellA;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    TObjectPtr<ASWGCell> CellB;   // nullptr if the other side is cell 0

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
    bool bIsOpen = false;

    UFUNCTION(BlueprintCallable, Category = "SWGEmu")
    void ToggleOpen();   // client-local prediction — see §3.3
};