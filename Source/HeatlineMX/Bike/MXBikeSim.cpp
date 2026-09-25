#include "Bike/MXBikeSim.h"
#include "Track/MXTrackModel.h"
#include "Core/MXTuning.h"

namespace
{
	FORCEINLINE float Approach(float Value, float Target, float MaxDelta)
	{
		if (Value < Target)
		{
			return FMath::Min(Target, Value + MaxDelta);
		}
		return FMath::Max(Target, Value - MaxDelta);
	}
}

float FMXBikeState::ForwardSpeed() const
{
	switch (Phase)
	{
	case EMXBikePhase::Airborne:
		return VX;
	case EMXBikePhase::Grounded:
	case EMXBikePhase::Stalled:
		return Speed * FMath::Cos(FMath::DegreesToRadians(SlopeDeg));
	case EMXBikePhase::Crashed:
		return Speed;
	default:
		return 0.f;
	}
}

void FMXBikeSim::Spawn(FMXBikeState& St, float S, float Y, const FMXTrackModel& Track)
{
	St = FMXBikeState();
	St.S = S;
	St.Y = Y;
	St.Z = Track.Height(S, Y);
	St.SlopeDeg = Track.SlopeDeg(S, Y);
	St.Pitch = St.SlopeDeg;
	St.TargetLane = Track.NearestLane(Y);
	St.Surface = Track.SurfaceAt(S, Y);
	St.Phase = EMXBikePhase::Grounded;
}

bool FMXBikeSim::UpdateHeat(float& Heat, float Throttle, bool bTurbo, float Dt, const UMXBikeTuning& T)
{
	float Target = T.HeatEquilibriumCoast;
	float Rate = 0.f;
	if (bTurbo)
	{
		// Turbo's equilibrium sits above the overheat threshold: holding it always ends in a stall (NES).
		Target = T.HeatOverheat + 10.f;
		Rate = T.HeatRateTurbo;
	}
	else if (Throttle > 0.1f)
	{
		Target = T.HeatEquilibriumNormal;
		Rate = T.HeatRateNormal;
	}
	const float Before = Heat;
	if (Heat < Target)
	{
		Heat = FMath::Min(Target, Heat + Rate * Dt);
	}
	else if (Heat > Target)
	{
		Heat = FMath::Max(Target, Heat - T.CoolRate * Dt);
	}
	Heat = FMath::Clamp(Heat, 0.f, T.HeatOverheat);
	return Before < T.HeatOverheat && Heat >= T.HeatOverheat;
}

EMXLandingGrade FMXBikeSim::ClassifyLanding(float DeltaDeg, float SurfaceDeg, const UMXBikeTuning& T)
{
	float CrashMin = T.WobbleMin;
	float CrashMax = T.WobbleMax;
	if (SurfaceDeg > T.UpslopeThreshold)
	{
		// Nose-first into an up-face digs in.
		CrashMin = FMath::Max(CrashMin, T.UpslopeNoseDownCrash);
	}
	if (SurfaceDeg < -T.DownslopeThreshold)
	{
		// Rear-wheel-first onto a down-face is survivable further.
		CrashMax = FMath::Max(CrashMax, T.DownslopeNoseUpWobble);
	}
	if (DeltaDeg < CrashMin || DeltaDeg > CrashMax)
	{
		return EMXLandingGrade::Crash;
	}
	if (FMath::Abs(DeltaDeg) <= T.PerfectTolerance)
	{
		return EMXLandingGrade::Perfect;
	}
	if (DeltaDeg >= T.CleanMin && DeltaDeg <= T.CleanMax)
	{
		return EMXLandingGrade::Clean;
	}
	return EMXLandingGrade::Wobble;
}

void FMXBikeSim::Crash(FMXBikeState& St, EMXCrashCause Cause, const UMXBikeTuning& T)
{
	if (!St.IsRiding())
	{
		return;
	}
	St.CrashS = St.S;
	St.CrashSpeed = FMath::Max(0.f, St.ForwardSpeed());
	St.Speed = St.CrashSpeed;
	St.Phase = EMXBikePhase::Crashed;
	St.CrashCause = Cause;
	St.CrashTimer = T.CrashTumbleBase + T.CrashTumblePerSpeed * FMath::Clamp(St.CrashSpeed / FMath::Max(1.f, T.MaxSpeedTurbo), 0.f, 1.2f);
	St.VisualSlide = 0.f;
	St.Crashes++;
	St.Events |= EMXBikeEvent::Crashed;
	St.bBounce = false;
	St.Wheelie = 0.f;
	St.WobbleTimer = 0.f;
	St.PitchEffect = 0.f;
	St.VY = 0.f;
	St.bStallPending = false;
}

