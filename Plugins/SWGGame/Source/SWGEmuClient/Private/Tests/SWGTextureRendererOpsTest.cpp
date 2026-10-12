#include "Misc/AutomationTest.h"
#include "TRE/SWGTextureRenderer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace TrtFixture
{
 template<typename T> void LE(TArray<uint8>& Bytes, T Value) { Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(Value)); }
 void String(TArray<uint8>& Bytes, const ANSICHAR* Value) { Bytes.Append(reinterpret_cast<const uint8*>(Value), FCStringAnsi::Strlen(Value) + 1); }
 TArray<uint8> Chunk(const ANSICHAR* Tag, const TArray<uint8>& Payload)
 {
  TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Tag), 4);
  for (int32 Shift : {24,16,8,0}) Bytes.Add(uint8(uint32(Payload.Num()) >> Shift));
  Bytes.Append(Payload); return Bytes;
 }
 TArray<uint8> Form(const ANSICHAR* Tag, const TArray<uint8>& Children)
 { TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Tag),4); Bytes.Append(Children); return Chunk("FORM",Bytes); }
 TArray<uint8> List(const ANSICHAR* Tag, const TArray<TArray<uint8>>& Children)
 {
  TArray<uint8> Count; LE(Count,uint32(Children.Num())); TArray<uint8> Bytes=Chunk("INFO",Count);
  for (const auto& Child:Children) Bytes.Append(Child); return Form(Tag,Bytes);
 }
 TArray<uint8> Op(const ANSICHAR* Tag, const TArray<uint8>& Tail)
 { TArray<uint8> Bytes; LE(Bytes,0u); LE(Bytes,0x4d41494eu); Bytes.Append(Tail); return Chunk(Tag,Bytes); }
 TArray<uint8> Texture(int32 Index) { TArray<uint8> Bytes; LE(Bytes,Index); return Op("SSTC",Bytes); }
 TArray<uint8> Fixture(const TArray<TArray<uint8>>& TrueOps, const TArray<TArray<uint8>>& FalseOps={}, const TArray<uint8>& Condition={}, bool bClear=false)
 {
  TArray<uint8> Body, Dest; LE(Dest,2u); LE(Dest,2u); Body.Append(Chunk("DEST",Dest));
  TArray<uint8> Shader; String(Shader,"shader/texren_copy_c1a1.sht"); Body.Append(List("SHTM",{Chunk("NAME",Shader)}));
  TArray<uint8> Textures; LE(Textures,7u); for (int32 I=0;I<7;++I) { const FTCHARToUTF8 Name(*FString::Printf(TEXT("texture/%d.dds"),I)); String(Textures,Name.Get()); }
  Body.Append(Form("TXTS",Chunk("INFO",Textures)));
  TArray<uint8> VInfo, VData; LE(VInfo,0x1101u); LE(VInfo,4u);
  for (FVector2D Point : {FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)})
  { LE(VData,float(Point.X)); LE(VData,float(Point.Y)); LE(VData,3.f); LE(VData,float(Point.X)); LE(VData,float(Point.Y)); }
  TArray<uint8> VertexBody=Chunk("INFO",VInfo); VertexBody.Append(Chunk("DATA",VData));
  Body.Append(List("VBS ",{Form("VTXA",Form("0003",VertexBody))}));
  TArray<uint8> DrawInfo; LE(DrawInfo,0u); LE(DrawInfo,1u); TArray<uint8> Index; LE(Index,0u);
  TArray<uint8> Draw=Chunk("INFO",DrawInfo); Draw.Append(Chunk("RTFC",Index));
  TArray<TArray<uint8>> Commands={Form("SRSC",Draw)};
  if (bClear) { TArray<uint8> Clear; for(uint32 Value:{1u,0xff123456u,1u,0x3f800000u,1u,0u}) LE(Clear,Value); Commands.Add(Chunk("CFBC",Clear)); }
  Body.Append(List("RCMS",Commands));
  TArray<uint8> Pcmd; if(!Condition.IsEmpty()) Pcmd.Append(Form("COND",Condition));
  Pcmd.Append(List("TOPS",TrueOps)); Pcmd.Append(List("FOPS",FalseOps)); Body.Append(List("PCMS",{Form("PCMD",Pcmd)}));
  return Form("BTRT",Form("0001",Body));
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGTextureRendererOpsTest,"SWGEmu.Appearance.TextureRenderer.ExtendedOps",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FSWGTextureRendererOpsTest::RunTest(const FString& Parameters)
{
 using namespace TrtFixture;
 FSWGTextureRenderer Renderer; FSWGImage Image, White; White.Width=White.Height=1; White.Pixels={FColor(255,255,255,128)};
 FSWGTextureCompositor::FInputs Inputs;
 Inputs.LoadImage=[&](const FString&) { return &White; };
 Inputs.GetVariable=[](const FString& Name) { return Name==TEXT("packed") ? int32(0xff204060u) : 256; };
 auto Read=[&](const TArray<uint8>& Bytes) { return FSWGTextureRenderer::Read(FSWGIffReader(Bytes),Renderer); };
 TArray<uint8> Grid; LE(Grid,1u); String(Grid,"row"); LE(Grid,2u); String(Grid,"column"); LE(Grid,3u);
 TestTrue(TEXT("SST2 and NOP parse"),Read(Fixture({Op("SST2",Grid),Chunk("NOP ",{})})));
 TestFalse(TEXT("new operations supported"),Renderer.bHasUnsupportedOps);
 if (Renderer.Ops.Num()!=1) return false;
 const auto& Selector=Renderer.Ops[0];
 TestEqual(TEXT("non-square grid uses column count as row stride"),FSWGTextureCompositor::ResolveTextureIndex(Selector,1,2),6);
 TestEqual(TEXT("each grid axis clamps separately"),FSWGTextureCompositor::ResolveTextureIndex(Selector,-1,999),3);
 TestTrue(TEXT("second variable participates in customization cache"),Renderer.GetVariables().Contains(TEXT("column")));
 FSWGTextureRenderer::FOp Huge=Selector; Huge.TextureCount=Huge.SecondTextureCount=MAX_int32;
 TestEqual(TEXT("texture index overflow rejected"),FSWGTextureCompositor::ResolveTextureIndex(Huge,MAX_int32,MAX_int32),INDEX_NONE);
 TArray<uint8> Packed; String(Packed,"packed");
 TestTrue(TEXT("packed factor parses"),Read(Fixture({Texture(0),Op("STF ",Packed)})));
 if (!TestTrue(TEXT("packed factor composites"),FSWGTextureCompositor::Compose(Renderer,Inputs,Image))) return false;
 TestEqual(TEXT("packed ARGB factor changes RGB, shader keeps texture alpha"),Image.Pixels[0],FColor(32,64,96,128));
 TArray<uint8> Alpha={12,34,56}; String(Alpha,"alpha");
 TestTrue(TEXT("alpha factor parses"),Read(Fixture({Texture(0),Op("STFA",Alpha)})));
 if (!TestTrue(TEXT("alpha factor composites"),FSWGTextureCompositor::Compose(Renderer,Inputs,Image))) return false;
 TestEqual(TEXT("STFA supplies RGB; copy shader ignores factor alpha"),Image.Pixels[0],FColor(12,34,56,128));
 for (const ANSICHAR* Tag:{"AND ","OR  "})
 {
  const bool bAnd=FCStringAnsi::Strcmp(Tag,"AND ")==0;
  const auto Condition=List(Tag,{Chunk("TRUE",{}),Chunk("FALS",{})});
  TestTrue(TEXT("nested condition parses"),Read(Fixture({Texture(1)},{Texture(2)},Condition)));
  TestFalse(TEXT("condition is supported"),Renderer.bHasUnsupportedOps);
  if (Renderer.Ops.Num()!=1) return false;
  TestEqual(TEXT("correct branch selected"),Renderer.Ops[0].TextureBase,bAnd?2:1);
  TestTrue(TEXT("empty logical condition parses"),Read(Fixture({Texture(1)},{Texture(2)},List(Tag,{}))));
  if (Renderer.Ops.Num()!=1) return false;
  TestEqual(TEXT("empty AND true, empty OR false"),Renderer.Ops[0].TextureBase,bAnd?1:2);
 }
 TestTrue(TEXT("unknown condition is flagged"),Read(Fixture({Texture(0)},{},Chunk("CVTP",{}))));
 TestTrue(TEXT("unknown condition not executed unconditionally"),Renderer.bHasUnsupportedOps);
 TestFalse(TEXT("unknown condition preserves static texture fallback"),FSWGTextureCompositor::Compose(Renderer,Inputs,Image));
 TestTrue(TEXT("unknown operation is flagged"),Read(Fixture({Texture(0),Chunk("BAD!",{})})));
 TestFalse(TEXT("unknown operation does not produce a partial texture"),FSWGTextureCompositor::Compose(Renderer,Inputs,Image));
 TestFalse(TEXT("truncated alpha factor rejected"),Read(Fixture({Op("STFA",{1,2})})));
 Packed.Pop(); TestFalse(TEXT("unterminated variable rejected"),Read(Fixture({Op("STF ",Packed)})));
 TestTrue(TEXT("clear command parses"),Read(Fixture({Texture(0)},{},{},true)));
 if (!TestTrue(TEXT("clear command composites"),FSWGTextureCompositor::Compose(Renderer,Inputs,Image))) return false;
 TestEqual(TEXT("clear occurs in command order after drawing"),Image.Pixels[0],FColor(0x12,0x34,0x56,255));
 return true;
}
#endif
