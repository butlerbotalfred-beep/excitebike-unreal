#include "AI/MXAIBrain.h"
#include "Track/MXTrackModel.h"

namespace
{
	/** Seconds until this rider's front wheel reaches a slower solid rider ahead in the band around LaneY. */
	float TimeToRearEnd(const FMXBikeState& St, float LaneY, const UMXBikeTuning& T, const TArray<FMXAIOtherBike>& Others, bool& bOutAlongside)
	{
		const float Band = T.BikeContactWidth + 0.5f;
		const float V = St.ForwardSpeed();
		float Ttc = TNumericLimits<float>::Max();
		bOutAlongside = false;
		for (const FMXAIOtherBike& O : Others)
		{
			if (!O.bRiding || !O.bSolid || FMath::Abs(O.Y - LaneY) > Band)
			{
				continue;
			}
			const float Gap = O.S - St.S - T.BikeContactLength;
			if (Gap < -2.f * T.BikeContactLength || Gap > 40.f)
			{
				continue;
			}
			if (Gap < 0.f)
			{
				bOutAlongside = true; // level with us in that band
				continue;
			}
			const float Closing = V - O.Speed;
			if (Closing > T.RearEndCrashSpeed * 0.5f)
			{
				Ttc = FMath::Min(Ttc, Gap / Closing);
			}
		}
		return Ttc;
	}

	/**
	 * Don't ride into the back of a slower rider (the rear rider is at fault): move over when a
	 * neighbouring lane is clear, otherwise ease off. The warning time grows with reaction delay.
	 * Returns true when the rider should ease off.
	 */
	bool AvoidRiderAhead(const FMXBikeState& St, FMXAIMemory& Mem, const FMXTrackModel& Track, const UMXBikeTuning& T,
		const FMXAISkill& Skill, const TArray<FMXAIOtherBike>& Others)
	{
		const float Warning = 0.45f + Skill.ReactionDelay * 1.5f;
		bool bAlongside = false;
		const float HereTtc = FMath::Min(TimeToRearEnd(St, St.Y, T, Others, bAlongside),
			TimeToRearEnd(St, Track.LaneCenterY(St.TargetLane), T, Others, bAlongside));
		if (HereTtc > Warning)
		{
			return false;
		}
		const int32 Cur = Track.NearestLane(St.Y);
		const int32 Pref = Mem.PlanLane > Cur ? 1 : -1;
		for (const int32 L : {Cur + Pref, Cur - Pref})
		{
			if (L < 0 || L >= MX::NumLanes)
			{
				continue;
			}
			bool bBlockedAlongside = false;
			const float Ttc = TimeToRearEnd(St, Track.LaneCenterY(L), T, Others, bBlockedAlongside);
			if (!bBlockedAlongside && Ttc > Warning * 1.5f)
			{
				Mem.PlanLane = L;
				return false;
			}
		}
		return true;
	}

	/** Turbo from the heat gauge: press up to the rider's limit, then rest until it has cooled by the resume margin. */
	bool WantsTurbo(const FMXBikeState& St, FMXAIMemory& Mem, const FMXTrackModel& Track, const FMXAISkill& Skill,
		bool bOverTarget, float Dt)
	{
		const float V = St.ForwardSpeed();
		float HeatLimit = Skill.TurboHeatLimit;
		if (Skill.bPlansCoolStrips)
		{
			// Push harder when a cool strip in the planned lane is coming up soon.
			TArray<const FMXPlacedPiece*> Ahead;
			Track.PiecesInRange(St.S, St.S + FMath::Max(20.f, V * 2.5f), Ahead);
			for (const FMXPlacedPiece* P : Ahead)
			{
				for (const FMXSurfaceSpan& Span : P->Geo.Surfaces)
				{
					if (Span.Surface == EMXSurface::Cool && MX::LaneInMask(Span.LaneMask, Mem.PlanLane))
					{
						HeatLimit = 96.f;
					}
				}
			}
		}
		Mem.TurboRestTimer = FMath::Max(0.f, Mem.TurboRestTimer - Dt);
		if (St.Heat >= HeatLimit)
		{
			if (!Mem.bTurboCooling)
			{
				Mem.TurboRestTimer = Skill.TurboMinRest;
			}
			Mem.bTurboCooling = true;
		}
		else if (St.Heat < HeatLimit - Skill.TurboResumeMargin && Mem.TurboRestTimer <= 0.f)
		{
			Mem.bTurboCooling = false;
		}
		bool bTurbo = !bOverTarget && !Mem.bTurboCooling && St.Heat < HeatLimit;
		if (Mem.Strategy == 2)
		{
			bTurbo = false;
		}
		else if (Mem.Strategy == 1)
		{
			bTurbo = St.Heat < 92.f;
		}
		else if (Mem.Strategy >= 3)
		{
			bTurbo = Mem.Strategy == 3;
		}
		// Sloppier riders sometimes get greedy and ride turbo into the red (they can overheat).
		if (!Mem.bAutopilot && Mem.GreedyTimer <= 0.f && Mem.Rng.FRand() < Skill.GreedyTurboRate * Dt)
		{
			Mem.GreedyTimer = 6.f;
		}
		if (Mem.GreedyTimer > 0.f)
		{
			Mem.GreedyTimer -= Dt;
			bTurbo = !bOverTarget;
		}
		return bTurbo;
	}
}

