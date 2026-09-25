#pragma once

#include "CoreMinimal.h"
#include "Core/MXTypes.h"
#include "MXRaceTypes.generated.h"

/** One rider taking part in a race. */
USTRUCT()
struct FMXRacerConfig
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() int32 ColorIndex = 0;
	/** Local player slot (0..3) or -1 for AI. */
	UPROPERTY() int32 PlayerSlot = -1;
	UPROPERTY() EMXAIDifficulty Difficulty = EMXAIDifficulty::Medium;
	UPROPERTY() bool bChaser = false;
	/** Autotest: a human slot driven by the AI brain through the real input path. */
	UPROPERTY() bool bAutopilot = false;
	UPROPERTY() bool bLandingAssist = true;
	UPROPERTY() int32 ChampionshipId = -1;

	bool IsHuman() const { return PlayerSlot >= 0; }
};

USTRUCT()
struct FMXRaceConfig
{
	GENERATED_BODY()

	UPROPERTY() EMXRaceMode Mode = EMXRaceMode::RaceAI;
	UPROPERTY() FString CourseId;
	UPROPERTY() EMXLayoutVariant Variant = EMXLayoutVariant::Main;
	/** 0 = course default. */
	UPROPERTY() int32 Laps = 0;
	UPROPERTY() TArray<FMXRacerConfig> Racers;
	UPROPERTY() bool bGhost = false;
};

/** Validated race progress for one rider (checkpoint integrity). */
USTRUCT()
struct FMXRacerProgress
{
	GENERATED_BODY()

	/** Index of the next gate this rider must cross. */
	UPROPERTY() int32 NextGate = 0;
	UPROPERTY() int32 LapsCompleted = 0;
	UPROPERTY() float LapStartTime = 0.f;
	UPROPERTY() float BestLap = -1.f;
	UPROPERTY() TArray<float> LapTimes;
	UPROPERTY() bool bFinished = false;
	UPROPERTY() float FinishTime = -1.f;
	UPROPERTY() int32 FinishOrder = -1;
	/** Largest s reached through valid movement (never increased by resets). */
	UPROPERTY() float ValidS = 0.f;
	UPROPERTY() int32 InvalidCrossings = 0;
	UPROPERTY() int32 Position = 1;
	/** Estimated time for riders that had not finished when the race was closed. */
	UPROPERTY() bool bEstimated = false;
};

/** Messages the HUD shows per rider (landing grades, overheat, laps...). */
USTRUCT()
struct FMXHudMessage
{
	GENERATED_BODY()

	UPROPERTY() FString Text;
	UPROPERTY() FLinearColor Color = FLinearColor::White;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() float Duration = 1.2f;
	UPROPERTY() float Scale = 1.f;
};

/** One recorded ghost sample (20 Hz). */
USTRUCT()
struct FMXGhostFrame
{
	GENERATED_BODY()

	UPROPERTY() float T = 0.f;
	UPROPERTY() float S = 0.f;
	UPROPERTY() float Y = 0.f;
	UPROPERTY() float Z = 0.f;
	UPROPERTY() float Pitch = 0.f;
	UPROPERTY() float Speed = 0.f;
	UPROPERTY() uint8 Phase = 0;
};

USTRUCT()
struct FMXGhostData
{
	GENERATED_BODY()

	UPROPERTY() FString CourseKey;
	UPROPERTY() float TotalTime = -1.f;
	UPROPERTY() int32 ColorIndex = 0;
	UPROPERTY() TArray<FMXGhostFrame> Frames;
};

enum class EMXRacePhase : uint8
{
	None,
	Intro,
	Countdown,
	Racing,
	Finished
};
