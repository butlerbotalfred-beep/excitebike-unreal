#include "Race/MXGameMode.h"
#include "Race/MXRaceManager.h"
#include "Race/MXPlayerController.h"
#include "Race/MXEnvironment.h"
#include "Bike/MXBike.h"
#include "Track/MXTrackActor.h"
#include "Track/MXCourseLibrary.h"
#include "Session/MXGameInstance.h"
#include "UI/MXHUD.h"
#include "UI/MXFrontend.h"
#include "UI/MXDraw.h"
#include "Designer/MXDesigner.h"
#include "Dev/MXAutoTest.h"
#include "FX/MXFXManager.h"
#include "Core/MXTuning.h"
#include "HeatlineMX.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Canvas.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"

AMXGameMode::AMXGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PlayerControllerClass = AMXPlayerController::StaticClass();
	HUDClass = AMXHUD::StaticClass();
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	BikeClass = AMXBike::StaticClass();
}

UMXGameInstance* AMXGameMode::GI() const
{
	return Cast<UMXGameInstance>(GetGameInstance());
}

void AMXGameMode::BeginPlay()
{
	Super::BeginPlay();
	MXTuning::LoadAssets();
	UWorld* W = GetWorld();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Environment = W->SpawnActor<AMXEnvironment>(AMXEnvironment::StaticClass(), FTransform::Identity, P);
	TrackActor = W->SpawnActor<AMXTrack>(AMXTrack::StaticClass(), FTransform::Identity, P);
	Race = W->SpawnActor<AMXRaceManager>(AMXRaceManager::StaticClass(), FTransform::Identity, P);
	Race->OnRaceOver.AddUObject(this, &AMXGameMode::OnRaceOver);
	Race->BikeClass = BikeClass ? BikeClass : TSubclassOf<AMXBike>(AMXBike::StaticClass());
	AMXFXManager::Get(W);

	Frontend = NewObject<UMXFrontend>(this);
	Frontend->Init(this);
	Designer = NewObject<UMXDesigner>(this);
	Designer->Init(this);

	LocalIndexToSlot = {0};
	if (AMXPlayerController* PC0 = GetPCForSlot(0))
	{
		PC0->Slot = 0;
		if (GI())
		{
			PC0->ApplyProfile(GI()->ProfileFor(0));
		}
	}
	StartAttract();

	FString Scenario;
	if (FParse::Value(FCommandLine::Get(), TEXT("mxautotest="), Scenario))
	{
		AutoTest = NewObject<UMXAutoTest>(this);
		AutoTest->Start(this, Scenario);
	}
}

void AMXGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bAttract && Race)
	{
		AttractLeaderTimer -= DeltaSeconds;
		if (AttractLeaderTimer <= 0.f && Race->GetStandings().Num() > 0)
		{
			AttractLeaderTimer = 0.5f;
			if (AMXPlayerController* PC0 = GetPCForSlot(LocalIndexToSlot.Num() > 0 ? LocalIndexToSlot[0] : 0))
			{
				AMXBike* Leader = Race->GetBike(Race->GetStandings()[0]);
				if (PC0->FocusBike.Get() != Leader)
				{
					PC0->FocusBike = Leader;
				}
			}
		}
		if (Race->IsRaceOver())
		{
			StartAttract();
		}
	}
	if (Frontend)
	{
		Frontend->Tick(DeltaSeconds);
	}
	if (Designer && State == EMXAppState::Designer)
	{
		Designer->Tick(DeltaSeconds);
	}
	if (AutoTest)
	{
		AutoTest->Tick(DeltaSeconds);
	}
}

bool AMXGameMode::IsRaceHUDVisible() const
{
	return !bAttract && (State == EMXAppState::Race || State == EMXAppState::DesignerTest || State == EMXAppState::Results);
}

bool AMXGameMode::SlotUsesKeyboard(int32 Slot) const
{
	return GI() && Slot >= 0 && Slot < 4 && GI()->Slots[Slot].UsesKeyboard();
}