float FMXAIBrain::LaneCost(int32 Lane, const FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T,
	const FMXAISkill& Skill, const TArray<FMXAIOtherBike>& Others, const FMXAIMemory& Mem)
{
	const int32 Current = Track.NearestLane(St.Y);
	const float MyV = St.ForwardSpeed();
	float Cost = FMath::Abs(Lane - Current) * 0.35f;
	TArray<const FMXPlacedPiece*> Pieces;
	Track.PiecesInRange(St.S, St.S + Skill.LaneHorizon, Pieces);
	for (const FMXPlacedPiece* P : Pieces)
	{
		const float Dist = FMath::Max(0.f, P->S0 - St.S);
		const float W = 1.f / (1.f + Dist / 25.f);
		for (const FMXSurfaceSpan& Span : P->Geo.Surfaces)
		{
			if (!MX::LaneInMask(Span.LaneMask, Lane) || P->S0 + Span.X1 < St.S)
			{
				continue;
			}
			const float Len = Span.X1 - Span.X0;
			switch (Span.Surface)
			{
			case EMXSurface::Mud: Cost += 2.5f * W * FMath::Max(1.f, Len / 3.75f); break;
			case EMXSurface::Grass: Cost += 2.2f * W * FMath::Max(1.f, Len / 12.f); break;
			case EMXSurface::Cool:
			{
				const float Want = Skill.bPlansCoolStrips ? FMath::Max(0.f, St.Heat - 20.f) : FMath::Max(0.f, St.Heat - 45.f);
				Cost -= Want / 100.f * 4.f * W;
				break;
			}
			default: break;
			}
		}
		for (const FMXBarrierSpec& B : P->Geo.Barriers)
		{
			if (MX::LaneInMask(B.LaneMask, Lane) && P->S0 + B.X > St.S)
			{
				Cost += (Skill.bUsesFlightControl ? 0.7f : 2.5f) * W;
				if (Lane != Current && P->S0 + B.X - St.S < FMath::Max(8.f, MyV * 0.6f))
				{
					Cost += 4.f; // swerving in right before one leaves no time to lift the front wheel
				}
			}
		}
		// Raised lanes vs sunken lanes (platform deck): being on the deck avoids the mud underneath.
		if (P->Type == EMXObstacleType::PlatformDeck && Lane >= 2)
		{
			Cost += 0.6f * W;
		}
	}
	for (const FMXAIOtherBike& O : Others)
	{
		if (!O.bRiding)
		{
			continue;
		}
		const float D = O.S - St.S;
		if (Track.NearestLane(O.Y) != Lane)
		{
			continue;
		}
		if (D > -2.5f && D < 2.5f && Lane != Current)
		{
			Cost += 3.f; // someone alongside in that lane: don't swerve into them
		}
		else if (D >= 2.5f && D < 16.f)
		{
			Cost += 1.2f * (1.f - D / 16.f) + (O.Speed < MyV - 1.f ? 1.2f : 0.f);
		}
		if (Mem.bChaser && O.bHuman && D > 3.f && D < 30.f && MyV > O.Speed)
		{
			Cost -= 1.0f; // pursuer: go after the human rider's line
		}
	}
	return Cost;
}

