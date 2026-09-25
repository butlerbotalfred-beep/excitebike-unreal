#pragma once

#include "CoreMinimal.h"
#include "Core/MXTypes.h"
#include "Core/MXTuning.h"
#include "Bike/MXBikeSim.h"

class FMXTrackModel;

/** What an AI rider can see of the other riders. */
struct FMXAIOtherBike
{
	float S = 0.f;
	float Y = 0.f;
	float Speed = 0.f;
	bool bRiding = true;
	bool bHuman = false;
	int32 Index = INDEX_NONE;
	/** Can be run into (not ghosted after a remount). */
	bool bSolid = true;
};

/** In-flight controller state, shared by the AI and the rollouts it uses to pick an air plan. */
struct FMXFlightControl
{
	/** Pitch input held early in the flight (it changes the trajectory, NES-style). */
	float Strategy = 0.f;
	/** Deliberate aim error for weaker riders (degrees). */
	float LandingError = 0.f;
	/** Switched from the strategy to lining up with the landing surface. */
	bool bAligning = false;
	float LastInput = 0.f;
	float PredTimer = 0.f;
	FMXFlightPrediction Pred;
};

/** Per-rider AI memory (plain data so the validator/autotest can run it headless). */
struct HEATLINEMX_API FMXAIMemory
{
	FRandomStream Rng;
	float ThinkTimer = 0.f;
	int32 PlanLane = 1;
	float TargetSpeed = 1000.f;
	FMXFlightControl Flight;
	float MashAccumulator = 0.f;
	float MistakeTimer = 0.f;
	bool bChaser = false;
	bool bWasAirborne = false;
	bool bWheelieFor = false;
	float WheelieHold = 0.f;
	/** Turbo hysteresis: once the heat limit is hit, cool down this far before using turbo again. */
	bool bTurboCooling = false;
	float GreedyTimer = 0.f;
	/**
	 * Strategy override for validator drive tests and balance checks: 0 = normal, 1 = turbo up to the red line,
	 * 2 = careful, 3 = hold turbo always (ignores heat), 4 = never turbo. Speed plans are ignored for 1, 3 and 4.
	 */
	int32 Strategy = 0;
	/** When true the rider is an autopilot for a human slot (autotest) and never makes random mistakes. */
	bool bAutopilot = false;

	void Init(int32 Seed, bool bInChaser)
	{
		Rng.Initialize(Seed);
		bChaser = bInChaser;
		ThinkTimer = Rng.FRandRange(0.f, 0.2f);
	}
};

class HEATLINEMX_API FMXAIBrain
{
public:
	/** Produces this frame's input. Same FMXBikeInput and physics as a human (no speed cheats). */
	static FMXBikeInput Think(const FMXBikeState& St, FMXAIMemory& Mem, const FMXTrackModel& Track,
		const UMXBikeTuning& T, const FMXAISkill& Skill, const TArray<FMXAIOtherBike>& Others, float Dt);

	/** Lane cost used by planning (exposed for tests / debug HUD). */
	static float LaneCost(int32 Lane, const FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T,
		const FMXAISkill& Skill, const TArray<FMXAIOtherBike>& Others, const FMXAIMemory& Mem);

	/** One step of the in-flight controller: hold the plan's input, then line up with the landing surface in time. */
	static float FlightInput(const FMXBikeState& St, FMXFlightControl& Ctl, const FMXTrackModel& Track, const UMXBikeTuning& T,
		float Reaction, float Dt);

private:
	static float ChooseAirInput(const FMXBikeState& St, const FMXTrackModel& Track, const UMXBikeTuning& T, const FMXAISkill& Skill);
	/** Flies an air plan with the real bike physics and controller; returns the distance reached at the horizon (crashes heavily penalised). */
	static float RolloutAirPlan(const FMXBikeState& St, float Strategy, const FMXTrackModel& Track, const UMXBikeTuning& T,
		float Reaction, float Horizon);
};
