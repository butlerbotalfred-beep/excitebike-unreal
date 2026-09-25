#include "Track/MXTrackValidator.h"
#include "Track/MXObstacleLibrary.h"
#include "Track/MXTrackModel.h"
#include "Bike/MXBikeSim.h"
#include "AI/MXAIBrain.h"
#include "Core/MXTuning.h"

bool FMXValidationResult::HasErrors() const
{
	return Count(EMXIssueSeverity::Error) > 0;
}

int32 FMXValidationResult::Count(EMXIssueSeverity Sev) const
{
	int32 N = 0;
	for (const FMXTrackIssue& I : Issues)
	{
		N += I.Severity == Sev ? 1 : 0;
	}
	return N;
}

FString FMXValidationResult::Summary() const
{
	if (Issues.Num() == 0)
	{
		return TEXT("Track OK");
	}
	return FString::Printf(TEXT("%d error(s), %d warning(s)"), Count(EMXIssueSeverity::Error), Count(EMXIssueSeverity::Warning));
}

namespace
{
	bool IsSurfacePiece(EMXObstacleType T)
	{
		return T == EMXObstacleType::Mud || T == EMXObstacleType::CoolStrip || T == EMXObstacleType::Grass || T == EMXObstacleType::Barrier;
	}

	void AddIssue(FMXValidationResult& R, EMXIssueSeverity Sev, const FString& Msg, const FString& Id = FString(), float S = -1.f)
	{
		FMXTrackIssue I;
		I.Severity = Sev;
		I.Message = Msg;
		I.SegmentId = Id;
		I.S = S;
		R.Issues.Add(I);
	}
}

FMXValidationResult FMXTrackValidator::Validate(const FMXTrackDefinition& InDef, const UMXTrackStyle& Style)
{
	FMXValidationResult R;
	FMXTrackDefinition Def = InDef;
	Def.SortSegments();

	if (Def.LapLengthM < MinLapLength || Def.LapLengthM > MaxLapLength)
	{
		AddIssue(R, EMXIssueSeverity::Error, FString::Printf(TEXT("Lap length %.0f m is outside %.0f-%.0f m"), Def.LapLengthM, MinLapLength, MaxLapLength));
	}
	if (Def.Laps < 1 || Def.Laps > 9)
	{
		AddIssue(R, EMXIssueSeverity::Error, TEXT("Laps must be 1-9"));
	}

	const FMXSegment* Finish = nullptr;
	int32 NumFinish = 0;
	for (const FMXSegment& S : Def.Segments)
	{
		if (S.Type == EMXObstacleType::FinishDeck)
		{
			Finish = &S;
			++NumFinish;
		}
	}
	if (!Finish)
	{
		AddIssue(R, EMXIssueSeverity::Error, TEXT("No finish deck: the lap has no finish line (use Auto-fix)"));
	}
	else if (NumFinish > 1)
	{
		AddIssue(R, EMXIssueSeverity::Error, TEXT("More than one finish deck"));
	}

	for (int32 i = 0; i < Def.Segments.Num(); ++i)
	{
		const FMXSegment& A = Def.Segments[i];
		const float Len = A.LengthM > 0.f ? A.LengthM : FMXObstacleLibrary::ComputeLength(A.Type, A.Runs, 0.f, Style);
		if (A.Type == EMXObstacleType::None)
		{
			AddIssue(R, EMXIssueSeverity::Error, TEXT("Unknown piece"), A.Id, A.StartM);
			continue;
		}
		if ((A.LaneMask & MX::AllLanes) == 0)
		{
			AddIssue(R, EMXIssueSeverity::Error, TEXT("Piece covers no lanes"), A.Id, A.StartM);
		}
		if (A.StartM < StartZone)
		{
			AddIssue(R, EMXIssueSeverity::Error, FString::Printf(TEXT("%s is inside the start zone (first %.0f m must stay flat)"), *MX::ObstacleName(A.Type), StartZone), A.Id, A.StartM);
		}
		if (A.StartM + Len > Def.LapLengthM + 0.01f)
		{
			AddIssue(R, EMXIssueSeverity::Error, FString::Printf(TEXT("%s runs past the end of the lap"), *MX::ObstacleName(A.Type)), A.Id, A.StartM);
		}
		if (Finish && &A != Finish && A.StartM > Finish->StartM)
		{
			AddIssue(R, EMXIssueSeverity::Error, FString::Printf(TEXT("%s is after the finish deck (the deck must close the lap)"), *MX::ObstacleName(A.Type)), A.Id, A.StartM);
		}
		for (int32 j = i + 1; j < Def.Segments.Num(); ++j)
		{
			const FMXSegment& B = Def.Segments[j];
			if (B.StartM >= A.StartM + Len - 0.01f)
			{
				break;
			}
			const bool bSurfA = IsSurfacePiece(A.Type);
			const bool bSurfB = IsSurfacePiece(B.Type);
			// Surface pieces may overlap each other only in different lanes; nothing may overlap a ramp.
			if (!bSurfA || !bSurfB || (A.LaneMask & B.LaneMask) != 0)
			{
				AddIssue(R, EMXIssueSeverity::Error, FString::Printf(TEXT("%s overlaps %s"), *MX::ObstacleName(B.Type), *MX::ObstacleName(A.Type)), B.Id, B.StartM);
			}
		}
	}

	// Blocked landings: a sheer drop followed immediately by a tall face leaves nowhere to land.
	for (int32 i = 0; i + 1 < Def.Segments.Num(); ++i)
	{
		const FMXSegment& A = Def.Segments[i];
		const FMXSegment& B = Def.Segments[i + 1];
		const bool bDrop = A.Type == EMXObstacleType::Kicker || A.Type == EMXObstacleType::PlatformDeck;
		if (!bDrop || !FMXObstacleLibrary::IsRamp(B.Type))
		{
			continue;
		}
		const float Len = A.LengthM > 0.f ? A.LengthM : FMXObstacleLibrary::ComputeLength(A.Type, A.Runs, 0.f, Style);
		const float Gap = B.StartM - (A.StartM + Len);
		const FMXPieceGeometry GB = FMXObstacleLibrary::Build(B, Style);
		if (Gap < 2.f && GB.MaxHeight > 2.f)
		{
			AddIssue(R, EMXIssueSeverity::Warning, FString::Printf(TEXT("Landing blocked: %s right after a drop"), *MX::ObstacleName(B.Type)), B.Id, B.StartM);
		}
	}

	// Heat: a long lap with no cool strip forces constant coasting to use turbo at all.
	bool bCool = false;
	for (const FMXSegment& S : Def.Segments)
	{
		bCool |= S.Type == EMXObstacleType::CoolStrip;
	}
	if (!bCool && Def.LapLengthM > 450.f)
	{
		AddIssue(R, EMXIssueSeverity::Info, TEXT("No cool strips: turbo will be hard to manage on a lap this long"));
	}

	// Checkpoints are generated from the finish line; make sure a race can be timed.
	if (Finish)
	{
		FMXTrackModel Model;
		Model.Build(Def, EMXLayoutVariant::Main, Def.Laps, Style);
		if (Model.GetGates().Num() < 2 * Def.Laps)
		{
			AddIssue(R, EMXIssueSeverity::Error, TEXT("Checkpoint setup failed"));
		}
	}
	return R;
}

