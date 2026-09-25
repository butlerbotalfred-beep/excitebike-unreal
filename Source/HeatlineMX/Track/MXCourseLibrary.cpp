#include "Track/MXCourseLibrary.h"
#include "Track/MXObstacleLibrary.h"
#include "Track/MXTrackValidator.h"
#include "Core/MXTuning.h"
#include "HeatlineMX.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

void UMXCourseLibrary::Initialize()
{
	BuiltIn.Reset();
	for (int32 T = 1; T <= 5; ++T)
	{
		FMXTrackDefinition Def;
		bool bLoaded = false;
		// Preferred: Data Asset (course assembly in the editor).
		const FString AssetPath = FString::Printf(TEXT("/Game/HeatlineMX/Courses/DA_Course_NES_T%d.DA_Course_NES_T%d"), T, T);
		if (UMXCourseAsset* Asset = Cast<UMXCourseAsset>(FSoftObjectPath(AssetPath).TryLoad()))
		{
			Def = Asset->Definition;
			bLoaded = Def.Segments.Num() > 0;
		}
		if (!bLoaded)
		{
			const FString Path = FPaths::ProjectContentDir() / FString::Printf(TEXT("Courses/nes_t%d.json"), T);
			FString Err;
			bLoaded = MXTrackIO::LoadFile(Path, Def, Err);
			if (!bLoaded)
			{
				UE_LOG(LogHeatline, Warning, TEXT("Course %d missing: %s"), T, *Err);
			}
		}
		if (bLoaded)
		{
			Def.bBuiltIn = true;
			Def.NesTrack = T;
			if (Def.Id.IsEmpty())
			{
				Def.Id = FString::Printf(TEXT("nes-t%d"), T);
			}
			Def.Name = FString::Printf(TEXT("Course %d"), T);
			BuiltIn.Add(Def);
		}
	}
	BuiltIn.Add(MakeTestStrip());
	RefreshUserTracks();
}

void UMXCourseLibrary::RebuildEntries()
{
	Entries.Reset();
	for (const FMXTrackDefinition& D : BuiltIn)
	{
		FMXCourseEntry E;
		E.Id = D.Id;
		E.DisplayName = D.Name;
		E.bBuiltIn = true;
		E.bTestStrip = D.Id == TEXT("test-strip");
		E.NesTrack = D.NesTrack;
		Entries.Add(E);
	}
	for (const FString& N : UserNames)
	{
		FMXCourseEntry E;
		E.Id = FString::Printf(TEXT("user:%s"), *N);
		E.DisplayName = N;
		E.bUser = true;
		Entries.Add(E);
	}
}

TArray<FString> UMXCourseLibrary::ChampionshipOrder() const
{
	TArray<FString> Out;
	for (const FMXTrackDefinition& D : BuiltIn)
	{
		if (D.NesTrack > 0)
		{
			Out.Add(D.Id);
		}
	}
	return Out;
}

const FMXTrackDefinition* UMXCourseLibrary::FindBuiltIn(const FString& Id) const
{
	return BuiltIn.FindByPredicate([&Id](const FMXTrackDefinition& D) { return D.Id == Id; });
}

bool UMXCourseLibrary::GetCourse(const FString& Id, FMXTrackDefinition& Out) const
{
	if (const FMXTrackDefinition* D = FindBuiltIn(Id))
	{
		Out = *D;
		return true;
	}
	if (Id.StartsWith(TEXT("user:")))
	{
		FString Err;
		return LoadUserTrack(Id.RightChop(5), Out, Err);
	}
	return false;
}

FString UMXCourseLibrary::UserTracksDir()
{
	return FPaths::ProjectSavedDir() / TEXT("Tracks");
}

FString UMXCourseLibrary::SanitizeName(const FString& Name)
{
	FString Out;
	for (TCHAR C : Name.TrimStartAndEnd())
	{
		if (FChar::IsAlnum(C) || C == TEXT(' ') || C == TEXT('-') || C == TEXT('_'))
		{
			Out.AppendChar(C);
		}
	}
	Out = Out.Left(24).TrimStartAndEnd();
	return Out.IsEmpty() ? TEXT("Track") : Out;
}

