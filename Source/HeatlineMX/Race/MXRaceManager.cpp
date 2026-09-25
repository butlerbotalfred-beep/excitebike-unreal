#include "Race/MXRaceManager.h"
#include "Race/MXProgress.h"
#include "Race/MXContact.h"
#include "Bike/MXBike.h"
#include "Track/MXTrackActor.h"
#include "Core/MXTuning.h"
#include "Audio/MXEngineAudio.h"
#include "FX/MXFXManager.h"
#include "HeatlineMX.h"
#include "Engine/World.h"

AMXRaceManager::AMXRaceManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	UIAudio = CreateDefaultSubobject<UMXEngineAudioComponent>(TEXT("UIAudio"));
	UIAudio->SetupAttachment(RootComponent);
}

bool AMXRaceManager::Setup(const FMXRaceConfig& InConfig, const FMXTrackDefinition& Course, AMXTrack* InTrackActor, bool bDressing)
{
	Teardown();
	Config = InConfig;
	TrackActor = InTrackActor;
	MXTuning::LoadAssets();
	const int32 Laps = Config.Laps > 0 ? Config.Laps : Course.Laps;
	Track.Build(Course, Config.Variant, Laps, MXTuning::Style());
	if (!Track.IsBuilt())
	{
		return false;
	}
	if (TrackActor)
	{
		TrackActor->Build(Track, bDressing);
	}
	if (UIAudio)
	{
		UIAudio->SetUIMode(true);
	}
	SpawnRiders();
	return true;
}

void AMXRaceManager::Teardown()
{
	for (AMXBike* B : Bikes)
	{
		if (B)
		{
			if (AController* C = B->GetController())
			{
				C->UnPossess();
			}
			B->Destroy();
		}
	}
	Bikes.Reset();
	if (GhostBike)
	{
		GhostBike->Destroy();
		GhostBike = nullptr;
	}
	Progress.Reset();
	AIMemory.Reset();
	AutopilotMemory.Reset();
	Messages.Reset();
	Standings.Reset();
	Phase = EMXRacePhase::None;
	bRaceOver = false;
	if (AMXFXManager* FX = AMXFXManager::Get(GetWorld()))
	{
		FX->ClearAll();
	}
}

void AMXRaceManager::SpawnRiders()
{
	UWorld* World = GetWorld();
	const int32 N = FMath::Min(Config.Racers.Num(), MX::MaxBikes);
	Progress.SetNum(N);
	AIMemory.SetNum(N);
	AutopilotMemory.SetNum(N);
	Messages.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FMXRacerConfig& RC = Config.Racers[i];
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* Cls = BikeClass ? BikeClass.Get() : AMXBike::StaticClass();
		AMXBike* Bike = World->SpawnActor<AMXBike>(Cls, FVector::ZeroVector, FRotator::ZeroRotator, P);
		Bike->SetupRider(i, RC.ColorIndex, RC.Name, !RC.IsHuman(), RC.PlayerSlot);
		Bikes.Add(Bike);
		AIMemory[i].Init(4242 + i * 97, RC.bChaser);
		AutopilotMemory[i].Init(777 + i * 13, false);
		AutopilotMemory[i].bAutopilot = true;
	}
	Restart();
}

void AMXRaceManager::Restart()
{
	const int32 N = Bikes.Num();
	for (int32 i = 0; i < N; ++i)
	{
		// Grid: two rows of four (humans take the front row in slot order).
		const int32 Row = i / MX::NumLanes;
		const int32 Lane = i % MX::NumLanes;
		const float S = -2.5f - Row * 6.5f;
		FMXBikeSim::Spawn(Bikes[i]->State, S, Track.LaneCenterY(Lane), Track);
		Bikes[i]->PrevState = Bikes[i]->State;
		Bikes[i]->CurrentInput = FMXBikeInput();
		Progress[i] = FMXRacerProgress();
		Progress[i].ValidS = S;
		Messages[i].Reset();
		AIMemory[i].PlanLane = Lane;
		AutopilotMemory[i].PlanLane = Lane;
	}
	Phase = EMXRacePhase::Intro;
	PhaseTime = 0.f;
	Countdown = 3.f;
	LastCountBeep = 4;
	RaceTime = 0.f;
	Accumulator = 0.f;
	FinishCounter = 0;
	bRaceOver = false;
	OverTimer = 0.f;
	FirstHumanFinishTime = -1.f;
	GhostRecording = FMXGhostData();
	GhostRecording.CourseKey = Config.CourseId;
	GhostRecordTimer = 0.f;
	TotalContacts = 0;
	TotalContactCrashes = 0;
	UpdateStandings();
	if (GhostBike)
	{
		FMXBikeSim::Spawn(GhostBike->State, -2.5f, Track.LaneCenterY(0), Track);
		GhostBike->PrevState = GhostBike->State;
	}
}

