#pragma once

#include "CoreMinimal.h"
#include "Core/MXTypes.h"

class FMXTrackModel;
class UMXBikeTuning;

/** Per-step event flags (consumed by audio, HUD, FX, AI, tests). */
namespace EMXBikeEvent
{
	enum Type : uint32
	{
		None = 0,
		Launched = 1 << 0,
		Landed = 1 << 1,
		Crashed = 1 << 2,
		Recovered = 1 << 3,
		CoolStrip = 1 << 4,
		Overheat = 1 << 5,
		StallEnd = 1 << 6,
		BarrierHop = 1 << 7,
		Wobble = 1 << 8,
		HeatWarning = 1 << 9,
		WheelieWarn = 1 << 10,
		Bumped = 1 << 11,
		WallBlocked = 1 << 12,
		SurfaceChanged = 1 << 13,
		RecoveryStarted = 1 << 14,
	};
}

/**
 * Complete simulation state of one bike. Plain data so it can be copied for prediction,
 * ghost recording and tests. Track-space units (metres, seconds, degrees).
 */
struct HEATLINEMX_API FMXBikeState
{
	// Kinematics
	float S = 0.f;          // along track
	float Y = 0.f;          // across track (+ = near side / lane 4)
	float Z = 0.f;          // height of the wheel contact line
	float Speed = 0.f;      // grounded: speed along the surface; airborne: unused (VX)
	float VX = 0.f;         // airborne horizontal speed
	float VZ = 0.f;         // airborne vertical speed
	float VY = 0.f;         // lateral speed
	float Pitch = 0.f;      // bike pitch vs horizontal (deg, + nose up)
	float Wheelie = 0.f;    // grounded pitch offset above the surface angle
	float SlopeDeg = 0.f;   // surface angle under the bike (grounded)
	EMXBikePhase Phase = EMXBikePhase::Grounded;
	EMXSurface Surface = EMXSurface::Dirt;

	// Engine
	float Heat = 0.f;
	float StallTimer = 0.f;
	bool bStallPending = false;  // overheated in the air: stall starts on touchdown
	bool bTurboActive = false;   // turbo applied this step (for audio/FX)
	float ThrottleApplied = 0.f;

	// Lanes
	int32 TargetLane = 1;
	bool bSteerLatched = false;
	float SteerRepeatTimer = 0.f;
	float LastLateralTime = 100.f; // seconds since significant lateral motion (cut-in rule)

	// Flight
	float PitchEffect = 0.f;     // blended flight modifier (-1..1)
	float LipInput = 0.f;        // smoothed pitch input for pop/scrub
	float AirTime = 0.f;
	bool bBounce = false;        // wobble re-launch: next touchdown is clean
	float LaunchS = 0.f;
	float LaunchSpeed = 0.f;
	float ApexZ = 0.f;
	float AssistTargetPitch = 0.f;
	float AssistTimer = 0.f;
	bool bAssistValid = false;

	// Landing results
	EMXLandingGrade LastLanding = EMXLandingGrade::None;
	float LastLandingDelta = 0.f;
	float LastLandingTime = -100.f;
	float WobbleTimer = 0.f;

	// Crash / recovery
	EMXCrashCause CrashCause = EMXCrashCause::None;
	float CrashTimer = 0.f;
	float CrashS = 0.f;
	float CrashSpeed = 0.f;
	float RecoveryTimer = 0.f;
	float RecoveryElapsed = 0.f;
	float RecoveryS = 0.f;
	float RecoveryY = 0.f;
	float VisualSlide = 0.f;     // visual-only tumble distance past CrashS
	float GhostTimer = 0.f;      // no collisions while > 0
	float ContactCooldown = 0.f; // can't be contact-crashed while > 0

	// Bookkeeping
	bool bFinished = false;      // crossed the final line (keeps riding in the run-out)
	float Distance = 0.f;        // odometer (wheel spin)
	float SimTime = 0.f;
	float SuspensionImpulse = 0.f; // m/s into the ground this step (visual suspension)
	int32 Landings = 0;
	int32 PerfectLandings = 0;
	int32 Crashes = 0;
	int32 Overheats = 0;
	int32 CoolStripHits = 0;
	uint32 Events = 0;

	bool IsRiding() const { return Phase == EMXBikePhase::Grounded || Phase == EMXBikePhase::Airborne; }
	bool IsAirborne() const { return Phase == EMXBikePhase::Airborne; }
	bool CanCollide() const { return IsRiding() && GhostTimer <= 0.f; }
	/** Horizontal speed along the track regardless of phase. */
	float ForwardSpeed() const;
	/** Visual s (includes crash slide). */
	float VisualS() const { return (Phase == EMXBikePhase::Crashed) ? CrashS + VisualSlide : S; }
};

/** Result of a flight prediction (AI, landing assist, validator). */
struct FMXFlightPrediction
{
	bool bLands = false;
	float LandS = 0.f;
	float LandY = 0.f;
	float SurfaceDeg = 0.f;
	float Time = 0.f;
	float VX = 0.f;
	float VZ = 0.f;
	float ApexZ = 0.f;
};

/** The deterministic arcade bike controller. Fixed-step; identical for players, AI and ghosts. */
class HEATLINEMX_API FMXBikeSim
{
public:
	static constexpr float FixedDt = 1.f / 120.f;

	/** Resets a bike onto the grid. */
	static void Spawn(FMXBikeState& St, float S, float Y, const FMXTrackModel& Track);

	/** One fixed step. Events for this step are written to St.Events. */
	static void Step(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T,
		float Dt, bool bLandingAssist);

	/** Forces a crash (contact, tests). */
	static void Crash(FMXBikeState& St, EMXCrashCause Cause, const UMXBikeTuning& T);

	/** Applies a wobble without crashing (cut-in contact). */
	static void Wobble(FMXBikeState& St, float SpeedLoss, const UMXBikeTuning& T);

	/**
	 * Predicts the rest of a flight holding a constant pitch input. Works from a grounded state too
	 * (assumes an immediate launch from the current slope at the current speed).
	 */
	static FMXFlightPrediction PredictFlight(const FMXBikeState& St, float PitchInput, const FMXTrackModel& Track,
		const UMXBikeTuning& T, float MaxTime = 3.f);

	/** Landing classification (pure; exposed for tests and the HUD tutorial). */
	static EMXLandingGrade ClassifyLanding(float DeltaDeg, float SurfaceDeg, const UMXBikeTuning& T);

	/** Heat update (pure; exposed for tests). Returns true when it crosses the overheat threshold. */
	static bool UpdateHeat(float& Heat, float Throttle, bool bTurbo, float Dt, const UMXBikeTuning& T);

private:
	static void StepGrounded(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T, float Dt);
	static void StepAirborne(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T, float Dt, bool bLandingAssist);
	static void StepLateral(FMXBikeState& St, const FMXBikeInput& In, const FMXTrackModel& Track, const UMXBikeTuning& T, float Dt);
	static void Touchdown(FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T, float GroundH, float SurfaceDeg);
	static void CheckBarrier(FMXBikeState& St, float SFrom, float STo, const FMXTrackModel& Track, const UMXBikeTuning& T);
	/** Shared airborne integration (Step and PredictFlight use the exact same maths). */
	static void IntegrateAir(FMXBikeState& St, float PitchInput, const UMXBikeTuning& T, float Dt);
	static void EnterStall(FMXBikeState& St, const UMXBikeTuning& T);
	static void UpdateSurface(FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T);
};