void UMXCourseLibrary::RefreshUserTracks()
{
	UserNames.Reset();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(UserTracksDir() / TEXT("*.json")), true, false);
	Files.Sort();
	for (const FString& F : Files)
	{
		UserNames.Add(FPaths::GetBaseFilename(F));
	}
	RebuildEntries();
}

TArray<FString> UMXCourseLibrary::GetUserTrackNames() const
{
	return UserNames;
}

bool UMXCourseLibrary::SaveUserTrack(FMXTrackDefinition Def, FString& OutError)
{
	Def.Name = SanitizeName(Def.Name);
	Def.Id = FString::Printf(TEXT("user:%s"), *Def.Name);
	Def.bBuiltIn = false;
	Def.NesTrack = 0;
	Def.EnsureIds(TEXT("U"));
	Def.SortSegments();
	const bool bOk = MXTrackIO::SaveFile(UserTracksDir() / (Def.Name + TEXT(".json")), Def, OutError);
	RefreshUserTracks();
	return bOk;
}

bool UMXCourseLibrary::LoadUserTrack(const FString& Name, FMXTrackDefinition& Out, FString& OutError) const
{
	const FString Path = UserTracksDir() / (SanitizeName(Name) + TEXT(".json"));
	if (!MXTrackIO::LoadFile(Path, Out, OutError))
	{
		return false;
	}
	Out.bBuiltIn = false;
	Out.Name = SanitizeName(Name);
	Out.Id = FString::Printf(TEXT("user:%s"), *Out.Name);
	return true;
}

bool UMXCourseLibrary::DeleteUserTrack(const FString& Name)
{
	const bool bOk = IFileManager::Get().Delete(*(UserTracksDir() / (SanitizeName(Name) + TEXT(".json"))));
	RefreshUserTracks();
	return bOk;
}

FMXTrackDefinition UMXCourseLibrary::MakeTestStrip()
{
	const UMXTrackStyle& Style = MXTuning::Style();
	FMXTrackDefinition D;
	D.Id = TEXT("test-strip");
	D.Name = TEXT("Test Strip");
	D.Author = TEXT("Heatline MX");
	D.bBuiltIn = true;
	D.Laps = 1;
	D.SourceNote = TEXT("Handling benchmark: every piece in isolation, then two rhythm sections.");
	float S = 30.f;
	int32 N = 1;
	auto Add = [&](EMXObstacleType Type, int32 Mask = -1, float Gap = 32.f, TArray<int32> Runs = {})
	{
		FMXSegment Seg = FMXObstacleLibrary::MakeDefault(Type, S, Style);
		if (Mask > 0)
		{
			Seg.LaneMask = Mask;
		}
		if (Runs.Num() > 0)
		{
			Seg.Runs = Runs;
			Seg.LengthM = FMXObstacleLibrary::ComputeLength(Type, Runs, 0.f, Style);
		}
		Seg.Id = FString::Printf(TEXT("TS.O%02d"), N++);
		D.Segments.Add(Seg);
		S += Seg.LengthM + Gap;
	};
	Add(EMXObstacleType::CoolStrip, MX::MaskFromLanes({1}), 10.f);
	Add(EMXObstacleType::CoolStrip, MX::MaskFromLanes({4}), 20.f);
	Add(EMXObstacleType::RampSmall);
	Add(EMXObstacleType::RampMedium);
	Add(EMXObstacleType::RampLarge, -1, 40.f);
	Add(EMXObstacleType::TableLow);
	Add(EMXObstacleType::RampSteep, -1, 40.f);
	Add(EMXObstacleType::RampSteepBack, -1, 36.f);
	Add(EMXObstacleType::RampSteepFace, -1, 36.f);
	Add(EMXObstacleType::Kicker);
	Add(EMXObstacleType::Barrier, MX::MaskFromLanes({1, 2}), 12.f);
	Add(EMXObstacleType::Barrier, MX::MaskFromLanes({3, 4}), 24.f);
	Add(EMXObstacleType::Mud, MX::MaskFromLanes({1, 3}), 6.f);
	Add(EMXObstacleType::Mud, MX::MaskFromLanes({2, 4}), 24.f);
	Add(EMXObstacleType::RampSteepBack, -1, 2.5f);
	Add(EMXObstacleType::Grass, MX::AllLanes, 30.f, {10});
	Add(EMXObstacleType::Mountain, -1, 40.f);
	Add(EMXObstacleType::PlatformDeck, -1, 40.f);
	// Rhythm sections (NES track 1 style).
	for (int32 i = 0; i < 3; ++i) { Add(EMXObstacleType::RampMedium, -1, 2.5f); }
	S += 30.f;
	for (int32 i = 0; i < 4; ++i) { Add(EMXObstacleType::RampSmall, -1, 2.5f); }
	S += 30.f;
	for (int32 i = 0; i < 4; ++i) { Add(EMXObstacleType::TableLow, -1, 2.5f); }
	S += 30.f;
	FMXSegment Fin = FMXObstacleLibrary::MakeDefault(EMXObstacleType::FinishDeck, S, Style);
	Fin.Id = TEXT("TS.FIN");
	D.Segments.Add(Fin);
	D.LapLengthM = S + Fin.LengthM + 4.f;
	return D;
}