void AMXRaceManager::SetGhost(const FMXGhostData& Ghost)
{
	GhostPlayback = Ghost;
	if (Ghost.Frames.Num() < 2)
	{
		return;
	}
	if (!GhostBike)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GhostBike = GetWorld()->SpawnActor<AMXBike>(AMXBike::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, P);
		GhostBike->SetupRider(99, Ghost.ColorIndex, TEXT("Best"), true, -1);
		GhostBike->MakeGhost();
	}
	FMXBikeSim::Spawn(GhostBike->State, Ghost.Frames[0].S, Ghost.Frames[0].Y, Track);
	GhostBike->PrevState = GhostBike->State;
}

AMXBike* AMXRaceManager::GetBikeForSlot(int32 Slot) const
{
	const int32 R = RacerForSlot(Slot);
	return R >= 0 ? Bikes[R] : nullptr;
}

int32 AMXRaceManager::RacerForSlot(int32 Slot) const
{
	for (int32 i = 0; i < Config.Racers.Num() && i < Bikes.Num(); ++i)
	{
		if (Config.Racers[i].PlayerSlot == Slot)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void AMXRaceManager::AddMessage(int32 Racer, const FString& Text, const FLinearColor& Color, float Duration, float Scale)
{
	if (!Messages.IsValidIndex(Racer))
	{
		return;
	}
	FMXHudMessage M;
	M.Text = Text;
	M.Color = Color;
	M.Time = RaceTime + PhaseTime * 0.f;
	M.Duration = Duration;
	M.Scale = Scale;
	Messages[Racer].Add(M);
	if (Messages[Racer].Num() > 4)
	{
		Messages[Racer].RemoveAt(0);
	}
}

void AMXRaceManager::PlayUISound(uint8 Sfx, float Volume)
{
	if (UIAudio)
	{
		UIAudio->PlayOneShot((EMXSfx)Sfx, Volume);
	}
}

FMXBikeState& AMXRaceManager::MutableState(int32 Racer)
{
	return Bikes[Racer]->State;
}

void AMXRaceManager::ForceCrash(int32 Racer)
{
	if (Bikes.IsValidIndex(Racer))
	{
		FMXBikeSim::Crash(Bikes[Racer]->State, EMXCrashCause::Landing, MXTuning::Bike());
	}
}

FMXBikeInput AMXRaceManager::ComputeAutopilotInput(int32 Racer, float DeltaSeconds)
{
	if (!Bikes.IsValidIndex(Racer))
	{
		return FMXBikeInput();
	}
	OthersScratch.Reset();
	for (int32 j = 0; j < Bikes.Num(); ++j)
	{
		if (j != Racer)
		{
			const FMXBikeState& O = Bikes[j]->State;
			OthersScratch.Add({O.S, O.Y, O.ForwardSpeed(), O.IsRiding(), Config.Racers[j].IsHuman(), j});
		}
	}
	const FMXAISkill& Skill = MXTuning::AI().ForDifficulty(EMXAIDifficulty::Hard);
	return FMXAIBrain::Think(Bikes[Racer]->State, AutopilotMemory[Racer], Track, MXTuning::Bike(), Skill, OthersScratch, DeltaSeconds);
}

void AMXRaceManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase == EMXRacePhase::None || bPaused)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	PhaseTime += Dt;

	switch (Phase)
	{
	case EMXRacePhase::Intro:
		if (PhaseTime > 0.8f)
		{
			Phase = EMXRacePhase::Countdown;
			PhaseTime = 0.f;
		}
		break;
	case EMXRacePhase::Countdown:
	{
		Countdown = 3.f - PhaseTime;
		const int32 Beep = FMath::CeilToInt(Countdown);
		if (Beep < LastCountBeep && Beep >= 1)
		{
			PlayUISound((uint8)EMXSfx::Beep, 0.8f);
		}
		LastCountBeep = Beep;
		if (Countdown <= 0.f)
		{
			Phase = EMXRacePhase::Racing;
			PhaseTime = 0.f;
			PlayUISound((uint8)EMXSfx::Go, 1.f);
			for (int32 i = 0; i < Bikes.Num(); ++i)
			{
				Progress[i].LapStartTime = 0.f;
			}
		}
		// Keep bikes on the grid; engines rev with the throttle but nothing moves.
		for (AMXBike* B : Bikes)
		{
			B->PrevState = B->State;
			B->SetInterpolation(1.f);
		}
		break;
	}
	case EMXRacePhase::Racing:
	case EMXRacePhase::Finished:
	{
		Accumulator += Dt;
		int32 Steps = 0;
		while (Accumulator >= FMXBikeSim::FixedDt && Steps < 12)
		{
			StepSimulation(FMXBikeSim::FixedDt);
			Accumulator -= FMXBikeSim::FixedDt;
			++Steps;
		}
		const float Alpha = Accumulator / FMXBikeSim::FixedDt;
		for (AMXBike* B : Bikes)
		{
			B->SetInterpolation(Alpha);
		}
		PlayGhost();
		CheckRaceOver(Dt);
		break;
	}
	default:
		break;
	}
	// Age HUD messages.
	for (TArray<FMXHudMessage>& List : Messages)
	{
		List.RemoveAll([this](const FMXHudMessage& M) { return RaceTime - M.Time > M.Duration + 0.5f; });
	}
}

