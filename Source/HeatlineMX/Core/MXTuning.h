#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/MXTypes.h"
#include "MXTuning.generated.h"

/**
 * All bike handling values. Units: metres, seconds, degrees, heat in percent (0..100).
 * Defaults are the shipped tuning; DA_BikeTuning (a Data Asset of this class) overrides them.
 * See docs/TUNING.md for the NES reference behind each value.
 */
UCLASS(BlueprintType)
class HEATLINEMX_API UMXBikeTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---------------- Speed ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float MaxSpeedNormal = 27.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float MaxSpeedTurbo = 30.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float AccelNormal = 11.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float AccelTurbo = 26.f;
	/** Engine braking when neither throttle nor turbo is held (NES: "let go and the brakes operate"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float CoastDecel = 9.f;
	/** Decel while above the throttle cap with normal throttle held (turbo holds over-cap speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float OverCapDecelNormal = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float OverCapDecelTurbo = 0.4f;
	/** Along-slope gravity on the ground (downhill speeds up, uphill slows down). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float SlopeGravity = 14.f;
	/** Absolute ceiling as a fraction of MaxSpeedTurbo (flow boost + slopes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed") float AbsoluteSpeedCapFraction = 1.15f;

	// ---------------- Surfaces ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface") float MudSpeedCap = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface") float MudSpeedCapTurbo = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface") float MudDecel = 38.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface") float GrassSpeedCap = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface") float GrassDecel = 26.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface") float VergeSpeedCap = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface") float VergeDecel = 20.f;

	// ---------------- Heat (percent) ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatRateTurbo = 14.7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatRateNormal = 6.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatEquilibriumNormal = 37.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatEquilibriumCoast = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float CoolRate = 10.7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatWarning = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatOverheat = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float StallDuration = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float StallDecel = 14.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatAfterStall = 0.f;
	/** Heat value a cool strip sets (on the ground only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float CoolStripHeat = 0.f;

	// ---------------- Lanes ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lanes") float LaneChangeSpeed = 11.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lanes") float SteerSpeed = 11.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lanes") float SteerAccel = 70.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lanes") float LaneMagnet = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lanes") float AirSteerFactor = 0.2f;
	/** A step taller than this blocks lateral movement (deck side walls). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lanes") float LateralWallHeight = 0.7f;

	// ---------------- Pitch ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float WheelieAngle = 38.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float WheelieRiseRate = 110.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float WheelieOverRate = 28.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float WheelieCrashAngle = 72.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float WheelieDropRate = 170.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float WheelieWarnAngle = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float AirPitchRate = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float AirPitchMax = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float AirPitchMin = -60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float LandingAssistRate = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch") float PitchEffectBlendTime = 0.1f;

	// ---------------- Flight ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float Gravity = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float AirDragNeutral = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float AirDragNoseUp = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float AirDragNoseDown = 0.f;
	/** Gravity multiplier at full nose-up input (NES: 24/52). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float NoseUpGravityScale = 0.5f;
	/** Vertical time rate at full nose-down input (NES skips every 4th vertical step). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float NoseDownVerticalTimeScale = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float DiveThrust = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float PopBonus = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float ScrubPenalty = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float LipInputWindow = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float LaunchMinSpeed = 4.f;
	/** Steeper faces launch no steeper than this (keeps very steep lips readable; ramp shape still matters below it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float MaxLaunchAngle = 36.f;
	/** Ground-stick: ballistic path must clear the ground by this to become airborne. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight") float LaunchClearance = 0.015f;

	// ---------------- Landing ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float PerfectTolerance = 7.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float CleanMin = -15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float CleanMax = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float WobbleMin = -35.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float WobbleMax = 50.f;
	/** On up-faces steeper than UpslopeThreshold, nose-down beyond this crashes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float UpslopeNoseDownCrash = -25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float UpslopeThreshold = 15.f;
	/** On down-faces steeper than DownslopeThreshold, nose-high is tolerated up to this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float DownslopeNoseUpWobble = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float DownslopeThreshold = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float CleanSpeedLoss = 0.04f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float WobbleSpeedLoss = 0.18f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float WobbleBounceFactor = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float WobbleNoDriveTime = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float HardLandingNormalSpeed = 16.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float HardLandingLossPerMS = 0.02f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float FlowBoostPerfect = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float FlowBoostClean = 0.04f;
	/** Surfaces steeper than this into the ground count as walls on landing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing") float WallSlope = 62.f;

	// ---------------- Barriers ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrier") float BarrierCrashSpeedFraction = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrier") float BarrierSafePitch = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrier") float BarrierHopSpeedLoss = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrier") float BarrierHeight = 0.45f;

	// ---------------- Crash / recovery ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float CrashTumbleBase = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float CrashTumblePerSpeed = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float RecoveryBase = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float RecoveryMashCut = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float RecoveryMin = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float GhostAfterRecovery = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float ContactCrashCooldown = 6.f;
	/** Recovery point search: step back this far at most looking for safe flat ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash") float RecoverySearchBack = 40.f;

	// ---------------- Rider contact ----------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float BikeContactLength = 2.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float BikeContactWidth = 0.95f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float BikeContactHeight = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float RearEndCrashSpeed = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float CutInWindow = 0.5f;
	/** A rear-end only crashes when both riders have been on the ground at least this long (else a wobble). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float ContactReactTime = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float CutInLateralSpeed = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float CutInSpeedLoss = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float SideBumpImpulse = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact") float SideBumpSpeedLoss = 0.03f;
};

/** Camera parameters for one viewport shape. */
USTRUCT(BlueprintType)
struct FMXCameraShapeTuning
{
	GENERATED_BODY()

