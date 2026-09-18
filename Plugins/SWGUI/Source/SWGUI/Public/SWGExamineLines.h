#pragma once

#include "CoreMinimal.h"

class UPanelWidget;
class UUserWidget;
struct FSWGExamineInfo;

/** Fills a vertical panel with an item's attribute lines the way retail's examine window lays them out. */
namespace SWGExamineLines
{
	/**
	 * Replaces Panel's children with ExamineLineClass rows (USWGUISettings): "Label: Value"
	 * lines, grouped under a header wherever the category changes. Leaves the panel alone
	 * when Info has no attributes yet.
	 */
	SWGUI_API void Fill(UUserWidget* Owner, UPanelWidget* Panel, const FSWGExamineInfo& Info);
}
