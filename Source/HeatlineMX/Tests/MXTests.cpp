// Headless automation tests for the pure gameplay layer (track model, bike simulation, heat,
// landings, contact rules, checkpoints, course data, validator, AI). Run with:
//   UnrealEditor-Cmd HeatlineMX.uproject -ExecCmds="Automation RunTests HeatlineMX; Quit" -unattended -nullrhi -nosound

#include "Misc/AutomationTest.h"
#include "Bike/MXBikeSim.h"
#include "Track/MXTrackModel.h"
#include "Track/MXCourseLibrary.h"
#include "Track/MXObstacleLibrary.h"
#include "Track/MXTrackValidator.h"
#include "Race/MXContact.h"
#include "Race/MXProgress.h"
#include "AI/MXAIBrain.h"
#include "Core/MXTuning.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MXTest
{
	/** Flat track with optional pieces for focused tests. */
	FMXTrackModel MakeTrack(TArray<FMXSegment> Pieces, float Lap = 500.f, int32 Laps = 1)
	{
		FMXTrackDefinition D;
		D.Id = TEXT("test");
		D.Name = TEXT("Test");
		D.Laps = Laps;
		D.LapLengthM = Lap;
		D.Segments = MoveTemp(Pieces);
		FMXSegment Fin = FMXObstacleLibrary::MakeDefault(EMXObstacleType::FinishDeck, Lap - 20.f, MXTuning::Style());
		Fin.Id = TEXT("FIN");
		D.Segments.Add(Fin);
		D.EnsureIds(TEXT("T"));
		FMXTrackModel M;
		M.Build(D, EMXLayoutVariant::Main, Laps, MXTuning::Style());
		return M;
	}

	FMXSegment Piece(EMXObstacleType T, float S, int32 Mask = -1)
	{
		FMXSegment X = FMXObstacleLibrary::MakeDefault(T, S, MXTuning::Style());
		if (Mask > 0)
		{
			X.LaneMask = Mask;
		}
		return X;
	}

	/** Runs a bike at a held input until a condition or timeout. Returns seconds simulated. */
	float Run(FMXBikeState& St, const FMXTrackModel& M, const FMXBikeInput& In, float MaxSeconds, TFunctionRef<bool(const FMXBikeState&)> Until)
	{
		const UMXBikeTuning& T = MXTuning::Bike();
		float Time = 0.f;
		while (Time < MaxSeconds)
		{
			FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
			Time += FMXBikeSim::FixedDt;
			if (Until(St))
			{
				break;
			}
		}
		return Time;
	}

	/** Launches off a medium ramp at full speed holding a pitch input during the flight. */
	struct FJump
	{
		float ApexZ = 0.f;
		float Distance = 0.f;
		float AirTime = 0.f;
		float LandSpeed = 0.f;
		EMXLandingGrade Grade = EMXLandingGrade::None;
	};
	FJump Jump(float AirPitchInput, float SpeedFrac = 1.f)
	{
		const UMXBikeTuning& T = MXTuning::Bike();
		FMXTrackModel M = MakeTrack({Piece(EMXObstacleType::RampMedium, 100.f)}, 400.f);
		FMXBikeState St;
		FMXBikeSim::Spawn(St, 60.f, M.LaneCenterY(1), M);
		St.Speed = T.MaxSpeedTurbo * SpeedFrac;
		FMXBikeInput Drive;
		Drive.Throttle = SpeedFrac >= 1.f ? 1.f : 0.f;
		Drive.bTurbo = SpeedFrac >= 1.f;
		Run(St, M, Drive, 5.f, [](const FMXBikeState& S) { return S.IsAirborne(); });
		FJump J;
		const float LaunchS = St.S;
		FMXBikeInput Air;
		Air.Pitch = AirPitchInput;
		Run(St, M, Air, 6.f, [&J](const FMXBikeState& S) { J.ApexZ = FMath::Max(J.ApexZ, S.Z); return !S.IsAirborne(); });
		J.Distance = St.S - LaunchS;
		J.AirTime = St.AirTime;
		J.LandSpeed = St.Speed;
		J.Grade = St.LastLanding;
		return J;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXHeatTest, "HeatlineMX.Bike.Heat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXHeatTest::RunTest(const FString& Parameters)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	// Turbo from cold overheats in ~6.8 s (NES: 24 units x 17.07 frames).
	float Heat = 0.f;
	float Time = 0.f;
	while (!FMXBikeSim::UpdateHeat(Heat, 1.f, true, 1.f / 120.f, T) && Time < 20.f)
	{
		Time += 1.f / 120.f;
	}
	TestTrue(FString::Printf(TEXT("turbo overheats in %.2f s (expected ~6.8)"), Time), FMath::Abs(Time - 6.8f) < 0.25f);
	// Normal throttle settles at 37.5 % and never overheats.
	Heat = 0.f;
	bool bOver = false;
	for (int32 i = 0; i < 120 * 60; ++i)
	{
		bOver |= FMXBikeSim::UpdateHeat(Heat, 1.f, false, 1.f / 120.f, T);
	}
	TestFalse(TEXT("normal throttle never overheats"), bOver);
	TestTrue(FString::Printf(TEXT("normal throttle equilibrium %.1f"), Heat), FMath::IsNearlyEqual(Heat, T.HeatEquilibriumNormal, 0.5f));
	// Cooling above equilibrium ~10.7 %/s.
	Heat = 80.f;
	for (int32 i = 0; i < 120; ++i)
	{
		FMXBikeSim::UpdateHeat(Heat, 0.f, false, 1.f / 120.f, T);
	}
	TestTrue(FString::Printf(TEXT("cooling rate (80 -> %.1f after 1 s)"), Heat), FMath::IsNearlyEqual(Heat, 80.f - T.CoolRate, 0.3f));

	// On a track: overheat stalls; stall ends with heat reset; cool strip resets heat on the ground only.
	FMXTrackModel M = MXTest::MakeTrack({MXTest::Piece(EMXObstacleType::CoolStrip, 150.f, MX::MaskFromLanes({2}))}, 600.f);
	FMXBikeState St;
	FMXBikeSim::Spawn(St, 0.f, M.LaneCenterY(1), M);
	St.Heat = 90.f;
	FMXBikeInput In;
	In.Throttle = 1.f;
	MXTest::Run(St, M, In, 20.f, [](const FMXBikeState& S) { return S.S > 160.f; });
	TestTrue(TEXT("cool strip hit"), St.CoolStripHits == 1);
	TestTrue(FString::Printf(TEXT("heat after cool strip %.1f"), St.Heat), St.Heat < 10.f);
	FMXBikeSim::Spawn(St, 0.f, M.LaneCenterY(0), M);
	In.bTurbo = true;
	MXTest::Run(St, M, In, 20.f, [](const FMXBikeState& S) { return S.Phase == EMXBikePhase::Stalled; });
	TestEqual(TEXT("continuous turbo stalls the engine"), (int32)St.Phase, (int32)EMXBikePhase::Stalled);
	const float StallStart = St.SimTime;
	MXTest::Run(St, M, In, 20.f, [](const FMXBikeState& S) { return S.Phase != EMXBikePhase::Stalled; });
	TestTrue(FString::Printf(TEXT("stall lasts %.2f s"), St.SimTime - StallStart), FMath::IsNearlyEqual(St.SimTime - StallStart, T.StallDuration, 0.05f));
	TestTrue(TEXT("heat reset after stall"), St.Heat <= T.HeatAfterStall + 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXLandingTest, "HeatlineMX.Bike.Landing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXLandingTest::RunTest(const FString& Parameters)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	TestEqual(TEXT("level on flat = perfect"), (int32)FMXBikeSim::ClassifyLanding(0.f, 0.f, T), (int32)EMXLandingGrade::Perfect);
	TestEqual(TEXT("+15 on flat = clean"), (int32)FMXBikeSim::ClassifyLanding(15.f, 0.f, T), (int32)EMXLandingGrade::Clean);
	TestEqual(TEXT("+40 on flat = wobble"), (int32)FMXBikeSim::ClassifyLanding(40.f, 0.f, T), (int32)EMXLandingGrade::Wobble);
	TestEqual(TEXT("-45 on flat = crash"), (int32)FMXBikeSim::ClassifyLanding(-45.f, 0.f, T), (int32)EMXLandingGrade::Crash);
	TestEqual(TEXT("nose-first into an up-face = crash"), (int32)FMXBikeSim::ClassifyLanding(-30.f, 30.f, T), (int32)EMXLandingGrade::Crash);
	TestEqual(TEXT("nose high on a down-face tolerated"), (int32)FMXBikeSim::ClassifyLanding(52.f, -30.f, T), (int32)EMXLandingGrade::Wobble);

	// Consistency: identical inputs give identical landings (deterministic fixed step).
	const MXTest::FJump A = MXTest::Jump(0.f);
	for (int32 i = 0; i < 10; ++i)
	{
		const MXTest::FJump B = MXTest::Jump(0.f);
		TestTrue(TEXT("deterministic landing distance"), FMath::IsNearlyEqual(A.Distance, B.Distance, 1e-4f));
		TestEqual(TEXT("deterministic landing grade"), (int32)A.Grade, (int32)B.Grade);
	}
	AddInfo(FString::Printf(TEXT("neutral medium-ramp jump at turbo speed: %.1f m, apex %.2f m, %.2f s, grade %d"), A.Distance, A.ApexZ, A.AirTime, (int32)A.Grade));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXPitchTrajectoryTest, "HeatlineMX.Bike.PitchChangesTrajectory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXPitchTrajectoryTest::RunTest(const FString& Parameters)
{
	// The explicit design decision: pitch INPUT changes the flight (not just the visual angle).
	const MXTest::FJump Up = MXTest::Jump(1.f);
	const MXTest::FJump Neutral = MXTest::Jump(0.f);
	const MXTest::FJump Down = MXTest::Jump(-1.f);
	AddInfo(FString::Printf(TEXT("nose-up   apex %.2f m  dist %.1f m  air %.2f s"), Up.ApexZ, Up.Distance, Up.AirTime));
	AddInfo(FString::Printf(TEXT("neutral   apex %.2f m  dist %.1f m  air %.2f s"), Neutral.ApexZ, Neutral.Distance, Neutral.AirTime));
	AddInfo(FString::Printf(TEXT("nose-down apex %.2f m  dist %.1f m  air %.2f s"), Down.ApexZ, Down.Distance, Down.AirTime));
	TestTrue(TEXT("nose-up flies higher than neutral"), Up.ApexZ > Neutral.ApexZ + 0.3f);
	TestTrue(TEXT("nose-up hangs longer"), Up.AirTime > Neutral.AirTime);
	TestTrue(TEXT("nose-down flies further than neutral"), Down.Distance > Neutral.Distance + 1.f);
	TestTrue(TEXT("nose-down flies further than nose-up"), Down.Distance > Up.Distance + 1.f);
	TestTrue(TEXT("nose-up is lower speed at landing than nose-down"), Up.LandSpeed <= Down.LandSpeed + 0.01f || Up.Grade == EMXLandingGrade::Crash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXObstacleTest, "HeatlineMX.Track.Obstacles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXObstacleTest::RunTest(const FString& Parameters)
{
	const UMXTrackStyle& Style = MXTuning::Style();
	FMXTrackModel M = MXTest::MakeTrack({
		MXTest::Piece(EMXObstacleType::RampLarge, 50.f),
		MXTest::Piece(EMXObstacleType::Mud, 100.f, MX::MaskFromLanes({1, 3})),
		MXTest::Piece(EMXObstacleType::Barrier, 130.f, MX::MaskFromLanes({3, 4})),
		MXTest::Piece(EMXObstacleType::PlatformDeck, 170.f),
		MXTest::Piece(EMXObstacleType::Grass, 250.f, MX::MaskFromLanes({1, 2})),
	}, 400.f);
	const float LargeLen = FMXObstacleLibrary::ComputeLength(EMXObstacleType::RampLarge, {}, 0.f, Style);
	TestTrue(TEXT("large ramp peak ~3 m"), FMath::IsNearlyEqual(M.Height(50.f + LargeLen * 0.5f, 0.f), 5.f * Style.MetersPerRow, 0.05f));
	TestTrue(TEXT("flat before ramp"), M.Height(49.f, 0.f) == 0.f);
	TestTrue(TEXT("up-slope positive"), M.SlopeDeg(52.f, 0.f) > 20.f);
	TestEqual(TEXT("mud in lane 1"), (int32)M.SurfaceAt(101.f, M.LaneCenterY(0)), (int32)EMXSurface::Mud);
	TestEqual(TEXT("no mud in lane 2"), (int32)M.SurfaceAt(101.f, M.LaneCenterY(1)), (int32)EMXSurface::Dirt);
	TestEqual(TEXT("mud in lane 3"), (int32)M.SurfaceAt(101.f, M.LaneCenterY(2)), (int32)EMXSurface::Mud);
	FMXBarrierHit Hit;
	TestTrue(TEXT("barrier in lane 4"), M.FindBarrierCrossing(129.f, 132.f, M.LaneCenterY(3), Hit));
	TestFalse(TEXT("no barrier in lane 1"), M.FindBarrierCrossing(129.f, 132.f, M.LaneCenterY(0), Hit));
	// Platform: deck continues over lanes 1-2; lanes 3-4 drop back to the ground.
	const float Deck = 170.f + 15.f * Style.MetersPerColumn;
	TestTrue(TEXT("deck high in lane 1"), M.Height(Deck, M.LaneCenterY(0)) > 3.f);
	TestTrue(TEXT("ground in lane 4 under the deck"), M.Height(Deck, M.LaneCenterY(3)) < 0.05f);
	TestEqual(TEXT("grass lanes 1-2"), (int32)M.SurfaceAt(255.f, M.LaneCenterY(1)), (int32)EMXSurface::Grass);
	TestEqual(TEXT("grass not in lane 3"), (int32)M.SurfaceAt(255.f, M.LaneCenterY(2)), (int32)EMXSurface::Dirt);
	TestEqual(TEXT("verge outside lanes"), (int32)M.SurfaceAt(20.f, M.TrackHalfWidth() - 0.2f), (int32)EMXSurface::Verge);

	// Barrier rule: fast with the front down crashes; a wheelie hops it.
	const UMXBikeTuning& T = MXTuning::Bike();
	FMXBikeState St;
	FMXBikeSim::Spawn(St, 120.f, M.LaneCenterY(3), M);
	St.Speed = T.MaxSpeedTurbo;
	FMXBikeInput In;
	In.Throttle = 1.f;
	In.bTurbo = true;
	MXTest::Run(St, M, In, 3.f, [](const FMXBikeState& S) { return S.S > 132.f || !S.IsRiding(); });
	TestEqual(TEXT("barrier crash at speed"), (int32)St.CrashCause, (int32)EMXCrashCause::Barrier);
	FMXBikeSim::Spawn(St, 118.f, M.LaneCenterY(3), M);
	St.Speed = T.MaxSpeedTurbo;
	In.Pitch = 1.f;
	MXTest::Run(St, M, In, 3.f, [](const FMXBikeState& S) { return S.S > 133.f || !S.IsRiding(); });
	TestTrue(TEXT("wheelie clears the barrier"), St.IsRiding() && St.S > 132.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXCheckpointTest, "HeatlineMX.Race.Checkpoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXCheckpointTest::RunTest(const FString& Parameters)
{
	FMXTrackModel M = MXTest::MakeTrack({MXTest::Piece(EMXObstacleType::RampSmall, 100.f)}, 400.f, 3);
	const TArray<FMXGate>& Gates = M.GetGates();
	TestTrue(TEXT("gates exist"), Gates.Num() >= 6);
	int32 LapLines = 0;
	for (int32 i = 0; i < Gates.Num(); ++i)
	{
		LapLines += Gates[i].bLapLine ? 1 : 0;
		if (i > 0)
		{
			TestTrue(TEXT("gates strictly ordered"), Gates[i].S > Gates[i - 1].S);
		}
	}
	TestEqual(TEXT("one lap line per lap"), LapLines, 3);
	TestTrue(TEXT("final gate is the race finish"), FMath::IsNearlyEqual(Gates.Last().S, M.RaceFinishS()));
	// Recovery never moves forward past the crash point.
	for (float S = 5.f; S < M.RaceFinishS(); S += 7.3f)
	{
		const float R = M.FindRecoveryS(S, 0.f, 40.f);
		if (R > S + 1e-3f)
		{
			AddError(FString::Printf(TEXT("recovery at %.2f is ahead of crash at %.2f"), R, S));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXProgressTest, "HeatlineMX.Race.Progress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXProgressTest::RunTest(const FString& Parameters)
{
	// 2 laps of 400 m: gates about every 60 m, lap lines at 400 and 800.
	FMXTrackModel M = MXTest::MakeTrack({}, 400.f, 2);
	const TArray<FMXGate>& G = M.GetGates();
	const float Finish = M.RaceFinishS();
	auto Ride = [&](FMXRacerProgress& P, float& S, float To, float& Time, int32& Counter)
	{
		while (S < To - 1e-4f && !P.bFinished)
		{
			const float Prev = S;
			S = FMath::Min(To, S + 0.25f);
			Time += FMXBikeSim::FixedDt;
			MXProgress::Advance(P, G, 2, Finish, Prev, S, MXProgress::IsValidMove(Prev, S, false), Time, Counter);
		}
	};
	{
		FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
		Ride(P, S, Finish + 5.f, Time, Counter);
		TestTrue(TEXT("clean ride finishes with 2 laps in order"), P.bFinished && P.LapsCompleted == 2 && P.LapTimes.Num() == 2
			&& P.FinishOrder == 1 && P.NextGate == G.Num());
	}
	{
		// A forward teleport is not movement: nothing is granted and the skipped gate can't be crossed from ahead.
		FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
		Ride(P, S, 50.f, Time, Counter);
		const int32 Before = P.NextGate;
		const float Prev = S;
		S += 120.f;
		const FMXProgressStep Step = MXProgress::Advance(P, G, 2, Finish, Prev, S, MXProgress::IsValidMove(Prev, S, false), Time, Counter);
		TestTrue(TEXT("teleport ignored"), Step.bIgnoredJump && P.NextGate == Before && P.ValidS <= G[Before].S + 1e-3f);
		Ride(P, S, Finish + 5.f, Time, Counter);
		TestTrue(TEXT("teleport can't finish"), !P.bFinished && P.LapsCompleted == 0);
	}
	{
		// Recovery placement behind a crossed gate: crossing it again doesn't count twice.
		FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
		Ride(P, S, G[2].S + 3.f, Time, Counter);
		const int32 Before = P.NextGate;
		const float Prev = S;
		S = G[2].S - 6.f;
		MXProgress::Advance(P, G, 2, Finish, Prev, S, MXProgress::IsValidMove(Prev, S, true), Time, Counter);
		TestEqual(TEXT("recovery keeps gate state"), P.NextGate, Before);
		Ride(P, S, Finish + 5.f, Time, Counter);
		TestTrue(TEXT("laps counted once after a recovery"), P.bFinished && P.LapsCompleted == 2 && P.LapTimes.Num() == 2);
	}
	{
		// Bumped back across the lap line, then across it again: still one lap.
		FMXRacerProgress P; float S = -2.f, Time = 0.f; int32 Counter = 0;
		int32 LapGate = 0;
		while (!G[LapGate].bLapLine)
		{
			++LapGate;
		}
		Ride(P, S, G[LapGate].S + 0.2f, Time, Counter);
		TestEqual(TEXT("lap 1 counted"), P.LapsCompleted, 1);
		const float Prev = S;
		S = G[LapGate].S - 0.4f;
		const FMXProgressStep Back = MXProgress::Advance(P, G, 2, Finish, Prev, S, MXProgress::IsValidMove(Prev, S, false), Time, Counter);
		TestTrue(TEXT("un-crossing the lap line un-counts the lap"), Back.bBackwardCrossing && P.LapsCompleted == 0 && P.LapTimes.Num() == 0);
		Ride(P, S, G[LapGate].S + 5.f, Time, Counter);
		TestEqual(TEXT("re-crossing counts once"), P.LapsCompleted, 1);
		Ride(P, S, Finish + 5.f, Time, Counter);
		TestTrue(TEXT("finish after the bump-back"), P.bFinished && P.LapsCompleted == 2 && P.LapTimes.Num() == 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXContactTest, "HeatlineMX.Race.Contact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXContactTest::RunTest(const FString& Parameters)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	FMXTrackModel M = MXTest::MakeTrack({}, 400.f);
	auto Make = [&M](float S, float Y, float Speed)
	{
		FMXBikeState St;
		FMXBikeSim::Spawn(St, S, Y, M);
		St.Speed = Speed;
		return St;
	};
	TArray<FMXContactResult> R;
	// Rear-end at high closing speed: the rear rider crashes (NES rule).
	FMXBikeState Front = Make(101.5f, M.LaneCenterY(1), 15.f);
	FMXBikeState Rear = Make(100.f, M.LaneCenterY(1), 25.f);
	TArray<FMXBikeState*> Pair = {&Rear, &Front};
	MXContact::Resolve(Pair, M, T, R);
	TestEqual(TEXT("rear rider crashes"), (int32)Rear.Phase, (int32)EMXBikePhase::Crashed);
	TestEqual(TEXT("front rider keeps riding"), (int32)Front.Phase, (int32)EMXBikePhase::Grounded);
	// Cut-in: the front rider just changed lanes -> wobble only.
	Front = Make(101.5f, M.LaneCenterY(1), 15.f);
	Rear = Make(100.f, M.LaneCenterY(1), 25.f);
	Front.LastLateralTime = 0.1f;
	R.Reset();
	MXContact::Resolve(Pair, M, T, R);
	TestTrue(TEXT("cut-in never crashes the victim"), Rear.IsRiding() && R.Num() == 1 && R[0].Outcome == EMXContactOutcome::CutOff);
	// Either rider only just landed: nobody could react, so it's a bump and the rear rider is held behind.
	for (int32 Who = 0; Who < 2; ++Who)
	{
		Front = Make(101.5f, M.LaneCenterY(1), 15.f);
		Rear = Make(100.f, M.LaneCenterY(1), 25.f);
		FMXBikeState& Landed = Who ? Rear : Front;
		Landed.LastLandingTime = Landed.SimTime - 0.2f;
		R.Reset();
		MXContact::Resolve(Pair, M, T, R);
		TestTrue(Who ? TEXT("rear rider just landed: bump, not a crash") : TEXT("front rider just landed: bump, not a crash"),
			Rear.IsRiding() && R.Num() == 1 && R[0].Outcome == EMXContactOutcome::LandingBump
			&& Rear.S <= Front.S - T.BikeContactLength + 1e-3f && Rear.Speed <= Front.Speed);
	}
	// Cooldown: a rider can't be contact-crashed twice in 6 s.
	Front = Make(101.5f, M.LaneCenterY(1), 15.f);
	Rear = Make(100.f, M.LaneCenterY(1), 25.f);
	Rear.ContactCooldown = 3.f;
	R.Reset();
	MXContact::Resolve(Pair, M, T, R);
	TestTrue(TEXT("cooldown prevents a crash chain"), Rear.IsRiding());
	// Side by side: pushed apart, no crash.
	Front = Make(100.3f, M.LaneCenterY(1) + 0.4f, 20.f);
	Rear = Make(100.f, M.LaneCenterY(1), 20.f);
	R.Reset();
	MXContact::Resolve(Pair, M, T, R);
	TestTrue(TEXT("side contact is never a crash"), Rear.IsRiding() && Front.IsRiding() && FMath::Abs(Front.Y - Rear.Y) >= T.BikeContactWidth - 0.01f);
	// Ghosted riders don't collide.
	Front = Make(101.5f, M.LaneCenterY(1), 15.f);
	Rear = Make(100.f, M.LaneCenterY(1), 25.f);
	Rear.GhostTimer = 1.f;
	R.Reset();
	MXContact::Resolve(Pair, M, T, R);
	TestEqual(TEXT("ghosted rider passes through"), R.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXCoursesTest, "HeatlineMX.Track.NESCourses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXCoursesTest::RunTest(const FString& Parameters)
{
	// Manifest lap lengths (main / challenge) in NES columns x 1.25 m.
	const float Main[5] = {706, 638, 750, 797, 674};
	const float Challenge[5] = {667, 604, 730, 746, 650};
	for (int32 T = 1; T <= 5; ++T)
	{
		FMXTrackDefinition D;
		FString Err;
		const bool bOk = MXTrackIO::LoadFile(FPaths::ProjectContentDir() / FString::Printf(TEXT("Courses/nes_t%d.json"), T), D, Err);
		TestTrue(FString::Printf(TEXT("course %d loads"), T), bOk);
		if (!bOk)
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("course %d laps"), T), D.Laps, 2);
		TestTrue(FString::Printf(TEXT("course %d main lap length"), T), FMath::IsNearlyEqual(D.LapLengthFor(EMXLayoutVariant::Main), Main[T - 1] * 1.25f, 0.1f));
		TestTrue(FString::Printf(TEXT("course %d challenge lap length"), T), FMath::IsNearlyEqual(D.LapLengthFor(EMXLayoutVariant::Challenge), Challenge[T - 1] * 1.25f, 0.1f));
		TestTrue(FString::Printf(TEXT("course %d ids stable"), T), D.Segments.Num() > 0 && D.Segments[0].Id.StartsWith(FString::Printf(TEXT("T%d.O"), T)));
		FMXValidationResult V = FMXTrackValidator::Validate(D, MXTuning::Style());
		for (const FMXTrackIssue& I : V.Issues)
		{
			if (I.Severity == EMXIssueSeverity::Error)
			{
				AddError(FString::Printf(TEXT("course %d: %s (%s)"), T, *I.Message, *I.SegmentId));
			}
		}
		FMXTrackModel Model;
		Model.Build(D, EMXLayoutVariant::Challenge, 2, MXTuning::Style());
		TestTrue(FString::Printf(TEXT("course %d finish deck"), T), Model.HasFinishDeck());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXValidatorTest, "HeatlineMX.Track.Validator", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXValidatorTest::RunTest(const FString& Parameters)
{
	const UMXTrackStyle& Style = MXTuning::Style();
	FMXTrackDefinition D;
	D.LapLengthM = 300.f;
	D.Laps = 2;
	D.Segments.Add(FMXObstacleLibrary::MakeDefault(EMXObstacleType::RampLarge, 5.f, Style));    // start zone
	D.Segments.Add(FMXObstacleLibrary::MakeDefault(EMXObstacleType::RampMedium, 60.f, Style));
	D.Segments.Add(FMXObstacleLibrary::MakeDefault(EMXObstacleType::RampSmall, 62.f, Style));   // overlap
	D.Segments.Add(FMXObstacleLibrary::MakeDefault(EMXObstacleType::Mountain, 290.f, Style));   // past lap end
	D.EnsureIds(TEXT("V"));
	FMXValidationResult R = FMXTrackValidator::Validate(D, Style);
	TestTrue(TEXT("errors reported"), R.Count(EMXIssueSeverity::Error) >= 4);
	TestTrue(TEXT("auto-fix adds a finish"), FMXTrackValidator::EnsureFinishDeck(D, Style));
	// Surface pieces can share a stretch in different lanes.
	FMXTrackDefinition Ok;
	Ok.LapLengthM = 300.f;
	Ok.Segments.Add(FMXObstacleLibrary::MakeDefault(EMXObstacleType::Mud, 50.f, Style));
	FMXSegment Cool = FMXObstacleLibrary::MakeDefault(EMXObstacleType::CoolStrip, 50.f, Style);
	Cool.LaneMask = MX::MaskFromLanes({2});
	Ok.Segments.Add(Cool);
	FMXTrackValidator::EnsureFinishDeck(Ok, Style);
	Ok.EnsureIds(TEXT("K"));
	R = FMXTrackValidator::Validate(Ok, Style);
	TestFalse(TEXT("lane-separated surfaces are valid"), R.HasErrors());
	// JSON round trip.
	FMXTrackDefinition Back;
	FString Err;
	TestTrue(TEXT("json parse"), MXTrackIO::FromJsonString(MXTrackIO::ToJsonString(Ok), Back, Err));
	TestEqual(TEXT("json round trip pieces"), Back.Segments.Num(), Ok.Segments.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXAITest, "HeatlineMX.AI.CompletesCourse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXAITest::RunTest(const FString& Parameters)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	FMXTrackDefinition D;
	FString Err;
	if (!MXTrackIO::LoadFile(FPaths::ProjectContentDir() / TEXT("Courses/nes_t1.json"), D, Err))
	{
		AddError(Err);
		return false;
	}
	FMXTrackModel M;
	M.Build(D, EMXLayoutVariant::Main, 1, MXTuning::Style());
	const TArray<FMXAIOtherBike> None;
	float Times[3] = {0, 0, 0};
	int32 Crashes[3] = {0, 0, 0};
	for (int32 Diff = 0; Diff < 3; ++Diff)
	{
		const FMXAISkill& Skill = MXTuning::AI().ForDifficulty((EMXAIDifficulty)Diff);
		FMXBikeState St;
		FMXBikeSim::Spawn(St, -2.f, M.LaneCenterY(1), M);
		FMXAIMemory Mem;
		Mem.Init(99, false);
		float Time = 0.f;
		while (St.S < M.RaceFinishS() && Time < 200.f)
		{
			const FMXBikeInput In = FMXAIBrain::Think(St, Mem, M, T, Skill, None, FMXBikeSim::FixedDt);
			FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
			Time += FMXBikeSim::FixedDt;
		}
		Times[Diff] = Time;
		Crashes[Diff] = St.Crashes;
		AddInfo(FString::Printf(TEXT("AI %d: lap in %.1f s, %d crashes, %d overheats, %d perfect landings, %d cool strips"),
			Diff, Time, St.Crashes, St.Overheats, St.PerfectLandings, St.CoolStripHits));
		TestTrue(FString::Printf(TEXT("AI difficulty %d finishes"), Diff), St.S >= M.RaceFinishS());
	}
	TestTrue(TEXT("hard AI is faster than easy AI (skill, not speed boosts)"), Times[2] < Times[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMXAITurboTest, "HeatlineMX.AI.TurboLikeAPerson", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMXAITurboTest::RunTest(const FString& Parameters)
{
	// Easy and Medium riders read the heat gauge the way people do: turbo in bursts, and they (almost) never let
	// off the gas in the air to cool the engine. Only Hard uses that expert trick on every jump.
	const UMXBikeTuning& T = MXTuning::Bike();
	const TArray<FMXAIOtherBike> None;
	for (int32 Diff = 0; Diff < 3; ++Diff)
	{
		const FMXAISkill& Skill = MXTuning::AI().ForDifficulty((EMXAIDifficulty)Diff);
		float Ground = 0.f, TurboGround = 0.f, Air = 0.f, CoastAir = 0.f;
		int32 Overheats = 0;
		for (int32 Course = 1; Course <= 5; ++Course)
		{
			FMXTrackDefinition D;
			FString Err;
			if (!MXTrackIO::LoadFile(FPaths::ProjectContentDir() / FString::Printf(TEXT("Courses/nes_t%d.json"), Course), D, Err))
			{
				AddError(Err);
				return false;
			}
			FMXTrackModel M;
			M.Build(D, EMXLayoutVariant::Main, D.Laps, MXTuning::Style());
			for (int32 Seed = 0; Seed < 2; ++Seed)
			{
				FMXBikeState St;
				FMXBikeSim::Spawn(St, -2.f, M.LaneCenterY(Seed), M);
				FMXAIMemory Mem;
				Mem.Init(300 + Course * 10 + Seed, false);
				float Time = 0.f;
				while (St.S < M.RaceFinishS() && Time < 300.f)
				{
					const FMXBikeInput In = FMXAIBrain::Think(St, Mem, M, T, Skill, None, FMXBikeSim::FixedDt);
					if (St.Phase == EMXBikePhase::Grounded)
					{
						Ground += FMXBikeSim::FixedDt;
						TurboGround += In.bTurbo ? FMXBikeSim::FixedDt : 0.f;
					}
					else if (St.Phase == EMXBikePhase::Airborne)
					{
						Air += FMXBikeSim::FixedDt;
						CoastAir += In.Throttle < 0.1f ? FMXBikeSim::FixedDt : 0.f;
					}
					FMXBikeSim::Step(St, In, M, T, FMXBikeSim::FixedDt, false);
					Time += FMXBikeSim::FixedDt;
				}
				Overheats += St.Overheats;
			}
		}
		const float TurboShare = TurboGround / FMath::Max(0.01f, Ground);
		const float CoastShare = CoastAir / FMath::Max(0.01f, Air);
		AddInfo(FString::Printf(TEXT("AI %d: turbo on %.0f%% of ground time, off the gas %.0f%% of air time, %d overheats in 10 races"),
			Diff, TurboShare * 100.f, CoastShare * 100.f, Overheats));
		if (Diff == 0)
		{
			TestTrue(TEXT("easy AI uses turbo in bursts"), TurboShare < 0.55f);
			TestTrue(TEXT("easy AI keeps the gas on in the air"), CoastShare < 0.01f);
			TestTrue(TEXT("easy AI overheats sometimes"), Overheats > 0);
		}
		else if (Diff == 1)
		{
			TestTrue(TEXT("medium AI uses turbo in bursts"), TurboShare < 0.7f);
			TestTrue(TEXT("medium AI rarely cools in the air"), CoastShare < 0.3f);
		}
		else
		{
			TestTrue(TEXT("hard AI cools in every jump"), CoastShare > 0.9f);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