void FMXBikeSim::Wobble(FMXBikeState& St, float SpeedLoss, const UMXBikeTuning& T)
{
	if (!St.IsRiding())
	{
		return;
	}
	if (St.Phase == EMXBikePhase::Grounded)
	{
		St.Speed *= 1.f - SpeedLoss;
	}
	else
	{
		St.VX *= 1.f - SpeedLoss;
	}
	St.WobbleTimer = FMath::Max(St.WobbleTimer, T.WobbleNoDriveTime);
	St.Events |= EMXBikeEvent::Wobble | EMXBikeEvent::Bumped;
}

void FMXBikeSim::EnterStall(FMXBikeState& St, const UMXBikeTuning& T)
{
	St.Phase = EMXBikePhase::Stalled;
	St.StallTimer = T.StallDuration;
	St.bStallPending = false;
	St.Overheats++;
	St.Wheelie = 0.f;
	St.GhostTimer = FMath::Max(St.GhostTimer, 0.25f);
	St.Events |= EMXBikeEvent::Overheat;
}

void FMXBikeSim::Step(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T,
	float Dt, bool bLandingAssist)
{
	St.Events = 0;
	St.SuspensionImpulse = 0.f;
	St.SimTime += Dt;
	St.GhostTimer = FMath::Max(0.f, St.GhostTimer - Dt);
	St.ContactCooldown = FMath::Max(0.f, St.ContactCooldown - Dt);
	St.WobbleTimer = FMath::Max(0.f, St.WobbleTimer - Dt);
	St.LastLateralTime += Dt;
	St.bTurboActive = false;
	St.ThrottleApplied = 0.f;

	switch (St.Phase)
	{
	case EMXBikePhase::Crashed:
	{
		// The rider tumbles forward (visual only): faster crashes roll further (NES manual).
		St.CrashTimer -= Dt;
		St.VisualSlide += St.Speed * Dt;
		St.Speed = FMath::Max(0.f, St.Speed - 20.f * Dt);
		UpdateHeat(St.Heat, 0.f, false, Dt, T);
		if (St.CrashTimer <= 0.f)
		{
			St.Phase = EMXBikePhase::Recovering;
			St.RecoveryTimer = T.RecoveryBase;
			St.RecoveryElapsed = 0.f;
			// Never place the bike beyond the crash point: resets can't award progress.
			St.RecoveryS = FMath::Min(St.CrashS, Track.FindRecoveryS(St.CrashS, St.Y, T.RecoverySearchBack));
			St.RecoveryY = Track.LaneCenterY(Track.NearestLane(St.Y));
			St.Events |= EMXBikeEvent::RecoveryStarted;
		}
		return;
	}
	case EMXBikePhase::Recovering:
	{
		St.RecoveryElapsed += Dt;
		St.RecoveryTimer -= Dt;
		if (In.MashPresses > 0)
		{
			St.RecoveryTimer -= In.MashPresses * T.RecoveryMashCut;
		}
		UpdateHeat(St.Heat, 0.f, false, Dt, T);
		if (St.RecoveryTimer <= 0.f && St.RecoveryElapsed >= T.RecoveryMin)
		{
			St.S = St.RecoveryS;
			St.Y = St.RecoveryY;
			St.Z = Track.Height(St.S, St.Y);
			St.SlopeDeg = Track.SlopeDeg(St.S, St.Y);
			St.Pitch = St.SlopeDeg;
			St.Speed = 0.f;
			St.VX = St.VZ = St.VY = 0.f;
			St.Wheelie = 0.f;
			St.Phase = EMXBikePhase::Grounded;
			St.GhostTimer = T.GhostAfterRecovery;
			St.VisualSlide = 0.f;
			St.TargetLane = Track.NearestLane(St.Y);
			St.bBounce = false;
			St.CrashCause = EMXCrashCause::None;
			St.Surface = Track.SurfaceAt(St.S, St.Y);
			St.Events |= EMXBikeEvent::Recovered;
		}
		return;
	}
	case EMXBikePhase::Stalled:
	{
		St.StallTimer -= Dt;
		St.Speed = FMath::Max(0.f, St.Speed - T.StallDecel * Dt);
		St.GhostTimer = FMath::Max(St.GhostTimer, 0.25f);
		// Gauge drains visibly while the engine recovers.
		St.Heat = FMath::Lerp(T.HeatAfterStall, T.HeatOverheat, FMath::Clamp(St.StallTimer / FMath::Max(0.01f, T.StallDuration), 0.f, 1.f));
		const float Slope = Track.SlopeDeg(St.S, St.Y);
		St.SlopeDeg = Slope;
		St.S += St.Speed * FMath::Cos(FMath::DegreesToRadians(Slope)) * Dt;
		St.Z = Track.Height(St.S, St.Y);
		St.Pitch = Slope;
		St.Distance += St.Speed * Dt;
		St.VY = 0.f;
		if (St.StallTimer <= 0.f)
		{
			St.Phase = EMXBikePhase::Grounded;
			St.Heat = T.HeatAfterStall;
			St.GhostTimer = FMath::Max(St.GhostTimer, 0.75f);
			St.Events |= EMXBikeEvent::StallEnd;
		}
		return;
	}
	default:
		break;
	}

	// ---- Riding (grounded or airborne) ----
	const float Throttle = FMath::Clamp(In.Throttle, 0.f, 1.f);
	if (!St.bFinished)
	{
		const float PrevHeat = St.Heat;
		// Heat follows the held buttons on the ground and in the air (NES), but turbo does nothing on rough ground.
		const bool bTurboHeats = In.bTurbo && !(St.Phase == EMXBikePhase::Grounded && (St.Surface == EMXSurface::Grass || St.Surface == EMXSurface::Verge));
		if (UpdateHeat(St.Heat, Throttle, bTurboHeats, Dt, T))
		{
			if (St.Phase == EMXBikePhase::Grounded)
			{
				EnterStall(St, T);
				return;
			}
			St.bStallPending = true;
		}
		if (PrevHeat < T.HeatWarning && St.Heat >= T.HeatWarning)
		{
			St.Events |= EMXBikeEvent::HeatWarning;
		}
	}
	else
	{
		UpdateHeat(St.Heat, 0.f, false, Dt, T);
	}

	// Smoothed pitch input used for pop / scrub at the lip.
	const float LipAlpha = 1.f - FMath::Exp(-Dt / FMath::Max(0.02f, T.LipInputWindow * 0.5f));
	St.LipInput = FMath::Lerp(St.LipInput, FMath::Clamp(In.Pitch, -1.f, 1.f), LipAlpha);

	StepLateral(St, In, Track, T, Dt);
	if (St.Phase == EMXBikePhase::Grounded)
	{
		StepGrounded(St, In, Track, T, Dt);
	}
	else if (St.Phase == EMXBikePhase::Airborne)
	{
		StepAirborne(St, In, Track, T, Dt, bLandingAssist);
	}
}

