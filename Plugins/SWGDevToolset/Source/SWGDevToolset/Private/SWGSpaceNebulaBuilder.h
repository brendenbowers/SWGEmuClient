#pragma once

namespace SWGSpaceNebulaAssets
{
	/**
	 * Generates the Stage 2 gas-cloud assets under /Game/SWGEmu: the tileable Perlin-Worley noise volume
	 * (Textures/T_SWGNebulaNoise), M_SWGSpaceNebula and M_SWGSpaceWisp. Existing assets are skipped unless bRebuild.
	 */
	void BuildAll(bool bRebuild);
}
