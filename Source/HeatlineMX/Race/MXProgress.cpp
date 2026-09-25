#include "Race/MXProgress.h"
#include "Track/MXTrackModel.h"

bool MXProgress::IsValidMove(float SPrev, float S, bool bRecoveredThisStep)
{
	return !bRecoveredThisStep && FMath::Abs(S - SPrev) < MaxStepDistance;
}

FMXProgressStep MXProgress::Advance(FMXRacerProgress& P, const TArray<FMXGate>& Gates, int32 Laps, float RaceFinishS,
	float SPrev, float S, bool bValidMove, float RaceTime, int32& FinishCounter)
{
	FMXProgressStep Out;
	if (P.bFinished)
	{
		return Out;
	}
	if (bValidMove)
	{
		// Backwards across the last gate (e.g. bumped back by contact): it must be crossed again.
		if (P.NextGate > 0 && S < SPrev && SPrev >= Gates[P.NextGate - 1].S && S < Gates[P.NextGate - 1].S)
		{
			--P.NextGate;
			++P.InvalidCrossings;
			Out.bBackwardCrossing = true;
			if (Gates[P.NextGate].bLapLine && P.LapsCompleted > 0 && P.LapTimes.Num() > 0)
			{
				// Un-crossing a lap line un-completes that lap, so crossing it again can't count it twice.
				P.LapStartTime -= P.LapTimes.Last();
				P.LapTimes.Pop();
				--P.LapsCompleted;
				P.BestLap = -1.f;
				for (const float Lap : P.LapTimes)
				{
					P.BestLap = P.BestLap < 0.f ? Lap : FMath::Min(P.BestLap, Lap);
				}
			}
		}
		while (P.NextGate < Gates.Num() && SPrev < Gates[P.NextGate].S && S >= Gates[P.NextGate].S)
		{
			const FMXGate& G = Gates[P.NextGate];
			++P.NextGate;
			if (!G.bLapLine)
			{
				continue;
			}
			const float LapTime = RaceTime - P.LapStartTime;
			P.LapTimes.Add(LapTime);
			P.BestLap = P.BestLap < 0.f ? LapTime : FMath::Min(P.BestLap, LapTime);
			P.LapStartTime = RaceTime;
			++P.LapsCompleted;
			++Out.LapsCompleted;
			if (P.LapsCompleted >= Laps)
			{
				P.bFinished = true;
				P.FinishTime = RaceTime;
				P.FinishOrder = ++FinishCounter;
				Out.bFinished = true;
				break;
			}
		}
	}
	else if (S > SPrev + MaxStepDistance)
	{
		// A forward jump that wasn't movement (reset/teleport): ignored.
		++P.InvalidCrossings;
		Out.bIgnoredJump = true;
	}
	// Validated progress can never move past the next uncrossed gate.
	const float Cap = P.NextGate < Gates.Num() ? Gates[P.NextGate].S : RaceFinishS;
	P.ValidS = FMath::Max(P.ValidS, FMath::Min(S, Cap));
	return Out;
}
