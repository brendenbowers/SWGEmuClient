#include "Subsystems/SWGSuiSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/SuiPageMessage.h"
#include "Network/Messages/Zone/SuiEventNotificationMessage.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGSui, Log, All);

namespace
{
	FString MakeKey(const FString& Widget, const FString& Property)
	{
		return Widget + TEXT(".") + Property.ToLower();
	}
}

FString FSWGSuiPage::GetProperty(const FString& Widget, const FString& Property) const
{
	const FString* Value = Properties.Find(MakeKey(Widget, Property));
	return Value ? *Value : FString();
}

bool FSWGSuiPage::GetPropertyBool(const FString& Widget, const FString& Property, bool bDefault) const
{
	const FString Value = GetProperty(Widget, Property);
	if (Value.IsEmpty())
	{
		return bDefault;
	}
	return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
}

const FSWGSuiSubscription* FSWGSuiPage::FindSubscription(int32 EventType) const
{
	return Subscriptions.FindByPredicate([EventType](const FSWGSuiSubscription& Subscription) { return Subscription.EventType == EventType; });
}

void USWGSuiSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGSuiSubsystem::HandleMessageReceived);
	}
}

void USWGSuiSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	Super::Deinitialize();
}

bool USWGSuiSubsystem::Respond(int32 PageId, int32 EventType, const TMap<FString, FString>& Values)
{
	FSWGSuiPage Page;
	if (!OpenPages.RemoveAndCopyValue(PageId, Page))
	{
		UE_LOG(LogSWGSui, Warning, TEXT("Respond: page %d is not open"), PageId);
		return false;
	}

	const FSWGSuiSubscription* Subscription = Page.FindSubscription(EventType);
	if (!Subscription)
	{
		// The server didn't subscribe to this event, so it doesn't want to hear about it —
		// a message box with no cancel button being dismissed, say. Just close.
		UE_LOG(LogSWGSui, Verbose, TEXT("page %d closed on unsubscribed event %d"), PageId, EventType);
		OnPageClosed.Broadcast(PageId);
		return true;
	}

	FSuiEventNotificationMessage Notification;
	Notification.PageId = PageId;
	Notification.EventIndex = Subscription->EventIndex;
	for (const FString& Field : Subscription->ReturnFields)
	{
		const FString* Value = Values.Find(Field);
		Notification.Arguments.Add(Value ? *Value : FString());
	}

	if (Network)
	{
		Network->SendMessage(Notification.Serialize());
	}
	UE_LOG(LogSWGSui, Log, TEXT("page %d answered: event %d (index %u), %d value(s)"),
		PageId, EventType, Notification.EventIndex, Notification.Arguments.Num());

	OnPageClosed.Broadcast(PageId);
	return true;
}

bool USWGSuiSubsystem::GetOpenPage(int32 PageId, FSWGSuiPage& OutPage) const
{
	if (const FSWGSuiPage* Page = OpenPages.Find(PageId))
	{
		OutPage = *Page;
		return true;
	}
	return false;
}

void USWGSuiSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message)
	{
		return;
	}

	switch (static_cast<ESWGMessageOp>(Message->Opcode))
	{
		case ESWGMessageOp::SuiCreatePage:
		case ESWGMessageOp::SuiUpdatePage:
		{
			const FSuiCreatePageMessage& PageMessage = *static_cast<const FSuiCreatePageMessage*>(Message.Get());
			FSWGSuiPage Page = DecodePage(PageMessage);

			UE_LOG(LogSWGSui, Log, TEXT("page %d %s: %s, %d propert(ies), %d list(s), %d subscription(s), using %lld"),
				Page.PageId, Message->Opcode == static_cast<uint32>(ESWGMessageOp::SuiUpdatePage) ? TEXT("updated") : TEXT("opened"),
				*Page.ScriptClass, Page.Properties.Num(), Page.Lists.Num(), Page.Subscriptions.Num(), Page.UsingObjectId);

			OpenPages.Add(Page.PageId, Page);
			OnPageOpened.Broadcast(Page);
			break;
		}
		case ESWGMessageOp::SuiForceClosePage:
		{
			const FSuiForceClosePageMessage& Close = *static_cast<const FSuiForceClosePageMessage*>(Message.Get());
			if (OpenPages.Remove(Close.PageId) > 0)
			{
				UE_LOG(LogSWGSui, Log, TEXT("page %u closed by the server"), Close.PageId);
				OnPageClosed.Broadcast(Close.PageId);
			}
			break;
		}
		default:
			break;
	}
}

