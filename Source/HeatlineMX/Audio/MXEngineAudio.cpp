#include "Audio/MXEngineAudio.h"
#include "Sound/SoundAttenuation.h"

namespace
{
	// Pade approximation of tanh, accurate enough for soft clipping.
	FORCEINLINE float SoftTanh(float X)
	{
		X = FMath::Clamp(X, -3.f, 3.f);
		const float X2 = X * X;
		return X * (27.f + X2) / (27.f + 9.f * X2);
	}
}

UMXEngineAudioComponent::UMXEngineAudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1;
	bAutoActivate = true;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	AttenuationOverrides.bAttenuate = true;
	AttenuationOverrides.bSpatialize = true;
	AttenuationOverrides.FalloffDistance = 9000.f;
	AttenuationOverrides.AttenuationShapeExtents = FVector(1500.f, 0.f, 0.f);
}

bool UMXEngineAudioComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	Rate = SampleRate > 0 ? SampleRate : 48000;
	Voices.Reserve(16);
	return true;
}

void UMXEngineAudioComponent::SetEngineParams(float SpeedFrac, float Throttle, bool bTurbo, float InHeat, bool bAirborne, bool bEngineOff)
{
	TargetSpeed.store(FMath::Clamp(SpeedFrac, 0.f, 1.3f));
	TargetThrottle.store(FMath::Clamp(Throttle, 0.f, 1.f));
	TargetTurbo.store(bTurbo ? 1.f : 0.f);
	TargetHeat.store(FMath::Clamp(InHeat, 0.f, 1.f));
	TargetAir.store(bAirborne ? 1.f : 0.f);
	TargetOff.store(bEngineOff ? 1.f : 0.f);
}

void UMXEngineAudioComponent::PlayOneShot(EMXSfx Sfx, float Volume)
{
	Shots.Enqueue({Sfx, Volume});
}

void UMXEngineAudioComponent::SetVoiceSeed(int32 InSeed)
{
	Seed.store(InSeed);
}

void UMXEngineAudioComponent::SetMuted(bool bMute)
{
	Muted.store(bMute ? 1.f : 0.f);
}

void UMXEngineAudioComponent::SetUIMode(bool bUI)
{
	UIMode.store(bUI ? 1.f : 0.f);
	bAllowSpatialization = !bUI;
	bIsUISound = bUI;
}

float UMXEngineAudioComponent::Noise()
{
	Rng ^= Rng << 13;
	Rng ^= Rng >> 17;
	Rng ^= Rng << 5;
	return (float)(Rng & 0xFFFFFF) / 8388608.f - 1.f;
}

