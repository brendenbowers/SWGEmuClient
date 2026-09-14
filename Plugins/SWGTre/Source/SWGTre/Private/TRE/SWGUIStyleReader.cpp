#include "TRE/SWGUIStyleReader.h"

namespace
{
	struct FTag
	{
		FString Name;
		TMap<FString, FString> Attributes;
		bool bClosing = false;     // </Name>
		bool bSelfClosing = false; // <Name ... />
	};

	/**
	 * Reads one tag starting at Data[Pos] == '<' and leaves Pos just past its
	 * '>'. Attribute values are single-quoted and never contain a quote.
	 */
	bool ReadTag(const FString& Data, int32& Pos, FTag& OutTag)
	{
		const int32 Length = Data.Len();
		check(Data[Pos] == TEXT('<'));
		++Pos;

		while (Pos < Length && FChar::IsWhitespace(Data[Pos]))
		{
			++Pos;
		}
		if (Pos < Length && Data[Pos] == TEXT('/'))
		{
			OutTag.bClosing = true;
			++Pos;
		}

		const int32 NameStart = Pos;
		while (Pos < Length && (FChar::IsAlnum(Data[Pos]) || Data[Pos] == TEXT('_')))
		{
			++Pos;
		}
		OutTag.Name = Data.Mid(NameStart, Pos - NameStart);

		while (Pos < Length)
		{
			while (Pos < Length && FChar::IsWhitespace(Data[Pos]))
			{
				++Pos;
			}
			if (Pos >= Length)
			{
				return false;
			}
			if (Data[Pos] == TEXT('>'))
			{
				++Pos;
				return true;
			}
			if (Data[Pos] == TEXT('/'))
			{
				OutTag.bSelfClosing = true;
				++Pos;
				continue;
			}

			const int32 KeyStart = Pos;
			while (Pos < Length && Data[Pos] != TEXT('=') && !FChar::IsWhitespace(Data[Pos]) && Data[Pos] != TEXT('>'))
			{
				++Pos;
			}
			const FString Key = Data.Mid(KeyStart, Pos - KeyStart);

			while (Pos < Length && FChar::IsWhitespace(Data[Pos]))
			{
				++Pos;
			}
			if (Pos >= Length || Data[Pos] != TEXT('='))
			{
				// Bare attribute without a value - keep scanning.
				continue;
			}
			++Pos;
			while (Pos < Length && FChar::IsWhitespace(Data[Pos]))
			{
				++Pos;
			}
			if (Pos >= Length || Data[Pos] != TEXT('\''))
			{
				return false;
			}
			++Pos;
			const int32 ValueStart = Pos;
			while (Pos < Length && Data[Pos] != TEXT('\''))
			{
				++Pos;
			}
			if (Pos >= Length)
			{
				return false;
			}
			OutTag.Attributes.Add(Key, Data.Mid(ValueStart, Pos - ValueStart));
			++Pos;
		}
		return false;
	}

	/** "/styles.icon.posture.upright" -> "icon.posture.upright"; relative paths are left as-is. */
	FString NormaliseStylePath(const FString& Path)
	{
		FString Result = Path.ToLower();
		if (Result.RemoveFromStart(TEXT("/styles.")))
		{
			return Result;
		}
		Result.RemoveFromStart(TEXT("/"));
		return Result;
	}

	bool ParseRect(const FString& Text, FIntRect& OutRect)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() != 4)
		{
			return false;
		}
		OutRect = FIntRect(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]), FCString::Atoi(*Parts[3]));
		return true;
	}
}

const FSWGUIImageStyle* FSWGUIStyleSheet::FindImageStyle(const FString& DottedPath) const
{
	FString Path = DottedPath.ToLower();

	// Alias chains are short in practice; the bound just guards against a cycle in the data.
	for (int32 Hop = 0; Hop < 8; ++Hop)
	{
		if (const FSWGUIImageStyle* Style = ImageStyles.Find(Path))
		{
			return Style;
		}
		const FString* Alias = Aliases.Find(Path);
		if (!Alias)
		{
			return nullptr;
		}
		Path = *Alias;
	}
	return nullptr;
}