void FMXBikeSim::UpdateSurface(FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T)
{
	const EMXSurface New = Track.SurfaceAt(St.S, St.Y);
	if (New != St.Surface)
	{
		St.Events |= EMXBikeEvent::SurfaceChanged;
		if (New == EMXSurface::Cool)
		{
			St.CoolStripHits++;
			St.Events |= EMXBikeEvent::CoolStrip;
		}
		St.Surface = New;
	}
	if (New == EMXSurface::Cool)
	{
		// Cool strips reset heat instantly, only while grounded (NES: on-ground & not stalled).
		St.Heat = FMath::Min(St.Heat, T.CoolStripHeat);
	}
}

void FMXBikeSim::StepLateral(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T, float Dt)
{
	const bool bAir = St.Phase == EMXBikePhase::Airborne;
	const float Factor = bAir ? T.AirSteerFactor : 1.f;
	float TargetVY = 0.f;
	if (In.bLaneTaps)
	{
		const int32 Dir = In.Steer > 0.5f ? 1 : (In.Steer < -0.5f ? -1 : 0);
		if (Dir != 0)
		{
			if (!St.bSteerLatched)
			{
				St.TargetLane = FMath::Clamp(St.TargetLane + Dir, 0, MX::NumLanes - 1);
				St.SteerRepeatTimer = 0.3f;
			}
			else
			{
				// Holding keeps moving one lane at a time once the current target is reached.
				St.SteerRepeatTimer -= Dt;
				if (St.SteerRepeatTimer <= 0.f && FMath::Abs(Track.LaneCenterY(St.TargetLane) - St.Y) < 0.5f)
				{
					St.TargetLane = FMath::Clamp(St.TargetLane + Dir, 0, MX::NumLanes - 1);
					St.SteerRepeatTimer = 0.12f;
				}
			}
			St.bSteerLatched = true;
		}
		else
		{
			St.bSteerLatched = false;
		}
		const float Err = Track.LaneCenterY(St.TargetLane) - St.Y;
		TargetVY = FMath::Clamp(Err * 9.f, -T.LaneChangeSpeed, T.LaneChangeSpeed) * Factor;
	}
	else
	{
		const float Steer = FMath::Clamp(In.Steer, -1.f, 1.f);
		if (FMath::Abs(Steer) > 0.15f)
		{
			TargetVY = Steer * T.SteerSpeed * Factor;
		}
		else
		{
			const float Center = Track.LaneCenterY(Track.NearestLane(St.Y));
			TargetVY = FMath::Clamp((Center - St.Y) * T.LaneMagnet, -3.f, 3.f) * Factor;
		}
		St.TargetLane = Track.NearestLane(St.Y);
	}

	St.VY = Approach(St.VY, TargetVY, T.SteerAccel * Dt);
	float NewY = St.Y + St.VY * Dt;

	const float Limit = Track.TrackHalfWidth() - 0.5f;
	if (NewY > Limit)
	{
		NewY = Limit;
		St.VY = FMath::Min(0.f, -St.VY * 0.3f);
		St.Events |= EMXBikeEvent::Bumped;
	}
	else if (NewY < -Limit)
	{
		NewY = -Limit;
		St.VY = FMath::Max(0.f, -St.VY * 0.3f);
		St.Events |= EMXBikeEvent::Bumped;
	}

	if (Track.LaneIndexAt(NewY) != Track.LaneIndexAt(St.Y))
	{
		const float HNow = Track.Height(St.S, St.Y);
		const float HNew = Track.Height(St.S, NewY);
		if (HNew - St.Z > T.LateralWallHeight)
		{
			// Side wall (e.g. the platform deck): blocked.
			NewY = St.Y;
			St.VY = 0.f;
			St.TargetLane = Track.NearestLane(St.Y);
			St.Events |= EMXBikeEvent::WallBlocked;
		}
		else if (St.Phase == EMXBikePhase::Grounded && HNew < HNow - 0.3f)
		{
			// Rolled sideways off a raised edge: now airborne.
			const float Th = FMath::DegreesToRadians(St.SlopeDeg);
			St.Phase = EMXBikePhase::Airborne;
			St.VX = St.Speed * FMath::Cos(Th);
			St.VZ = St.Speed * FMath::Sin(Th);
			St.AirTime = 0.f;
			St.LaunchS = St.S;
			St.LaunchSpeed = St.Speed;
			St.ApexZ = St.Z;
			St.bAssistValid = false;
			St.Events |= EMXBikeEvent::Launched;
		}
	}
	if (FMath::Abs(St.VY) > T.CutInLateralSpeed)
	{
		St.LastLateralTime = 0.f;
	}
	St.Y = NewY;
}