void UMXCourseLibrary::GetMedalTimes(const FMXTrackDefinition& Def, EMXLayoutVariant Variant, int32 Laps, float& OutGold, float& OutSilver, float& OutBronze)
{
	Laps = FMath::Max(1, Laps);
	if (Def.MedalGold > 0.f && Variant == EMXLayoutVariant::Challenge && Laps == Def.Laps)
	{
		OutGold = Def.MedalGold;
		OutSilver = Def.MedalSilver > 0.f ? Def.MedalSilver : Def.MedalGold * 1.08f;
		OutBronze = Def.MedalBronze > 0.f ? Def.MedalBronze : Def.MedalGold * 1.18f;
		return;
	}
	// Measured on this exact layout: the reference rider's time, cached by content, layout and laps.
	uint32 Key = GetTypeHash(Def.Id);
	Key = HashCombine(Key, GetTypeHash((int32)Variant * 16 + Laps));
	Key = HashCombine(Key, GetTypeHash(Def.LapLengthM));
	for (const FMXSegment& S : Def.Segments)
	{
		Key = HashCombine(Key, GetTypeHash((int32)S.Type * 256 + S.LaneMask));
		Key = HashCombine(Key, GetTypeHash(S.StartM));
		Key = HashCombine(Key, GetTypeHash(S.LengthM));
		Key = HashCombine(Key, GetTypeHash(S.bMainOnly ? 1 : 0));
		for (const int32 R : S.Runs)
		{
			Key = HashCombine(Key, GetTypeHash(R));
		}
	}
	static TMap<uint32, float> Cache;
	float Ref;
	if (const float* Found = Cache.Find(Key))
	{
		Ref = *Found;
	}
	else
	{
		Ref = FMXTrackValidator::ReferenceTime(Def, Variant, Laps);
		Cache.Add(Key, Ref);
	}
	const UMXAITuning& AI = MXTuning::AI();
	if (Ref > 0.f)
	{
		OutGold = Ref * AI.MedalGoldFactor;
		OutSilver = Ref * AI.MedalSilverFactor;
		OutBronze = Ref * AI.MedalBronzeFactor;
		return;
	}
	// The reference rider couldn't finish (a broken user track): fall back to distance at a strong pace.
	const UMXBikeTuning& T = MXTuning::Bike();
	OutGold = Def.LapLengthFor(Variant) * Laps / (T.MaxSpeedTurbo * 0.8f) + 3.f;
	OutSilver = OutGold * 1.08f;
	OutBronze = OutGold * 1.2f;
}
