#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGBeamReader.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGEffectTables.h"
#include "TRE/SWGLightningEffectReader.h"
#include "TRE/SWGLightsaberReader.h"
#include "TRE/SWGParticleEffectReader.h"
#include "TRE/SWGShaderReader.h"
#include "TRE/SWGSpriteReader.h"
#include "TRE/SWGSwooshReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Parses every retail .prt/.swh/.lsb/.spr/BEAM/.ltn, animated and scrolling shader, .eft and the .cdf chunks the effects work needs. */
namespace
{
	USWGTreSubsystem* LoadTreSubsystem(FAutomationTestBase& Test)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}

		Tre->AddToRoot();
		Tre->LoadConfig();

		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	TArray<FString> FindFilesWithExtension(const USWGTreSubsystem& Tre, const TCHAR* Extension)
	{
		TArray<FString> Paths = Tre.FindVirtualPaths(Extension);
		Paths.RemoveAll([Extension](const FString& Path) { return !Path.EndsWith(Extension, ESearchCase::IgnoreCase); });
		Paths.Sort();
		return Paths;
	}

	const TCHAR* BlendModeName(ESWGEffectBlendMode Mode)
	{
		switch (Mode)
		{
			case ESWGEffectBlendMode::Opaque: return TEXT("Opaque");
			case ESWGEffectBlendMode::Masked: return TEXT("Masked");
			case ESWGEffectBlendMode::Translucent: return TEXT("Translucent");
			case ESWGEffectBlendMode::Additive: return TEXT("Additive");
			case ESWGEffectBlendMode::Subtractive: return TEXT("Subtractive");
			case ESWGEffectBlendMode::Modulate: return TEXT("Modulate");
			default: return TEXT("Other");
		}
	}

	/** Parse failures are errors; references that retail itself leaves dangling are warnings, capped so the log stays readable. */
	struct FFxCheck
	{
		FAutomationTestBase& Test;
		const USWGTreSubsystem& Tre;
		int32 DanglingCount = 0;
		TSet<FString> DanglingSeen;

		FFxCheck(FAutomationTestBase& InTest, const USWGTreSubsystem& InTre) : Test(InTest), Tre(InTre) {}

		void Fail(const FString& Path, const FString& Why)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Path, *Why));
		}

		bool Exists(const FString& Target) const { return !Target.IsEmpty() && Tre.FileExists(Target); }

		void CheckReference(const FString& Owner, const TCHAR* Kind, const FString& Target)
		{
			if (Target.IsEmpty() || Exists(Target))
			{
				return;
			}
			++DanglingCount;
			if (DanglingSeen.Num() < 60 && !DanglingSeen.Contains(Target.ToLower()))
			{
				UE_LOG(LogTemp, Warning, TEXT("[SWGEmu.Fx.Formats] dangling %s in %s: %s"), Kind, *Owner, *Target);
			}
			DanglingSeen.Add(Target.ToLower());
		}
	};

	/** Blend mode of a particle shader, via its .eft (cached by shader path). */
	ESWGEffectBlendMode ResolveShaderBlendMode(const USWGTreSubsystem& Tre, const FString& ShaderPath, TMap<FString, ESWGEffectBlendMode>& Cache, bool& bOutResolved)
	{
		bOutResolved = false;
		if (const ESWGEffectBlendMode* Cached = Cache.Find(ShaderPath))
		{
			bOutResolved = true;
			return *Cached;
		}

		FSWGShaderData Shader;
		FSWGEffectData Effect;
		if (!Tre.FileExists(ShaderPath))
		{
			return ESWGEffectBlendMode::Other;
		}
		const FSWGIffReader ShaderReader = Tre.CreateIffReader(ShaderPath);
		if (!FSWGShaderReader::ReadShader(ShaderReader, Shader))
		{
			return ESWGEffectBlendMode::Other;
		}
		const FString EffectPath = Shader.EffectName.Replace(TEXT("\\"), TEXT("/"));
		const bool bEffectRead = Shader.bHasInlineEffect
			? FSWGShaderReader::ReadEffect(ShaderReader, Effect)
			: FSWGShaderReader::ReadEffect(Tre.CreateIffReader(EffectPath), Effect);
		if (!bEffectRead)
		{
			return ESWGEffectBlendMode::Other;
		}
		bOutResolved = true;
		Cache.Add(ShaderPath, Effect.GetBlendMode());
		return Effect.GetBlendMode();
	}

	void LogHistogram(const TCHAR* Title, const TMap<FString, int32>& Histogram)
	{
		TArray<FString> Keys;
		Histogram.GenerateKeyArray(Keys);
		Keys.Sort();
		FString Line;
		for (const FString& Key : Keys)
		{
			Line += FString::Printf(TEXT(" %s=%d"), *Key, Histogram[Key]);
		}
		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] %s:%s"), Title, *Line);
	}

	void ParseParticleEffects(FFxCheck& Check)
	{
		const TArray<FString> Paths = FindFilesWithExtension(Check.Tre, TEXT(".prt"));
		Check.Test.TestTrue(TEXT("found the retail .prt set"), Paths.Num() > 1000);

		TMap<FString, int32> EmitterVersions, ShapeKinds, FlockModes, BlendModes, ParticleVersions, EffectVersions;
		TMap<FString, ESWGEffectBlendMode> BlendCache;
		int32 Parsed = 0, Stubs = 0, Emitters = 0, Flocking = 0, Children = 0, Sounds = 0;
		for (const FString& Path : Paths)
		{
			const FSWGIffReader Reader = Check.Tre.CreateIffReader(Path);
			if (!Reader.IsValid())
			{
				++Stubs;
				continue;
			}

			FSWGParticleEffect Effect;
			FString Error;
			if (!FSWGParticleEffectReader::Read(Reader, Effect, &Error))
			{
				Check.Fail(Path, Error);
				continue;
			}
			++Parsed;
			++EffectVersions.FindOrAdd(FString::Printf(TEXT("%d"), Effect.Version));

			for (const FSWGEmitterGroup& Group : Effect.Groups)
			{
				for (const FSWGParticleEmitter& Emitter : Group.Emitters)
				{
					++Emitters;
					if (Emitter.Version >= 6 && (Emitter.Params.Always1A != 1 || Emitter.Params.Always1B != 1))
					{
						Check.Fail(Path, TEXT("params bytes documented as always 1 are not"));
					}
					for (ESWGEmitterCurve Unused : { ESWGEmitterCurve::InheritVelocity, ESWGEmitterCurve::ClusterCount, ESWGEmitterCurve::ClusterRadius })
					{
						const FSWGWaveform& Curve = Emitter.GetCurve(Unused);
						if (!Curve.IsConstant() || Curve.EvaluateValue(0.f) != 0.f)
						{
							Check.Fail(Path, TEXT("curve documented as always zero is not"));
						}
					}
					++EmitterVersions.FindOrAdd(FString::Printf(TEXT("%04d"), Emitter.Version));
					++ParticleVersions.FindOrAdd(FString::Printf(TEXT("%d"), Emitter.Particle.Version));
					++ShapeKinds.FindOrAdd(Emitter.IsQuad() ? TEXT("quad") : (Emitter.Kind == ESWGParticleKind::Mesh ? TEXT("mesh") : TEXT("bare")));
					if (Emitter.Params.IsFlocking())
					{
						++Flocking;
						++FlockModes.FindOrAdd(FString::Printf(TEXT("%d"), Emitter.Params.FlockMode));
					}
					if (Emitter.IsQuad() && Emitter.Texture.ShaderPath.IsEmpty())
					{
						++BlendModes.FindOrAdd(TEXT("no texture"));
					}
					else if (Emitter.IsQuad())
					{
						bool bResolved = false;
						const ESWGEffectBlendMode Mode = ResolveShaderBlendMode(Check.Tre, Emitter.Texture.ShaderPath, BlendCache, bResolved);
						++BlendModes.FindOrAdd(bResolved ? BlendModeName(Mode) : TEXT("unresolved"));
					}
					Children += Emitter.Particle.bHasChild ? 1 : 0;
					Sounds += Emitter.Params.SoundPath.IsEmpty() ? 0 : 1;
				}
			}

			TArray<FString> Shaders, Meshes, ChildEffects, EmitterSounds;
			Effect.CollectReferences(Shaders, Meshes, ChildEffects, EmitterSounds);
			for (const FString& Shader : Shaders) { Check.CheckReference(Path, TEXT("shader"), Shader); }
			for (const FString& Mesh : Meshes) { Check.CheckReference(Path, TEXT("mesh"), Mesh); }
			for (const FString& Child : ChildEffects) { Check.CheckReference(Path, TEXT("child effect"), Child); }
			for (const FString& Sound : EmitterSounds) { Check.CheckReference(Path, TEXT("sound"), Sound); }
		}

		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] .prt: %d files, %d parsed, %d zero-byte stubs, %d emitters, %d with a child effect, %d with a sound, %d flocking"),
			Paths.Num(), Parsed, Stubs, Emitters, Children, Sounds, Flocking);
		LogHistogram(TEXT("PEFT versions"), EffectVersions);
		LogHistogram(TEXT("EMTR versions"), EmitterVersions);
		LogHistogram(TEXT("PTCL versions"), ParticleVersions);
		LogHistogram(TEXT("emitter shapes"), ShapeKinds);
		LogHistogram(TEXT("flock modes"), FlockModes);
		LogHistogram(TEXT("quad blend modes"), BlendModes);
		Check.Test.TestTrue(TEXT("every non-empty .prt parses"), Parsed + Stubs == Paths.Num());
	}

	void ParseSmallEffectFormats(FFxCheck& Check)
	{
		int32 Swooshes = 0, Sabers = 0, Sprites = 0, QuadSprites = 0, Beams = 0, Lightning = 0;

		const TArray<FString> SwooshPaths = FindFilesWithExtension(Check.Tre, TEXT(".swh"));
		for (const FString& Path : SwooshPaths)
		{
			FSWGSwoosh Swoosh;
			if (!FSWGSwooshReader::Read(Check.Tre.CreateIffReader(Path), Swoosh))
			{
				Check.Fail(Path, TEXT("swoosh does not parse"));
				continue;
			}
			++Swooshes;
			Check.CheckReference(Path, TEXT("shader"), Swoosh.Texture.ShaderPath);
			Check.CheckReference(Path, TEXT("trail effect"), Swoosh.TrailEffectPath);
		}

		for (const FString& Path : FindFilesWithExtension(Check.Tre, TEXT(".lsb")))
		{
			FSWGLightsaber Saber;
			if (!FSWGLightsaberReader::Read(Check.Tre.CreateIffReader(Path), Saber))
			{
				Check.Fail(Path, TEXT("lightsaber does not parse"));
				continue;
			}
			++Sabers;
			Check.CheckReference(Path, TEXT("hilt"), Saber.HiltAppearancePath);
			Check.CheckReference(Path, TEXT("hum"), Saber.HumSoundPath);
			for (const FSWGLightsaberBlade& Blade : Saber.Blades)
			{
				Check.CheckReference(Path, TEXT("blade shader"), Blade.ShaderPath);
			}
		}

		for (const FString& Path : FindFilesWithExtension(Check.Tre, TEXT(".spr")))
		{
			FSWGSprite Sprite;
			if (!FSWGSpriteReader::Read(Check.Tre.CreateIffReader(Path), Sprite))
			{
				Check.Fail(Path, TEXT("sprite does not parse"));
				continue;
			}
			++Sprites;
			QuadSprites += Sprite.bIsQuad ? 1 : 0;
			Check.CheckReference(Path, TEXT("shader"), Sprite.GetShaderPath());
		}

		for (const TCHAR* Path : { TEXT("appearance/beam_red.iff"), TEXT("appearance/beam_green.iff"), TEXT("appearance/beam_yellow.iff"), TEXT("appearance/beam_template.iff") })
		{
			FSWGBeam Beam;
			if (!FSWGBeamReader::Read(Check.Tre.CreateIffReader(Path), Beam))
			{
				Check.Fail(Path, TEXT("beam does not parse"));
				continue;
			}
			++Beams;
			Check.CheckReference(Path, TEXT("shader"), Beam.ShaderPath);
		}

		for (const FString& Path : FindFilesWithExtension(Check.Tre, TEXT(".ltn")))
		{
			FSWGLightningEffect Effect;
			if (!FSWGLightningEffectReader::Read(Check.Tre.CreateIffReader(Path), Effect))
			{
				Check.Fail(Path, TEXT("lightning effect does not parse"));
				continue;
			}
			++Lightning;
			Check.CheckReference(Path, TEXT("shader"), Effect.Texture.ShaderPath);
			Check.CheckReference(Path, TEXT("start effect"), Effect.StartEffectPath);
			Check.CheckReference(Path, TEXT("end effect"), Effect.EndEffectPath);
		}

		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] .swh %d/%d, .lsb %d, .spr %d (%d quad), beam %d, .ltn %d"),
			Swooshes, SwooshPaths.Num(), Sabers, Sprites, QuadSprites, Beams, Lightning);
		Check.Test.TestEqual(TEXT("every .swh parses"), Swooshes, SwooshPaths.Num());
	}

	void ParseShaders(FFxCheck& Check)
	{
		int32 Animated = 0, Switching = 0, Scrolling = 0, Plain = 0, Skipped = 0;
		TMap<FString, int32> Timings;
		for (const FString& Path : FindFilesWithExtension(Check.Tre, TEXT(".sht")))
		{
			const FSWGIffReader Reader = Check.Tre.CreateIffReader(Path);
			if (!Reader.IsValid())
			{
				++Skipped;
				continue;
			}

			const FName RootType = Reader.GetRootFormType();
			if (RootType == FName(TEXT("SWTS")) || RootType == FName(TEXT("SWSH")))
			{
				FSWGShaderAnimation Animation;
				if (!FSWGShaderReader::ReadAnimation(Reader, Animation))
				{
					Check.Fail(Path, TEXT("animated shader does not parse"));
					continue;
				}
				++(Animation.bSwitchesShaders ? Switching : Animated);
				++Timings.FindOrAdd(FString::Printf(TEXT("%s/%d"), Animation.bSwitchesShaders ? TEXT("SWSH") : TEXT("SWTS"), (int32)Animation.Timing));
				if (Animation.GetDeclaredFrames() != Animation.FrameCount)
				{
					Check.Fail(Path, FString::Printf(TEXT("declares %d frames but lists %d"), Animation.FrameCount, Animation.GetDeclaredFrames()));
				}
				Check.CheckReference(Path, TEXT("base shader"), Animation.BaseShaderPath);
				for (const FSWGShaderAnimationTexture& Frame : Animation.Textures)
				{
					Check.CheckReference(Path, TEXT("frame texture"), Frame.TexturePath);
				}
				for (const FString& Frame : Animation.Shaders)
				{
					Check.CheckReference(Path, TEXT("frame shader"), Frame);
				}
				continue;
			}

			FSWGIffChunk Scroll;
			if (Reader.FindForm(SWG_IFF_TAG('T', 'S', 'N', 'S'), Scroll))
			{
				FSWGShaderData Shader;
				if (!FSWGShaderReader::ReadShader(Reader, Shader) || Shader.Scrolls.IsEmpty())
				{
					Check.Fail(Path, TEXT("scrolling shader does not parse or lost its TSNS"));
					continue;
				}
				++Scrolling;
				Check.CheckReference(Path, TEXT("effect"), Shader.EffectName.Replace(TEXT("\\"), TEXT("/")));
				continue;
			}
			++Plain;
		}

		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] shaders: %d SWTS, %d SWSH, %d TSNS, %d other, %d empty"), Animated, Switching, Scrolling, Plain, Skipped);
		LogHistogram(TEXT("animation timing (0 none,1 DTST,2 DFST,3 DRFS,4 DRTS)"), Timings);

		// Ground-truth values from the plan.
		FSWGShaderAnimation Terminal;
		if (Check.Test.TestTrue(TEXT("anim_mission_terminal.sht reads"), FSWGShaderReader::ReadAnimation(Check.Tre.CreateIffReader(TEXT("shader/anim_mission_terminal.sht")), Terminal)))
		{
			Check.Test.TestEqual(TEXT("terminal frames"), Terminal.FrameCount, 8);
			Check.Test.TestEqual(TEXT("terminal seconds per frame"), Terminal.SecondsPerFrame, 0.1f, 0.001f);
			Check.Test.TestEqual(TEXT("terminal listed textures"), Terminal.Textures.Num(), 8);
			Check.Test.TestEqual(TEXT("terminal frame at 0.25 s"), Terminal.GetFrameAt(0.25f), 2);
		}
	}

	void ParseEffects(FFxCheck& Check)
	{
		TMap<FString, int32> BlendModes;
		int32 Parsed = 0;
		const TArray<FString> Paths = FindFilesWithExtension(Check.Tre, TEXT(".eft"));
		for (const FString& Path : Paths)
		{
			FSWGEffectData Effect;
			if (!FSWGShaderReader::ReadEffect(Check.Tre.CreateIffReader(Path), Effect))
			{
				Check.Fail(Path, TEXT("effect does not parse"));
				continue;
			}
			++Parsed;
			++BlendModes.FindOrAdd(BlendModeName(Effect.GetBlendMode()));
			for (const FSWGEffectImplementation& Implementation : Effect.Implementations)
			{
				for (const FSWGEffectPass& Pass : Implementation.Passes)
				{
					Check.CheckReference(Path, TEXT("vertex program"), Pass.VertexProgram);
					Check.CheckReference(Path, TEXT("pixel program"), Pass.PixelProgram);
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] .eft: %d/%d parsed"), Parsed, Paths.Num());
		LogHistogram(TEXT("effect blend modes"), BlendModes);
		Check.Test.TestEqual(TEXT("every .eft parses"), Parsed, Paths.Num());

		const TPair<const TCHAR*, ESWGEffectBlendMode> Expected[] =
		{
			{ TEXT("effect/e_particle_emisadd.eft"), ESWGEffectBlendMode::Additive },
			{ TEXT("effect/e_particle_alpha.eft"), ESWGEffectBlendMode::Translucent },
			{ TEXT("effect/e_particle_subtract.eft"), ESWGEffectBlendMode::Subtractive },
			{ TEXT("effect/e_particle_punchout.eft"), ESWGEffectBlendMode::Masked },
		};
		for (const TPair<const TCHAR*, ESWGEffectBlendMode>& Case : Expected)
		{
			FSWGEffectData Effect;
			if (Check.Test.TestTrue(Case.Key, FSWGShaderReader::ReadEffect(Check.Tre.CreateIffReader(Case.Key), Effect)))
			{
				Check.Test.TestEqual(*FString::Printf(TEXT("%s blend"), Case.Key), (int32)Effect.GetBlendMode(), (int32)Case.Value);
			}
		}
	}

	void ParseClientData(FFxCheck& Check)
	{
		int32 Files = 0, WithFlags = 0, Flags = 0, Hardpoints = 0, Lights = 0, Children = 0, ObjectChildren = 0;
		int32 ExpectedFlags = 0, ExpectedHardpoints = 0, ExpectedLights = 0, ExpectedChildren = 0;
		TMap<FString, int32> LightTypes;
		for (const FString& Path : FindFilesWithExtension(Check.Tre, TEXT(".cdf")))
		{
			const FSWGIffReader Reader = Check.Tre.CreateIffReader(Path);
			if (!Reader.IsValid())
			{
				continue;
			}
			FSWGClientDataFile Data;
			if (!FSWGClientDataFileReader::Read(Reader, Data))
			{
				Check.Fail(Path, TEXT("cdf does not parse"));
				continue;
			}
			++Files;

			// Count what the file holds so a silently dropped chunk shows up as a mismatch.
			FSWGIffChunk Cldf, Form0000;
			int32 FileFlags = 0, FileHardpoints = 0, FileLights = 0, FileChildren = 0;
			if (Reader.FindForm(SWG_IFF_TAG('C', 'L', 'D', 'F'), Cldf) && Reader.FindChildForm(Cldf, SWG_IFF_TAG('0', '0', '0', '0'), Form0000))
			{
				for (const FSWGIffChunk& Child : Reader.ReadChildren(Form0000))
				{
					if (Child.IsForm() && (Child.FormType == SWG_IFF_TAG('F', 'L', 'A', 'G') || Child.FormType == SWG_IFF_TAG('B', 'A', 'N', 'R'))) { ++FileFlags; }
					if (Child.Tag == SWG_IFF_TAG('H', 'O', 'B', 'J')) { ++FileHardpoints; }
					if (Child.Tag == SWG_IFF_TAG('L', 'O', 'B', 'J')) { ++FileLights; }
					if (Child.Tag == SWG_IFF_TAG('C', 'H', 'L', 'D') || Child.Tag == SWG_IFF_TAG('C', 'H', 'L', '2')) { ++FileChildren; }
				}
			}
			if (FileFlags != Data.Flags.Num() || FileHardpoints != Data.HardpointObjects.Num() || FileLights != Data.Lights.Num() || FileChildren != Data.Children.Num())
			{
				Check.Fail(Path, FString::Printf(TEXT("chunk counts differ: flags %d/%d hobj %d/%d lobj %d/%d child %d/%d"),
					Data.Flags.Num(), FileFlags, Data.HardpointObjects.Num(), FileHardpoints, Data.Lights.Num(), FileLights, Data.Children.Num(), FileChildren));
			}

			ExpectedFlags += FileFlags;
			ExpectedHardpoints += FileHardpoints;
			ExpectedLights += FileLights;
			ExpectedChildren += FileChildren;
			Flags += Data.Flags.Num();
			Hardpoints += Data.HardpointObjects.Num();
			Lights += Data.Lights.Num();
			Children += Data.Children.Num();
			WithFlags += Data.Flags.IsEmpty() ? 0 : 1;

			for (const FSWGClientDataFlag& Flag : Data.Flags)
			{
				Check.CheckReference(Path, TEXT("flag appearance"), Flag.AppearancePath);
			}
			for (const FSWGClientDataHardpointObject& Object : Data.HardpointObjects)
			{
				Check.CheckReference(Path, TEXT("hardpoint object"), Object.AppearancePath);
			}
			for (const FSWGClientDataLight& Light : Data.Lights)
			{
				++LightTypes.FindOrAdd(FString::Printf(TEXT("%d"), Light.Type));
				Check.CheckReference(Path, TEXT("light effect"), Light.EffectPath);
			}
			for (const FSWGClientDataChild& Child : Data.Children)
			{
				ObjectChildren += Child.bObjectTemplate ? 1 : 0;
				Check.CheckReference(Path, TEXT("child"), Child.Path);
			}
		}

		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] .cdf: %d parsed; FLAG/BANR %d (in %d files), HOBJ %d, LOBJ %d, CHLD+CHL2 %d (%d CHL2)"),
			Files, Flags, WithFlags, Hardpoints, Lights, Children, ObjectChildren);
		LogHistogram(TEXT("LOBJ types"), LightTypes);
		Check.Test.TestEqual(TEXT("no flag chunk dropped"), Flags, ExpectedFlags);
		Check.Test.TestEqual(TEXT("no hobj chunk dropped"), Hardpoints, ExpectedHardpoints);
		Check.Test.TestEqual(TEXT("no lobj chunk dropped"), Lights, ExpectedLights);
		Check.Test.TestEqual(TEXT("no child chunk dropped"), Children, ExpectedChildren);
	}

	void ParseTables(FFxCheck& Check)
	{
		FSWGDataTableData WeaponTable, SwooshTable, HueTable;
		if (!Check.Test.TestTrue(TEXT("weapon.iff parses"), FSWGDataTableReader::ReadDataTable(Check.Tre.CreateIffReader(TEXT("datatables/weapon/weapon.iff")), WeaponTable))
			|| !Check.Test.TestTrue(TEXT("swoosh.iff parses"), FSWGDataTableReader::ReadDataTable(Check.Tre.CreateIffReader(TEXT("datatables/weapon/swoosh.iff")), SwooshTable))
			|| !Check.Test.TestTrue(TEXT("swoosh_hue.iff parses"), FSWGDataTableReader::ReadDataTable(Check.Tre.CreateIffReader(TEXT("datatables/weapon/swoosh_hue.iff")), HueTable)))
		{
			return;
		}

		int32 ProjectileRows = 0, Resolved = 0;
		for (int32 RowIndex = 0; RowIndex < WeaponTable.Rows.Num(); ++RowIndex)
		{
			FString Projectile = WeaponTable.GetCell(RowIndex, TEXT("Projectile Appearance Template"));
			if (Projectile.IsEmpty())
			{
				continue;
			}
			++ProjectileRows;
			const FString EffectId = WeaponTable.GetCell(RowIndex, TEXT("Weapon Effect Id"));
			const int32 EffectIndex = FCString::Atoi(*WeaponTable.GetCell(RowIndex, TEXT("Weapon Effect Index")));
			// The helper returns the first row with this id/index; only an identical answer proves the lookup, so duplicate keys are skipped.
			Resolved += FSWGEffectTables::FindProjectileAppearance(WeaponTable, EffectId, EffectIndex).IsEmpty() ? 0 : 1;

			Projectile.ReplaceInline(TEXT("\\"), TEXT("/"));
			Check.CheckReference(TEXT("weapon.iff"), TEXT("projectile"), Projectile);
			FSWGParticleEffect Effect;
			if (Check.Exists(Projectile) && !FSWGParticleEffectReader::Read(Check.Tre.CreateIffReader(Projectile), Effect))
			{
				Check.Fail(Projectile, TEXT("projectile effect does not parse"));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] weapon.iff: %d rows, %d with a projectile, %d resolved by the helper"), WeaponTable.Rows.Num(), ProjectileRows, Resolved);
		Check.Test.TestTrue(TEXT("weapon.iff names projectile effects"), ProjectileRows > 20);

		int32 ResolvedSwooshes = 0;
		for (int32 RowIndex = 0; RowIndex < SwooshTable.Rows.Num(); ++RowIndex)
		{
			const FString TemplatePath = SwooshTable.GetCell(RowIndex, TEXT("ObjectTemplateName"));
			FString SwooshPath;
			bool bUseHue = false;
			if (!FSWGEffectTables::FindSwoosh(SwooshTable, TemplatePath, SwooshPath, bUseHue))
			{
				Check.Fail(TemplatePath, TEXT("swoosh row does not resolve"));
				continue;
			}
			++ResolvedSwooshes;
			Check.CheckReference(TEXT("swoosh.iff"), TEXT("swoosh"), SwooshPath);
			Check.CheckReference(TEXT("swoosh.iff"), TEXT("weapon template"), TemplatePath);
		}
		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] swoosh.iff: %d/%d rows resolve; swoosh_hue.iff %d bands"), ResolvedSwooshes, SwooshTable.Rows.Num(), HueTable.Rows.Num());

		FColor Red;
		Check.Test.TestTrue(TEXT("hue 5 maps to a colour"), FSWGEffectTables::FindSwooshHueColor(HueTable, 5.f, Red));
	}

	void CheckKnownEffects(FFxCheck& Check)
	{
		FSWGParticleEffect Bolt;
		FString Error;
		if (Check.Test.TestTrue(TEXT("pt_bolt_red.prt parses"), FSWGParticleEffectReader::Read(Check.Tre.CreateIffReader(TEXT("appearance/pt_bolt_red.prt")), Bolt, &Error)))
		{
			Check.Test.TestEqual(TEXT("bolt groups"), Bolt.Groups.Num(), 1);
			if (!Bolt.Groups.IsEmpty() && !Bolt.Groups[0].Emitters.IsEmpty())
			{
				const FSWGParticleEmitter& Core = Bolt.Groups[0].Emitters[0];
				Check.Test.TestTrue(TEXT("bolt core is a quad"), Core.IsQuad());
				Check.Test.TestEqual(TEXT("bolt core texture"), Core.Texture.ShaderPath, FString(TEXT("shader/pt_bolt_red.sht")));
				Check.Test.TestEqual(TEXT("bolt core lifetime"), Core.GetCurve(ESWGEmitterCurve::Lifetime).EvaluateValue(0.f), 20.f, 0.01f);
			}
		}

		FSWGParticleEffect Campfire;
		if (Check.Test.TestTrue(TEXT("pt_campfire_s01.prt parses"), FSWGParticleEffectReader::Read(Check.Tre.CreateIffReader(TEXT("appearance/pt_campfire_s01.prt")), Campfire, &Error)))
		{
			bool bFlipbook = false;
			for (const FSWGEmitterGroup& Group : Campfire.Groups)
			{
				for (const FSWGParticleEmitter& Emitter : Group.Emitters)
				{
					bFlipbook |= Emitter.IsQuad() && Emitter.Texture.FrameCount == 16 && Emitter.Texture.GridColumns == 4;
				}
			}
			Check.Test.TestTrue(TEXT("campfire has a 4x4 flipbook"), bFlipbook);
		}

		FSWGParticleEffect Insects;
		if (Check.Test.TestTrue(TEXT("pt_buzzing_insects_small.prt parses"), FSWGParticleEffectReader::Read(Check.Tre.CreateIffReader(TEXT("appearance/pt_buzzing_insects_small.prt")), Insects, &Error)))
		{
			Check.Test.TestTrue(TEXT("insects have emitters"), Insects.GetEmitterCount() > 0);
		}

		FSWGShaderData Waterfall;
		if (Check.Test.TestTrue(TEXT("waterfall_scroll.sht reads"), FSWGShaderReader::ReadShader(Check.Tre.CreateIffReader(TEXT("shader/waterfall_scroll.sht")), Waterfall)) && Check.Test.TestEqual(TEXT("one TSNS record"), Waterfall.Scrolls.Num(), 1))
		{
			Check.Test.TestEqual(TEXT("scroll tag"), Waterfall.Scrolls[0].Tag, FString(TEXT("MAIN")));
			Check.Test.TestEqual(TEXT("scroll V1"), Waterfall.Scrolls[0].V1, -0.5f, 0.001f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGFxFormatsTest,
	"SWGEmu.Fx.Formats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGFxFormatsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FFxCheck Check(*this, *Tre);
	ParseParticleEffects(Check);
	ParseSmallEffectFormats(Check);
	ParseShaders(Check);
	ParseEffects(Check);
	ParseClientData(Check);
	ParseTables(Check);
	CheckKnownEffects(Check);

	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.Formats] %d dangling references (%d distinct targets); retail leaves these dangling"), Check.DanglingCount, Check.DanglingSeen.Num());
	if (Check.DanglingCount > 0)
	{
		AddWarning(FString::Printf(TEXT("%d dangling references in retail data (%d distinct targets), see the log"), Check.DanglingCount, Check.DanglingSeen.Num()));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
