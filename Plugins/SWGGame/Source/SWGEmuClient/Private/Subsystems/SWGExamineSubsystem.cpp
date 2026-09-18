#include "Subsystems/SWGExamineSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/AttributeListMessageIn.h"

void USWGExamineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Commands = Collection.InitializeDependency<USWGCommandSubsystem>();
	ObjectGraph = Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGExamineSubsystem::HandleMessageReceived);
	}
}

void USWGExamineSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	Super::Deinitialize();
}

void USWGExamineSubsystem::RequestExamine(int64 ObjectId)
{
	if (ObjectId != 0)
	{
		OnExamineRequested.Broadcast(ObjectId);
	}
}

bool USWGExamineSubsystem::Describe(int64 ObjectId, FSWGExamineInfo& OutInfo)
{
	if (!DescribeLocal(ObjectId, OutInfo))
	{
		return false;
	}

	// The batch form takes ids as text; retail sends it this way for a single
	// object too, and Core3's plain getattributes replies with nothing.
	if (Commands)
	{
		Commands->SendCommand(TEXT("getattributesbatch"), 0, FString::Printf(TEXT("%lld"), ObjectId));
	}
	return true;
}

bool USWGExamineSubsystem::DescribeLocal(int64 ObjectId, FSWGExamineInfo& OutInfo) const
{
	AActor* Actor = ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr;
	if (!Actor)
	{
		return false;
	}

	OutInfo = FSWGExamineInfo();
	OutInfo.ObjectId = ObjectId;

	if (const USWGTangibleComponent* Tangible = Actor->FindComponentByClass<USWGTangibleComponent>())
	{
		OutInfo.Name = Tangible->GetDisplayName();
	}

	const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor);
	if (NetObject && Tre)
	{
		const FString TemplatePath = Tre->ResolveTemplatePath(NetObject->GetObjectCrc());
		FString Table, Key;
		if (!TemplatePath.IsEmpty() && Tre->FindTemplateStringId(TemplatePath, TEXT("detailedDescription"), Table, Key))
		{
			OutInfo.Description = Tre->LookupString(Table, Key);
		}
		if (OutInfo.Name.IsEmpty())
		{
			OutInfo.Name = Tre->ResolveTemplateObjectName(NetObject->GetObjectCrc());
		}
	}
	return true;
}

void USWGExamineSubsystem::ResolveAttributeLabel(const FString& Key, FString& OutLabel, FString& OutCategory) const
{
	// Retail labels every attribute through obj_attr_n; a key with no entry
	// there shows as itself, which is what retail did too.
	const auto Lookup = [this](const FString& Name)
	{
		const FString Resolved = Tre ? Tre->LookupString(TEXT("obj_attr_n"), Name) : FString();
		return Resolved.IsEmpty() ? Name : Resolved;
	};

	OutCategory.Reset();
	if (Key.StartsWith(TEXT("@")))
	{
		OutLabel = Tre ? Tre->ResolveStringId(Key) : Key;
		return;
	}

	FString Category, Name;
	if (Key.Split(TEXT("."), &Category, &Name))
	{
		OutCategory = Lookup(Category);
		OutLabel = Lookup(Name);
		return;
	}
	OutLabel = Lookup(Key);
}

FString USWGExamineSubsystem::ResolveAttributeValue(const FString& Value) const
{
	return (Tre && Value.StartsWith(TEXT("@"))) ? Tre->ResolveStringId(Value) : Value;
}

void USWGExamineSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message || Message->Opcode != static_cast<uint32>(ESWGMessageOp::AttributeListMessage))
	{
		return;
	}

	const FAttributeListMessageIn& List = *static_cast<const FAttributeListMessageIn*>(Message.Get());

	FSWGExamineInfo Info;
	if (!DescribeLocal(List.ObjectId, Info))
	{
		// The object may have gone since we asked; the lines are still worth showing.
		Info.ObjectId = List.ObjectId;
	}

	for (const FSWGObjectAttribute& Attribute : List.Attributes)
	{
		FSWGExamineAttribute& Line = Info.Attributes.AddDefaulted_GetRef();
		ResolveAttributeLabel(Attribute.Name, Line.Label, Line.Category);
		Line.Value = ResolveAttributeValue(Attribute.Value);
	}

	OnExamineInfo.Broadcast(Info);
}
