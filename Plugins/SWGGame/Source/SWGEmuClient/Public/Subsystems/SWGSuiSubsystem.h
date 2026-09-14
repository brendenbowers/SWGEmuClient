#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGSuiSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;
struct FSuiCreatePageMessage;

/** A button/event the server wants to hear about, and the values it wants back with it. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGSuiSubscription
{
	GENERATED_BODY()

	/** Position of the subscribe command in the page — what goes back as EventIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	int32 EventIndex = 0;

	/** Retail UI event type: 9 = OK/primary button, 10 = Cancel. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	int32 EventType = 0;

	/** "widget.property" keys whose current values are sent back, in order. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	TArray<FString> ReturnFields;
};

/** TMap values can't be TArray directly in a USTRUCT, hence the wrapper. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGSuiStringList
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	TArray<FString> Entries;
};

/**
 * A server UI page decoded from its command script into what a window
 * needs: property values by "widget.property", list entries by data source,
 * and the events to answer.
 *
 * Widget/property names are retail's (Core3 Sui*BoxImplementation):
 *   bg.caption.lblTitle.Text  Prompt.lblPrompt.Text
 *   btnOk.Text btnCancel.Text btnRevert.Text (message box "other")  btnOther.Text (list box)
 *   btnCancel.Visible/Enabled  btnRevert.Visible  btnOther.visible
 *   txtInput.Text txtInput.MaxLength (input box)
 *   List.dataList entries: <entry>.Text (list box rows)
 */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGSuiPage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	int32 PageId = 0;

	/** Retail template name: "Script.messageBox", "Script.listBox", "Script.inputBox", ... */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	FString ScriptClass;

	/** "widget.property" (property lowercased) -> value, "@file:key" references already resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	TMap<FString, FString> Properties;

	/** Data source ("List.dataList") -> row texts, in order. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	TMap<FString, FSWGSuiStringList> Lists;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	TArray<FSWGSuiSubscription> Subscriptions;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	int64 UsingObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|SUI")
	float ForceCloseDistance = 0.f;

	/** Property lookup, case-insensitive on the property half. */
	FString GetProperty(const FString& Widget, const FString& Property) const;
	bool GetPropertyBool(const FString& Widget, const FString& Property, bool bDefault) const;

	/** Subscription for a retail event type (9 OK, 10 Cancel), or nullptr if the page didn't ask for it. */
	const FSWGSuiSubscription* FindSubscription(int32 EventType) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnSuiPageOpened, const FSWGSuiPage&, Page);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnSuiPageClosed, int32, PageId);

/** Retail SUI event types, as written in a subscribe command's second narrow parameter. */
namespace SWGSuiEvent
{
	constexpr int32 Ok = 9;
	constexpr int32 Cancel = 10;
}

/**
 * Server UI (SUI) windows: message boxes, list pickers, text prompts and the
 * rest of what the server opens through SuiCreatePage. Decodes pages, keeps
 * the open ones, and sends the player's answer back as SuiEventNotification.
 */
UCLASS()
class SWGEMUCLIENT_API USWGSuiSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Answers a page: EventType is the retail event (SWGSuiEvent::Ok/Cancel),
	 * Values the current "widget.property" -> value of the window's fields; the
	 * subscription decides which of them are sent. Closes the page locally.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|SUI")
	bool Respond(int32 PageId, int32 EventType, const TMap<FString, FString>& Values);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|SUI")
	bool GetOpenPage(int32 PageId, FSWGSuiPage& OutPage) const;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|SUI")
	FSWGOnSuiPageOpened OnPageOpened;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|SUI")
	FSWGOnSuiPageClosed OnPageClosed;

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	FSWGSuiPage DecodePage(const FSuiCreatePageMessage& Message) const;
	FString Resolve(const FString& Text) const;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	FDelegateHandle MessageHandle;

	TMap<int32, FSWGSuiPage> OpenPages;
};