void AMXGameMode::StartAttract()
{
	bAttract = true;
	State = EMXAppState::Frontend;
	bPaused = false;
	Race->SetPaused(false);
	RemoveExtraLocalPlayers();
	FMXRaceConfig C;
	C.Mode = EMXRaceMode::Attract;
	C.CourseId = TEXT("nes-t1");
	C.Variant = EMXLayoutVariant::Main;
	C.Laps = 2;
	static const TCHAR* Names[] = {TEXT("Rook"), TEXT("Vega"), TEXT("Kato"), TEXT("Mira"), TEXT("Dash"), TEXT("Ozzy")};
	for (int32 i = 0; i < 4; ++i)
	{
		FMXRacerConfig R;
		R.Name = Names[i];
		R.ColorIndex = (i + 1) % MX::NumRiderColors;
		R.Difficulty = i < 2 ? EMXAIDifficulty::Hard : EMXAIDifficulty::Medium;
		R.bChaser = false;
		C.Racers.Add(R);
	}
	FMXTrackDefinition Def;
	if (!GI() || !GI()->GetCourses()->GetCourse(C.CourseId, Def))
	{
		Def = UMXCourseLibrary::MakeTestStrip();
	}
	Race->Setup(C, Def, TrackActor, true);
	if (AMXPlayerController* PC0 = GetPCForSlot(LocalIndexToSlot.Num() > 0 ? LocalIndexToSlot[0] : 0))
	{
		PC0->UnPossess();
		PC0->CamMode = EMXCamMode::Attract;
		PC0->FocusBike = Race->GetBike(0);
		PC0->bSnapCamera = true;
	}
	AttractLeaderTimer = 0.f;
}

FMXRaceConfig AMXGameMode::BuildConfig(EMXRaceMode Mode, const FString& CourseId, EMXLayoutVariant Variant, int32 Laps) const
{
	FMXRaceConfig C;
	C.Mode = Mode;
	C.CourseId = CourseId;
	C.Variant = Variant;
	C.Laps = Laps;
	UMXGameInstance* G = GI();
	const FMXSettings& Settings = G->GetSave()->Settings;
	int32 Humans = 0;
	for (int32 s = 0; s < 4; ++s)
	{
		const FMXPlayerSlot& Slot = G->Slots[s];
		if (!Slot.bJoined)
		{
			continue;
		}
		if ((Mode == EMXRaceMode::TimeTrial || Mode == EMXRaceMode::RaceAI || Mode == EMXRaceMode::DesignerTest) && Humans >= 1)
		{
			break;
		}
		FMXRacerConfig R;
		R.Name = FString::Printf(TEXT("P%d"), s + 1);
		R.PlayerSlot = s;
		R.ColorIndex = Slot.ColorIndex;
		R.bLandingAssist = Slot.Profile.bLandingAssist;
		R.ChampionshipId = s;
		C.Racers.Add(R);
		++Humans;
	}
	int32 AICount = 0;
	switch (Mode)
	{
	case EMXRaceMode::RaceAI: AICount = FMath::Clamp(Settings.AICountRace, 1, MX::MaxBikes - 1); break;
	case EMXRaceMode::Versus: AICount = FMath::Clamp(Settings.AICountVersus, 0, MX::MaxBikes - Humans); break;
	case EMXRaceMode::Championship: AICount = FMath::Clamp(FMath::Max(Settings.AICountVersus, Humans == 1 ? 5 : Settings.AICountVersus), 0, MX::MaxBikes - Humans); break;
	default: AICount = 0; break;
	}
	static const TCHAR* Names[] = {TEXT("Rook"), TEXT("Vega"), TEXT("Kato"), TEXT("Mira"), TEXT("Dash"), TEXT("Ozzy"), TEXT("Juno")};
	for (int32 i = 0; i < AICount; ++i)
	{
		FMXRacerConfig R;
		R.Name = Names[i % 7];
		R.PlayerSlot = -1;
		R.ColorIndex = G->FirstFreeColor((i + 4) % MX::NumRiderColors, -1);
		// Avoid duplicate colours with other AI where possible.
		for (const FMXRacerConfig& Other : C.Racers)
		{
			if (Other.ColorIndex == R.ColorIndex)
			{
				R.ColorIndex = (R.ColorIndex + 1 + i) % MX::NumRiderColors;
			}
		}
		R.Difficulty = Settings.AIDifficulty;
		R.bChaser = (i % 2 == 1) && Settings.AIDifficulty != EMXAIDifficulty::Easy; // NES: some rivals pursue
		R.ChampionshipId = 4 + i;
		C.Racers.Add(R);
	}
	return C;
}

