#include "Track/MXTrackTypes.h"
#include "HeatlineMX.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

void FMXTrackDefinition::SortSegments()
{
	Segments.StableSort([](const FMXSegment& A, const FMXSegment& B) { return A.StartM < B.StartM; });
}

void FMXTrackDefinition::EnsureIds(const FString& Prefix)
{
	TSet<FString> Seen;
	int32 Next = 1;
	for (FMXSegment& S : Segments)
	{
		if (S.Id.IsEmpty() || Seen.Contains(S.Id))
		{
			do
			{
				S.Id = FString::Printf(TEXT("%s-%d"), *Prefix, Next++);
			} while (Seen.Contains(S.Id));
		}
		Seen.Add(S.Id);
	}
}

float FMXTrackDefinition::LapLengthFor(EMXLayoutVariant Variant) const
{
	if (Variant == EMXLayoutVariant::Main)
	{
		return LapLengthM;
	}
	float Removed = 0.f;
	for (const FMXSegment& S : Segments)
	{
		if (S.bMainOnly)
		{
			Removed += S.LengthM;
		}
	}
	return FMath::Max(50.f, LapLengthM - Removed);
}

namespace MXTrackIO
{
	static int32 LanesToMask(const TArray<TSharedPtr<FJsonValue>>& Lanes)
	{
		int32 Mask = 0;
		for (const TSharedPtr<FJsonValue>& V : Lanes)
		{
			const int32 L = (int32)V->AsNumber();
			if (L >= 1 && L <= MX::NumLanes)
			{
				Mask |= 1 << (L - 1);
			}
		}
		return Mask ? Mask : MX::AllLanes;
	}

	bool FromJsonString(const FString& Json, FMXTrackDefinition& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Not valid JSON");
			return false;
		}
		const FString Schema = Root->GetStringField(TEXT("schema"));
		if (!Schema.StartsWith(TEXT("heatline.")))
		{
			OutError = FString::Printf(TEXT("Unknown schema '%s'"), *Schema);
			return false;
		}
		Out = FMXTrackDefinition();
		Out.Id = Root->GetStringField(TEXT("id"));
		Out.Name = Root->GetStringField(TEXT("name"));
		Root->TryGetStringField(TEXT("author"), Out.Author);
		Out.Laps = FMath::Clamp((int32)Root->GetNumberField(TEXT("laps")), 1, 9);
		Out.LapLengthM = (float)Root->GetNumberField(TEXT("lapLengthM"));
		Root->TryGetBoolField(TEXT("builtIn"), Out.bBuiltIn);
		double Medal = 0.0;
		if (Root->TryGetNumberField(TEXT("medalGold"), Medal)) { Out.MedalGold = (float)Medal; }
		if (Root->TryGetNumberField(TEXT("medalSilver"), Medal)) { Out.MedalSilver = (float)Medal; }
		if (Root->TryGetNumberField(TEXT("medalBronze"), Medal)) { Out.MedalBronze = (float)Medal; }

		const TSharedPtr<FJsonObject>* Source = nullptr;
		if (Root->TryGetObjectField(TEXT("source"), Source) && Source && Source->IsValid())
		{
			double Track = 0.0;
			if ((*Source)->TryGetNumberField(TEXT("track"), Track))
			{
				Out.NesTrack = (int32)Track;
				Out.bBuiltIn = true;
			}
			FString Method;
			(*Source)->TryGetStringField(TEXT("method"), Method);
			FString Game;
			(*Source)->TryGetStringField(TEXT("game"), Game);
			Out.SourceNote = FString::Printf(TEXT("%s track %d (%s)"), *Game, Out.NesTrack, *Method);
		}

