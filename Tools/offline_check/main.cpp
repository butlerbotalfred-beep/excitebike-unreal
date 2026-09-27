#include <string>
#include <cstdlib>
// Offline harness: compiles Heatline MX's pure gameplay sources against shim types and runs the
// same checks as the UE automation tests, plus AI/validator runs over the five NES courses.
#include "CoreMinimal.h"
#include "Core/MXTypes.h"
#include "Core/MXTuning.h"
#include "Track/MXTrackTypes.h"
#include "Track/MXObstacleLibrary.h"
#include "Track/MXTrackModel.h"
#include "Track/MXTrackValidator.h"
#include "Bike/MXBikeSim.h"
#include "AI/MXAIBrain.h"
#include "Race/MXProgress.h"
#include "Race/MXContact.h"
#include <fstream>
#include <sstream>

// ---- FMXTrackDefinition methods (the UE build compiles them from MXTrackTypes.cpp) ----
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
			do { S.Id = FString::Printf("%s-%d", *Prefix, Next++); } while (Seen.Contains(S.Id));
		}
		Seen.Add(S.Id);
	}
}
float FMXTrackDefinition::LapLengthFor(EMXLayoutVariant Variant) const
{
	if (Variant == EMXLayoutVariant::Main) { return LapLengthM; }
	float Removed = 0.f;
	for (const FMXSegment& S : Segments) { if (S.bMainOnly) { Removed += S.LengthM; } }
	return FMath::Max(50.f, LapLengthM - Removed);
}

static int GFail = 0, GPass = 0;
#define EXPECT(cond, ...) do { if (cond) { ++GPass; } else { ++GFail; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static TArray<FMXTrackDefinition> LoadCourses(const char* Path)
{
	TArray<FMXTrackDefinition> Out;
	std::ifstream In(Path);
	std::string Line;
	while (std::getline(In, Line))
	{
		std::istringstream SS(Line);
		std::string Kind;
		SS >> Kind;
		if (Kind == "COURSE")
		{
			FMXTrackDefinition D;
			std::string Id;
			int Laps, Track;
			float Len;
			SS >> Id >> Laps >> Len >> Track;
			D.Id = FString(Id);
			D.Name = FString::Printf("Course %d", Track);
			D.Laps = Laps;
			D.LapLengthM = Len;
			D.NesTrack = Track;
			D.bBuiltIn = true;
			Out.Add(D);
		}
		else if (Kind == "SEG")
		{
			FMXSegment S;
			std::string Id, Type;
			int Mask, Main, NRuns;
			float Start, Len;
			SS >> Id >> Type >> Mask >> Start >> Len >> Main >> NRuns;
			S.Id = FString(Id);
			S.Type = MX::ObstacleFromString(FString(Type));
			S.LaneMask = Mask;
			S.StartM = Start;
			S.LengthM = Len;
			S.bMainOnly = Main != 0;
			for (int i = 0; i < NRuns; ++i) { int R; SS >> R; S.Runs.Add(R); }
			Out.Last().Segments.Add(S);
		}
	}
	return Out;
}

static FMXTrackModel MakeTrack(TArray<FMXSegment> Pieces, float Lap = 500.f, int32 Laps = 1)
{
	FMXTrackDefinition D;
	D.Id = "test";
	D.Laps = Laps;
	D.LapLengthM = Lap;
	D.Segments = Pieces;
	FMXSegment Fin = FMXObstacleLibrary::MakeDefault(EMXObstacleType::FinishDeck, Lap - 20.f, MXTuning::Style());
	Fin.Id = "FIN";
	D.Segments.Add(Fin);
	D.EnsureIds("T");
	FMXTrackModel M;
	M.Build(D, EMXLayoutVariant::Main, Laps, MXTuning::Style());
	return M;
}
static FMXSegment Piece(EMXObstacleType T, float S, int32 Mask = -1)
{
	FMXSegment X = FMXObstacleLibrary::MakeDefault(T, S, MXTuning::Style());
	if (Mask > 0) { X.LaneMask = Mask; }
	return X;
}
template <typename F> static float Run(FMXBikeState& St, const FMXTrackModel& M, const FMXBikeInput& In, float MaxSec, F Until)
{
	float T = 0.f;
	while (T < MaxSec) { FMXBikeSim::Step(St, In, M, MXTuning::Bike(), FMXBikeSim::FixedDt, false); T += FMXBikeSim::FixedDt; if (Until(St)) { break; } }
	return T;
}
struct FJump { float Apex = 0, Dist = 0, Air = 0, LandV = 0; EMXLandingGrade G = EMXLandingGrade::None; };
static FJump Jump(float AirIn, EMXObstacleType Ramp = EMXObstacleType::RampMedium, float SpeedFrac = 1.f)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	FMXTrackModel M = MakeTrack({Piece(Ramp, 100.f)}, 400.f);
	FMXBikeState St;
	FMXBikeSim::Spawn(St, 60.f, M.LaneCenterY(1), M);
	St.Speed = T.MaxSpeedTurbo * SpeedFrac;
	FMXBikeInput D; D.Throttle = SpeedFrac >= 1.f ? 1.f : 0.f; D.bTurbo = SpeedFrac >= 1.f;
	Run(St, M, D, 5.f, [](const FMXBikeState& S) { return S.IsAirborne(); });
	FJump J;
	const float L = St.S;
	FMXBikeInput A; A.Pitch = AirIn;
	Run(St, M, A, 6.f, [&J](const FMXBikeState& S) { J.Apex = std::max(J.Apex, S.Z); return !S.IsAirborne(); });
	J.Dist = St.S - L; J.Air = St.AirTime; J.LandV = St.Speed; J.G = St.LastLanding;
	return J;
}