void FMXBikeSim::StepGrounded(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T, float Dt)
{
	const float Slope = Track.SlopeDeg(St.S, St.Y);
	St.SlopeDeg = Slope;
	UpdateSurface(St, Track, T);
	const EMXSurface Surf = St.Surface;
	const bool bRough = Surf == EMXSurface::Grass || Surf == EMXSurface::Verge;
	const float Throttle = FMath::Clamp(In.Throttle, 0.f, 1.f);
	const bool bTurbo = In.bTurbo && !bRough && St.WobbleTimer <= 0.f;
	St.bTurboActive = bTurbo;
	St.ThrottleApplied = bTurbo ? 1.f : Throttle;

	// ---- Drive ----
	float V = St.Speed;
	if (St.WobbleTimer > 0.f)
	{
		V -= T.CoastDecel * Dt;
	}
	else if (bTurbo)
	{
		if (V < T.MaxSpeedTurbo)
		{
			V = FMath::Min(T.MaxSpeedTurbo, V + T.AccelTurbo * Dt);
		}
		else
		{
			V -= T.OverCapDecelTurbo * Dt; // turbo holds flow speed
		}
	}
	else if (Throttle > 0.05f)
	{
		const float Cap = T.MaxSpeedNormal * FMath::Lerp(0.35f, 1.f, Throttle);
		if (V < Cap)
		{
			V = FMath::Min(Cap, V + T.AccelNormal * Dt);
		}
		else
		{
			V = FMath::Max(Cap, V - (V > T.MaxSpeedNormal ? T.OverCapDecelNormal : T.CoastDecel * 0.5f) * Dt);
		}
	}
	else
	{
		V -= T.CoastDecel * Dt;
	}

	// ---- Surfaces ----
	float SurfCap = TNumericLimits<float>::Max();
	float SurfDecel = 0.f;
	switch (Surf)
	{
	case EMXSurface::Mud:
		SurfCap = (In.bTurbo ? T.MudSpeedCapTurbo : T.MudSpeedCap) * T.MaxSpeedTurbo;
		SurfDecel = T.MudDecel;
		break;
	case EMXSurface::Grass:
		SurfCap = T.GrassSpeedCap * T.MaxSpeedTurbo;
		SurfDecel = T.GrassDecel;
		break;
	case EMXSurface::Verge:
		SurfCap = T.VergeSpeedCap * T.MaxSpeedTurbo;
		SurfDecel = T.VergeDecel;
		break;
	default:
		break;
	}
	if (V > SurfCap)
	{
		V = FMath::Max(SurfCap, V - SurfDecel * Dt);
	}

	// ---- Slope gravity ----
	const float SlopeRad = FMath::DegreesToRadians(Slope);
	V -= T.SlopeGravity * FMath::Sin(SlopeRad) * Dt;
	V = FMath::Clamp(V, 0.f, T.MaxSpeedTurbo * T.AbsoluteSpeedCapFraction);
	St.Speed = V;

	// ---- Wheelie / pitch ----
	const float PitchIn = FMath::Clamp(In.Pitch, -1.f, 1.f);
	const float PrevWheelie = St.Wheelie;
	if (PitchIn > 0.25f && V > 2.f)
	{
		const float Target = T.WheelieAngle * FMath::Min(1.f, PitchIn * 1.2f);
		const bool bDriving = Throttle > 0.05f || In.bTurbo;
		if (St.Wheelie < Target - 0.01f)
		{
			St.Wheelie = FMath::Min(Target, St.Wheelie + T.WheelieRiseRate * Dt);
		}
		else if (bDriving && PitchIn > 0.8f && St.Wheelie >= T.WheelieAngle - 0.5f)
		{
			// Keep holding back with the throttle on and the front keeps coming up (NES loop-out).
			St.Wheelie += T.WheelieOverRate * Dt;
		}
		else if (St.Wheelie > Target)
		{
			St.Wheelie = FMath::Max(Target, St.Wheelie - T.WheelieDropRate * Dt);
		}
	}
	else if (PitchIn < -0.25f && St.Wheelie <= 0.f)
	{
		St.Wheelie = FMath::Max(-4.f, St.Wheelie - 40.f * Dt);
	}
	else
	{
		St.Wheelie = Approach(St.Wheelie, 0.f, T.WheelieDropRate * Dt);
	}
	if (PrevWheelie < T.WheelieWarnAngle && St.Wheelie >= T.WheelieWarnAngle)
	{
		St.Events |= EMXBikeEvent::WheelieWarn;
	}
	if (St.Wheelie >= T.WheelieCrashAngle)
	{
		St.Pitch = Slope + St.Wheelie;
		Crash(St, EMXCrashCause::Wheelie, T);
		return;
	}
	St.Pitch = Slope + St.Wheelie;

	// ---- Advance along the surface; detect take-off ----
	const float VXg = V * FMath::Cos(SlopeRad);
	const float VZg = V * FMath::Sin(SlopeRad);
	const float S0 = St.S;
	const float S1 = S0 + VXg * Dt;
	CheckBarrier(St, S0, S1, Track, T);
	if (!St.IsRiding() || St.Phase == EMXBikePhase::Airborne)
	{
		return;
	}
	const float H1 = Track.Height(S1, St.Y);
	if (H1 - St.Z > 1.0f)
	{
		// Rode straight into something wall-like.
		Crash(St, EMXCrashCause::Wall, T);
		return;
	}
	const float ZBall = St.Z + VZg * Dt - 0.5f * T.Gravity * Dt * Dt;
	if (ZBall > H1 + T.LaunchClearance && V > T.LaunchMinSpeed)
	{
		St.Phase = EMXBikePhase::Airborne;
		St.VX = VXg;
		St.VZ = VZg - T.Gravity * Dt;
		// Cap the launch angle: very steep lips don't catapult the bike (speed is kept).
		const float PathDeg = FMath::RadiansToDegrees(FMath::Atan2(St.VZ, St.VX));
		if (PathDeg > T.MaxLaunchAngle)
		{
			const float Mag = FMath::Sqrt(St.VX * St.VX + St.VZ * St.VZ);
			const float Cap = FMath::DegreesToRadians(T.MaxLaunchAngle);
			St.VX = Mag * FMath::Cos(Cap);
			St.VZ = Mag * FMath::Sin(Cap);
		}
		if (St.VZ > 0.f)
		{
			if (St.LipInput > 0.3f)
			{
				St.VZ *= 1.f + T.PopBonus * FMath::Min(1.f, St.LipInput);
			}
			else if (St.LipInput < -0.3f)
			{
				St.VZ *= 1.f - T.ScrubPenalty * FMath::Min(1.f, -St.LipInput);
				St.VX *= 1.02f;
			}
		}
		St.S = S1;
		St.Z = ZBall;
		St.AirTime = 0.f;
		St.LaunchS = S0;
		St.LaunchSpeed = V;
		St.ApexZ = St.Z;
		St.Wheelie = 0.f;
		St.bBounce = false;
		St.bAssistValid = false;
		St.Events |= EMXBikeEvent::Launched;
	}
	else
	{
		St.S = S1;
		St.Z = H1;
	}
	St.Distance += V * Dt;
}