bool AMXGameMode::StartRace(const FMXRaceConfig& Config)
{
	FMXTrackDefinition Def;
	if (Config.Mode == EMXRaceMode::DesignerTest)
	{
		Def = DesignerTestDef;
	}
	else if (!GI()->GetCourses()->GetCourse(Config.CourseId, Def))
	{
		UE_LOG(LogHeatline, Warning, TEXT("StartRace: unknown course %s"), *Config.CourseId);
		return false;
	}
	bAttract = false;
	TArray<int32> Slots;
	for (const FMXRacerConfig& R : Config.Racers)
	{
		if (R.IsHuman())
		{
			Slots.Add(R.PlayerSlot);
		}
	}
	SetupLocalPlayers(Slots);
	if (!Race->Setup(Config, Def, TrackActor, true))
	{
		return false;
	}
	GI()->LastRace = Config;
	if (Config.Mode == EMXRaceMode::TimeTrial)
	{
		const int32 Laps = Config.Laps > 0 ? Config.Laps : Def.Laps;
		if (const FMXGhostData* G = GI()->GetGhost(Config.CourseId, Config.Variant, Laps))
		{
			Race->SetGhost(*G);
		}
	}
	PossessBikes();
	State = Config.Mode == EMXRaceMode::DesignerTest ? EMXAppState::DesignerTest : EMXAppState::Race;
	bPaused = false;
	Race->SetPaused(false);
	Frontend->OnRaceStarted();
	UE_LOG(LogHeatline, Log, TEXT("Race started: %s (%d racers, %d humans)"), *Config.CourseId, Config.Racers.Num(), Slots.Num());
	return true;
}

void AMXGameMode::PossessBikes()
{
	const FMXRaceConfig& C = Race->GetConfig();
	for (int32 i = 0; i < C.Racers.Num(); ++i)
	{
		const FMXRacerConfig& R = C.Racers[i];
		if (!R.IsHuman())
		{
			continue;
		}
		if (AMXPlayerController* PC = GetPCForSlot(R.PlayerSlot))
		{
			AMXBike* Bike = Race->GetBike(i);
			if (PC->GetPawn() != Bike)
			{
				PC->UnPossess();
				PC->Possess(Bike);
			}
			PC->FocusBike = Bike;
			PC->CamMode = EMXCamMode::FollowBike;
			PC->bSnapCamera = true;
			PC->ApplyProfile(GI()->Slots[R.PlayerSlot].Profile);
		}
	}
}

void AMXGameMode::SetupLocalPlayers(const TArray<int32>& InSlots)
{
	TArray<int32> Slots = InSlots;
	if (Slots.Num() == 0)
	{
		Slots.Add(0);
	}
	UGameInstance* G = GetGameInstance();
	while (G->GetNumLocalPlayers() < Slots.Num())
	{
		UGameplayStatics::CreatePlayer(this, 10 + G->GetNumLocalPlayers(), true);
	}
	while (G->GetNumLocalPlayers() > Slots.Num())
	{
		ULocalPlayer* LP = G->GetLocalPlayerByIndex(G->GetNumLocalPlayers() - 1);
		if (LP && LP->PlayerController)
		{
			LP->PlayerController->UnPossess();
		}
		G->RemoveLocalPlayer(LP);
	}
	LocalIndexToSlot = Slots;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		ULocalPlayer* LP = G->GetLocalPlayerByIndex(i);
		if (AMXPlayerController* PC = LP ? Cast<AMXPlayerController>(LP->PlayerController) : nullptr)
		{
			PC->Slot = Slots[i];
			PC->ApplyProfile(GI()->Slots[Slots[i]].Profile);
			PC->bSnapCamera = true;
		}
	}
}

