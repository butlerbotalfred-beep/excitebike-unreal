#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Race/MXRaceTypes.h"
#include "Track/MXTrackModel.h"
#include "AI/MXAIBrain.h"
#include "MXRaceManager.generated.h"

class AMXBike;
class AMXTrack;
class UMXEngineAudioComponent;

DECLARE_MULTICAST_DELEGATE(FMXOnRaceOver);

/**
 * Runs a race: fixed-step simulation of every bike (players and AI through the same controller),
 * rider contact, ordered checkpoints / laps / finish validation, standings, ghosts and HUD messages.
 */
UCLASS()
class HEATLINEMX_API AMXRaceManager : public AActor
{
	GENERATED_BODY()

public:
	AMXRaceManager();

	/** Builds the track model + visuals and spawns every rider on the grid. */
	bool Setup(const FMXRaceConfig& InConfig, const FMXTrackDefinition& Course, AMXTrack* TrackActor, bool bDressing = true);
	/** Same riders, same course, back on the grid. */
	void Restart();
	void Teardown();

	virtual void Tick(float DeltaSeconds) override;

	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }

	// ---- queries for HUD / camera / game mode ----
	EMXRacePhase GetPhase() const { return Phase; }
	float GetRaceTime() const { return RaceTime; }
	float GetCountdown() const { return Countdown; }
	const FMXTrackModel& GetTrack() const { return Track; }
	const FMXRaceConfig& GetConfig() const { return Config; }
	int32 NumRacers() const { return Bikes.Num(); }
	AMXBike* GetBike(int32 Racer) const { return Bikes.IsValidIndex(Racer) ? Bikes[Racer] : nullptr; }
	AMXBike* GetBikeForSlot(int32 Slot) const;
	int32 RacerForSlot(int32 Slot) const;
	const FMXRacerProgress& GetProgress(int32 Racer) const { return Progress[Racer]; }
	const TArray<int32>& GetStandings() const { return Standings; }
	const TArray<FMXHudMessage>& GetMessages(int32 Racer) const { return Messages[Racer]; }
	int32 GetPosition(int32 Racer) const { return Progress.IsValidIndex(Racer) ? Progress[Racer].Position : 0; }
	float GetGhostTotal() const { return GhostPlayback.TotalTime; }
	const FMXGhostData& GetRecordedGhost() const { return GhostRecording; }
	bool IsRaceOver() const { return bRaceOver; }

	/** Autotest: the brain's recommendation for a human slot (fed back through the real input path). */
	FMXBikeInput ComputeAutopilotInput(int32 Racer, float DeltaSeconds);

	/** Ghost to race against (time trial). */
	void SetGhost(const FMXGhostData& Ghost);

	void AddMessage(int32 Racer, const FString& Text, const FLinearColor& Color, float Duration = 1.2f, float Scale = 1.f);

	FMXOnRaceOver OnRaceOver;

	/** Test hooks. */
	void ForceCrash(int32 Racer);
	FMXBikeState& MutableState(int32 Racer);

	/** Bike class to spawn (the game mode passes its Blueprint presentation class). */
	UPROPERTY() TSubclassOf<AMXBike> BikeClass;

	/** Statistics for reports. */
	int32 TotalContacts = 0;
	int32 TotalContactCrashes = 0;

private:
	void SpawnRiders();
	void StepSimulation(float Dt);
	void ResolveContacts();
	void UpdateProgress(int32 Racer, float SPrev, bool bValidMove);
	void UpdateStandings();
	void CheckRaceOver(float DeltaSeconds);
	void FinishRace();
	void RecordGhost();
	void PlayGhost();
	void PlayUISound(uint8 Sfx, float Volume = 1.f);

	FMXRaceConfig Config;
	FMXTrackModel Track;
	UPROPERTY() TObjectPtr<AMXTrack> TrackActor;
	UPROPERTY() TArray<TObjectPtr<AMXBike>> Bikes;
	UPROPERTY() TObjectPtr<AMXBike> GhostBike;
	UPROPERTY() TObjectPtr<UMXEngineAudioComponent> UIAudio;
	TArray<FMXRacerProgress> Progress;
	TArray<FMXAIMemory> AIMemory;
	TArray<FMXAIMemory> AutopilotMemory;
	TArray<TArray<FMXHudMessage>> Messages;
	TArray<int32> Standings;
	TArray<FMXAIOtherBike> OthersScratch;

	FMXGhostData GhostPlayback;
	FMXGhostData GhostRecording;
	float GhostRecordTimer = 0.f;

	EMXRacePhase Phase = EMXRacePhase::None;
	float PhaseTime = 0.f;
	float Countdown = 3.f;
	int32 LastCountBeep = 4;
	float RaceTime = 0.f;
	float Accumulator = 0.f;
	float OverTimer = 0.f;
	int32 FinishCounter = 0;
	bool bPaused = false;
	bool bRaceOver = false;
	float FirstHumanFinishTime = -1.f;
};