void FMXBikeSim::IntegrateAir(FMXBikeState& St, float PitchInput, const UMXBikeTuning& T, float Dt)
{
	const float Alpha = 1.f - FMath::Exp(-Dt / FMath::Max(0.01f, T.PitchEffectBlendTime));
	St.PitchEffect = FMath::Lerp(St.PitchEffect, FMath::Clamp(PitchInput, -1.f, 1.f), Alpha);
	const float Up = FMath::Max(0.f, St.PitchEffect);
	const float Down = FMath::Max(0.f, -St.PitchEffect);
	const float GScale = FMath::Lerp(1.f, T.NoseUpGravityScale, Up);
	const float Drag = T.AirDragNeutral * FMath::Max(0.f, 1.f - Up - Down) + T.AirDragNoseUp * Up + T.AirDragNoseDown * Down;
	const float VTime = FMath::Lerp(1.f, T.NoseDownVerticalTimeScale, Down);
	const float Thrust = T.DiveThrust * Down;
	St.VX = FMath::Max(1.5f, St.VX + (Thrust - Drag) * Dt);
	const float Vdt = Dt * VTime;
	St.VZ -= T.Gravity * GScale * Vdt;
	St.Z += St.VZ * Vdt;
	St.S += St.VX * Dt;
	St.AirTime += Dt;
	St.ApexZ = FMath::Max(St.ApexZ, St.Z);
}