// Multi-seed AI benchmark: mean race time / crashes / overheats per difficulty (argv "ai").
static int RunAIBench(int Seeds, bool bTrace)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	TArray<FMXTrackDefinition> Courses = LoadCourses("courses.txt");
	const TArray<FMXAIOtherBike> None;
	int Inversions = 0;
	for (const FMXTrackDefinition& D : Courses)
	{
		for (int Variant = 0; Variant < 2; ++Variant)
		{
			FMXTrackModel M;
			M.Build(D, (EMXLayoutVariant)Variant, 2, MXTuning::Style());
			float Mean[3] = {0, 0, 0};
			for (int Diff = 0; Diff < 3; ++Diff)
			{
				const FMXAISkill& Skill = MXTuning::AI().ForDifficulty((EMXAIDifficulty)Diff);
				float SumT = 0, Best = 1e9f, Worst = 0; int SumC = 0, SumO = 0, SumP = 0, SumL = 0;
				for (int Seed = 0; Seed < Seeds; ++Seed)
				{
					FMXBikeState St; FMXBikeSim::Spawn(St, -2.f, M.LaneCenterY(Seed % 4), M);
					FMXAIMemory Mem; Mem.Init(1000 + Seed * 7 + Diff, false);
					float Time = 0.f;
					while (St.S < M.RaceFinishS() && Time < 400.f)
					{
						const FMXBikeInput In = FMXAIBrain::Think(St, Mem, M, T, Skill, None, FMXBikeSim::FixedDt);
						const float HeatBefore = St.Heat;
						const EMXBikePhase PhaseBefore = St.Phase;
						FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
						Time += FMXBikeSim::FixedDt;
						if (bTrace && PhaseBefore != EMXBikePhase::Stalled && St.Phase == EMXBikePhase::Stalled)
						{
							std::printf("    OVERHEAT %s v%d AI%d seed %d t=%.1f s=%.1f lane %d heat %.0f->%.0f turbo %d greedy %.1f cooling %d\n", *D.Name, Variant, Diff, Seed,
								Time, St.S, M.NearestLane(St.Y), HeatBefore, St.Heat, In.bTurbo, Mem.GreedyTimer, Mem.bTurboCooling);
						}
						if (bTrace && PhaseBefore != EMXBikePhase::Crashed && St.Phase == EMXBikePhase::Crashed)
						{
							std::printf("    CRASH %s v%d AI%d seed %d t=%.1f s=%.1f lane %d cause %d speed %.1f\n", *D.Name, Variant, Diff, Seed,
								Time, St.S, M.NearestLane(St.Y), (int)St.CrashCause, St.ForwardSpeed());
						}
					}
					SumT += Time; Best = std::min(Best, Time); Worst = std::max(Worst, Time);
					SumC += St.Crashes; SumO += St.Overheats; SumP += St.PerfectLandings; SumL += St.Landings;
				}
				Mean[Diff] = SumT / Seeds;
				std::printf("  %s %-9s AI%d: mean %6.1f s  (best %5.1f worst %5.1f)  crashes %.2f  overheats %.2f  perfect %.0f%%\n", *D.Name,
					Variant ? "main" : "challenge", Diff, Mean[Diff], Best, Worst, SumC / float(Seeds), SumO / float(Seeds), 100.f * SumP / std::max(1, SumL));
			}
			if (!(Mean[2] < Mean[1] && Mean[1] < Mean[0]))
			{
				++Inversions;
				std::printf("  ^^ difficulty order inverted\n");
			}
		}
	}
	std::printf("\n%d inversions\n", Inversions);
	return Inversions;
}

// Trace one AI run between two distances (argv: trace <course 1-5> <variant> <diff> <seed> <s0> <s1>).
static int RunTrace(int Course, int Variant, int Diff, int Seed, float S0, float S1)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	TArray<FMXTrackDefinition> Courses = LoadCourses("courses.txt");
	const FMXTrackDefinition& D = Courses[Course - 1];
	FMXTrackModel M; M.Build(D, (EMXLayoutVariant)Variant, 2, MXTuning::Style());
	const FMXAISkill& Skill = MXTuning::AI().ForDifficulty((EMXAIDifficulty)Diff);
	const TArray<FMXAIOtherBike> None;
	FMXBikeState St; FMXBikeSim::Spawn(St, -2.f, M.LaneCenterY(Seed % 4), M);
	FMXAIMemory Mem; Mem.Init(1000 + Seed * 7 + Diff, false);
	float Time = 0.f; int N = 0;
	while (St.S < M.RaceFinishS() && Time < 400.f)
	{
		const FMXBikeInput In = FMXAIBrain::Think(St, Mem, M, T, Skill, None, FMXBikeSim::FixedDt);
		const EMXBikePhase Before = St.Phase;
		FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
		Time += FMXBikeSim::FixedDt;
		if (St.S >= S0 && St.S <= S1 && ((++N % 6) == 0 || St.Phase != Before))
		{
			std::printf("t %6.2f s %7.2f y %5.2f z %5.2f ground %5.2f v %5.1f vx %5.1f vz %5.1f pitch %6.1f slope %6.1f phase %d thr %.0f turbo %d pitchIn %5.2f tgtV %5.1f plan %d heat %3.0f\n",
				Time, St.S, St.Y, St.Z, M.Height(St.S, St.Y), St.ForwardSpeed(), St.VX, St.VZ, St.Pitch, St.SlopeDeg, (int)St.Phase, In.Throttle, In.bTurbo, In.Pitch,
				Mem.TargetSpeed, Mem.PlanLane, St.Heat);
		}
		if (St.S > S1 + 5.f) { break; }
	}
	return 0;
}