void AMXRaceManager::StepSimulation(float Dt)
{
	const UMXBikeTuning& T = MXTuning::Bike();
	RaceTime += Dt;
	const int32 N = Bikes.Num();

	TArray<float> SPrev;
	SPrev.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		AMXBike* B = Bikes[i];
		B->PrevState = B->State;
		SPrev[i] = B->State.S;
		const FMXRacerConfig& RC = Config.Racers[i];
		FMXBikeInput In;
		if (B->State.bFinished)
		{
			// Cruise down the run-out after the flag.
			In.Throttle = B->State.ForwardSpeed() > 12.f ? 0.f : 0.4f;
			In.bLaneTaps = true;
		}
		else if (RC.IsHuman())
		{
			In = B->CurrentInput;
			B->CurrentInput.MashPresses = 0;
		}
		else
		{
			OthersScratch.Reset();
			for (int32 j = 0; j < N; ++j)
			{
				if (j != i)
				{
					const FMXBikeState& O = Bikes[j]->State;
					OthersScratch.Add({O.S, O.Y, O.ForwardSpeed(), O.IsRiding(), Config.Racers[j].IsHuman(), j, O.CanCollide()});
				}
			}
			In = FMXAIBrain::Think(B->State, AIMemory[i], Track, T, MXTuning::AI().ForDifficulty(RC.Difficulty), OthersScratch, Dt);
			B->CurrentInput = In;
		}
		B->LastInput = In;
		const bool bAssist = RC.IsHuman() ? RC.bLandingAssist : false;
		FMXBikeSim::Step(B->State, In, Track, T, Dt, bAssist);
	}

	ResolveContacts();

	for (int32 i = 0; i < N; ++i)
	{
		AMXBike* B = Bikes[i];
		const FMXBikeState& St = B->State;
		// Recovery placement and other teleports never count as movement through a gate.
		const bool bValidMove = MXProgress::IsValidMove(SPrev[i], St.S, (St.Events & EMXBikeEvent::Recovered) != 0);
		UpdateProgress(i, SPrev[i], bValidMove);
		B->OnSimStep(Track);

		// HUD feedback derived from simulation events.
		if (Config.Racers[i].IsHuman())
		{
			if (St.Events & EMXBikeEvent::Landed)
			{
				switch (St.LastLanding)
				{
				case EMXLandingGrade::Perfect: AddMessage(i, TEXT("PERFECT"), FLinearColor(0.3f, 1.f, 0.4f), 0.9f, 1.1f); break;
				case EMXLandingGrade::Wobble: AddMessage(i, St.LastLandingDelta > 0.f ? TEXT("SLOPPY - NOSE HIGH") : TEXT("SLOPPY - NOSE LOW"), FLinearColor(1.f, 0.75f, 0.2f), 1.1f); break;
				default: break;
				}
			}
			if (St.Events & EMXBikeEvent::Crashed)
			{
				FString Why;
				switch (St.CrashCause)
				{
				case EMXCrashCause::Landing: Why = St.LastLandingDelta > 0.f ? TEXT("CRASH - landed nose high") : TEXT("CRASH - landed nose first"); break;
				case EMXCrashCause::Barrier: Why = TEXT("CRASH - lift the front for barriers"); break;
				case EMXCrashCause::Wheelie: Why = TEXT("CRASH - looped the wheelie"); break;
				case EMXCrashCause::Contact: Why = TEXT("CRASH - clipped a rear wheel"); break;
				case EMXCrashCause::Wall: Why = TEXT("CRASH - hit the face"); break;
				default: Why = TEXT("CRASH"); break;
				}
				AddMessage(i, Why, FLinearColor(1.f, 0.25f, 0.2f), 2.2f, 1.1f);
			}
			if (St.Events & EMXBikeEvent::RecoveryStarted)
			{
				AddMessage(i, TEXT("MASH ACCELERATE TO REMOUNT"), FLinearColor(1.f, 1.f, 1.f), 1.6f, 0.9f);
			}
			if (St.Events & EMXBikeEvent::Overheat)
			{
				AddMessage(i, TEXT("OVERHEATED"), FLinearColor(1.f, 0.2f, 0.1f), T.StallDuration, 1.3f);
			}
			if (St.Events & EMXBikeEvent::CoolStrip)
			{
				AddMessage(i, TEXT("COOLED"), FLinearColor(0.3f, 0.8f, 1.f), 0.9f);
			}
			if (St.Events & EMXBikeEvent::WheelieWarn)
			{
				AddMessage(i, TEXT("EASE OFF THE WHEELIE"), FLinearColor(1.f, 0.7f, 0.2f), 0.8f, 0.9f);
			}
		}
		if (St.Events & EMXBikeEvent::Crashed)
		{
			PlayUISound((uint8)EMXSfx::Cheer, 0.35f);
		}
	}
	RecordGhost();
	UpdateStandings();
}