float FMXTrackValidator::ReferenceTime(const FMXTrackDefinition& Def, EMXLayoutVariant Variant, int32 Laps)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	FMXTrackModel Model;
	Model.Build(Def, Variant, FMath::Max(1, Laps), MXTuning::Style());
	const FMXAISkill& Skill = MXTuning::AI().ForDifficulty(EMXAIDifficulty::Hard);
	const TArray<FMXAIOtherBike> NoOthers;
	FMXBikeState St;
	// Same start as a race: front row, 2.5 m behind the line; the clock starts at GO.
	FMXBikeSim::Spawn(St, -2.5f, Model.LaneCenterY(1), Model);
	FMXAIMemory Mem;
	Mem.Init(4242, false);
	Mem.bAutopilot = true;
	Mem.PlanLane = 1;
	const float Goal = Model.RaceFinishS();
	float Time = 0.f;
	while (St.S < Goal)
	{
		if (Time > 120.f * Model.GetLaps())
		{
			return -1.f;
		}
		const FMXBikeInput In = FMXAIBrain::Think(St, Mem, Model, T, Skill, NoOthers, FMXBikeSim::FixedDt);
		FMXBikeSim::Step(St, In, Model, T, FMXBikeSim::FixedDt, false);
		Time += FMXBikeSim::FixedDt;
	}
	return Time;
}