// Full 8-bike AI races with contact, run the same way AMXRaceManager steps them (argv: pack [seeds]).
static int RunPack(int Seeds)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	TArray<FMXTrackDefinition> Courses = LoadCourses("courses.txt");
	int Problems = 0;
	const EMXAIDifficulty Mix[8] = {EMXAIDifficulty::Hard, EMXAIDifficulty::Medium, EMXAIDifficulty::Easy, EMXAIDifficulty::Hard,
		EMXAIDifficulty::Medium, EMXAIDifficulty::Easy, EMXAIDifficulty::Medium, EMXAIDifficulty::Hard};
	for (const FMXTrackDefinition& D : Courses)
	{
		FMXTrackModel M;
		M.Build(D, EMXLayoutVariant::Main, 2, MXTuning::Style());
		int Contacts = 0, ContactCrashes = 0, CutOffs = 0, Cooldowns = 0, MaxCrashOneBike = 0, Unfinished = 0;
		float Slowest = 0.f, Fastest = 1e9f;
		float WinByDiff[3] = {0, 0, 0};
		for (int Seed = 0; Seed < Seeds; ++Seed)
		{
			FMXBikeState St[8];
			FMXAIMemory Mem[8];
			FMXRacerProgress Prog[8];
			int32 ContactCrashBy[8] = {0};
			int32 Counter = 0;
			for (int i = 0; i < 8; ++i)
			{
				FMXBikeSim::Spawn(St[i], -2.5f - (i / 4) * 6.5f, M.LaneCenterY(i % 4), M);
				Mem[i].Init(5000 + Seed * 31 + i, i % 3 == 0);
				Mem[i].PlanLane = i % 4;
			}
			float Time = 0.f;
			bool bAllDone = false;
			while (Time < 300.f && !bAllDone)
			{
				float SPrev[8];
				for (int i = 0; i < 8; ++i)
				{
					SPrev[i] = St[i].S;
					St[i].Events = 0;
					TArray<FMXAIOtherBike> Others;
					for (int j = 0; j < 8; ++j)
					{
						if (j != i) { Others.Add({St[j].S, St[j].Y, St[j].ForwardSpeed(), St[j].IsRiding(), false, j, St[j].CanCollide()}); }
					}
					FMXBikeInput In;
					if (Prog[i].bFinished) { In.Throttle = St[i].ForwardSpeed() > 12.f ? 0.f : 0.4f; In.bLaneTaps = true; }
					else { In = FMXAIBrain::Think(St[i], Mem[i], M, T, MXTuning::AI().ForDifficulty(Mix[i]), Others, FMXBikeSim::FixedDt); }
					FMXBikeSim::Step(St[i], In, M, T, FMXBikeSim::FixedDt, false);
				}
				TArray<FMXBikeState*> Ptrs;
				for (int i = 0; i < 8; ++i) { Ptrs.Add(&St[i]); }
				TArray<FMXContactResult> Results;
				MXContact::Resolve(TArrayView<FMXBikeState*>(Ptrs), M, T, Results);
				for (const FMXContactResult& R : Results)
				{
					++Contacts;
					if (R.Outcome == EMXContactOutcome::Crashed)
					{
						++ContactCrashes;
						if (R.Rear >= 0)
						{
							++ContactCrashBy[R.Rear];
							const int F = R.Rear == R.A ? R.B : R.A;
							if (getenv("MX_TRACE"))
							{
								std::printf("    rear-end t=%.1f s=%.1f rear AI%d(%d) lane %d v=%.1f pitch %.0f air %.2f | front AI%d(%d) lane %d v=%.1f phase %d air %.2f\n", Time, St[R.Rear].S,
									(int)Mix[R.Rear], R.Rear, M.NearestLane(St[R.Rear].Y), St[R.Rear].Speed, St[R.Rear].Pitch, St[R.Rear].AirTime,
									(int)Mix[F], F, M.NearestLane(St[F].Y), St[F].ForwardSpeed(), (int)St[F].Phase, St[F].AirTime);
							}
						}
					}
					if (R.Outcome == EMXContactOutcome::CutOff) { ++CutOffs; }
					if (R.Outcome == EMXContactOutcome::CooldownBump) { ++Cooldowns; }
				}
				Time += FMXBikeSim::FixedDt;
				bAllDone = true;
				for (int i = 0; i < 8; ++i)
				{
					const bool bValid = MXProgress::IsValidMove(SPrev[i], St[i].S, (St[i].Events & EMXBikeEvent::Recovered) != 0);
					const FMXProgressStep Step = MXProgress::Advance(Prog[i], M.GetGates(), 2, M.RaceFinishS(), SPrev[i], St[i].S, bValid, Time, Counter);
					if (Step.bFinished) { St[i].bFinished = true; }
					bAllDone &= Prog[i].bFinished;
				}
			}
			for (int i = 0; i < 8; ++i)
			{
				MaxCrashOneBike = std::max(MaxCrashOneBike, ContactCrashBy[i]);
				if (!Prog[i].bFinished) { ++Unfinished; continue; }
				Slowest = std::max(Slowest, Prog[i].FinishTime);
				Fastest = std::min(Fastest, Prog[i].FinishTime);
				if (Prog[i].FinishOrder == 1) { WinByDiff[(int)Mix[i]] += 1.f; }
			}
		}
		std::printf("  %s main, 8 AI x %d races: contacts %.1f/race  contact crashes %.2f/race (max %d for one rider in one race)  cut-offs %.1f  cooldown bumps %.1f  unfinished %d  finish %.1f-%.1f s  wins E/M/H %.0f/%.0f/%.0f\n",
			*D.Name, Seeds, Contacts / float(Seeds), ContactCrashes / float(Seeds), MaxCrashOneBike, CutOffs / float(Seeds), Cooldowns / float(Seeds), Unfinished,
			Fastest, Slowest, WinByDiff[0], WinByDiff[1], WinByDiff[2]);
		if (Unfinished > 0 || MaxCrashOneBike > 2) { ++Problems; std::printf("  ^^ problem\n"); }
	}
	return Problems;
}