void FMXBikeSim::StepAirborne(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T, float Dt, bool bLandingAssist)
{
	St.bTurboActive = false;
	St.ThrottleApplied = 0.f;
	const float PitchIn = FMath::Clamp(In.Pitch, -1.f, 1.f);

	// Rotation: the bike keeps its angle unless the rider moves it (NES), optional gentle assist.
	if (FMath::Abs(PitchIn) > 0.1f)
	{
		St.Pitch += PitchIn * T.AirPitchRate * Dt;
	}
	else if (bLandingAssist && !St.bBounce)
	{
		St.AssistTimer -= Dt;
		if (St.AssistTimer <= 0.f || !St.bAssistValid)
		{
			const FMXFlightPrediction P = PredictFlight(St, 0.f, Track, T, 2.5f);
			St.bAssistValid = P.bLands;
			St.AssistTargetPitch = P.SurfaceDeg;
			St.AssistTimer = 0.1f;
		}
		if (St.bAssistValid)
		{
			St.Pitch = Approach(St.Pitch, St.AssistTargetPitch, T.LandingAssistRate * Dt);
		}
	}
	St.Pitch = FMath::Clamp(St.Pitch, T.AirPitchMin, T.AirPitchMax);

	const float S0 = St.S;
	IntegrateAir(St, PitchIn, T, Dt);
	St.Distance += St.VX * Dt;

	const float H = Track.Height(St.S, St.Y);
	if (St.Z - H < T.BarrierHeight)
	{
		CheckBarrier(St, S0, St.S, Track, T);
		if (!St.IsRiding())
		{
			return;
		}
	}
	if (St.Z <= H)
	{
		Touchdown(St, Track, T, H, Track.SlopeDeg(St.S, St.Y));
	}
}

