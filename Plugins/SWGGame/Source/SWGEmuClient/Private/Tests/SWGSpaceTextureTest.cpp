#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "TRE/SWGDDSTextureLoader.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceTextureTest, "SWG.Space.RGB24Texture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceTextureTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Bytes;
	Bytes.SetNumZeroed(128);
	auto Put = [&](int32 Offset, uint32 Value) { FMemory::Memcpy(Bytes.GetData() + Offset, &Value, 4); };
	Put(0, 0x20534444); Put(4, 124); Put(8, 0x2100f);
	Put(12, 1); Put(16, 2); Put(20, 6); Put(28, 2);
	Put(76, 32); Put(80, 0x40); Put(88, 24);
	Put(92, 0xff0000); Put(96, 0xff00); Put(100, 0xff); Put(108, 0x401008);
	const uint8 Pixels[] = { 1,2,3, 40,50,60, 4,5,6 };
	Bytes.Append(Pixels, UE_ARRAY_COUNT(Pixels));
	UTexture2D* Texture = FSWGDDSTextureLoader::LoadTexture2D(Bytes, TEXT("SpaceBGR24Fixture"), true);
	if (!TestNotNull(TEXT("24-bit BGR DDS loads"), Texture)) return false;
	TestEqual(TEXT("expanded pixel format"), Texture->GetPixelFormat(), PF_B8G8R8A8);
	TestEqual(TEXT("all authored mip levels retained"), Texture->GetPlatformData()->Mips.Num(), 2);
	for (int32 Mip = 0; Mip < 2; ++Mip)
	{
		FByteBulkData& Bulk = Texture->GetPlatformData()->Mips[Mip].BulkData;
		const uint8* Data = static_cast<const uint8*>(Bulk.LockReadOnly());
		if (!Data) { Bulk.Unlock(); return false; }
		const int32 SourceOffset = Mip == 0 ? 0 : 6;
		const int32 Count = Mip == 0 ? 2 : 1;
		for (int32 Pixel = 0; Pixel < Count; ++Pixel)
		{
			for (int32 Channel = 0; Channel < 3; ++Channel)
				TestEqual(TEXT("BGR channels preserved"), Data[Pixel*4+Channel], Pixels[SourceOffset+Pixel*3+Channel]);
			TestEqual(TEXT("opaque alpha added"), Data[Pixel*4+3], uint8(255));
		}
		Bulk.Unlock();
	}
	Put(92, 0xff); Put(100, 0xff0000);
	UTexture2D* RGB = FSWGDDSTextureLoader::LoadTexture2D(Bytes, TEXT("SpaceRGB24Fixture"), true);
	if (!TestNotNull(TEXT("24-bit RGB DDS loads"), RGB)) return false;
	FByteBulkData& RGBBulk = RGB->GetPlatformData()->Mips[0].BulkData;
	const uint8* RGBData = static_cast<const uint8*>(RGBBulk.LockReadOnly());
	if (!RGBData) { RGBBulk.Unlock(); return false; }
	TestEqual(TEXT("RGB red moves to BGRA red"), RGBData[2], uint8(1));
	TestEqual(TEXT("RGB blue moves to BGRA blue"), RGBData[0], uint8(3));
	TestEqual(TEXT("RGB opaque alpha"), RGBData[3], uint8(255));
	RGBBulk.Unlock();
	Bytes.RemoveAt(Bytes.Num()-1);
	TestNull(TEXT("truncated RGB mip rejected"), FSWGDDSTextureLoader::LoadTexture2D(Bytes, TEXT("SpaceTruncated24Fixture"), true));
	return true;
}
#endif
