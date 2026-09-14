#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Screen-space layout for the character-select preview scene (the 3D character
 * and its backdrop, composited behind WBP_CharacterSelect's transparent
 * CharacterPreviewPanel).
 *
 * None of this can be authored as fixed values in the level, because it all
 * depends on the runtime viewport: the renderer holds the VERTICAL fov
 * constant (AspectRatio_MaintainYFOV), so a viewport height change alone
 * re-maps every world position to a different screen X. Anything positioned
 * once at startup drifts as soon as the viewport settles to a different size -
 * which it routinely does while the editor/game window is still coming up.
 */
namespace SWGCharacterPreview
{
	/**
	 * Positions/orients/scales the CharacterPreviewBackdrop-tagged actor so it
	 * exactly covers the CharacterPreviewCamera's frustum at the backdrop's
	 * authored distance. The authored distance is preserved (measured along the
	 * camera's forward axis), keeping "how far back it sits" a level-editing
	 * decision; extents and orientation are derived. Safe to call repeatedly -
	 * it's idempotent for an unchanged viewport.
	 */
	SWGEMUCLIENT_API void FitBackdropToCamera(UWorld& World);

	/** Current game viewport size, or a zero vector if there isn't one yet. */
	SWGEMUCLIENT_API FVector2D GetViewportSize();
}