void AMXGameMode::RemoveExtraLocalPlayers()
{
	const int32 Keep = (GI() && GI()->Slots[0].bJoined) ? 0 : 0;
	TArray<int32> One;
	One.Add(Keep);
	if (GetGameInstance()->GetNumLocalPlayers() > 1 || LocalIndexToSlot.Num() != 1)
	{
		SetupLocalPlayers(One);
	}
}

int32 AMXGameMode::NumLocalPlayers() const
{
	return GetGameInstance() ? GetGameInstance()->GetNumLocalPlayers() : 0;
}

AMXPlayerController* AMXGameMode::GetPCForSlot(int32 Slot) const
{
	UGameInstance* G = GetGameInstance();
	if (!G)
	{
		return nullptr;
	}
	for (int32 i = 0; i < G->GetNumLocalPlayers(); ++i)
	{
		ULocalPlayer* LP = G->GetLocalPlayerByIndex(i);
		AMXPlayerController* PC = LP ? Cast<AMXPlayerController>(LP->PlayerController) : nullptr;
		if (PC && (LocalIndexToSlot.IsValidIndex(i) ? LocalIndexToSlot[i] : i) == Slot)
		{
			return PC;
		}
	}
	// Frontend: local player 0 serves whoever is driving the menus.
	if (G->GetNumLocalPlayers() > 0 && Slot == 0)
	{
		return Cast<AMXPlayerController>(G->GetLocalPlayerByIndex(0)->PlayerController);
	}
	return nullptr;
}

AMXPlayerController* AMXGameMode::GetPCForDevice(int32 DeviceKey) const
{
	const int32 Slot = GI() ? GI()->SlotForDevice(DeviceKey) : INDEX_NONE;
	return Slot == INDEX_NONE ? nullptr : GetPCForSlot(Slot);
}

bool AMXGameMode::WantsRaceInput() const
{
	return !bAttract && !bPaused && (State == EMXAppState::Race || State == EMXAppState::DesignerTest) && LostDeviceSlot() == INDEX_NONE;
}

void AMXGameMode::RequestPause(int32 Slot)
{
	if (bAttract || (State != EMXAppState::Race && State != EMXAppState::DesignerTest))
	{
		return;
	}
	SetPaused(!bPaused, Slot);
}

void AMXGameMode::SetPaused(bool bInPaused, int32 BySlot)
{
	bPaused = bInPaused;
	PausedBy = bInPaused ? BySlot : -1;
	Race->SetPaused(bInPaused);
	for (int32 i = 0; i < Race->NumRacers(); ++i)
	{
		if (AMXBike* B = Race->GetBike(i))
		{
			B->CustomTimeDilation = bInPaused ? 0.f : 1.f;
		}
	}
	Frontend->OnPauseChanged(bInPaused, BySlot);
}

void AMXGameMode::OnRaceOver()
{
	if (bAttract)
	{
		return; // restarted from Tick
	}
	ComputeResults();
	State = EMXAppState::Results;
	Frontend->OnResults();
	if (AutoTest)
	{
		AutoTest->OnRaceOver();
	}
}