float FMXAIBrain::FlightInput(const FMXBikeState& St, FMXFlightControl& Ctl, const FMXTrackModel& Track, const UMXBikeTuning& T,
	float Reaction, float Dt)
{
	// Re-predict the landing at ~30 Hz; in between, count the cached time down.
	Ctl.PredTimer -= Dt;
	if (Ctl.PredTimer <= 0.f)
	{
		Ctl.PredTimer = 1.f / 30.f;
		Ctl.Pred = FMXBikeSim::PredictFlight(St, Ctl.bAligning ? Ctl.LastInput : Ctl.Strategy, Track, T, 3.f);
	}
	else
	{
		Ctl.Pred.Time -= Dt;
	}
	if (!Ctl.Pred.bLands)
	{
		Ctl.LastInput = Ctl.bAligning ? 0.f : Ctl.Strategy;
		return Ctl.LastInput;
	}
	// Slight nose-up bias: the clean window is wider on that side.
	const float Err = Ctl.Pred.SurfaceDeg + 2.f + Ctl.LandingError - St.Pitch;
	const float AlignTime = FMath::Abs(Err) / FMath::Max(1.f, T.AirPitchRate) + 0.18f + Reaction * 0.5f;
	if (!Ctl.bAligning && (FMath::IsNearlyZero(Ctl.Strategy) || Ctl.Pred.Time < AlignTime))
	{
		Ctl.bAligning = true;
		Ctl.PredTimer = 0.f; // re-aim with the lining-up input from the next step
	}
	Ctl.LastInput = Ctl.bAligning ? FMath::Clamp(Err / 15.f, -1.f, 1.f) : Ctl.Strategy;
	return Ctl.LastInput;
}

float FMXAIBrain::RolloutAirPlan(const FMXBikeState& Start, float Strategy, const FMXTrackModel& Track, const UMXBikeTuning& T,
	float Reaction, float Horizon)
{
	constexpr float CrashPenalty = 150.f; // metres: a crash costs more than any landing can gain
	FMXBikeState St = Start;
	FMXFlightControl Ctl;
	Ctl.Strategy = Strategy;
	const float Dt = FMXBikeSim::FixedDt;
	for (float Time = 0.f; Time < Horizon; Time += Dt)
	{
		FMXBikeInput In;
		switch (St.Phase)
		{
		case EMXBikePhase::Airborne:
			In.Pitch = FlightInput(St, Ctl, Track, T, Reaction, Dt);
			break;
		case EMXBikePhase::Grounded:
			In.Throttle = 1.f;
			Ctl = FMXFlightControl(); // later jumps inside the horizon: plain lining up
			break;
		case EMXBikePhase::Crashed:
		case EMXBikePhase::Recovering:
			return St.S - CrashPenalty;
		default:
			break;
		}
		FMXBikeSim::Step(St, In, Track, T, Dt, false);
	}
	return St.IsRiding() ? St.S : St.S - CrashPenalty;
}

float FMXAIBrain::ChooseAirInput(const FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T, const FMXAISkill& Skill)
{
	// The pitch input changes the flight (NES-style), so every plan is flown with the real physics and the same
	// controller the rider will use, including the switch to lining up for the landing. Plans are compared by
	// the distance reached at a common horizon, which folds landing grade, speed loss and crashes together.
	const FMXFlightPrediction Neutral = FMXBikeSim::PredictFlight(St, 0.f, Track, T, 3.f);
	const float Horizon = FMath::Max(Neutral.bLands ? Neutral.Time : 3.f, 1.2f) + 1.2f;
	float Best = 0.f;
	float BestScore = -TNumericLimits<float>::Max();
	const float Plans[] = {0.f, -1.f, 1.f, -0.5f, 0.5f};
	for (float Plan : Plans)
	{
		// Holding a direction must pay for itself (small bias toward the neutral plan).
		const float Score = RolloutAirPlan(St, Plan, Track, T, Skill.ReactionDelay, Horizon) - FMath::Abs(Plan) * 0.3f;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Plan;
		}
	}
	return Best;
}