void FMXTrackValidator::DriveTest(const FMXTrackDefinition& Def, FMXValidationResult& InOut)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	const UMXTrackStyle& Style = MXTuning::Style();
	FMXTrackModel Model;
	Model.Build(Def, EMXLayoutVariant::Main, 1, Style);
	const FMXAISkill& Skill = MXTuning::AI().ForDifficulty(EMXAIDifficulty::Hard);
	const TArray<FMXAIOtherBike> NoOthers;

	// Try three strategies in every lane; a crash location shared by all attempts is a real problem.
	TMap<FString, int32> FailuresByPiece;
	int32 Attempts = 0;
	for (int32 Strategy = 0; Strategy < 3; ++Strategy)
	{
		for (int32 Lane = 0; Lane < MX::NumLanes; ++Lane)
		{
			++Attempts;
			FMXBikeState St;
			FMXBikeSim::Spawn(St, -2.f, Model.LaneCenterY(Lane), Model);
			FMXAIMemory Mem;
			Mem.Init(1000 + Strategy * 10 + Lane, false);
			Mem.Strategy = Strategy;
			Mem.bAutopilot = true;
			Mem.PlanLane = Lane;
			const float Goal = Model.RaceFinishS();
			TSet<FString> CrashedAt;
			for (int32 Step = 0; Step < 120 * 240 && St.S < Goal; ++Step)
			{
				const FMXBikeInput In = FMXAIBrain::Think(St, Mem, Model, T, Skill, NoOthers, FMXBikeSim::FixedDt);
				FMXBikeSim::Step(St, In, Model, T, FMXBikeSim::FixedDt, false);
				if (St.Events & EMXBikeEvent::Crashed)
				{
					const FMXPlacedPiece* P = Model.PieceAt(St.CrashS);
					if (!P)
					{
						TArray<const FMXPlacedPiece*> Near;
						Model.PiecesInRange(St.CrashS - 12.f, St.CrashS + 2.f, Near);
						P = Near.Num() > 0 ? Near.Last() : nullptr;
					}
					CrashedAt.Add(P ? Def.Segments.IsValidIndex(P->SegmentIndex) ? Def.Segments[P->SegmentIndex].Id : P->Id : FString::Printf(TEXT("@%.0fm"), St.CrashS));
				}
			}
			for (const FString& Id : CrashedAt)
			{
				FailuresByPiece.FindOrAdd(Id)++;
			}
			if (St.S < Goal)
			{
				FailuresByPiece.FindOrAdd(TEXT("__stuck__"))++;
			}
		}
	}
	for (const TPair<FString, int32>& Pair : FailuresByPiece)
	{
		if (Pair.Value >= Attempts)
		{
			if (Pair.Key == TEXT("__stuck__"))
			{
				AddIssue(InOut, EMXIssueSeverity::Error, TEXT("Disconnected: no test rider could reach the finish"));
			}
			else
			{
				float S = -1.f;
				for (const FMXSegment& Seg : Def.Segments)
				{
					if (Seg.Id == Pair.Key)
					{
						S = Seg.StartM;
					}
				}
				AddIssue(InOut, EMXIssueSeverity::Error, TEXT("Impossible section: every test ride crashed here"), Pair.Key, S);
			}
		}
		else if (Pair.Value * 2 >= Attempts && Pair.Key != TEXT("__stuck__"))
		{
			AddIssue(InOut, EMXIssueSeverity::Warning, FString::Printf(TEXT("Very hard: %d of %d test rides crashed here"), Pair.Value, Attempts), Pair.Key);
		}
	}
}

bool FMXTrackValidator::EnsureFinishDeck(FMXTrackDefinition& Def, const UMXTrackStyle& Style)
{
	Def.SortSegments();
	const float DeckLen = FMXObstacleLibrary::ComputeLength(EMXObstacleType::FinishDeck, {}, 0.f, Style);
	int32 FinishIdx = Def.Segments.IndexOfByPredicate([](const FMXSegment& S) { return S.Type == EMXObstacleType::FinishDeck; });
	float LastEnd = StartZone;
	for (const FMXSegment& S : Def.Segments)
	{
		if (S.Type != EMXObstacleType::FinishDeck)
		{
			const float Len = S.LengthM > 0.f ? S.LengthM : FMXObstacleLibrary::ComputeLength(S.Type, S.Runs, 0.f, Style);
			LastEnd = FMath::Max(LastEnd, S.StartM + Len);
		}
	}
	const float WantStart = FMath::Max(LastEnd + 12.f, Def.LapLengthM - DeckLen - 4.f);
	bool bChanged = false;
	if (FinishIdx == INDEX_NONE)
	{
		FMXSegment Fin = FMXObstacleLibrary::MakeDefault(EMXObstacleType::FinishDeck, WantStart, Style);
		Fin.Id = TEXT("FIN");
		Def.Segments.Add(Fin);
		bChanged = true;
	}
	else if (!FMath::IsNearlyEqual(Def.Segments[FinishIdx].StartM, WantStart, 0.5f) && Def.Segments[FinishIdx].StartM < LastEnd)
	{
		Def.Segments[FinishIdx].StartM = WantStart;
		bChanged = true;
	}
	const float NeededLap = WantStart + DeckLen + 4.f;
	if (Def.LapLengthM < NeededLap)
	{
		Def.LapLengthM = FMath::Min(MaxLapLength, NeededLap);
		bChanged = true;
	}
	Def.SortSegments();
	return bChanged;
}