void AMXGameMode::ComputeResults()
{
	Results.Reset();
	const FMXRaceConfig& C = Race->GetConfig();
	const FMXTrackModel& Track = Race->GetTrack();
	UMXGameInstance* G = GI();
	FMXChampionship& Champ = G->Championship;
	for (int32 Pos = 0; Pos < Race->GetStandings().Num(); ++Pos)
	{
		const int32 i = Race->GetStandings()[Pos];
		const FMXRacerProgress& P = Race->GetProgress(i);
		const FMXRacerConfig& R = C.Racers[i];
		const AMXBike* Bike = Race->GetBike(i);
		FMXResultRow Row;
		Row.Racer = i;
		Row.Position = Pos + 1;
		Row.Name = R.Name;
		Row.ColorIndex = R.ColorIndex;
		Row.Time = P.FinishTime;
		Row.BestLap = P.BestLap;
		Row.bHuman = R.IsHuman();
		Row.bEstimated = P.bEstimated;
		Row.Crashes = Bike ? Bike->State.Crashes : 0;
		Row.Perfects = Bike ? Bike->State.PerfectLandings : 0;
		if (R.IsHuman() && !P.bEstimated && C.Mode != EMXRaceMode::DesignerTest)
		{
			// Medals are a time-trial concept, kept separate from race positions.
			if (C.Mode == EMXRaceMode::TimeTrial)
			{
				FMXTrackDefinition Def;
				G->GetCourses()->GetCourse(C.CourseId, Def);
				float Gold, Silver, Bronze;
				UMXCourseLibrary::GetMedalTimes(Def, C.Variant, Track.GetLaps(), Gold, Silver, Bronze);
				Row.Medal = P.FinishTime <= Gold ? 3 : (P.FinishTime <= Silver ? 2 : (P.FinishTime <= Bronze ? 1 : 0));
			}
			const FMXGhostData* Ghost = C.Mode == EMXRaceMode::TimeTrial ? &Race->GetRecordedGhost() : nullptr;
			Row.bNewRecord = G->SubmitResult(C.CourseId, C.Variant, Track.GetLaps(), P.FinishTime, P.BestLap, Row.Medal, Ghost);
		}
		if (C.Mode == EMXRaceMode::Championship && Champ.bActive && R.ChampionshipId >= 0)
		{
			if (Champ.Points.Num() <= R.ChampionshipId)
			{
				Champ.Points.SetNumZeroed(R.ChampionshipId + 1);
				Champ.Names.SetNum(R.ChampionshipId + 1);
				Champ.Colors.SetNumZeroed(R.ChampionshipId + 1);
				Champ.LastPositions.SetNumZeroed(R.ChampionshipId + 1);
			}
			Row.Points = FMXChampionship::PointsFor(Row.Position);
			Champ.Points[R.ChampionshipId] += Row.Points;
			Champ.Names[R.ChampionshipId] = R.Name;
			Champ.Colors[R.ChampionshipId] = R.ColorIndex;
			Champ.LastPositions[R.ChampionshipId] = Row.Position;
		}
		Results.Add(Row);
	}
}

void AMXGameMode::Rematch()
{
	if (State == EMXAppState::Results || State == EMXAppState::Race)
	{
		const FMXRaceConfig C = Race->GetConfig();
		Race->Restart();
		if (C.Mode == EMXRaceMode::TimeTrial)
		{
			if (const FMXGhostData* G = GI()->GetGhost(C.CourseId, C.Variant, Race->GetTrack().GetLaps()))
			{
				Race->SetGhost(*G);
			}
		}
		PossessBikes();
		State = C.Mode == EMXRaceMode::DesignerTest ? EMXAppState::DesignerTest : EMXAppState::Race;
		SetPaused(false);
		Frontend->OnRaceStarted();
	}
}

void AMXGameMode::BeginChampionship(const FMXRaceConfig& Template)
{
	FMXChampionship& Champ = GI()->Championship;
	Champ = FMXChampionship();
	Champ.bActive = true;
	Champ.Courses = GI()->GetCourses()->ChampionshipOrder();
	Champ.RaceIndex = 0;
	FMXRaceConfig C = Template;
	C.Mode = EMXRaceMode::Championship;
	C.CourseId = Champ.Courses.Num() > 0 ? Champ.Courses[0] : TEXT("nes-t1");
	StartRace(C);
}

