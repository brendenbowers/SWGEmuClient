#pragma once

namespace SWGHoloUIAssets
{
	/** Generates M_SWGHoloUI (atlas sprites), M_SWGHoloUIArc (procedural gauge arcs) and M_SWGHoloRadar (the 3D radar mesh material) under /Game/SWGEmu/Materials. Skipped if they exist unless bRebuild. */
	void BuildAll(bool bRebuild);
}