void AMXRaceManager::ResolveContacts()
{
	TArray<FMXBikeState*> States;
	for (AMXBike* B : Bikes)
	{
		States.Add(&B->State);
	}
	TArray<FMXContactResult> Results;
	MXContact::Resolve(States, Track, MXTuning::Bike(), Results);
	for (const FMXContactResult& R : Results)
	{
		++TotalContacts;
		if (R.Outcome == EMXContactOutcome::Crashed)
		{
			++TotalContactCrashes;
		}
		if (R.Rear != INDEX_NONE && Config.Racers.IsValidIndex(R.Rear) && Config.Racers[R.Rear].IsHuman())
		{
			if (R.Outcome == EMXContactOutcome::CutOff)
			{
				AddMessage(R.Rear, TEXT("CUT OFF!"), FLinearColor(1.f, 0.7f, 0.2f), 1.f);
			}
			else if (R.Outcome == EMXContactOutcome::CooldownBump || R.Outcome == EMXContactOutcome::LandingBump)
			{
				AddMessage(R.Rear, TEXT("BUMPED"), FLinearColor(1.f, 0.7f, 0.2f), 1.f);
			}
		}
	}
}

void AMXRaceManager::UpdateProgress(int32 Racer, float SPrev, bool bValidMove)
{
	FMXRacerProgress& P = Progress[Racer];
	AMXBike* B = Bikes[Racer];
	const FMXProgressStep Step = MXProgress::Advance(P, Track.GetGates(), Track.GetLaps(), Track.RaceFinishS(),
		SPrev, B->State.S, bValidMove, RaceTime, FinishCounter);
	if (Step.bFinished)
	{
		B->State.bFinished = true;
		if (Config.Racers[Racer].IsHuman())
		{
			AddMessage(Racer, FString::Printf(TEXT("FINISHED  %s"), *MX::FormatRaceTime(RaceTime)), FLinearColor(1.f, 0.9f, 0.3f), 4.f, 1.3f);
			if (FirstHumanFinishTime < 0.f)
			{
				FirstHumanFinishTime = RaceTime;
			}
			PlayUISound((uint8)EMXSfx::Fanfare, 0.9f);
		}
		PlayUISound((uint8)EMXSfx::Cheer, 0.6f);
	}
	else if (Step.LapsCompleted > 0 && Config.Racers[Racer].IsHuman())
	{
		AddMessage(Racer, P.LapsCompleted + 1 == Track.GetLaps() ? TEXT("FINAL LAP") : FString::Printf(TEXT("LAP %d"), P.LapsCompleted + 1),
			FLinearColor(1.f, 1.f, 1.f), 1.5f, 1.2f);
	}
}

