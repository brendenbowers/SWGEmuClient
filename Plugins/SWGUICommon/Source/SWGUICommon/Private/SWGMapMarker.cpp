#include "SWGMapMarker.h"

bool FSWGMapMarker::LooksLike(const FSWGMapMarker& Other) const
{
	return Id == Other.Id && Label.EqualTo(Other.Label) && Style == Other.Style && LabelColor == Other.LabelColor
		&& bSelected == Other.bSelected && bCustomPinColor == Other.bCustomPinColor && PinColor == Other.PinColor
		&& bHasHeading == Other.bHasHeading;
}