void FMXBikeSim::Touchdown(FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T, float GroundH, float SurfaceDeg)
{
	const float Theta = FMath::DegreesToRadians(SurfaceDeg);
	const float Cos = FMath::Cos(Theta);
	const float Sin = FMath::Sin(Theta);
	// Tangential (along the surface) and normal (into the surface) components of the flight velocity.
	const float Vt = St.VX * Cos + St.VZ * Sin;
	const float Vn = St.VX * Sin - St.VZ * Cos;
	const float Delta = St.Pitch - SurfaceDeg;

	St.SuspensionImpulse = FMath::Max(0.f, Vn);
	St.Z = GroundH;
	St.SlopeDeg = SurfaceDeg;
	St.Events |= EMXBikeEvent::Landed;
	St.Landings++;
	St.bAssistValid = false;

	EMXLandingGrade Grade;
	if (SurfaceDeg > T.WallSlope)
	{
		Grade = EMXLandingGrade::Crash;
	}
	else if (St.bBounce)
	{
		// Second contact after a wobble/hop is always clean (NES), unless absurd.
		Grade = FMath::Abs(Delta) > 80.f ? EMXLandingGrade::Crash : EMXLandingGrade::Clean;
	}
	else
	{
		Grade = ClassifyLanding(Delta, SurfaceDeg, T);
	}
	St.LastLanding = Grade;
	St.LastLandingDelta = Delta;
	St.LastLandingTime = St.SimTime;

	if (Grade == EMXLandingGrade::Crash)
	{
		St.Phase = EMXBikePhase::Grounded; // so Crash() records it as riding
		St.Speed = FMath::Max(0.f, Vt);
		Crash(St, SurfaceDeg > T.WallSlope ? EMXCrashCause::Wall : EMXCrashCause::Landing, T);
		return;
	}

	float V = FMath::Max(0.f, Vt);
	if (Vn > T.HardLandingNormalSpeed)
	{
		V *= FMath::Max(0.5f, 1.f - (Vn - T.HardLandingNormalSpeed) * T.HardLandingLossPerMS);
	}
	const bool bDownslope = SurfaceDeg < -T.DownslopeThreshold;
	switch (Grade)
	{
	case EMXLandingGrade::Perfect:
		St.PerfectLandings++;
		if (bDownslope)
		{
			V *= 1.f + T.FlowBoostPerfect;
		}
		break;
	case EMXLandingGrade::Clean:
		V *= 1.f - T.CleanSpeedLoss;
		if (bDownslope)
		{
			V *= 1.f + T.FlowBoostClean;
		}
		break;
	case EMXLandingGrade::Wobble:
	{
		V *= 1.f - T.WobbleSpeedLoss;
		St.WobbleTimer = T.WobbleNoDriveTime;
		St.Events |= EMXBikeEvent::Wobble;
		const float Bounce = Vn * T.WobbleBounceFactor;
		if (Bounce > 1.f)
		{
			// NES wobble: a small re-launch; the next touchdown is always clean.
			St.Phase = EMXBikePhase::Airborne;
			St.bBounce = true;
			St.VX = V * Cos;
			St.VZ = Bounce + V * Sin;
			St.Z = GroundH + 0.02f;
			St.Pitch = SurfaceDeg + Delta * 0.4f;
			St.AirTime = 0.f;
			St.ApexZ = St.Z;
			return;
		}
		break;
	}
	default:
		break;
	}
	St.bBounce = false;
	St.Phase = EMXBikePhase::Grounded;
	St.Speed = FMath::Min(V, T.MaxSpeedTurbo * T.AbsoluteSpeedCapFraction);
	St.Wheelie = FMath::Clamp(Delta, 0.f, 25.f); // front wheel drops after a nose-high landing
	St.Pitch = SurfaceDeg + St.Wheelie;
	St.PitchEffect = 0.f;
	if (St.bStallPending)
	{
		EnterStall(St, T);
	}
}

