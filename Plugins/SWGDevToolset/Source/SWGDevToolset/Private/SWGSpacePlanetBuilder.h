#pragma once

namespace SWGSpacePlanetAssets
{
	/**
	 * Generates the planet materials under /Game/SWGEmu/Materials: M_SWGPlanetAtmosphere (sun-aware shell),
	 * M_SWGPlanetSurface (retail DTLA/DTLB/MASK blend, globe terrain with retail ground-detail textures, terminator, aerial tint, cloud shadows,
	 * water-only glint; land is rough and non-specular), M_SWGPlanetCloud and M_SWGSunSprite (round disc and corona for the sun and stars).
	 * Existing assets are skipped unless bRebuild.
	 */
	void BuildAll(bool bRebuild);
}
