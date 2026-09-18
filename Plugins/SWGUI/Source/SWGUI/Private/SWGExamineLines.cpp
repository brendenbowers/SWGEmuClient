#include "SWGExamineLines.h"
#include "SWGExamineLineWidget.h"
#include "SWGUISettings.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"

void SWGExamineLines::Fill(UUserWidget* Owner, UPanelWidget* Panel, const FSWGExamineInfo& Info)
{
	if (Info.Attributes.IsEmpty())
	{
		return;
	}

	TSubclassOf<USWGExamineLineWidget> LineClass = USWGUISettings::Get().ExamineLineClass.LoadSynchronous();
	if (!LineClass)
	{
		UE_LOG(LogTemp, Error, TEXT("SWGExamineLines: SWG UI settings have no ExamineLineClass"));
		return;
	}

	Panel->ClearChildren();
	FString LastCategory;
	for (const FSWGExamineAttribute& Attribute : Info.Attributes)
	{
		// Retail prints the group once, above its lines, when it changes.
		if (Attribute.Category != LastCategory)
		{
			LastCategory = Attribute.Category;
			if (!LastCategory.IsEmpty())
			{
				USWGExamineLineWidget* Header = CreateWidget<USWGExamineLineWidget>(Owner, LineClass);
				Panel->AddChild(Header);
				Header->SetHeader(FText::FromString(LastCategory));
			}
		}

		USWGExamineLineWidget* Line = CreateWidget<USWGExamineLineWidget>(Owner, LineClass);
		Panel->AddChild(Line);
		Line->SetLine(FText::FromString(Attribute.Label), FText::FromString(Attribute.Value), !Attribute.Category.IsEmpty());
	}
}
