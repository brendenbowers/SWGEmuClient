#pragma once

namespace SWGFxMaterials
{
	/**
	 * Generates the M_SWGParticle_* masters (Additive, Translucent, Masked, Modulate, Subtractive) and the M_SWGRibbon_* ones (Additive, Translucent) under /Game/SWGEmu/Materials.
	 * Skips ones that exist unless bRebuildExisting, which clears and rebuilds them in place.
	 */
	void BuildAll(bool bRebuildExisting);
}