bool FSWGUIStyleReader::Read(const TArray<uint8>& Data, FSWGUIStyleSheet& OutSheet)
{
	if (Data.IsEmpty())
	{
		return false;
	}

	FString Text;
	Text.AppendChars(reinterpret_cast<const ANSICHAR*>(Data.GetData()), Data.Num());

	// Every open element pushes an entry so closing tags pop correctly;
	// only Namespace entries contribute to the dotted path.
	struct FOpenElement
	{
		FString Name;
		FString PathSegment; // empty for non-namespaces
	};
	TArray<FOpenElement> Stack;

	auto CurrentPath = [&Stack]() -> FString
	{
		FString Path;
		for (const FOpenElement& Element : Stack)
		{
			if (!Element.PathSegment.IsEmpty())
			{
				if (!Path.IsEmpty())
				{
					Path += TEXT(".");
				}
				Path += Element.PathSegment;
			}
		}
		return Path;
	};

	int32 Pos = 0;
	const int32 Length = Text.Len();
	while (Pos < Length)
	{
		if (Text[Pos] != TEXT('<'))
		{
			++Pos;
			continue;
		}

		FTag Tag;
		const int32 TagStart = Pos;
		if (!ReadTag(Text, Pos, Tag))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGUIStyleReader: malformed tag at offset %d"), TagStart);
			return false;
		}

		if (Tag.bClosing)
		{
			if (!Stack.IsEmpty())
			{
				Stack.Pop();
			}
			continue;
		}

		const bool bIsNamespace = Tag.Name == TEXT("Namespace");
		const FString* NameAttribute = Tag.Attributes.Find(TEXT("Name"));
		const FString ParentPath = CurrentPath();

		if (bIsNamespace)
		{
			FString Segment = NameAttribute ? NameAttribute->ToLower() : FString();
			// The root "Styles" namespace is implied by the "/styles." prefix aliases use.
			if (Stack.IsEmpty() && Segment == TEXT("styles"))
			{
				Segment.Reset();
			}
			const FString OwnPath = ParentPath.IsEmpty() ? Segment : (Segment.IsEmpty() ? ParentPath : ParentPath + TEXT(".") + Segment);

			for (const TPair<FString, FString>& Attribute : Tag.Attributes)
			{
				if (Attribute.Key != TEXT("Name") && Attribute.Value.StartsWith(TEXT("/")))
				{
					const FString AliasPath = OwnPath.IsEmpty() ? Attribute.Key.ToLower() : OwnPath + TEXT(".") + Attribute.Key.ToLower();
					OutSheet.Aliases.Add(AliasPath, NormaliseStylePath(Attribute.Value));
				}
			}

			if (!Tag.bSelfClosing)
			{
				Stack.Add({ Tag.Name, Segment });
			}
			continue;
		}

		if (Tag.Name == TEXT("ImageStyle") && NameAttribute)
		{
			const FString* Source = Tag.Attributes.Find(TEXT("Source"));
			const FString* SourceRect = Tag.Attributes.Find(TEXT("SourceRect"));
			FSWGUIImageStyle Style;
			if (Source && SourceRect && ParseRect(*SourceRect, Style.SourceRect))
			{
				Style.Source = *Source;
				int32 ColonIndex;
				if (Style.Source.FindLastChar(TEXT(':'), ColonIndex))
				{
					Style.Source.RightChopInline(ColonIndex + 1);
				}
				const FString Path = ParentPath.IsEmpty() ? NameAttribute->ToLower() : ParentPath + TEXT(".") + NameAttribute->ToLower();
				OutSheet.ImageStyles.Add(Path, MoveTemp(Style));
			}
		}

		if (!Tag.bSelfClosing)
		{
			Stack.Add({ Tag.Name, FString() });
		}
	}

	return !OutSheet.ImageStyles.IsEmpty();
}