float UMXEngineAudioComponent::RenderVoice(FVoice& V, float Dt)
{
	const float T = V.Time;
	float Out = 0.f;
	auto Sine = [&V, Dt](float Hz) { V.Phase += Hz * Dt; V.Phase -= FMath::FloorToFloat(V.Phase); return FMath::Sin(2.f * PI * V.Phase); };
	switch (V.Type)
	{
	case EMXSfx::Land:
	{
		const float N = Noise();
		V.Lp += (N - V.Lp) * 0.08f;
		Out = Sine(52.f + 30.f * FMath::Exp(-T * 20.f)) * FMath::Exp(-T * 13.f) * 0.9f + V.Lp * FMath::Exp(-T * 30.f) * 1.4f;
		break;
	}
	case EMXSfx::Bump:
		Out = Sine(95.f) * FMath::Exp(-T * 32.f) * 0.8f + Noise() * FMath::Exp(-T * 60.f) * 0.2f;
		break;
	case EMXSfx::Crash:
	{
		const float N = Noise();
		const float Cut = 0.25f * FMath::Exp(-T * 3.f) + 0.02f;
		V.Lp += (N - V.Lp) * Cut;
		V.Phase2 += 1234.f * Dt;
		V.Phase2 -= FMath::FloorToFloat(V.Phase2);
		Out = V.Lp * FMath::Exp(-T * 3.5f) * 1.6f + Sine(410.f) * FMath::Sin(2.f * PI * V.Phase2) * FMath::Exp(-T * 7.f) * 0.35f;
		break;
	}
	case EMXSfx::Cool:
	{
		const float N = Noise();
		V.Lp += (N - V.Lp) * 0.5f;
		const float Hiss = N - V.Lp;
		const float Env = FMath::Min(1.f, T * 40.f) * FMath::Exp(-T * 4.f);
		Out = Hiss * Env * 0.7f + Sine(900.f - T * 700.f) * Env * 0.12f;
		break;
	}
	case EMXSfx::Overheat:
	{
		const float N = Noise();
		V.Lp += (N - V.Lp) * 0.4f;
		const float Hiss = (N - V.Lp) * FMath::Exp(-T * 1.2f) * 0.5f;
		const float Pop = (FMath::Fmod(T * 9.f, 1.f) < 0.08f && T < 0.9f) ? Noise() * 0.9f : 0.f;
		Out = Hiss + Pop * FMath::Exp(-T * 2.f);
		break;
	}
	case EMXSfx::Warning:
	{
		const bool bOn = FMath::Fmod(T, 0.16f) < 0.1f;
		Out = bOn ? (Sine(1320.f) > 0.f ? 0.25f : -0.25f) : 0.f;
		break;
	}
	case EMXSfx::Beep:
		Out = Sine(880.f) * (T < 0.16f ? 0.5f : 0.f);
		break;
	case EMXSfx::Go:
		Out = (Sine(1760.f) * 0.4f + Sine(880.f) * 0.2f) * FMath::Exp(-T * 3.f);
		break;
	case EMXSfx::MenuMove:
		Out = Sine(1250.f) * FMath::Exp(-T * 60.f) * 0.35f;
		break;
	case EMXSfx::MenuSelect:
		Out = Sine(T < 0.06f ? 990.f : 1480.f) * FMath::Exp(-T * 14.f) * 0.4f;
		break;
	case EMXSfx::Join:
		Out = Sine(600.f + T * 3000.f) * FMath::Exp(-T * 10.f) * 0.4f;
		break;
	case EMXSfx::Leave:
		Out = Sine(1200.f - T * 2500.f) * FMath::Exp(-T * 10.f) * 0.35f;
		break;
	case EMXSfx::Cheer:
	{
		// Crowd swell: band-passed noise with a rise and fall, plus scattered whoops.
		const float N = Noise();
		V.Lp += (N - V.Lp) * 0.2f;
		const float Band = V.Lp - V.Phase2;
		V.Phase2 += (V.Lp - V.Phase2) * 0.02f;
		const float Env = FMath::Sin(PI * FMath::Clamp(T / V.Duration, 0.f, 1.f));
		const float Whoop = (Noise() > 0.9995f) ? 1.f : 0.f;
		Out = Band * Env * 1.8f + Whoop * 0.3f;
		break;
	}
	case EMXSfx::Fanfare:
	{
		static const float Notes[] = {523.25f, 659.25f, 783.99f, 1046.5f, 783.99f, 1046.5f};
		const int32 Idx = FMath::Clamp((int32)(T / 0.18f), 0, 5);
		const float Local = T - Idx * 0.18f;
		const float S = Sine(Notes[Idx]);
		Out = (S * 0.35f + (S > 0.f ? 0.08f : -0.08f)) * FMath::Exp(-Local * 4.f) * (Idx == 5 ? 1.f : 0.9f);
		break;
	}
	default:
		break;
	}
	return Out * V.Volume;
}