void AMXRaceManager::UpdateStandings()
{
	const int32 N = Bikes.Num();
	Standings.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		Standings[i] = i;
	}
	const TArray<FMXGate>& Gates = Track.GetGates();
	auto ProgressKey = [this, &Gates](int32 i) -> float
	{
		const FMXRacerProgress& P = Progress[i];
		const float Cap = P.NextGate < Gates.Num() ? Gates[P.NextGate].S : Track.RaceFinishS();
		return FMath::Min(Bikes[i]->State.S, Cap) + P.NextGate * 0.001f;
	};
	Standings.Sort([this, &ProgressKey](int32 A, int32 B)
	{
		const FMXRacerProgress& PA = Progress[A];
		const FMXRacerProgress& PB = Progress[B];
		if (PA.bFinished != PB.bFinished)
		{
			return PA.bFinished;
		}
		if (PA.bFinished)
		{
			return PA.FinishOrder < PB.FinishOrder;
		}
		return ProgressKey(A) > ProgressKey(B);
	});
	for (int32 Pos = 0; Pos < N; ++Pos)
	{
		Progress[Standings[Pos]].Position = Pos + 1;
	}
}

void AMXRaceManager::CheckRaceOver(float DeltaSeconds)
{
	if (bRaceOver || Phase != EMXRacePhase::Racing)
	{
		if (Phase == EMXRacePhase::Finished && !bRaceOver)
		{
			OverTimer += DeltaSeconds;
			if (OverTimer > 3.f)
			{
				FinishRace();
			}
		}
		return;
	}
	bool bAnyHuman = false;
	bool bAllHumansDone = true;
	bool bAllDone = true;
	for (int32 i = 0; i < Bikes.Num(); ++i)
	{
		const bool bDone = Progress[i].bFinished;
		bAllDone &= bDone;
		if (Config.Racers[i].IsHuman())
		{
			bAnyHuman = true;
			bAllHumansDone &= bDone;
		}
	}
	// Close the race when every human is home (or everyone, in AI-only attract races), or when the
	// last humans are far behind the first finisher.
	const bool bTimeout = FirstHumanFinishTime > 0.f && RaceTime - FirstHumanFinishTime > 75.f;
	if ((bAnyHuman && bAllHumansDone) || (!bAnyHuman && bAllDone) || bTimeout)
	{
		Phase = EMXRacePhase::Finished;
		OverTimer = 0.f;
	}
}