FMXBikeInput FMXAIBrain::Think(const FMXBikeState& St, FMXAIMemory& Mem, const FMXTrackModel& Track,
	const UMXBikeTuning& T, const FMXAISkill& Skill, const TArray<FMXAIOtherBike>& Others, float Dt)
{
	FMXBikeInput In;
	In.bLaneTaps = true;
	In.Throttle = 1.f;

	if (St.Phase == EMXBikePhase::Crashed || St.Phase == EMXBikePhase::Recovering)
	{
		In.Throttle = 0.f;
		if (St.Phase == EMXBikePhase::Recovering)
		{
			Mem.MashAccumulator += Skill.MashRate * Dt;
			In.MashPresses = FMath::FloorToInt(Mem.MashAccumulator);
			Mem.MashAccumulator -= In.MashPresses;
		}
		Mem.bWasAirborne = false;
		return In;
	}
	if (St.Phase == EMXBikePhase::Stalled)
	{
		In.Throttle = 0.f;
		Mem.bWasAirborne = false;
		return In;
	}

	const float V = St.ForwardSpeed();
	Mem.ThinkTimer -= Dt;
	if (Mem.ThinkTimer <= 0.f)
	{
		Mem.ThinkTimer = Skill.ReactionDelay * Mem.Rng.FRandRange(0.8f, 1.2f);
		if (St.Phase == EMXBikePhase::Grounded)
		{
			int32 Best = Track.NearestLane(St.Y);
			float BestCost = TNumericLimits<float>::Max();
			for (int32 L = 0; L < MX::NumLanes; ++L)
			{
				const float C = LaneCost(L, St, Track, T, Skill, Others, Mem);
				if (C < BestCost - 0.05f)
				{
					BestCost = C;
					Best = L;
				}
			}
			if (!Mem.bAutopilot && Mem.Rng.FRand() < Skill.MistakeRate * Mem.ThinkTimer * 4.f)
			{
				Best = FMath::Clamp(Best + (Mem.Rng.FRand() < 0.5f ? -1 : 1), 0, MX::NumLanes - 1);
			}
			Mem.PlanLane = Best;
			// No pre-jump speed plan: the air plan chosen by rollouts at take-off handles every NES jump at full
			// speed, and the offline balance check showed slowing for crests cost time without saving landings.
			Mem.TargetSpeed = Mem.Strategy == 2 ? T.MaxSpeedTurbo * 0.72f : T.MaxSpeedTurbo * 1.2f;
		}
	}

	if (St.Phase == EMXBikePhase::Grounded)
	{
		Mem.bWasAirborne = false;
		const bool bEaseOff = AvoidRiderAhead(St, Mem, Track, T, Skill, Others);
		if (St.TargetLane != Mem.PlanLane)
		{
			In.Steer = St.TargetLane < Mem.PlanLane ? 1.f : -1.f;
		}

		const bool bOverTarget = V > Mem.TargetSpeed + 0.5f || bEaseOff;
		In.Throttle = bOverTarget ? 0.f : 1.f;
		In.bTurbo = WantsTurbo(St, Mem, Track, Skill, bOverTarget, Dt);

		// Barriers: wheelie over them (or slow down if the rider can't).
		// The lane being moved into counts too.
		FMXBarrierHit Hit;
		const float Look = FMath::Max(5.f, V * 0.45f);
		const bool bBarrierAhead = Track.FindBarrierCrossing(St.S, St.S + Look, St.Y, Hit)
			|| Track.FindBarrierCrossing(St.S, St.S + Look, Track.LaneCenterY(St.TargetLane), Hit);
		if (bBarrierAhead && V > T.BarrierCrashSpeedFraction * T.MaxSpeedTurbo * 0.9f)
		{
			if (!Mem.bWheelieFor)
			{
				Mem.bWheelieFor = Skill.bUsesFlightControl || Mem.bAutopilot || Mem.Rng.FRand() < 0.6f;
			}
			if (Mem.bWheelieFor)
			{
				Mem.WheelieHold = 0.35f;
			}
			else
			{
				In.Throttle = 0.f;
				In.bTurbo = false;
			}
		}
		else if (Mem.WheelieHold <= 0.f)
		{
			Mem.bWheelieFor = false;
		}
		if (Mem.WheelieHold > 0.f)
		{
			// A partial lift clears the barrier (front-wheel rule) and can't over-rotate like a held full lift.
			Mem.WheelieHold -= Dt;
			In.Pitch = 0.6f;
		}
		return In;
	}

	// ---- Airborne ----
	if (!Mem.bWasAirborne)
	{
		Mem.Flight = FMXFlightControl();
		const float G = (Mem.Rng.FRand() + Mem.Rng.FRand() + Mem.Rng.FRand() - 1.5f) * 1.4f; // ~N(0,1)
		Mem.Flight.LandingError = Mem.bAutopilot ? 0.f : G * Skill.LandingErrorDeg;
		const bool bPlansJump = Skill.bUsesFlightControl && !St.bBounce && (Mem.bAutopilot || Mem.Rng.FRand() < Skill.FlightPlanChance);
		Mem.Flight.Strategy = bPlansJump ? ChooseAirInput(St, Track, T, Skill) : 0.f;
		Mem.bAirCoast = Mem.bAutopilot || Mem.Rng.FRand() < Skill.AirCoastChance;
	}
	Mem.bWasAirborne = true;
	// Off the gas in the air, the engine cools (NES; the TAS uses every jump this way). Riders who don't know the
	// trick keep holding the gas, and turbo while they're in a burst, which keeps heating the engine.
	const bool bWantsTurbo = WantsTurbo(St, Mem, Track, Skill, false, Dt); // keeps the gauge-reading state ticking
	In.Throttle = Mem.bAirCoast ? 0.f : 1.f;
	In.bTurbo = !Mem.bAirCoast && bWantsTurbo;
	In.Pitch = FlightInput(St, Mem.Flight, Track, T, Skill.ReactionDelay, Dt);
	return In;
}
