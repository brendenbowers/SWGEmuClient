#pragma once

namespace SWGTerrainTintMaterial
{
	/** Logs every expression of M_SWGTerrainBlend and what feeds its base colour. */
	void Dump();

	/**
	 * Multiplies M_SWGTerrainBlend's base colour by the ground tint the terrain mesh carries in UV1 (R,G) and UV2 (R = B).
	 * Idempotent: a material that already has the SWGGroundTint node is left alone.
	 */
	void AddToTerrainMaterial();
}