	/**
	 * The camera frames the bike instead of chasing a look-at point: its orientation is fixed (Pitch, Yaw)
	 * and it sits Distance metres back along the line of sight that puts the bike at (ScreenX, ScreenY).
	 * So the bike holds the same spot in every view shape, whatever the field of view.
	 */
	/** Distance from the camera to the bike along the line of sight (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Distance = 26.f;
	/** Horizontal field of view in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FOV = 58.f;
	/** Camera pitch down in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Pitch = 18.f;
	/** Turn from looking straight across the track towards the direction of travel (the three-quarter angle), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Yaw = 20.f;
	/** Where the bike sits on screen, 0..1 from the left and from the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ScreenX = 0.36f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ScreenY = 0.62f;
	/** At full speed the bike slides this much further left on screen, showing more track ahead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SpeedScreenShift = 0.06f;
	/** Fraction of the bike's height above the track base the camera follows (0 = fixed, 1 = full). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float HeightFollow = 0.35f;
	/** Fraction of the bike's lateral position the camera follows (1 = tracks the lane, 0 = stays on the centre line). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LateralFollow = 0.35f;
	/** Smoothing time constants (s) for lateral and height following; along the track the camera is locked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FollowLag = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float HeightLag = 0.35f;
};

UCLASS(BlueprintType)
class HEATLINEMX_API UMXCameraTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UMXCameraTuning();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera") FMXCameraShapeTuning Full;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera") FMXCameraShapeTuning Wide;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera") FMXCameraShapeTuning Tall;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera") FMXCameraShapeTuning Quarter;
	/** Designer overview camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera") FMXCameraShapeTuning Designer;

	const FMXCameraShapeTuning& ForShape(EMXViewShape Shape) const;
};

/** AI skill parameters. Same physics as players: only decisions and precision change. */
USTRUCT(BlueprintType)
struct FMXAISkill
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReactionDelay = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LandingErrorDeg = 7.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TurboHeatLimit = 75.f;
	/** After reaching the limit, turbo resumes once heat has dropped this far below it (smaller = feathers turbo nearer the red line). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TurboResumeMargin = 15.f;
	/** Shortest rest after letting go of turbo at the limit (seconds): people wait a moment before pressing again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TurboMinRest = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPlansCoolStrips = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LaneHorizon = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUsesFlightControl = true;
	/** With flight control: chance per jump of planning the best nose-up/nose-down input (otherwise it just lines up the landing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FlightPlanChance = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MashRate = 7.f;
	/** Probability per second of a small line mistake (drifting into a worse lane). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MistakeRate = 0.05f;
	/** Probability per second of getting greedy with turbo for a few seconds (can overheat). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float GreedyTurboRate = 0.01f;
	/**
	 * Chance per jump of letting off the gas in the air, which cools the engine (an expert NES trick).
	 * Riders who don't keep holding what they held on take-off, like most people do.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AirCoastChance = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bChasersAllowed = true;
};

UCLASS(BlueprintType)
class HEATLINEMX_API UMXAITuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UMXAITuning();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") FMXAISkill Easy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") FMXAISkill Medium;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") FMXAISkill Hard;

	/** Time-trial medal targets as multiples of the reference rider's time (FMXTrackValidator::ReferenceTime). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Medals") float MedalGoldFactor = 1.03f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Medals") float MedalSilverFactor = 1.10f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Medals") float MedalBronzeFactor = 1.20f;

	const FMXAISkill& ForDifficulty(EMXAIDifficulty D) const;
};

/** Track scale and dressing knobs shared by built-in and user tracks. */
UCLASS(BlueprintType)
class HEATLINEMX_API UMXTrackStyle : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale") float MetersPerColumn = 1.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale") float MetersPerRow = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale") float LaneWidth = 3.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale") float VergeWidth = 2.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale") float CheckpointSpacing = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale") float StartGridLength = 24.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale") float RunOutLength = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor DirtColor = FLinearColor(0.42f, 0.22f, 0.10f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor DirtColorAlt = FLinearColor(0.36f, 0.18f, 0.08f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor RampColor = FLinearColor(0.55f, 0.33f, 0.16f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor MudColor = FLinearColor(0.10f, 0.07f, 0.03f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor GrassColor = FLinearColor(0.12f, 0.36f, 0.06f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor VergeColor = FLinearColor(0.22f, 0.30f, 0.08f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor CoolColor = FLinearColor(0.1f, 0.65f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor InfieldColor = FLinearColor(0.10f, 0.32f, 0.07f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FLinearColor LaneLineColor = FLinearColor(0.85f, 0.80f, 0.70f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") int32 CrowdDensity = 3;
};

/** Central access to tuning with C++ defaults as fallback when assets are absent. */
namespace MXTuning
{
	HEATLINEMX_API const UMXBikeTuning& Bike();
	HEATLINEMX_API const UMXCameraTuning& Camera();
	HEATLINEMX_API const UMXAITuning& AI();
	HEATLINEMX_API const UMXTrackStyle& Style();
	/** Loads /Game/HeatlineMX/Data/DA_* if present. Safe to call repeatedly. */
	HEATLINEMX_API void LoadAssets();
}
