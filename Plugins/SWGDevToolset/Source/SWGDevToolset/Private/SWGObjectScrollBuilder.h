#pragma once

namespace SWGObjectScroll
{
	/** Logs every expression of the three object masters and what feeds each texture sample's UVs. */
	void Dump();

	/**
	 * Adds the ScrollUV1/ScrollUV2 vector parameters to M_SWGObjectTextured/Masked/Translucent, added to the diffuse sample's UVs.
	 * Idempotent: a master that already has ScrollUV1 is left alone.
	 */
	void AddToObjectMaterials();
}