// Balance check: is holding turbo everywhere the right answer? (argv: choices)
static int RunChoices()
{
	const UMXBikeTuning& T = MXTuning::Bike();
	TArray<FMXTrackDefinition> Courses = LoadCourses("courses.txt");
	const FMXAISkill& Skill = MXTuning::AI().ForDifficulty(EMXAIDifficulty::Hard);
	const TArray<FMXAIOtherBike> None;
	const char* Names[] = {"managed (Hard AI)", "turbo to red line", "careful", "hold turbo always", "never turbo"};
	int Bad = 0;
	for (const FMXTrackDefinition& D : Courses)
	{
		FMXTrackModel M;
		M.Build(D, EMXLayoutVariant::Main, 2, MXTuning::Style());
		float Times[5];
		for (int Strat : {0, 1, 3, 4})
		{
			float Sum = 0.f; int Over = 0, Crash = 0;
			for (int Lane = 0; Lane < 4; ++Lane)
			{
				FMXBikeState St; FMXBikeSim::Spawn(St, -2.5f, M.LaneCenterY(Lane), M);
				FMXAIMemory Mem; Mem.Init(77 + Lane, false); Mem.bAutopilot = true; Mem.Strategy = Strat; Mem.PlanLane = Lane;
				float Time = 0.f;
				while (St.S < M.RaceFinishS() && Time < 400.f)
				{
					FMXBikeSim::Step(St, FMXAIBrain::Think(St, Mem, M, T, Skill, None, FMXBikeSim::FixedDt), M, T, FMXBikeSim::FixedDt, false);
					Time += FMXBikeSim::FixedDt;
				}
				Sum += Time; Over += St.Overheats; Crash += St.Crashes;
			}
			Times[Strat] = Sum / 4.f;
			std::printf("  %s main  %-18s %6.1f s  overheats %.2f  crashes %.2f\n", *D.Name, Names[Strat], Times[Strat], Over / 4.f, Crash / 4.f);
		}
		if (!(Times[0] < Times[3] && Times[0] < Times[4])) { ++Bad; std::printf("  ^^ managed riding is not the fastest\n"); }
	}
	return Bad;
}

// Player styles vs the AI levels (argv: human [seeds]). The player rows are the AI brain set up to ride the way
// people do: they hold the gas through jumps and let go of turbo when the heat warning shows. Real players make
// more mistakes than these rows, so each AI level should be clearly slower than the player style it is meant for.
static FMXAISkill PlayerStyle(int Level)
{
	FMXAISkill S;
	S.bChasersAllowed = false;
	S.GreedyTurboRate = 0.f;
	S.bPlansCoolStrips = false;
	S.bUsesFlightControl = false;
	S.AirCoastChance = 0.f;
	S.ReactionDelay = 0.3f; S.LandingErrorDeg = 10.f; S.TurboHeatLimit = 75.f; S.TurboResumeMargin = 25.f; S.LaneHorizon = 25.f; S.MashRate = 5.f; S.MistakeRate = 0.08f;
	if (Level >= 1) { S.ReactionDelay = 0.25f; S.LandingErrorDeg = 7.f; S.TurboHeatLimit = 85.f; S.TurboResumeMargin = 20.f; S.LaneHorizon = 35.f; S.MistakeRate = 0.05f; S.AirCoastChance = 0.3f; }
	if (Level >= 2) { S.ReactionDelay = 0.15f; S.LandingErrorDeg = 4.f; S.TurboHeatLimit = 92.f; S.TurboResumeMargin = 10.f; S.LaneHorizon = 50.f; S.MistakeRate = 0.02f; S.AirCoastChance = 0.9f; S.bUsesFlightControl = true; S.bPlansCoolStrips = true; }
	return S;
}

static int RunHuman(int Seeds)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	TArray<FMXTrackDefinition> Courses = LoadCourses("courses.txt");
	const TArray<FMXAIOtherBike> None;
	const char* Names[6] = {"player: novice", "player: casual", "player: good", "AI Easy", "AI Medium", "AI Hard"};
	const FMXAISkill Skills[6] = {PlayerStyle(0), PlayerStyle(1), PlayerStyle(2), MXTuning::AI().Easy, MXTuning::AI().Medium, MXTuning::AI().Hard};
	float Total[6] = {0, 0, 0, 0, 0, 0};
	std::printf("  %-15s %8s %7s %9s %8s %8s  per course (s)\n", "rider", "total s", "turbo%", "overheat", "crashes", "perfect");
	for (int R = 0; R < 6; ++R)
	{
		float Ground = 0, TurboGround = 0, Air = 0, CoastAir = 0;
		int Over = 0, Crash = 0, Perfect = 0, Landings = 0;
		std::string PerCourse;
		for (const FMXTrackDefinition& D : Courses)
		{
			FMXTrackModel M;
			M.Build(D, EMXLayoutVariant::Main, D.Laps, MXTuning::Style());
			float Sum = 0.f;
			for (int Seed = 0; Seed < Seeds; ++Seed)
			{
				FMXBikeState St; FMXBikeSim::Spawn(St, -2.f, M.LaneCenterY(Seed % 4), M);
				FMXAIMemory Mem; Mem.Init(500 + Seed * 13 + R, false);
				float Time = 0.f;
				while (St.S < M.RaceFinishS() && Time < 400.f)
				{
					const FMXBikeInput In = FMXAIBrain::Think(St, Mem, M, T, Skills[R], None, FMXBikeSim::FixedDt);
					if (St.Phase == EMXBikePhase::Grounded) { Ground += FMXBikeSim::FixedDt; TurboGround += In.bTurbo ? FMXBikeSim::FixedDt : 0.f; }
					if (St.Phase == EMXBikePhase::Airborne) { Air += FMXBikeSim::FixedDt; CoastAir += (In.Throttle < 0.1f && !In.bTurbo) ? FMXBikeSim::FixedDt : 0.f; }
					FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
					Time += FMXBikeSim::FixedDt;
				}
				Sum += Time; Over += St.Overheats; Crash += St.Crashes; Perfect += St.PerfectLandings; Landings += St.Landings;
			}
			Total[R] += Sum / Seeds;
			char Buf[16]; std::snprintf(Buf, sizeof(Buf), " %6.1f", Sum / Seeds); PerCourse += Buf;
		}
		const float Races = float(Seeds * Courses.Num());
		std::printf("  %-15s %8.1f %6.0f%% %9.2f %8.2f %7.0f%% %s  (off the gas %.0f%% of air time)\n", Names[R], Total[R], 100.f * TurboGround / std::max(0.01f, Ground),
			Over / Races, Crash / Races, 100.f * Perfect / std::max(1, Landings), PerCourse.c_str(), 100.f * CoastAir / std::max(0.01f, Air));
	}
	std::printf("\n  AI Easy vs novice %+.1f%%, AI Medium vs casual %+.1f%%, AI Hard vs good %+.1f%% (positive = AI slower)\n",
		100.f * (Total[3] / Total[0] - 1.f), 100.f * (Total[4] / Total[1] - 1.f), 100.f * (Total[5] / Total[2] - 1.f));
	return 0;
}

