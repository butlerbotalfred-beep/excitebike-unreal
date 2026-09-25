#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Containers/Queue.h"
#include <atomic>
#include "MXEngineAudio.generated.h"

enum class EMXSfx : uint8
{
	Land,
	Crash,
	Cool,
	Overheat,
	Warning,
	Bump,
	// UI / race (2D)
	Beep,
	Go,
	MenuMove,
	MenuSelect,
	Cheer,
	Fanfare,
	Join,
	Leave
};

/**
 * Procedural single-cylinder two-stroke motocross engine (no samples). Speed/throttle set RPM with a
 * simple gearbox, turbo adds a whine and rasp, heat adds knock and a steam hiss, overheat sputters.
 * One-shot SFX (landing thump, crash, cool-strip hiss...) are synthesised into the same stream.
 */
UCLASS(ClassGroup = (HeatlineMX), meta = (BlueprintSpawnableComponent))
class HEATLINEMX_API UMXEngineAudioComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UMXEngineAudioComponent(const FObjectInitializer& ObjectInitializer);

	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

	/** Game thread. SpeedFrac 0..1+, Heat 0..1. */
	void SetEngineParams(float SpeedFrac, float Throttle, bool bTurbo, float Heat, bool bAirborne, bool bEngineOff);
	void PlayOneShot(EMXSfx Sfx, float Volume);
	void SetVoiceSeed(int32 Seed);
	void SetMuted(bool bMute);
	/** 2D mode for UI/race sounds (no engine tone). */
	void SetUIMode(bool bUI);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float EngineVolume = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float IdleRPM = 1900.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float MaxRPM = 12500.f;

private:
	struct FVoice
	{
		EMXSfx Type = EMXSfx::Land;
		float Volume = 1.f;
		float Time = 0.f;
		float Duration = 0.5f;
		float Phase = 0.f;
		float Phase2 = 0.f;
		float Lp = 0.f;
	};
	float Noise();
	float RenderVoice(FVoice& V, float Dt);

	std::atomic<float> TargetSpeed{0.f};
	std::atomic<float> TargetThrottle{0.f};
	std::atomic<float> TargetTurbo{0.f};
	std::atomic<float> TargetHeat{0.f};
	std::atomic<float> TargetAir{0.f};
	std::atomic<float> TargetOff{0.f};
	std::atomic<float> Muted{0.f};
	std::atomic<float> UIMode{0.f};
	std::atomic<int32> Seed{1};

	struct FShot
	{
		EMXSfx Type;
		float Volume;
	};
	TQueue<FShot, EQueueMode::Mpsc> Shots;

	// Audio-thread state.
	int32 Rate = 48000;
	float Rpm = 1900.f;
	float Load = 0.f;
	float Turbo = 0.f;
	float Heat = 0.f;
	float Off = 0.f;
	float CyclePhase = 0.f;
	float WhinePhase = 0.f;
	float Bp1 = 0.f, Bp2 = 0.f;   // resonant band-pass state
	float Lp = 0.f;
	float Hp = 0.f, HpPrev = 0.f;
	float SputterTimer = 0.f;
	float KnockTimer = 0.f;
	uint32 Rng = 22222u;
	TArray<FVoice> Voices;
};