		const TArray<TSharedPtr<FJsonValue>>* Segs = nullptr;
		if (!Root->TryGetArrayField(TEXT("segments"), Segs) || !Segs)
		{
			OutError = TEXT("Missing segments");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& SV : *Segs)
		{
			const TSharedPtr<FJsonObject> SO = SV->AsObject();
			if (!SO.IsValid())
			{
				continue;
			}
			FMXSegment Seg;
			Seg.Id = SO->GetStringField(TEXT("id"));
			Seg.Type = MX::ObstacleFromString(SO->GetStringField(TEXT("type")));
			if (Seg.Type == EMXObstacleType::None)
			{
				OutError = FString::Printf(TEXT("Segment %s has unknown type"), *Seg.Id);
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* Lanes = nullptr;
			double MaskNum = 0.0;
			if (SO->TryGetArrayField(TEXT("lanes"), Lanes) && Lanes)
			{
				Seg.LaneMask = LanesToMask(*Lanes);
			}
			else if (SO->TryGetNumberField(TEXT("laneMask"), MaskNum))
			{
				Seg.LaneMask = FMath::Clamp((int32)MaskNum, 1, MX::AllLanes);
			}
			Seg.StartM = (float)SO->GetNumberField(TEXT("startM"));
			Seg.LengthM = (float)SO->GetNumberField(TEXT("lengthM"));
			FString Variant;
			if (SO->TryGetStringField(TEXT("variant"), Variant))
			{
				Seg.bMainOnly = Variant.Equals(TEXT("main"), ESearchCase::IgnoreCase);
			}
			SO->TryGetBoolField(TEXT("mainOnly"), Seg.bMainOnly);
			const TArray<TSharedPtr<FJsonValue>>* Runs = nullptr;
			if (SO->TryGetArrayField(TEXT("runs"), Runs) && Runs)
			{
				for (const TSharedPtr<FJsonValue>& RV : *Runs)
				{
					Seg.Runs.Add((int32)RV->AsNumber());
				}
			}
			const TSharedPtr<FJsonObject>* Nes = nullptr;
			if (SO->TryGetObjectField(TEXT("nes"), Nes) && Nes && Nes->IsValid())
			{
				Seg.SourceRef = FString::Printf(TEXT("NES T%d col %d %s (%s)"), Out.NesTrack,
					(int32)(*Nes)->GetNumberField(TEXT("col")), *(*Nes)->GetStringField(TEXT("piece")),
					*(*Nes)->GetStringField(TEXT("letter")));
			}
			else
			{
				SO->TryGetStringField(TEXT("source"), Seg.SourceRef);
			}
			Out.Segments.Add(MoveTemp(Seg));
		}
		Out.SortSegments();
		if (Out.Id.IsEmpty())
		{
			Out.Id = Out.Name.IsEmpty() ? TEXT("untitled") : Out.Name;
		}
		return true;
	}

	FString ToJsonString(const FMXTrackDefinition& Def)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("heatline.track/1"));
		Root->SetStringField(TEXT("id"), Def.Id);
		Root->SetStringField(TEXT("name"), Def.Name);
		Root->SetStringField(TEXT("author"), Def.Author);
		Root->SetNumberField(TEXT("laps"), Def.Laps);
		Root->SetNumberField(TEXT("lapLengthM"), Def.LapLengthM);
		Root->SetBoolField(TEXT("builtIn"), Def.bBuiltIn);
		if (Def.MedalGold > 0.f)
		{
			Root->SetNumberField(TEXT("medalGold"), Def.MedalGold);
			Root->SetNumberField(TEXT("medalSilver"), Def.MedalSilver);
			Root->SetNumberField(TEXT("medalBronze"), Def.MedalBronze);
		}
		TArray<TSharedPtr<FJsonValue>> Segs;
		for (const FMXSegment& S : Def.Segments)
		{
			TSharedRef<FJsonObject> SO = MakeShared<FJsonObject>();
			SO->SetStringField(TEXT("id"), S.Id);
			SO->SetStringField(TEXT("type"), MX::ObstacleToString(S.Type));
			TArray<TSharedPtr<FJsonValue>> Lanes;
			for (int32 L = 0; L < MX::NumLanes; ++L)
			{
				if (MX::LaneInMask(S.LaneMask, L))
				{
					Lanes.Add(MakeShared<FJsonValueNumber>(L + 1));
				}
			}
			SO->SetArrayField(TEXT("lanes"), Lanes);
			SO->SetNumberField(TEXT("startM"), S.StartM);
			SO->SetNumberField(TEXT("lengthM"), S.LengthM);
			if (S.bMainOnly)
			{
				SO->SetStringField(TEXT("variant"), TEXT("main"));
			}
			if (S.Runs.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Runs;
				for (int32 R : S.Runs)
				{
					Runs.Add(MakeShared<FJsonValueNumber>(R));
				}
				SO->SetArrayField(TEXT("runs"), Runs);
			}
			if (!S.SourceRef.IsEmpty())
			{
				SO->SetStringField(TEXT("source"), S.SourceRef);
			}
			Segs.Add(MakeShared<FJsonValueObject>(SO));
		}
		Root->SetArrayField(TEXT("segments"), Segs);

		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	bool LoadFile(const FString& Path, FMXTrackDefinition& Out, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *Path);
			return false;
		}
		return FromJsonString(Text, Out, OutError);
	}

	bool SaveFile(const FString& Path, const FMXTrackDefinition& Def, FString& OutError)
	{
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		PF.CreateDirectoryTree(*FPaths::GetPath(Path));
		if (!FFileHelper::SaveStringToFile(ToJsonString(Def), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write %s"), *Path);
			return false;
		}
		return true;
	}
}