void AMXRaceManager::FinishRace()
{
	bRaceOver = true;
	// Riders still out on course get an estimated time from their remaining distance.
	for (int32 i = 0; i < Bikes.Num(); ++i)
	{
		FMXRacerProgress& P = Progress[i];
		if (!P.bFinished)
		{
			const float Remaining = FMath::Max(0.f, Track.RaceFinishS() - Bikes[i]->State.S);
			const float Pace = FMath::Max(8.f, Bikes[i]->State.S / FMath::Max(1.f, RaceTime));
			P.FinishTime = RaceTime + Remaining / Pace;
			P.bEstimated = true;
		}
	}
	// Unfinished riders are ordered by estimated time after every real finisher.
	TArray<int32> Unfinished;
	for (int32 i = 0; i < Bikes.Num(); ++i)
	{
		if (!Progress[i].bFinished)
		{
			Unfinished.Add(i);
		}
	}
	Unfinished.Sort([this](int32 A, int32 B) { return Progress[A].FinishTime < Progress[B].FinishTime; });
	for (int32 i : Unfinished)
	{
		Progress[i].FinishOrder = ++FinishCounter;
		Progress[i].bFinished = true;
	}
	UpdateStandings();
	OnRaceOver.Broadcast();
}

void AMXRaceManager::RecordGhost()
{
	if (Config.Mode != EMXRaceMode::TimeTrial || Bikes.Num() == 0)
	{
		return;
	}
	GhostRecordTimer -= FMXBikeSim::FixedDt;
	if (GhostRecordTimer > 0.f || Progress[0].bFinished)
	{
		if (Progress[0].bFinished && GhostRecording.TotalTime < 0.f)
		{
			GhostRecording.TotalTime = Progress[0].FinishTime;
			GhostRecording.ColorIndex = Config.Racers[0].ColorIndex;
		}
		return;
	}
	GhostRecordTimer = 0.05f;
	const FMXBikeState& S = Bikes[0]->State;
	FMXGhostFrame F;
	F.T = RaceTime;
	F.S = S.VisualS();
	F.Y = S.Y;
	F.Z = S.Z;
	F.Pitch = S.IsAirborne() ? S.Pitch : S.SlopeDeg + S.Wheelie;
	F.Speed = S.ForwardSpeed();
	F.Phase = (uint8)S.Phase;
	GhostRecording.Frames.Add(F);
}

void AMXRaceManager::PlayGhost()
{
	if (!GhostBike || GhostPlayback.Frames.Num() < 2)
	{
		return;
	}
	const TArray<FMXGhostFrame>& F = GhostPlayback.Frames;
	int32 Lo = 0;
	int32 Hi = F.Num() - 1;
	if (RaceTime >= F.Last().T)
	{
		Lo = Hi;
	}
	else
	{
		while (Hi - Lo > 1)
		{
			const int32 Mid = (Lo + Hi) / 2;
			(F[Mid].T <= RaceTime ? Lo : Hi) = Mid;
		}
	}
	const FMXGhostFrame& A = F[Lo];
	const FMXGhostFrame& B = F[FMath::Min(Lo + 1, F.Num() - 1)];
	const float K = B.T > A.T ? FMath::Clamp((RaceTime - A.T) / (B.T - A.T), 0.f, 1.f) : 0.f;
	FMXBikeState& G = GhostBike->State;
	GhostBike->PrevState = G;
	G.S = FMath::Lerp(A.S, B.S, K);
	G.Y = FMath::Lerp(A.Y, B.Y, K);
	G.Z = FMath::Lerp(A.Z, B.Z, K);
	G.Phase = (EMXBikePhase)A.Phase == EMXBikePhase::Airborne ? EMXBikePhase::Airborne : EMXBikePhase::Grounded;
	G.Pitch = FMath::Lerp(A.Pitch, B.Pitch, K);
	G.SlopeDeg = G.Phase == EMXBikePhase::Grounded ? G.Pitch : 0.f;
	G.Wheelie = 0.f;
	G.Speed = FMath::Lerp(A.Speed, B.Speed, K);
	G.Distance += G.Speed * FMXBikeSim::FixedDt;
	GhostBike->SetInterpolation(1.f);
	const bool bHidden = (EMXBikePhase)A.Phase == EMXBikePhase::Crashed || (EMXBikePhase)A.Phase == EMXBikePhase::Recovering;
	GhostBike->SetActorHiddenInGame(bHidden);
}