int32 UMXEngineAudioComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const float Dt = 1.f / (float)Rate;

	// New one-shots.
	FShot Shot;
	while (Shots.Dequeue(Shot))
	{
		if (Voices.Num() >= 12)
		{
			Voices.RemoveAt(0);
		}
		FVoice V;
		V.Type = Shot.Type;
		V.Volume = Shot.Volume;
		switch (Shot.Type)
		{
		case EMXSfx::Crash: V.Duration = 1.2f; break;
		case EMXSfx::Cool: V.Duration = 0.8f; break;
		case EMXSfx::Overheat: V.Duration = 1.8f; break;
		case EMXSfx::Warning: V.Duration = 0.34f; break;
		case EMXSfx::Cheer: V.Duration = 2.6f; break;
		case EMXSfx::Fanfare: V.Duration = 1.3f; break;
		case EMXSfx::Go: V.Duration = 0.7f; break;
		case EMXSfx::Land: V.Duration = 0.45f; break;
		default: V.Duration = 0.25f; break;
		}
		Voices.Add(V);
	}

	const bool bUI = UIMode.load() > 0.5f;
	const float Mute = Muted.load() > 0.5f ? 0.f : 1.f;
	const float TSpeed = TargetSpeed.load();
	const float TThrottle = TargetThrottle.load();
	const float TTurbo = TargetTurbo.load();
	const float THeat = TargetHeat.load();
	const bool bAir = TargetAir.load() > 0.5f;
	const float TOff = TargetOff.load();
	const float Variant = 1.f + ((Seed.load() * 37) % 11 - 5) * 0.012f; // each rider sounds slightly different

	// Simple 5-speed gearbox: RPM climbs through each gear and drops at the shift.
	float TargetRpmFrac;
	if (bAir)
	{
		TargetRpmFrac = TThrottle > 0.1f ? 0.95f : 0.45f;
	}
	else if (TSpeed < 0.03f)
	{
		TargetRpmFrac = 0.05f + TThrottle * 0.45f;
	}
	else
	{
		const float G = TSpeed * 5.f / 1.08f;
		const float InGear = G - FMath::FloorToFloat(FMath::Min(G, 4.999f));
		const float Top = FMath::Min(G, 5.f) >= 4.999f ? FMath::Min(1.f, 0.55f + (G - 4.f) * 0.4f) : 0.42f + 0.55f * InGear;
		TargetRpmFrac = FMath::Lerp(Top * 0.8f, Top, TThrottle);
	}
	const float TargetRpm = TOff > 0.5f ? 0.f : (IdleRPM + (MaxRPM - IdleRPM) * TargetRpmFrac) * Variant;

	for (int32 i = 0; i < NumSamples; ++i)
	{
		// Parameter smoothing.
		const float RpmRate = TargetRpm > Rpm ? 9.f : 5.f;
		Rpm += (TargetRpm - Rpm) * FMath::Min(1.f, RpmRate * Dt);
		Load += (TThrottle - Load) * FMath::Min(1.f, 12.f * Dt);
		Turbo += (TTurbo - Turbo) * FMath::Min(1.f, 8.f * Dt);
		Heat += (THeat - Heat) * FMath::Min(1.f, 2.f * Dt);
		Off += (TOff - Off) * FMath::Min(1.f, 3.f * Dt);

		float Out = 0.f;
		if (!bUI)
		{
			// Combustion pulses: one per revolution (two-stroke single).
			const float Freq = FMath::Max(1.f, Rpm / 60.f);
			CyclePhase += Freq * Dt;
			if (CyclePhase >= 1.f)
			{
				CyclePhase -= 1.f;
			}
			const float Duty = 0.14f;
			const float Pulse = CyclePhase < Duty ? FMath::Sin(PI * CyclePhase / Duty) : 0.f;
			const float Env = FMath::Exp(-CyclePhase * 6.f);
			float Raw = Pulse * (0.7f + 0.3f * Load) + Noise() * Env * (0.18f + 0.35f * Load + 0.25f * Turbo);

			// Exhaust resonance (state-variable band-pass).
			const float Fc = FMath::Clamp(160.f + 650.f * Load + Rpm * 0.025f + 300.f * Turbo, 80.f, 4000.f);
			const float F = 2.f * FMath::Sin(PI * Fc / (float)Rate);
			const float Q = 1.f / 2.6f;
			Bp2 += F * Bp1;
			const float High = Raw - Bp2 - Q * Bp1;
			Bp1 += F * High;
			Lp += (Raw - Lp) * 0.12f;
			float Eng = Bp1 * 0.9f + Lp * 0.5f;

			// Turbo whine.
			WhinePhase += (Freq * 4.1f + 900.f * Turbo) * Dt;
			WhinePhase -= FMath::FloorToFloat(WhinePhase);
			Eng += FMath::Sin(2.f * PI * WhinePhase) * 0.1f * Turbo;

			const float Amp = (0.3f + 0.7f * Load) * FMath::Clamp(Rpm / (IdleRPM * 0.8f), 0.f, 1.f);
			Out = Eng * Amp * (1.f - Off);

			// Heat: knock above ~75%, steam hiss rising with heat.
			if (Heat > 0.75f && Off < 0.5f)
			{
				KnockTimer -= Dt;
				if (KnockTimer <= 0.f)
				{
					KnockTimer = FMath::Lerp(0.35f, 0.05f, (Heat - 0.75f) * 4.f) * (0.6f + 0.8f * (Noise() * 0.5f + 0.5f));
					Hp = 1.f;
				}
			}
			Hp *= 0.992f;
			const float N = Noise();
			const float Hiss = (N - HpPrev) * 0.5f;
			HpPrev = N;
			Out += Noise() * Hp * 0.25f;
			Out += Hiss * FMath::Max(0.f, Heat - 0.7f) * 0.8f + Hiss * Off * 0.25f;

			// Stalled: occasional sputter pops while the engine is off.
			if (Off > 0.5f)
			{
				SputterTimer -= Dt;
				if (SputterTimer <= 0.f)
				{
					SputterTimer = 0.15f + (Noise() * 0.5f + 0.5f) * 0.5f;
					Hp = 0.8f;
				}
			}
			Out *= EngineVolume;
		}

		for (int32 v = Voices.Num() - 1; v >= 0; --v)
		{
			FVoice& V = Voices[v];
			Out += RenderVoice(V, Dt);
			V.Time += Dt;
			if (V.Time >= V.Duration)
			{
				Voices.RemoveAtSwap(v);
			}
		}
		// Soft clip.
		OutAudio[i] = FMath::Clamp(SoftTanh(Out * 1.5f) * 0.75f * Mute, -1.f, 1.f);
	}
	return NumSamples;
}