void AMXGameMode::NextChampionshipRace()
{
	FMXChampionship& Champ = GI()->Championship;
	if (!Champ.bActive)
	{
		ReturnToLobby();
		return;
	}
	++Champ.RaceIndex;
	if (Champ.RaceIndex >= Champ.Courses.Num())
	{
		Champ.bActive = false;
		ReturnToLobby();
		return;
	}
	FMXRaceConfig C = GI()->LastRace;
	C.CourseId = Champ.Courses[Champ.RaceIndex];
	StartRace(C);
}

void AMXGameMode::ReturnToLobby()
{
	Race->Teardown();
	SetPaused(false);
	GI()->ResetLobby(true);
	StartAttract();
	Frontend->GoToLobby();
}

void AMXGameMode::ReturnToMainMenu()
{
	Race->Teardown();
	SetPaused(false);
	StartAttract();
	Frontend->GoToMainMenu();
}

void AMXGameMode::EnterDesigner()
{
	bAttract = false;
	Race->Teardown();
	RemoveExtraLocalPlayers();
	State = EMXAppState::Designer;
	Designer->Open();
}

void AMXGameMode::ExitDesigner()
{
	Designer->Close();
	ReturnToMainMenu();
}

bool AMXGameMode::StartDesignerTest(const FMXTrackDefinition& Def)
{
	DesignerTestDef = Def;
	DesignerTestDef.Id = TEXT("designer-test");
	FMXRaceConfig C = BuildConfig(EMXRaceMode::DesignerTest, TEXT("designer-test"), EMXLayoutVariant::Main, Def.Laps);
	if (C.Racers.Num() == 0)
	{
		FMXRacerConfig R;
		R.Name = TEXT("P1");
		R.PlayerSlot = 0;
		R.ColorIndex = GI()->Slots[0].ColorIndex;
		C.Racers.Add(R);
	}
	return StartRace(C);
}

void AMXGameMode::EndDesignerTest()
{
	Race->Teardown();
	SetPaused(false);
	RemoveExtraLocalPlayers();
	State = EMXAppState::Designer;
	Designer->Resume();
}

