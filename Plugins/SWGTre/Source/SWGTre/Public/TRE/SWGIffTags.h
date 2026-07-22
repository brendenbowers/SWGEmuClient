#pragma once

#include "TRE/SWGIffReader.h"

/**
 * Common IFF tags with one fixed meaning wherever they appear (unlike
 * version-number tags like "0001", whose meaning is form-specific). Prefer
 * these over spelling out SWG_IFF_TAG(...) again at the call site.
 */
namespace SWGIffTags
{
	constexpr FSWGIffTag Data = SWG_IFF_TAG('D', 'A', 'T', 'A');
	constexpr FSWGIffTag Name = SWG_IFF_TAG('N', 'A', 'M', 'E');
	constexpr FSWGIffTag Info = SWG_IFF_TAG('I', 'N', 'F', 'O');
}
