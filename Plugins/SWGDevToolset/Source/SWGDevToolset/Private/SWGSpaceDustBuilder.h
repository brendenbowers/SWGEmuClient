#pragma once

namespace SWGSpaceDustAssets
{
	/** Generates M_SWGSpaceDust under /Game/SWGEmu/Materials, the streak-quad material of the Stage 3 dust field. Skipped if it exists unless bRebuild. */
	void BuildAll(bool bRebuild);
}