int main(int argc, char** argv)
{
	if (argc > 1 && std::string(argv[1]) == "human") { return RunHuman(argc > 2 ? std::atoi(argv[2]) : 8); }
	if (argc > 1 && std::string(argv[1]) == "choices") { return RunChoices(); }
	if (argc > 1 && std::string(argv[1]) == "pack") { return RunPack(argc > 2 ? std::atoi(argv[2]) : 6); }
	if (argc > 7 && std::string(argv[1]) == "trace") { return RunTrace(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atof(argv[6]), std::atof(argv[7])); }
	if (argc > 1 && std::string(argv[1]) == "ai") { return RunAIBench(argc > 2 ? std::atoi(argv[2]) : 8, argc > 3); }
	const UMXBikeTuning& T = MXTuning::Bike();
	std::printf("== Heat\n");
	{
		float Heat = 0.f, Time = 0.f;
		while (!FMXBikeSim::UpdateHeat(Heat, 1.f, true, 1.f / 120.f, T) && Time < 20.f) { Time += 1.f / 120.f; }
		std::printf("  turbo overheat after %.2f s\n", Time);
		EXPECT(std::fabs(Time - 6.8f) < 0.25f, "turbo overheat time %.2f", Time);
		Heat = 0.f; bool bOver = false;
		for (int i = 0; i < 120 * 60; ++i) { bOver |= FMXBikeSim::UpdateHeat(Heat, 1.f, false, 1.f / 120.f, T); }
		EXPECT(!bOver && std::fabs(Heat - T.HeatEquilibriumNormal) < 0.5f, "normal equilibrium %.2f", Heat);
		FMXTrackModel M = MakeTrack({Piece(EMXObstacleType::CoolStrip, 150.f, MX::MaskFromLanes({2}))}, 600.f);
		FMXBikeState St; FMXBikeSim::Spawn(St, 0.f, M.LaneCenterY(1), M); St.Heat = 90.f;
		FMXBikeInput In; In.Throttle = 1.f;
		Run(St, M, In, 20.f, [](const FMXBikeState& S) { return S.S > 160.f; });
		EXPECT(St.CoolStripHits == 1 && St.Heat < 10.f, "cool strip hits %d heat %.1f", St.CoolStripHits, St.Heat);
		FMXBikeSim::Spawn(St, 0.f, M.LaneCenterY(0), M); In.bTurbo = true;
		Run(St, M, In, 20.f, [](const FMXBikeState& S) { return S.Phase == EMXBikePhase::Stalled; });
		EXPECT(St.Phase == EMXBikePhase::Stalled, "turbo stalls");
		const float Start = St.SimTime;
		Run(St, M, In, 20.f, [](const FMXBikeState& S) { return S.Phase != EMXBikePhase::Stalled; });
		EXPECT(std::fabs(St.SimTime - Start - T.StallDuration) < 0.05f, "stall %.2f s", St.SimTime - Start);
	}
	std::printf("== Landing + pitch trajectory\n");
	{
		EXPECT(FMXBikeSim::ClassifyLanding(0, 0, T) == EMXLandingGrade::Perfect, "perfect");
		EXPECT(FMXBikeSim::ClassifyLanding(-30, 30, T) == EMXLandingGrade::Crash, "nose into face crash");
		EXPECT(FMXBikeSim::ClassifyLanding(52, -30, T) == EMXLandingGrade::Wobble, "nose high downslope wobble");
		const FJump Up = Jump(1.f), N = Jump(0.f), Dn = Jump(-1.f);
		std::printf("  nose-up   apex %.2f dist %.1f air %.2f land %.1f grade %d\n", Up.Apex, Up.Dist, Up.Air, Up.LandV, (int)Up.G);
		std::printf("  neutral   apex %.2f dist %.1f air %.2f land %.1f grade %d\n", N.Apex, N.Dist, N.Air, N.LandV, (int)N.G);
		std::printf("  nose-down apex %.2f dist %.1f air %.2f land %.1f grade %d\n", Dn.Apex, Dn.Dist, Dn.Air, Dn.LandV, (int)Dn.G);
		EXPECT(Up.Apex > N.Apex + 0.3f && Up.Air > N.Air, "nose-up higher/longer hang");
		EXPECT(Dn.Dist > N.Dist + 1.f && Dn.Dist > Up.Dist + 1.f, "nose-down further");
		for (EMXObstacleType R : {EMXObstacleType::RampSmall, EMXObstacleType::RampLarge, EMXObstacleType::RampSteep, EMXObstacleType::Kicker, EMXObstacleType::TableLow})
		{
			const FJump J = Jump(0.f, R), Slow = Jump(0.f, R, 0.5f);
			std::printf("  %-14s full: %5.1f m %.2f s apex %.2f   half-speed: %5.1f m %.2f s\n", *MX::ObstacleName(R), J.Dist, J.Air, J.Apex, Slow.Dist, Slow.Air);
		}
		const FJump A = Jump(0.f);
		for (int i = 0; i < 5; ++i) { const FJump B = Jump(0.f); EXPECT(A.Dist == B.Dist && A.G == B.G, "deterministic"); }
	}
	std::printf("== Obstacles\n");
	{
		const UMXTrackStyle& Style = MXTuning::Style();
		FMXTrackModel M = MakeTrack({Piece(EMXObstacleType::RampLarge, 50.f), Piece(EMXObstacleType::Mud, 100.f, MX::MaskFromLanes({1, 3})),
			Piece(EMXObstacleType::Barrier, 130.f, MX::MaskFromLanes({3, 4})), Piece(EMXObstacleType::PlatformDeck, 170.f), Piece(EMXObstacleType::Grass, 250.f, MX::MaskFromLanes({1, 2}))}, 400.f);
		const float LL = FMXObstacleLibrary::ComputeLength(EMXObstacleType::RampLarge, {}, 0.f, Style);
		EXPECT(std::fabs(M.Height(50.f + LL * 0.5f, 0.f) - 3.f) < 0.05f, "large ramp peak %.2f", M.Height(50.f + LL * 0.5f, 0.f));
		EXPECT(M.SurfaceAt(101.f, M.LaneCenterY(0)) == EMXSurface::Mud && M.SurfaceAt(101.f, M.LaneCenterY(1)) == EMXSurface::Dirt, "mud lanes");
		FMXBarrierHit Hit;
		EXPECT(M.FindBarrierCrossing(129.f, 132.f, M.LaneCenterY(3), Hit) && !M.FindBarrierCrossing(129.f, 132.f, M.LaneCenterY(0), Hit), "barrier lanes");
		const float Deck = 170.f + 15.f * Style.MetersPerColumn;
		EXPECT(M.Height(Deck, M.LaneCenterY(0)) > 3.f && M.Height(Deck, M.LaneCenterY(3)) < 0.05f, "platform deck lanes");
		FMXBikeState St; FMXBikeSim::Spawn(St, 120.f, M.LaneCenterY(3), M); St.Speed = T.MaxSpeedTurbo;
		FMXBikeInput In; In.Throttle = 1.f; In.bTurbo = true;
		Run(St, M, In, 3.f, [](const FMXBikeState& S) { return S.S > 132.f || !S.IsRiding(); });
		EXPECT(St.CrashCause == EMXCrashCause::Barrier, "barrier crash (cause %d)", (int)St.CrashCause);
		FMXBikeSim::Spawn(St, 118.f, M.LaneCenterY(3), M); St.Speed = T.MaxSpeedTurbo; In.Pitch = 1.f;
		Run(St, M, In, 3.f, [](const FMXBikeState& S) { return S.S > 133.f || !S.IsRiding(); });
		EXPECT(St.IsRiding() && St.S > 132.f, "wheelie clears barrier (phase %d s %.1f)", (int)St.Phase, St.S);
	}
	std::printf("== Contact\n");
	{
		FMXTrackModel M = MakeTrack({}, 400.f);
		auto Make = [&M](float S, float Y, float V) { FMXBikeState St; FMXBikeSim::Spawn(St, S, Y, M); St.Speed = V; return St; };
		TArray<FMXContactResult> R;
		FMXBikeState Front = Make(101.5f, M.LaneCenterY(1), 15.f), Rear = Make(100.f, M.LaneCenterY(1), 25.f);
		TArray<FMXBikeState*> Pair = {&Rear, &Front};
		MXContact::Resolve(Pair, M, T, R);
		EXPECT(Rear.Phase == EMXBikePhase::Crashed && Front.IsRiding(), "rear-end crash");
		Front = Make(101.5f, M.LaneCenterY(1), 15.f); Rear = Make(100.f, M.LaneCenterY(1), 25.f); Front.LastLateralTime = 0.1f; R.Reset();
		MXContact::Resolve(Pair, M, T, R);
		EXPECT(Rear.IsRiding() && R.Num() == 1 && R[0].Outcome == EMXContactOutcome::CutOff, "cut-in wobble");
		for (int Who = 0; Who < 2; ++Who)
		{
			Front = Make(101.5f, M.LaneCenterY(1), 15.f); Rear = Make(100.f, M.LaneCenterY(1), 25.f); R.Reset();
			(Who ? Rear : Front).LastLandingTime = (Who ? Rear : Front).SimTime - 0.2f;
			MXContact::Resolve(Pair, M, T, R);
			EXPECT(Rear.IsRiding() && R.Num() == 1 && R[0].Outcome == EMXContactOutcome::LandingBump && Rear.S <= Front.S - T.BikeContactLength + 1e-3f
				&& Rear.Speed <= Front.Speed, "%s just landed: bump, held behind", Who ? "rear" : "front");
		}
		Front = Make(100.3f, M.LaneCenterY(1) + 0.4f, 20.f); Rear = Make(100.f, M.LaneCenterY(1), 20.f); R.Reset();
		MXContact::Resolve(Pair, M, T, R);
		EXPECT(Rear.IsRiding() && Front.IsRiding(), "side bump no crash");
	}
	std::printf("== Checkpoints / recovery\n");
	{
		FMXTrackModel M = MakeTrack({Piece(EMXObstacleType::RampSmall, 100.f)}, 400.f, 3);
		int Lines = 0; bool bOrdered = true;
		for (int i = 0; i < M.GetGates().Num(); ++i) { Lines += M.GetGates()[i].bLapLine; if (i) { bOrdered &= M.GetGates()[i].S > M.GetGates()[i - 1].S; } }
		EXPECT(Lines == 3 && bOrdered, "gates");
		bool bOk = true;
		for (float S = 5.f; S < M.RaceFinishS(); S += 7.3f) { bOk &= M.FindRecoveryS(S, 0.f, 40.f) <= S + 1e-3f; }
		EXPECT(bOk, "recovery never ahead");
	}
	std::printf("== Progress integrity\n");
	{
		// 2 laps of 400 m: gates every ~60 m, lap lines at 400 and 800.
		FMXTrackModel M = MakeTrack({}, 400.f, 2);
		const TArray<FMXGate>& G = M.GetGates();
		auto Ride = [&](FMXRacerProgress& P, float& S, float To, float& Time, int32& Counter)
		{
			while (S < To - 1e-4f && !P.bFinished)
			{
				const float Prev = S;
				S = std::min(To, S + 0.25f);
				Time += 1.f / 120.f;
				MXProgress::Advance(P, G, 2, M.RaceFinishS(), Prev, S, MXProgress::IsValidMove(Prev, S, false), Time, Counter);
			}
		};
		// Clean ride: every gate once, 2 laps, finish order 1.
		{
			FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
			Ride(P, S, M.RaceFinishS() + 5.f, Time, Counter);
			EXPECT(P.bFinished && P.LapsCompleted == 2 && P.LapTimes.Num() == 2 && P.FinishOrder == 1 && P.NextGate == G.Num(), "clean ride finishes");
		}
		// A forward teleport (not movement) grants nothing, and the skipped gate can't be crossed from ahead.
		{
			FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
			Ride(P, S, 50.f, Time, Counter);
			const int32 Before = P.NextGate;
			const float Prev = S; S += 120.f;
			const FMXProgressStep Step = MXProgress::Advance(P, G, 2, M.RaceFinishS(), Prev, S, MXProgress::IsValidMove(Prev, S, false), Time, Counter);
			EXPECT(Step.bIgnoredJump && P.NextGate == Before && P.ValidS <= G[Before].S + 1e-3f, "teleport ignored");
			Ride(P, S, M.RaceFinishS() + 5.f, Time, Counter);
			EXPECT(!P.bFinished && P.LapsCompleted == 0, "teleport can't finish (laps %d)", P.LapsCompleted);
		}
		// Recovery placement behind a crossed gate: re-crossing it doesn't count twice.
		{
			FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
			Ride(P, S, G[2].S + 3.f, Time, Counter);
			const int32 Before = P.NextGate;
			const float Prev = S; S = G[2].S - 6.f; // recovery (flagged as such)
			MXProgress::Advance(P, G, 2, M.RaceFinishS(), Prev, S, MXProgress::IsValidMove(Prev, S, true), Time, Counter);
			EXPECT(P.NextGate == Before, "recovery keeps gate state");
			Ride(P, S, M.RaceFinishS() + 5.f, Time, Counter);
			EXPECT(P.bFinished && P.LapsCompleted == 2 && P.LapTimes.Num() == 2, "recovery then finish counts laps once");
		}
		// Bumped back across the lap line and crossing it again: still one lap.
		{
			FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
			int32 LapGate = 0; while (!G[LapGate].bLapLine) { ++LapGate; }
			Ride(P, S, G[LapGate].S + 0.2f, Time, Counter);
			EXPECT(P.LapsCompleted == 1, "lap 1 counted");
			float Prev = S; S = G[LapGate].S - 0.4f; // contact push-back (valid, small)
			const FMXProgressStep Back = MXProgress::Advance(P, G, 2, M.RaceFinishS(), Prev, S, MXProgress::IsValidMove(Prev, S, false), Time, Counter);
			EXPECT(Back.bBackwardCrossing && P.LapsCompleted == 0 && P.LapTimes.Num() == 0, "un-crossing the lap line un-counts the lap");
			Ride(P, S, G[LapGate].S + 5.f, Time, Counter);
			EXPECT(P.LapsCompleted == 1 && P.LapTimes.Num() == 1, "re-crossing counts once (laps %d)", P.LapsCompleted);
			Ride(P, S, M.RaceFinishS() + 5.f, Time, Counter);
			EXPECT(P.bFinished && P.LapsCompleted == 2 && P.LapTimes.Num() == 2, "finish after bump-back");
		}
		// Validated progress is capped at the next uncrossed gate.
		{
			FMXRacerProgress P; float S = 10.f, Time = 0.f; int32 Counter = 0;
			P.NextGate = 0;
			MXProgress::Advance(P, G, 2, M.RaceFinishS(), 10.f, 500.f, false, Time, Counter);
			EXPECT(P.ValidS <= G[0].S + 1e-3f, "ValidS capped (%.1f)", P.ValidS);
		}
	}
	std::printf("== NES courses: validation, AI runs, drive test\n");
	TArray<FMXTrackDefinition> Courses = LoadCourses("courses.txt");
	EXPECT(Courses.Num() == 5, "5 courses");
	const TArray<FMXAIOtherBike> None;
	for (const FMXTrackDefinition& D : Courses)
	{
		FMXValidationResult V = FMXTrackValidator::Validate(D, MXTuning::Style());
		for (const FMXTrackIssue& I : V.Issues) { std::printf("  %s: [%d] %s %s @%.1f\n", *D.Name, (int)I.Severity, *I.Message, *I.SegmentId, I.S); }
		EXPECT(!V.HasErrors(), "%s validates", *D.Name);
		for (int Variant = 0; Variant < 2; ++Variant)
		{
			FMXTrackModel M;
			M.Build(D, (EMXLayoutVariant)Variant, 2, MXTuning::Style());
			for (int Diff = 0; Diff < 3; ++Diff)
			{
				const FMXAISkill& Skill = MXTuning::AI().ForDifficulty((EMXAIDifficulty)Diff);
				FMXBikeState St; FMXBikeSim::Spawn(St, -2.f, M.LaneCenterY(1), M);
				FMXAIMemory Mem; Mem.Init(99 + Diff, false);
				float Time = 0.f; float MaxHeat = 0.f;
				int Stalls = 0; float AirTime = 0.f;
				while (St.S < M.RaceFinishS() && Time < 400.f)
				{
					const FMXBikeInput In = FMXAIBrain::Think(St, Mem, M, T, Skill, None, FMXBikeSim::FixedDt);
					FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
					Time += FMXBikeSim::FixedDt; MaxHeat = std::max(MaxHeat, St.Heat);
					if (St.IsAirborne()) { AirTime += FMXBikeSim::FixedDt; }
				}
				std::printf("  %s %-9s AI%d: race %6.1f s (2 laps) crashes %2d overheats %d perfect %3d/%3d cool %2d maxheat %3.0f air %4.1f s%s\n", *D.Name,
					Variant ? "main" : "challenge", Diff, Time, St.Crashes, St.Overheats, St.PerfectLandings, St.Landings, St.CoolStripHits, MaxHeat, AirTime,
					St.S < M.RaceFinishS() ? "  DID NOT FINISH" : "");
				EXPECT(St.S >= M.RaceFinishS(), "%s AI%d finishes", *D.Name, Diff);
			}
		}
		{
			const UMXAITuning& AIT = MXTuning::AI();
			for (int Variant = 0; Variant < 2; ++Variant)
			{
				const float Ref = FMXTrackValidator::ReferenceTime(D, (EMXLayoutVariant)Variant, 2);
				const float Ref1 = FMXTrackValidator::ReferenceTime(D, (EMXLayoutVariant)Variant, 1);
				const float Ref4 = FMXTrackValidator::ReferenceTime(D, (EMXLayoutVariant)Variant, 4);
				std::printf("  %s %-9s reference %.1f s (1 lap %.1f, 4 laps %.1f) -> medals gold %.1f silver %.1f bronze %.1f\n", *D.Name, Variant ? "main" : "challenge",
					Ref, Ref1, Ref4, Ref * AIT.MedalGoldFactor, Ref * AIT.MedalSilverFactor, Ref * AIT.MedalBronzeFactor);
				EXPECT(Ref > 0.f && Ref1 > 0.f && Ref4 > Ref && Ref > Ref1, "%s reference times scale with laps", *D.Name);
			}
		}
		FMXValidationResult DV;
		FMXTrackValidator::DriveTest(D, DV);
		for (const FMXTrackIssue& I : DV.Issues) { std::printf("  drive-test %s: [%d] %s %s\n", *D.Name, (int)I.Severity, *I.Message, *I.SegmentId); }
	}
	std::printf("== AI turbo like a person (HeatlineMX.AI.TurboLikeAPerson)\n");
	for (int Diff = 0; Diff < 3; ++Diff)
	{
		const FMXAISkill& Skill = MXTuning::AI().ForDifficulty((EMXAIDifficulty)Diff);
		float Ground = 0, TurboGround = 0, Air = 0, CoastAir = 0;
		int Overheats = 0;
		for (int Course = 1; Course <= 5; ++Course)
		{
			const FMXTrackDefinition& D = Courses[Course - 1];
			FMXTrackModel M;
			M.Build(D, EMXLayoutVariant::Main, D.Laps, MXTuning::Style());
			for (int Seed = 0; Seed < 2; ++Seed)
			{
				FMXBikeState St; FMXBikeSim::Spawn(St, -2.f, M.LaneCenterY(Seed), M);
				FMXAIMemory Mem; Mem.Init(300 + Course * 10 + Seed, false);
				float Time = 0.f;
				while (St.S < M.RaceFinishS() && Time < 300.f)
				{
					const FMXBikeInput In = FMXAIBrain::Think(St, Mem, M, T, Skill, None, FMXBikeSim::FixedDt);
					if (St.Phase == EMXBikePhase::Grounded) { Ground += FMXBikeSim::FixedDt; TurboGround += In.bTurbo ? FMXBikeSim::FixedDt : 0.f; }
					else if (St.Phase == EMXBikePhase::Airborne) { Air += FMXBikeSim::FixedDt; CoastAir += In.Throttle < 0.1f ? FMXBikeSim::FixedDt : 0.f; }
					FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
					Time += FMXBikeSim::FixedDt;
				}
				Overheats += St.Overheats;
			}
		}
		const float TurboShare = TurboGround / std::max(0.01f, Ground), CoastShare = CoastAir / std::max(0.01f, Air);
		std::printf("  AI%d: turbo on %.0f%% of ground time, off the gas %.0f%% of air time, %d overheats in 10 races\n", Diff, TurboShare * 100.f, CoastShare * 100.f, Overheats);
		if (Diff == 0) { EXPECT(TurboShare < 0.55f && CoastShare < 0.01f && Overheats > 0, "easy AI: turbo bursts, gas on in the air, overheats sometimes"); }
		else if (Diff == 1) { EXPECT(TurboShare < 0.7f && CoastShare < 0.3f, "medium AI: turbo bursts, rarely cools in the air"); }
		else { EXPECT(CoastShare > 0.9f, "hard AI cools in every jump"); }
	}
	std::printf("\n%d passed, %d failed\n", GPass, GFail);
	return GFail ? 1 : 0;
}