FSWGSuiPage USWGSuiSubsystem::DecodePage(const FSuiCreatePageMessage& Message) const
{
	FSWGSuiPage Page;
	Page.PageId = Message.PageId;
	Page.ScriptClass = Message.ScriptClass;
	Page.UsingObjectId = static_cast<int64>(Message.UsingObjectId);
	Page.ForceCloseDistance = Message.ForceCloseDistance;

	for (int32 CommandIndex = 0; CommandIndex < Message.Commands.Num(); ++CommandIndex)
	{
		const FSWGSuiCommand& Command = Message.Commands[CommandIndex];
		switch (Command.GetType())
		{
			case ESWGSuiCommandType::SetProperty:
			case ESWGSuiCommandType::AddDataItem:
			{
				if (Command.NarrowParams.Num() < 2 || Command.WideParams.IsEmpty())
				{
					break;
				}
				const FString& Widget = Command.NarrowParams[0];
				const FString& Property = Command.NarrowParams[1];
				const FString Value = Resolve(Command.WideParams[0]);

				if (Command.GetType() == ESWGSuiCommandType::AddDataItem)
				{
					// Retail follows each "Name" row with a SetProperty on
					// "<source>.<name>" Text; the Text is what's displayed.
					Page.Lists.FindOrAdd(Widget);
					break;
				}

				// A row of a data source: "List.dataList.3" -> Text
				int32 DotIndex;
				if (Property.Equals(TEXT("Text"), ESearchCase::IgnoreCase) && Widget.FindLastChar(TEXT('.'), DotIndex))
				{
					const FString Source = Widget.Left(DotIndex);
					if (FSWGSuiStringList* List = Page.Lists.Find(Source))
					{
						List->Entries.Add(Value);
						break;
					}
				}
				Page.Properties.Add(MakeKey(Widget, Property), Value);
				break;
			}
			case ESWGSuiCommandType::SubscribeToEvent:
			{
				// narrow: source widget, 1-byte event type, callback, then (widget, property) pairs
				if (Command.NarrowParams.Num() < 3)
				{
					break;
				}
				FSWGSuiSubscription& Subscription = Page.Subscriptions.AddDefaulted_GetRef();
				Subscription.EventIndex = CommandIndex;
				Subscription.EventType = Command.NarrowParams[1].IsEmpty() ? 0 : static_cast<int32>(Command.NarrowParams[1][0]);
				for (int32 ParamIndex = 3; ParamIndex + 1 < Command.NarrowParams.Num(); ParamIndex += 2)
				{
					Subscription.ReturnFields.Add(MakeKey(Command.NarrowParams[ParamIndex], Command.NarrowParams[ParamIndex + 1]));
				}
				break;
			}
			case ESWGSuiCommandType::ClearDataSource:
			case ESWGSuiCommandType::AddDataSource:
			case ESWGSuiCommandType::AddDataSourceContainer:
			case ESWGSuiCommandType::ClearDataSourceContainer:
			{
				if (!Command.NarrowParams.IsEmpty())
				{
					Page.Lists.FindOrAdd(Command.NarrowParams[0]).Entries.Reset();
				}
				break;
			}
			default:
				break;
		}
	}

	return Page;
}

FString USWGSuiSubsystem::Resolve(const FString& Text) const
{
	// Only references are resolved; retail also passes raw strings (list rows, defaults) through untouched.
	if (!Tre || !Text.StartsWith(TEXT("@")))
	{
		return Text;
	}

	// A bare "@ok" / "@cancel" is a key in the ui table.
	int32 ColonIndex;
	if (!Text.FindChar(TEXT(':'), ColonIndex))
	{
		const FString Resolved = Tre->LookupString(TEXT("ui"), Text.Mid(1));
		return Resolved.IsEmpty() ? Text.Mid(1) : Resolved;
	}
	return Tre->ResolveStringId(Text);
}