void FMXBikeSim::CheckBarrier(FMXBikeState& St, float SFrom, float STo, const FMXTrackModel& Track, const UMXBikeTuning& T)
{
	FMXBarrierHit Hit;
	if (!Track.FindBarrierCrossing(SFrom, STo, St.Y, Hit))
	{
		return;
	}
	const float GroundH = Track.Height(Hit.S, St.Y);
	if (St.Phase == EMXBikePhase::Airborne && St.Z - GroundH > Hit.Height)
	{
		return; // cleared it in the air
	}
	const float V = St.ForwardSpeed();
	const float Rel = St.Pitch - (St.Phase == EMXBikePhase::Grounded ? St.SlopeDeg : 0.f);
	if (Rel < T.BarrierSafePitch && V > T.BarrierCrashSpeedFraction * T.MaxSpeedTurbo)
	{
		Crash(St, EMXCrashCause::Barrier, T);
		return;
	}
	St.Events |= EMXBikeEvent::BarrierHop;
	if (St.Phase == EMXBikePhase::Grounded)
	{
		St.Speed *= 1.f - T.BarrierHopSpeedLoss;
		St.Phase = EMXBikePhase::Airborne;
		St.VX = St.Speed * FMath::Cos(FMath::DegreesToRadians(St.SlopeDeg));
		St.VZ = 2.6f;
		St.AirTime = 0.f;
		St.bBounce = true;
		St.Z = GroundH + 0.02f;
		St.S = Hit.S;
		St.ApexZ = St.Z;
		St.Events |= EMXBikeEvent::Launched;
	}
	else
	{
		St.VX *= 1.f - T.BarrierHopSpeedLoss;
	}
}

FMXFlightPrediction FMXBikeSim::PredictFlight(const FMXBikeState& InSt, float PitchInput, const FMXTrackModel& Track,
	const UMXBikeTuning& T, float MaxTime)
{
	FMXFlightPrediction P;
	FMXBikeState St = InSt;
	if (St.Phase != EMXBikePhase::Airborne)
	{
		const float Th = FMath::DegreesToRadians(St.SlopeDeg);
		St.VX = St.Speed * FMath::Cos(Th);
		St.VZ = St.Speed * FMath::Sin(Th);
		St.Phase = EMXBikePhase::Airborne;
		St.PitchEffect = 0.f;
	}
	const float Dt = 1.f / 60.f;
	for (float t = 0.f; t < MaxTime; t += Dt)
	{
		IntegrateAir(St, PitchInput, T, Dt);
		const float H = Track.Height(St.S, St.Y);
		if (St.Z <= H)
		{
			P.bLands = true;
			P.LandS = St.S;
			P.LandY = St.Y;
			P.SurfaceDeg = Track.SlopeDeg(St.S, St.Y);
			P.Time = t + Dt;
			P.VX = St.VX;
			P.VZ = St.VZ;
			break;
		}
	}
	P.ApexZ = St.ApexZ;
	return P;
}
