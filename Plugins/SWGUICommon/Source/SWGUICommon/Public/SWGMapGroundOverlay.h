#pragma once

#include "CoreMinimal.h"
#include "SWGMapGroundOverlay.generated.h"

/** A colour field laid on the ground of a map (a survey's concentrations). Raw metres, x east, y north. */
USTRUCT(BlueprintType)
struct SWGUICOMMON_API FSWGMapGroundOverlay
{
	GENERATED_BODY()

	/** South-west corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FVector2D Min = FVector2D::ZeroVector;

	/** North-east corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FVector2D Max = FVector2D::ZeroVector;

	/** Vertices per row; at least 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	int32 Columns = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	int32 Rows = 0;

	/** Columns x Rows vertex colours, row 0 along the north edge; alpha fades the field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	TArray<FLinearColor> Colors;

	/** Border drawn round the field; transparent draws none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FLinearColor OutlineColor = FLinearColor::Transparent;

	bool IsValid() const { return Columns >= 2 && Rows >= 2 && Colors.Num() == Columns * Rows && Max.X > Min.X && Max.Y > Min.Y; }
};