int32 AMXGameMode::LostDeviceSlot() const
{
	if (!GI())
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < 4; ++i)
	{
		if (GI()->Slots[i].bJoined && GI()->Slots[i].bDeviceLost)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void AMXGameMode::HandleDeviceConnection(int32 DeviceKey, bool bConnected)
{
	UMXGameInstance* G = GI();
	if (!G || DeviceKey < 0)
	{
		return;
	}
	for (int32 i = 0; i < 4; ++i)
	{
		FMXPlayerSlot& S = G->Slots[i];
		if (!S.bJoined || S.DeviceKey != DeviceKey)
		{
			continue;
		}
		S.bDeviceLost = !bConnected;
		if (!bConnected)
		{
			UE_LOG(LogHeatline, Warning, TEXT("P%d controller disconnected"), i + 1);
			if ((State == EMXAppState::Race || State == EMXAppState::DesignerTest) && !bPaused)
			{
				SetPaused(true, i);
			}
		}
		else
		{
			UE_LOG(LogHeatline, Log, TEXT("P%d controller reconnected"), i + 1);
		}
		Frontend->OnDeviceChanged(i, bConnected);
	}
}

void AMXGameMode::HandleMenuAction(int32 DeviceKey, EMXMenuAction Action)
{
	UMXGameInstance* G = GI();
	// A free controller can take over a slot whose controller was lost.
	const int32 Lost = LostDeviceSlot();
	if (Lost != INDEX_NONE && Action == EMXMenuAction::Confirm && G->SlotForDevice(DeviceKey) == INDEX_NONE)
	{
		G->Slots[Lost].DeviceKey = DeviceKey;
		G->Slots[Lost].bDeviceLost = false;
		Frontend->OnDeviceChanged(Lost, true);
		return;
	}
	if (State == EMXAppState::Designer)
	{
		Designer->HandleAction(DeviceKey, Action);
		return;
	}
	Frontend->HandleAction(DeviceKey, Action);
}

void AMXGameMode::HandleAnyKey(int32 DeviceKey, const FKey& Key)
{
	if (Frontend)
	{
		Frontend->HandleAnyKey(DeviceKey, Key);
	}
	if (State == EMXAppState::Designer && Designer)
	{
		Designer->HandleAnyKey(DeviceKey, Key);
	}
}

void AMXGameMode::HandleTextChar(int32 DeviceKey, TCHAR Char)
{
	if (State == EMXAppState::Designer && Designer)
	{
		Designer->HandleTextChar(Char);
	}
}

void AMXGameMode::DrawOverlay(UCanvas* Canvas)
{
	if (!Canvas)
	{
		return;
	}
	// 3 players: the fourth quadrant shows live standings.
	if (NumLocalPlayers() == 3 && (State == EMXAppState::Race || State == EMXAppState::Results) && Race)
	{
		const float W = Canvas->ClipX;
		const float H = Canvas->ClipY;
		const float X0 = W * 0.5f;
		const float Y0 = H * 0.5f;
		const float U = FMath::Max(0.7f, (H * 0.5f) / 720.f);
		MXDraw::Rect(Canvas, X0, Y0, W * 0.5f, H * 0.5f, FLinearColor(0.02f, 0.03f, 0.05f, 0.92f));
		MXDraw::Text(Canvas, TEXT("STANDINGS"), X0 + W * 0.25f, Y0 + 24.f * U, 38.f * U, FLinearColor(1.f, 0.85f, 0.2f), 0.5f, 0.f);
		float Y = Y0 + 80.f * U;
		const float Row = FMath::Min(46.f * U, (H * 0.5f - 110.f * U) / FMath::Max(1, Race->NumRacers()));
		for (int32 Pos = 0; Pos < Race->GetStandings().Num(); ++Pos)
		{
			const int32 R = Race->GetStandings()[Pos];
			const FMXRacerConfig& RC = Race->GetConfig().Racers[R];
			const FMXRacerProgress& P = Race->GetProgress(R);
			const FLinearColor Col = MX::RiderColor(RC.ColorIndex);
			MXDraw::Rect(Canvas, X0 + 30.f * U, Y + 4.f * U, 10.f * U, Row - 8.f * U, Col);
			MXDraw::Text(Canvas, MXDraw::Ordinal(Pos + 1), X0 + 52.f * U, Y + Row * 0.5f, 28.f * U, FLinearColor::White, 0.f, 0.5f);
			MXDraw::Text(Canvas, RC.Name + (RC.IsHuman() ? TEXT("") : TEXT(" (AI)")), X0 + 130.f * U, Y + Row * 0.5f, 26.f * U, RC.IsHuman() ? Col : FLinearColor(0.85f, 0.85f, 0.85f), 0.f, 0.5f);
			const FString Right = P.bFinished ? MX::FormatRaceTime(P.FinishTime) : FString::Printf(TEXT("LAP %d"), FMath::Min(P.LapsCompleted + 1, Race->GetTrack().GetLaps()));
			MXDraw::Text(Canvas, Right, W - 30.f * U, Y + Row * 0.5f, 24.f * U, FLinearColor::White, 1.f, 0.5f);
			Y += Row;
		}
	}
	if (Frontend)
	{
		Frontend->Draw(Canvas);
	}
	if (State == EMXAppState::Designer && Designer)
	{
		Designer->Draw(Canvas);
	}
	if (AutoTest)
	{
		AutoTest->Draw(Canvas);
	}
}
