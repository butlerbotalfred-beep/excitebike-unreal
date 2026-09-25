#include "Dev/MXAutoTest.h"
#include "Race/MXGameMode.h"
#include "Race/MXRaceManager.h"
#include "Race/MXPlayerController.h"
#include "Bike/MXBike.h"
#include "Session/MXGameInstance.h"
#include "Session/MXGameViewportClient.h"
#include "UI/MXFrontend.h"
#include "UI/MXDraw.h"
#include "Designer/MXDesigner.h"
#include "Track/MXObstacleLibrary.h"
#include "Core/MXTuning.h"
#include "HeatlineMX.h"
#include "Engine/Canvas.h"
#include "Styling/CoreStyle.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "RenderCore.h"
#include "RHI.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	int32 Pad(int32 Slot) { return 1001 + Slot; }
}

UMXGameViewportClient* UMXAutoTest::VC() const
{
	return GEngine ? Cast<UMXGameViewportClient>(GM->GetWorld()->GetGameViewport()) : nullptr;
}

void UMXAutoTest::Start(AMXGameMode* InGM, const FString& InScenario)
{
	GM = InGM;
	Scenario = InScenario.ToLower();
	Step = 0;
	Wait = 2.5f; // let the attract race and track build settle
	UE_LOG(LogHeatline, Display, TEXT("AUTOTEST start: %s"), *Scenario);
}

void UMXAutoTest::Press(int32 Device, const FKey& Key)
{
	if (UMXGameViewportClient* V = VC())
	{
		V->InjectKey(Device, Key, IE_Pressed, 1.f);
		V->InjectKey(Device, Key, IE_Released, 0.f);
	}
}

void UMXAutoTest::Menu(int32 Device, EMXMenuAction Action)
{
	// Menus are driven by real key presses on the virtual device (same mapping as a pad).
	FKey Key;
	switch (Action)
	{
	case EMXMenuAction::Up: Key = EKeys::Gamepad_DPad_Up; break;
	case EMXMenuAction::Down: Key = EKeys::Gamepad_DPad_Down; break;
	case EMXMenuAction::Left: Key = EKeys::Gamepad_DPad_Left; break;
	case EMXMenuAction::Right: Key = EKeys::Gamepad_DPad_Right; break;
	case EMXMenuAction::Confirm: Key = EKeys::Gamepad_FaceButton_Bottom; break;
	case EMXMenuAction::Back: Key = EKeys::Gamepad_FaceButton_Right; break;
	case EMXMenuAction::Start: Key = EKeys::Gamepad_Special_Right; break;
	case EMXMenuAction::Aux: Key = EKeys::Gamepad_FaceButton_Top; break;
	case EMXMenuAction::Aux2: Key = EKeys::Gamepad_FaceButton_Left; break;
	case EMXMenuAction::PageLeft: Key = EKeys::Gamepad_LeftShoulder; break;
	case EMXMenuAction::PageRight: Key = EKeys::Gamepad_RightShoulder; break;
	}
	if (Device == MXDevice::Keyboard)
	{
		switch (Action)
		{
		case EMXMenuAction::Up: Key = EKeys::Up; break;
		case EMXMenuAction::Down: Key = EKeys::Down; break;
		case EMXMenuAction::Left: Key = EKeys::Left; break;
		case EMXMenuAction::Right: Key = EKeys::Right; break;
		case EMXMenuAction::Confirm: Key = EKeys::Enter; break;
		case EMXMenuAction::Back: Key = EKeys::Escape; break;
		case EMXMenuAction::Start: Key = EKeys::P; break;
		case EMXMenuAction::Aux: Key = EKeys::Tab; break;
		case EMXMenuAction::Aux2: Key = EKeys::R; break;
		case EMXMenuAction::PageLeft: Key = EKeys::Q; break;
		case EMXMenuAction::PageRight: Key = EKeys::E; break;
		}
	}
	Press(Device, Key);
}

void UMXAutoTest::Check(const FString& Name, bool bPass, const FString& Detail)
{
	FCheck C;
	C.Name = Name;
	C.bPass = bPass;
	C.Detail = Detail;
	Checks.Add(C);
	UE_LOG(LogHeatline, Display, TEXT("AUTOTEST %s: %s %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
}

void UMXAutoTest::Shot(const FString& Name)
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("AutoTest");
	const FString File = Dir / (Scenario + TEXT("_") + Name + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(File, true, false);
	Screenshots.Add(File);
}

void UMXAutoTest::MenuSelect(int32 Device, int32 Target)
{
	UMXFrontend* F = GM ? GM->GetFrontend() : nullptr;
	for (int32 Guard = 0; F && F->GetMenuIndex() != Target && Guard < 16; ++Guard)
	{
		Menu(Device, F->GetMenuIndex() < Target ? EMXMenuAction::Down : EMXMenuAction::Up);
	}
}

void UMXAutoTest::Next(int32 NewStep, float InWait)
{
	Step = NewStep;
	StepTime = 0.f;
	Wait = InWait;
}

void UMXAutoTest::OnRaceOver()
{
	bRaceOver = true;
	++RaceOverCount;
}

void UMXAutoTest::DriveHumans(float DeltaSeconds)
{
	AMXRaceManager* Race = GM->GetRace();
	UMXGameViewportClient* V = VC();
	if (!Race || !V || !bAutopilot)
	{
		return;
	}
	UMXGameInstance* G = GM->GI();
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const int32 Racer = Race->RacerForSlot(Slot);
		if (Racer == INDEX_NONE || !G->Slots[Slot].bJoined)
		{
			continue;
		}
		const int32 Dev = G->Slots[Slot].DeviceKey;
		const bool bKb = Dev == MXDevice::Keyboard;
		FMXBikeInput In = Race->ComputeAutopilotInput(Racer, DeltaSeconds);
		AMXBike* Bike = Race->GetBike(Racer);
		// Sabotage: hold nose-down through a flight so this rider crashes on landing.
		if (Slot == SabotageSlot && !bSabotaged && Race->GetRaceTime() > SabotageAfter && Bike->State.IsAirborne() && Bike->State.AirTime > 0.1f)
		{
			In.Pitch = -1.f;
			if (Bike->State.Pitch < -45.f)
			{
				bSabotaged = true;
			}
		}
		else if (Slot == SabotageSlot && bSabotaged && !bSawCrash && Bike->State.IsAirborne())
		{
			In.Pitch = -1.f;
		}

		// Throttle.
		if (bKb)
		{
			const bool bWant = In.Throttle > 0.5f;
			static bool bKbThrottle = false;
			if (bWant != bKbThrottle)
			{
				V->InjectKey(Dev, EKeys::X, bWant ? IE_Pressed : IE_Released, bWant ? 1.f : 0.f);
				bKbThrottle = bWant;
			}
		}
		else
		{
			V->InjectAxis(Dev, EKeys::Gamepad_RightTriggerAxis, In.Throttle);
		}
		// Turbo.
		if (In.bTurbo != bTurboDown[Slot])
		{
			V->InjectKey(Dev, bKb ? EKeys::Z : EKeys::Gamepad_FaceButton_Left, In.bTurbo ? IE_Pressed : IE_Released, In.bTurbo ? 1.f : 0.f);
			bTurboDown[Slot] = In.bTurbo;
		}
		// Lanes (classic taps) as d-pad / arrow presses.
		const int32 Dir = In.Steer > 0.5f ? 1 : (In.Steer < -0.5f ? -1 : 0);
		if (Dir != SteerDir[Slot])
		{
			const FKey Up = bKb ? EKeys::Up : EKeys::Gamepad_DPad_Up;
			const FKey Down = bKb ? EKeys::Down : EKeys::Gamepad_DPad_Down;
			if (SteerDir[Slot] < 0) { V->InjectKey(Dev, Up, IE_Released, 0.f); }
			if (SteerDir[Slot] > 0) { V->InjectKey(Dev, Down, IE_Released, 0.f); }
			if (Dir < 0) { V->InjectKey(Dev, Up, IE_Pressed, 1.f); }
			if (Dir > 0) { V->InjectKey(Dev, Down, IE_Pressed, 1.f); }
			SteerDir[Slot] = Dir;
		}
		// Pitch: classic stick X (left = nose up) or arrow keys.
		if (bKb)
		{
			static int32 KbPitch = 0;
			const int32 P = In.Pitch > 0.3f ? 1 : (In.Pitch < -0.3f ? -1 : 0);
			if (P != KbPitch)
			{
				if (KbPitch > 0) { V->InjectKey(Dev, EKeys::Left, IE_Released, 0.f); }
				if (KbPitch < 0) { V->InjectKey(Dev, EKeys::Right, IE_Released, 0.f); }
				if (P > 0) { V->InjectKey(Dev, EKeys::Left, IE_Pressed, 1.f); }
				if (P < 0) { V->InjectKey(Dev, EKeys::Right, IE_Pressed, 1.f); }
				KbPitch = P;
			}
		}
		else
		{
			V->InjectAxis(Dev, EKeys::Gamepad_LeftX, -In.Pitch);
			V->InjectAxis(Dev, EKeys::Gamepad_LeftY, 0.f);
		}
		// Mash accelerate while recovering (a press is a separate key from the trigger axis).
		if (Bike->State.Phase == EMXBikePhase::Recovering)
		{
			MashTimer[Slot] -= DeltaSeconds;
			if (MashTimer[Slot] <= 0.f)
			{
				MashTimer[Slot] = 0.12f;
				Press(Dev, bKb ? EKeys::J : EKeys::Gamepad_FaceButton_Bottom);
			}
		}
	}
}

void UMXAutoTest::Tick(float DeltaSeconds)
{
	if (bDone)
	{
		return;
	}
	TotalTime += DeltaSeconds;
	StepTime += DeltaSeconds;
	if (TotalTime > 900.f)
	{
		Check(TEXT("scenario finished within 15 minutes"), false, FString::Printf(TEXT("stuck at step %d"), Step));
		Finish();
		return;
	}
	DriveHumans(DeltaSeconds);
	if (Wait > 0.f)
	{
		Wait -= DeltaSeconds;
		return;
	}
	if (Scenario.StartsWith(TEXT("loop")))
	{
		TickLoop(DeltaSeconds);
	}
	else if (Scenario == TEXT("perf"))
	{
		TickPerf(DeltaSeconds);
	}
	else if (Scenario == TEXT("designer"))
	{
		TickDesigner(DeltaSeconds);
	}
	else if (Scenario == TEXT("shots"))
	{
		TickShots(DeltaSeconds);
	}
	else if (Scenario == TEXT("menus"))
	{
		TickMenus(DeltaSeconds);
	}
	else if (Scenario == TEXT("demo"))
	{
		TickDemo(DeltaSeconds);
	}
	else if (Scenario == TEXT("fonts"))
	{
		// Diagnostic: Draw() renders text through several canvas paths; one screenshot shows which work.
		if (Step == 0)
		{
			Next(1, 1.5f);
		}
		else if (Step == 1)
		{
			Shot(TEXT("fonts"));
			Next(2, 1.f);
		}
		else
		{
			Finish();
		}
	}
	else
	{
		Check(TEXT("known scenario"), false, Scenario);
		Finish();
	}
}

void UMXAutoTest::TickLoop(float DeltaSeconds)
{
	UMXGameInstance* G = GM->GI();
	AMXRaceManager* Race = GM->GetRace();
	UMXFrontend* F = GM->GetFrontend();
	const bool bKbP1 = Scenario == TEXT("loopkb");
	const int32 Dev0 = bKbP1 ? MXDevice::Keyboard : Pad(0);
	switch (Step)
	{
	case 0:
		Shot(TEXT("00_title"));
		Menu(Dev0, EMXMenuAction::Confirm);
		Next(1, 0.5f);
		break;
	case 1:
		Check(TEXT("title -> main menu"), F->GetScreen() == EMXScreen::MainMenu);
		MenuSelect(Dev0, 2);
		Menu(Dev0, EMXMenuAction::Confirm); // LOCAL VERSUS
		Next(2, 0.5f);
		break;
	case 2:
		Check(TEXT("main menu -> versus lobby with P1 joined"), F->GetScreen() == EMXScreen::Lobby && G->Slots[0].bJoined && G->Slots[0].DeviceKey == Dev0);
		for (int32 s = 1; s < 4; ++s)
		{
			Menu(Pad(s), EMXMenuAction::Confirm); // join
		}
		Menu(Pad(1), EMXMenuAction::Right); // colour change
		Menu(Pad(3), EMXMenuAction::Aux);   // P4 switches to the Modern layout...
		Menu(Pad(3), EMXMenuAction::Aux);   // ...and back to Classic
		Next(3, 0.6f);
		break;
	case 3:
	{
		bool bColoursUnique = true;
		for (int32 a = 0; a < 4; ++a)
		{
			for (int32 b = a + 1; b < 4; ++b)
			{
				bColoursUnique &= G->Slots[a].ColorIndex != G->Slots[b].ColorIndex;
			}
		}
		Check(TEXT("4 players joined via 4 devices"), G->NumJoined() == 4, FString::Printf(TEXT("joined=%d"), G->NumJoined()));
		Check(TEXT("rider colours unique"), bColoursUnique);
		Shot(TEXT("01_lobby"));
		// Leave + rejoin P3 to exercise leave.
		Menu(Pad(2), EMXMenuAction::Back);
		Next(4, 0.4f);
		break;
	}
	case 4:
		Check(TEXT("leave lobby"), !G->Slots[2].bJoined && G->NumJoined() == 3);
		Menu(Pad(2), EMXMenuAction::Confirm);
		Next(5, 0.4f);
		break;
	case 5:
		Check(TEXT("rejoin lobby"), G->NumJoined() == 4);
		for (int32 s = 0; s < 4; ++s)
		{
			Menu(s == 0 ? Dev0 : Pad(s), EMXMenuAction::Confirm); // ready
		}
		Next(6, 2.f);
		break;
	case 6:
		Check(TEXT("all ready -> course select"), F->GetScreen() == EMXScreen::CourseSelect);
		// Course 1, 1 lap for the loop test.
		F->SetCourseIndexById(TEXT("nes-t1"));
		Menu(Dev0, EMXMenuAction::PageRight);
		Menu(Dev0, EMXMenuAction::Confirm);
		Next(7, 0.5f);
		break;
	case 7:
	{
		Check(TEXT("race started with 4 local players"), GM->NumLocalPlayers() == 4 && Race->GetConfig().Racers.Num() >= 4,
			FString::Printf(TEXT("local=%d racers=%d"), GM->NumLocalPlayers(), Race->GetConfig().Racers.Num()));
		bool bPossess = true;
		for (int32 s = 0; s < 4; ++s)
		{
			AMXPlayerController* PC = GM->GetPCForSlot(s);
			bPossess &= PC && PC->GetPawn() == Race->GetBikeForSlot(s);
		}
		Check(TEXT("each local player controls its own bike"), bPossess);
		Next(8);
		break;
	}
	case 8:
		// Wait for GO.
		if (Race->GetPhase() == EMXRacePhase::Racing)
		{
			for (int32 s = 0; s < 4; ++s)
			{
				const AMXBike* B = Race->GetBikeForSlot(s);
				IndepLaneStart[s] = Race->GetTrack().NearestLane(B->State.Y);
			}
			Next(9);
		}
		break;
	case 9:
	{
		// Independence probe: P1 turbo, P2 lane change, P3 wheelie, P4 hands off.
		UMXGameViewportClient* V = VC();
		const int32 Devs[4] = {Dev0, Pad(1), Pad(2), Pad(3)};
		if (StepTime < 0.05f)
		{
			V->InjectKey(Devs[0], bKbP1 ? EKeys::Z : EKeys::Gamepad_FaceButton_Left, IE_Pressed, 1.f);
			V->InjectKey(Devs[1], EKeys::Gamepad_DPad_Down, IE_Pressed, 1.f);
		}
		if (bKbP1)
		{
			if (StepTime < 0.05f) { V->InjectKey(Devs[0], EKeys::X, IE_Pressed, 1.f); }
		}
		else
		{
			V->InjectAxis(Devs[0], EKeys::Gamepad_RightTriggerAxis, 1.f);
		}
		V->InjectAxis(Devs[1], EKeys::Gamepad_RightTriggerAxis, 1.f);
		V->InjectAxis(Devs[2], EKeys::Gamepad_RightTriggerAxis, 1.f);
		V->InjectAxis(Devs[2], EKeys::Gamepad_LeftX, StepTime > 1.5f ? -0.9f : 0.f); // nose up after getting rolling
		for (int32 s = 0; s < 4; ++s)
		{
			const AMXBike* B = Race->GetBikeForSlot(s);
			IndepHeat[s] = B->State.Heat;
			IndepMaxPitch[s] = FMath::Max(IndepMaxPitch[s], B->State.Wheelie);
			IndepSpeed[s] = B->State.ForwardSpeed();
		}
		if (StepTime > 3.5f)
		{
			V->InjectKey(Devs[0], bKbP1 ? EKeys::Z : EKeys::Gamepad_FaceButton_Left, IE_Released, 0.f);
			V->InjectKey(Devs[1], EKeys::Gamepad_DPad_Down, IE_Released, 0.f);
			V->InjectAxis(Devs[2], EKeys::Gamepad_LeftX, 0.f);
			const int32 Lane2 = Race->GetTrack().NearestLane(Race->GetBikeForSlot(1)->State.Y);
			Check(TEXT("independent: only P1's turbo heats P1"), IndepHeat[0] > IndepHeat[1] + 5.f && IndepHeat[0] > IndepHeat[2] + 5.f,
				FString::Printf(TEXT("heat P1=%.1f P2=%.1f P3=%.1f P4=%.1f"), IndepHeat[0], IndepHeat[1], IndepHeat[2], IndepHeat[3]));
			Check(TEXT("independent: P2 changed lane"), Lane2 != IndepLaneStart[1], FString::Printf(TEXT("lane %d -> %d"), IndepLaneStart[1], Lane2));
			Check(TEXT("independent: P3 wheelie"), IndepMaxPitch[2] > 15.f, FString::Printf(TEXT("max wheelie %.1f"), IndepMaxPitch[2]));
			Check(TEXT("independent: P4 idle stayed put"), IndepSpeed[3] < 1.f, FString::Printf(TEXT("speed %.2f"), IndepSpeed[3]));
			Check(TEXT("independent: P4 lane unchanged"), Race->GetTrack().NearestLane(Race->GetBikeForSlot(3)->State.Y) == IndepLaneStart[3]);
			Shot(TEXT("02_four_player_race"));
			bAutopilot = true;
			SabotageSlot = 1;
			SabotageAfter = Race->GetRaceTime() + 4.f;
			Next(10);
		}
		break;
	}
	case 10:
	{
		// Watch for the sabotaged crash and its recovery.
		const AMXBike* B = Race->GetBikeForSlot(1);
		const int32 R = Race->RacerForSlot(1);
		if (!bSawCrash && B->State.Phase == EMXBikePhase::Crashed)
		{
			bSawCrash = true;
			CrashS = B->State.CrashS;
			CrashValidS = Race->GetProgress(R).ValidS;
			Shot(TEXT("03_crash"));
		}
		if (bSawCrash && !bSawRecover && B->State.Phase == EMXBikePhase::Grounded && B->State.GhostTimer > 0.f)
		{
			bSawRecover = true;
			RecoverS = B->State.S;
			Check(TEXT("crash -> recover"), true, FString::Printf(TEXT("crash at %.1f m, back on the bike at %.1f m"), CrashS, RecoverS));
			Check(TEXT("recovery never gains progress"), RecoverS <= CrashS + 0.01f && Race->GetProgress(R).ValidS <= CrashValidS + 0.5f,
				FString::Printf(TEXT("recoverS=%.2f crashS=%.2f validS %.2f -> %.2f"), RecoverS, CrashS, CrashValidS, Race->GetProgress(R).ValidS));
		}
		if (bSawRecover || StepTime > 40.f)
		{
			if (!bSawRecover)
			{
				Check(TEXT("crash -> recover"), false, bSawCrash ? TEXT("crashed but did not recover") : TEXT("sabotage did not produce a crash"));
			}
			Next(11, 1.f);
		}
		break;
	}
	case 11:
		// Controller disconnect / reconnect on P3.
		VC()->InjectConnection(Pad(2), false);
		Next(12, 0.5f);
		break;
	case 12:
		Check(TEXT("disconnect pauses and flags P3"), GM->IsPaused() && GM->LostDeviceSlot() == 2, FString::Printf(TEXT("paused=%d lost=%d"), GM->IsPaused(), GM->LostDeviceSlot()));
		Shot(TEXT("04_disconnected"));
		VC()->InjectConnection(Pad(2), true);
		Next(13, 0.5f);
		break;
	case 13:
		Check(TEXT("reconnect clears the lost controller"), GM->LostDeviceSlot() == INDEX_NONE);
		Menu(Pad(2), EMXMenuAction::Start); // P3 resumes
		Next(14, 0.5f);
		break;
	case 14:
		Check(TEXT("resume after reconnect"), !GM->IsPaused());
		bRaceOver = false;
		Next(15);
		break;
	case 15:
		if (bRaceOver)
		{
			const TArray<FMXResultRow>& Rows = GM->GetResults();
			bool bOrdered = true;
			float Last = -1.f;
			int32 Humans = 0;
			for (const FMXResultRow& Row : Rows)
			{
				if (!Row.bEstimated)
				{
					bOrdered &= Row.Time >= Last;
					Last = Row.Time;
				}
				Humans += Row.bHuman ? 1 : 0;
			}
			Check(TEXT("finish -> results with validated order"), Rows.Num() == Race->NumRacers() && bOrdered && Humans == 4,
				FString::Printf(TEXT("rows=%d humans=%d"), Rows.Num(), Humans));
			// Every finisher has exactly the race's laps (resets / bumps never added or removed one).
			int32 Invalid = 0;
			bool bLapsConsistent = true;
			const int32 RaceLaps = Race->GetTrack().GetLaps();
			for (int32 i = 0; i < Race->NumRacers(); ++i)
			{
				const FMXRacerProgress& P = Race->GetProgress(i);
				Invalid += P.InvalidCrossings;
				if (P.bFinished && !P.bEstimated)
				{
					bLapsConsistent &= P.LapsCompleted == RaceLaps && P.LapTimes.Num() == RaceLaps;
				}
			}
			Check(TEXT("finishers have exactly the race's laps"), bLapsConsistent, FString::Printf(TEXT("laps %d, ignored invalid moves %d"), RaceLaps, Invalid));
			Shot(TEXT("05_results"));
			Next(40, 0.6f); // let the screenshot capture the results screen before leaving it
		}
		else if (StepTime > 240.f)
		{
			Check(TEXT("race finished"), false, TEXT("timeout"));
			Finish();
		}
		break;
	case 40:
		Menu(Dev0, EMXMenuAction::Confirm); // REMATCH
		bRaceOver = false;
		Next(16, 1.f);
		break;
	case 16:
		Check(TEXT("rematch restarts the same race"), Race->GetPhase() == EMXRacePhase::Intro || Race->GetPhase() == EMXRacePhase::Countdown || (Race->GetPhase() == EMXRacePhase::Racing && Race->GetRaceTime() < 2.f));
		Next(17);
		break;
	case 17:
		if (bRaceOver)
		{
			Check(TEXT("rematch finished"), GM->GetResults().Num() == Race->NumRacers());
			Menu(Dev0, EMXMenuAction::Down);
			Menu(Dev0, EMXMenuAction::Down);
			Menu(Dev0, EMXMenuAction::Confirm); // BACK TO LOBBY
			Next(18, 1.f);
		}
		else if (StepTime > 240.f)
		{
			Check(TEXT("rematch finished"), false, TEXT("timeout"));
			Finish();
		}
		break;
	case 18:
		Check(TEXT("back to lobby"), F->GetScreen() == EMXScreen::Lobby && GM->NumLocalPlayers() == 1 && G->NumJoined() == 4,
			FString::Printf(TEXT("screen=%d local=%d joined=%d"), (int32)F->GetScreen(), GM->NumLocalPlayers(), G->NumJoined()));
		Shot(TEXT("06_back_to_lobby"));
		Finish();
		break;
	default:
		break;
	}
}

void UMXAutoTest::TickPerf(float DeltaSeconds)
{
	UMXGameInstance* G = GM->GI();
	AMXRaceManager* Race = GM->GetRace();
	switch (Step)
	{
	case 0:
	{
		G->ResetLobby(false);
		for (int32 s = 0; s < 4; ++s)
		{
			G->Slots[s].bJoined = true;
			G->Slots[s].DeviceKey = Pad(s);
			G->Slots[s].ColorIndex = s;
			G->Slots[s].Profile = FMXControlProfile::Defaults(EMXControlScheme::Classic);
		}
		G->GetSave()->Settings.AICountVersus = 4;
		FString Course = TEXT("nes-t4");
		FParse::Value(FCommandLine::Get(), TEXT("mxcourse="), Course);
		GM->StartRace(GM->BuildConfig(EMXRaceMode::Versus, Course, EMXLayoutVariant::Main, 2));
		bAutopilot = true;
		Next(1);
		break;
	}
	case 1:
		if (Race->GetPhase() == EMXRacePhase::Racing && Race->GetRaceTime() > 5.f)
		{
			Next(2);
		}
		break;
	case 2:
	{
		FrameTimes.Add(DeltaSeconds * 1000.f);
		GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
		float Duration = 60.f;
		FParse::Value(FCommandLine::Get(), TEXT("mxperfseconds="), Duration);
		if (StepTime > Duration || bRaceOver)
		{
			// The screenshot (frame readback + PNG encode) is taken after sampling so it can't show up as a hitch.
			Shot(TEXT("4p_8bikes"));
			Next(3, 1.f);
		}
		break;
	}
	case 3:
		Finish();
		break;
	default:
		break;
	}
}

void UMXAutoTest::TickDesigner(float DeltaSeconds)
{
	UMXGameInstance* G = GM->GI();
	UMXFrontend* F = GM->GetFrontend();
	UMXDesigner* D = GM->GetDesigner();
	AMXRaceManager* Race = GM->GetRace();
	switch (Step)
	{
	case 0:
		Menu(Pad(0), EMXMenuAction::Confirm);
		Next(1, 0.4f);
		break;
	case 1:
		MenuSelect(Pad(0), 4);
		Menu(Pad(0), EMXMenuAction::Confirm); // TRACK DESIGNER
		Next(2, 1.f);
		break;
	case 2:
	{
		Check(TEXT("designer opens from the menu"), GM->GetState() == EMXAppState::Designer);
		D->NewTrack();
		const int32 Base = D->GetDefinition().Segments.Num();
		// Place through the real UI path: move the cursor with the d-pad and press A.
		for (int32 i = 0; i < 15; ++i)
		{
			Menu(Pad(0), EMXMenuAction::Right);
		}
		Menu(Pad(0), EMXMenuAction::Confirm); // place palette piece 0 (small ramp) at the cursor
		Menu(Pad(0), EMXMenuAction::Confirm); // deselect
		Check(TEXT("place a piece with the controller"), D->GetDefinition().Segments.Num() == Base + 1);
		// And through the API for a full course.
		D->PlacePiece(EMXObstacleType::RampMedium, 70.f);
		D->PlacePiece(EMXObstacleType::Mud, 100.f, MX::MaskFromLanes({1, 3}));
		D->PlacePiece(EMXObstacleType::CoolStrip, 125.f, MX::MaskFromLanes({4}));
		D->PlacePiece(EMXObstacleType::RampSteepBack, 160.f);
		D->PlacePiece(EMXObstacleType::Grass, 169.f, MX::AllLanes);
		D->PlacePiece(EMXObstacleType::Barrier, 230.f, MX::MaskFromLanes({3, 4}));
		D->PlacePiece(EMXObstacleType::RampLarge, 270.f);
		const int32 Count = D->GetDefinition().Segments.Num();
		const int32 Idx = D->PlacePiece(EMXObstacleType::Kicker, 320.f);
		D->MovePiece(Idx, 325.f);
		D->DeletePiece(Idx);
		Check(TEXT("move + delete"), D->GetDefinition().Segments.Num() == Count);
		D->Undo();
		Check(TEXT("undo restores the deleted piece"), D->GetDefinition().Segments.Num() == Count + 1);
		D->Redo();
		Check(TEXT("redo deletes it again"), D->GetDefinition().Segments.Num() == Count);
		D->SetLaps(3);
		D->RunValidation(true);
		Check(TEXT("validation passes (with physics drive test)"), !D->GetValidation().HasErrors(), D->GetValidation().Summary());
		DesignerTrackName = FString::Printf(TEXT("AutoTest %d"), FMath::RandRange(100, 999));
		FString Err;
		Check(TEXT("save named track"), D->Save(DesignerTrackName, Err), Err);
		const FMXTrackDefinition Saved = D->GetDefinition();
		D->NewTrack();
		Check(TEXT("load named track"), D->Load(DesignerTrackName, Err), Err);
		const FMXTrackDefinition& L = D->GetDefinition();
		bool bSame = L.Segments.Num() == Saved.Segments.Num() && L.Laps == Saved.Laps && FMath::IsNearlyEqual(L.LapLengthM, Saved.LapLengthM);
		for (int32 i = 0; bSame && i < L.Segments.Num(); ++i)
		{
			bSame &= L.Segments[i].Type == Saved.Segments[i].Type && FMath::IsNearlyEqual(L.Segments[i].StartM, Saved.Segments[i].StartM, 0.01f) && L.Segments[i].LaneMask == Saved.Segments[i].LaneMask;
		}
		Check(TEXT("save/load round trip identical"), bSame, FString::Printf(TEXT("%d segments"), L.Segments.Num()));
		// Invalid track is flagged.
		FMXTrackDefinition Bad = L;
		Bad.Segments.RemoveAll([](const FMXSegment& S) { return S.Type == EMXObstacleType::FinishDeck; });
		Bad.Segments.Add(FMXObstacleLibrary::MakeDefault(EMXObstacleType::RampLarge, 71.f, MXTuning::Style()));
		const FMXValidationResult BadR = FMXTrackValidator::Validate(Bad, MXTuning::Style());
		Check(TEXT("validator flags missing finish + overlap"), BadR.Count(EMXIssueSeverity::Error) >= 2, BadR.Summary());
		Shot(TEXT("01_designer"));
		Next(3, 0.5f);
		break;
	}
	case 3:
		// Test ride from the designer.
		bRaceOver = false;
		Check(TEXT("test ride starts"), GM->StartDesignerTest(D->GetDefinition()));
		bAutopilot = true;
		Next(4);
		break;
	case 4:
		if (bRaceOver)
		{
			Check(TEXT("test ride finishes"), GM->GetResults().Num() == 1);
			Menu(Pad(0), EMXMenuAction::Back); // back to designer
			bAutopilot = false;
			Next(5, 1.f);
		}
		else if (StepTime > 200.f)
		{
			Check(TEXT("test ride finishes"), false, TEXT("timeout"));
			Finish();
		}
		break;
	case 5:
		Check(TEXT("returns to the designer after the test ride"), GM->GetState() == EMXAppState::Designer && D->GetDefinition().Name == DesignerTrackName);
		// Race the saved track in split screen with two players.
		G->PendingCourseId = FString::Printf(TEXT("user:%s"), *DesignerTrackName);
		GM->ExitDesigner();
		G->ResetLobby(false);
		Next(6, 0.5f);
		break;
	case 6:
		MenuSelect(Pad(0), 2);
		Menu(Pad(0), EMXMenuAction::Confirm); // LOCAL VERSUS
		Next(7, 0.4f);
		break;
	case 7:
		G->PendingCourseId = FString::Printf(TEXT("user:%s"), *DesignerTrackName);
		Menu(Pad(1), EMXMenuAction::Confirm);
		Menu(Pad(0), EMXMenuAction::Confirm);
		Menu(Pad(1), EMXMenuAction::Confirm);
		Next(8, 2.f);
		break;
	case 8:
		Check(TEXT("course select preselects the saved track"), F->SelectedCourseId() == FString::Printf(TEXT("user:%s"), *DesignerTrackName), F->SelectedCourseId());
		Menu(Pad(0), EMXMenuAction::Confirm);
		Next(9, 3.f);
		break;
	case 9:
		Check(TEXT("saved track races in split screen"), GM->NumLocalPlayers() == 2 && Race->GetConfig().CourseId == FString::Printf(TEXT("user:%s"), *DesignerTrackName));
		bAutopilot = true;
		Shot(TEXT("02_saved_track_split"));
		Next(10, 6.f);
		break;
	case 10:
		Finish();
		break;
	default:
		break;
	}
}

void UMXAutoTest::TickShots(float DeltaSeconds)
{
	UMXGameInstance* G = GM->GI();
	AMXRaceManager* Race = GM->GetRace();
	static const int32 Players[] = {1, 2, 2, 3, 4};
	if (Step == 0)
	{
		Shot(TEXT("title"));
		Next(1, 0.5f);
		return;
	}
	const int32 Case = (Step - 1) / 2;
	if (Case >= 5)
	{
		Finish();
		return;
	}
	if ((Step - 1) % 2 == 0)
	{
		G->ResetLobby(false);
		for (int32 s = 0; s < Players[Case]; ++s)
		{
			G->Slots[s].bJoined = true;
			G->Slots[s].DeviceKey = Pad(s);
			G->Slots[s].ColorIndex = s;
			G->Slots[s].Profile = FMXControlProfile::Defaults(EMXControlScheme::Classic);
		}
		G->GetSave()->Settings.TwoPlayerSplit = Case == 2 ? EMXSplitOrientation::Vertical : EMXSplitOrientation::Horizontal;
		G->GetSave()->Settings.AICountVersus = 2;
		GM->StartRace(GM->BuildConfig(Players[Case] == 1 ? EMXRaceMode::RaceAI : EMXRaceMode::Versus, TEXT("nes-t1"), EMXLayoutVariant::Main, 2));
		bAutopilot = true;
		Next(Step + 1, 12.f);
	}
	else
	{
		// Camera framing: every player's own bike must be inside their view where the camera puts it,
		// with enough track visible ahead at speed.
		const FString Layout = FString::Printf(TEXT("%dp%s"), Players[Case], Case == 2 ? TEXT("_vertical") : TEXT(""));
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AMXPlayerController* PC = Cast<AMXPlayerController>(It->Get());
			AMXBike* Bike = PC ? PC->FocusBike.Get() : nullptr;
			AMXPlayerCameraManager* Cam = PC ? Cast<AMXPlayerCameraManager>(PC->PlayerCameraManager) : nullptr;
			ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
			if (!Bike || !Cam || !LP || !LP->ViewportClient)
			{
				continue;
			}
			FVector2D VP(1920.f, 1080.f);
			LP->ViewportClient->GetViewportSize(VP);
			const FVector2D ViewSize(LP->Size.X * VP.X, LP->Size.Y * VP.Y);
			FVector2D Screen;
			const bool bOnScreen = UGameplayStatics::ProjectWorldToScreen(PC, Bike->GetVisualLocation() + FVector(0.f, 0.f, 100.f), Screen, true);
			const float FX = Screen.X / FMath::Max(1.f, ViewSize.X);
			const float FY = Screen.Y / FMath::Max(1.f, ViewSize.Y);
			const float Ahead = Cam->VisibleLookAheadSeconds;
			const bool bFramed = bOnScreen && FX > 0.15f && FX < 0.55f && FY > 0.3f && FY < 0.85f;
			Check(FString::Printf(TEXT("%s: P%d bike framed"), *Layout, PC->Slot + 1), bFramed,
				FString::Printf(TEXT("screen x %.2f y %.2f, view %.0fx%.0f, shape %d, speed %.1f m/s"), FX, FY, ViewSize.X, ViewSize.Y,
					(int32)Cam->GetViewShape(), Bike->State.ForwardSpeed()));
			Check(FString::Printf(TEXT("%s: P%d sees >= 1.0 s of track ahead"), *Layout, PC->Slot + 1), Ahead >= 1.0f,
				FString::Printf(TEXT("%.2f s"), Ahead));
		}
		Shot(Layout);
		Next(Step + 1, 1.f);
	}
}

void UMXAutoTest::TickMenus(float DeltaSeconds)
{
	// Every menu screen reached through real (virtual-device) menu input, with a screenshot of each.
	UMXFrontend* F = GM->GetFrontend();
	const int32 Dev0 = Pad(0);
	switch (Step)
	{
	case 0:
		Menu(Dev0, EMXMenuAction::Confirm);
		Next(1, 0.6f);
		break;
	case 1:
		Check(TEXT("title -> main menu"), F->GetScreen() == EMXScreen::MainMenu);
		Shot(TEXT("01_main_menu"));
		Next(2, 0.4f);
		break;
	case 2:
		MenuSelect(Dev0, 5); // OPTIONS
		Menu(Dev0, EMXMenuAction::Confirm);
		Next(3, 0.6f);
		break;
	case 3:
		Check(TEXT("main menu -> options"), F->GetScreen() == EMXScreen::Options);
		Shot(TEXT("02_options"));
		Next(4, 0.4f);
		break;
	case 4:
		Menu(Dev0, EMXMenuAction::Back);
		MenuSelect(Dev0, 0); // TIME TRIAL
		Menu(Dev0, EMXMenuAction::Confirm);
		Next(5, 0.6f);
		break;
	case 5:
		Check(TEXT("time trial lobby"), F->GetScreen() == EMXScreen::Lobby && F->GetMode() == EMXRaceMode::TimeTrial);
		Shot(TEXT("03_lobby_time_trial"));
		Next(6, 0.4f);
		break;
	case 6:
		Menu(Dev0, EMXMenuAction::Aux2); // remap controls
		Next(7, 0.6f);
		break;
	case 7:
		Check(TEXT("lobby -> controls"), F->GetScreen() == EMXScreen::Controls);
		Shot(TEXT("04_controls"));
		Next(8, 0.4f);
		break;
	case 8:
		Menu(Dev0, EMXMenuAction::Back);
		Next(9, 0.4f);
		break;
	case 9:
		Menu(Dev0, EMXMenuAction::Confirm); // ready -> course select (after the lobby's 1.2 s "ALL READY!")
		Next(10, 1.8f);
		break;
	case 10:
		Check(TEXT("lobby -> course select"), F->GetScreen() == EMXScreen::CourseSelect);
		Shot(TEXT("05_course_select"));
		Next(11, 0.4f);
		break;
	case 11:
		bAutopilot = true;
		Menu(Dev0, EMXMenuAction::Confirm); // start
		Next(12, 7.f);
		break;
	case 12:
		Press(Dev0, EKeys::Gamepad_Special_Right); // pause
		Next(13, 0.6f);
		break;
	case 13:
		Check(TEXT("start button pauses"), F->GetScreen() == EMXScreen::Pause);
		Shot(TEXT("06_pause"));
		Next(14, 0.4f);
		break;
	case 14:
		Menu(Dev0, EMXMenuAction::Confirm); // RESUME
		Next(15, 1.f);
		break;
	case 15:
		if (F->GetScreen() == EMXScreen::Results)
		{
			Next(16, 1.5f);
		}
		else if (StepTime > 200.f)
		{
			Check(TEXT("time trial finished"), false, TEXT("timeout"));
			Finish();
		}
		break;
	case 16:
		Check(TEXT("time trial -> results"), F->GetScreen() == EMXScreen::Results);
		Shot(TEXT("07_results"));
		Next(17, 1.f);
		break;
	default:
		Finish();
		break;
	}
}

void UMXAutoTest::TickDemo(float DeltaSeconds)
{
	UMXGameInstance* G = GM->GI();
	AMXRaceManager* Race = GM->GetRace();
	int32 Frames = 360;
	FParse::Value(FCommandLine::Get(), TEXT("mxdemoframes="), Frames);
	if (Step == 0)
	{
		// Four local riders, no AI: the brief's first playable demo.
		G->ResetLobby(false);
		for (int32 s = 0; s < 4; ++s)
		{
			G->Slots[s].bJoined = true;
			G->Slots[s].DeviceKey = Pad(s);
			G->Slots[s].ColorIndex = s;
			G->Slots[s].Profile = FMXControlProfile::Defaults(EMXControlScheme::Classic);
		}
		G->GetSave()->Settings.AICountVersus = 0;
		GM->StartRace(GM->BuildConfig(EMXRaceMode::Versus, TEXT("nes-t1"), EMXLayoutVariant::Main, 2));
		bAutopilot = true;
		Next(1, 0.f);
		return;
	}
	if (Step == 1)
	{
		if (Race && Race->GetPhase() == EMXRacePhase::Racing && Race->GetRaceTime() > 1.5f)
		{
			Next(2, 0.f);
		}
		return;
	}
	const int32 Frame = Step - 2;
	if (Frame < Frames)
	{
		Shot(FString::Printf(TEXT("f_%04d"), Frame));
		Step++;
		return;
	}
	Check(TEXT("demo frames recorded"), true, FString::Printf(TEXT("%d frames"), Frames));
	Finish();
}

void UMXAutoTest::Draw(UCanvas* Canvas)
{
	if (!Canvas || bDone)
	{
		return;
	}
	if (Scenario != TEXT("demo"))
	{
		MXDraw::Text(Canvas, FString::Printf(TEXT("AUTOTEST %s  step %d  checks %d  (simulated input)"), *Scenario, Step, Checks.Num()), 10.f, Canvas->ClipY - 26.f, 16.f, FLinearColor(1.f, 1.f, 0.f, 0.8f), 0.f, 0.f, false);
	}
	if (Scenario == TEXT("fonts"))
	{
		MXDraw::Rect(Canvas, 60.f, 60.f, 1300.f, 760.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		// 1: current path (Slate font through FCanvasTextItem).
		MXDraw::Text(Canvas, TEXT("1 Slate font FCanvasTextItem"), 100.f, 90.f, 40.f, FLinearColor::White, 0.f, 0.f, true, false);
		// 2: UCanvas::DrawText with engine UFonts.
		Canvas->SetDrawColor(FColor::White);
		Canvas->DrawText(GEngine->GetLargeFont(), TEXT("2 DrawText LargeFont x2"), 100.f, 180.f, 2.f, 2.f);
		Canvas->DrawText(GEngine->GetMediumFont(), TEXT("3 DrawText MediumFont x2"), 100.f, 260.f, 2.f, 2.f);
		Canvas->DrawText(GEngine->GetSmallFont(), TEXT("4 DrawText SmallFont x2"), 100.f, 340.f, 2.f, 2.f);
		// 5: FCanvasTextItem with a UFont.
		{
			FCanvasTextItem Item(FVector2D(100.f, 420.f), FText::FromString(TEXT("5 TextItem UFont(Large) scale 2")), GEngine->GetLargeFont(), FLinearColor::Green);
			Item.Scale = FVector2D(2.f, 2.f);
			Canvas->DrawItem(Item);
		}
		// 6/7: Slate font with other blend modes.
		{
			FCanvasTextItem Item(FVector2D(100.f, 500.f), FText::FromString(TEXT("6 Slate font AlphaOnly")), FCoreStyle::GetDefaultFontStyle("Bold", 40), FLinearColor::Yellow);
			Item.BlendMode = SE_BLEND_TranslucentAlphaOnly;
			Canvas->DrawItem(Item);
		}
		{
			FCanvasTextItem Item(FVector2D(100.f, 580.f), FText::FromString(TEXT("7 Slate font Opaque")), FCoreStyle::GetDefaultFontStyle("Bold", 40), FLinearColor(1.f, 0.4f, 0.4f, 1.f));
			Item.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Item);
		}
		{
			FCanvasTextItem Item(FVector2D(100.f, 660.f), FText::FromString(TEXT("8 SubtitleFont TextItem")), GEngine->GetSubtitleFont(), FLinearColor(0.5f, 0.8f, 1.f, 1.f));
			Item.Scale = FVector2D(1.5f, 1.5f);
			Canvas->DrawItem(Item);
		}
		const FVector2D M = MXDraw::Measure(TEXT("1 Slate font FCanvasTextItem"), 40.f, true);
		Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("measure(1) = %.1f x %.1f  DPI %.2f"), M.X, M.Y, Canvas->GetDPIScale()), 100.f, 760.f, 1.5f, 1.5f);
	}
}

void UMXAutoTest::Finish()
{
	bDone = true;
	int32 Passed = 0;
	for (const FCheck& C : Checks)
	{
		Passed += C.bPass ? 1 : 0;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("scenario"), Scenario);
	Root->SetStringField(TEXT("inputKind"), TEXT("simulated (synthetic key/axis events through UMXGameViewportClient routing)"));
	Root->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : TEXT("unknown"));
	FIntPoint Res(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Res = GEngine->GameViewport->Viewport->GetSizeXY();
	}
	Root->SetStringField(TEXT("resolution"), FString::Printf(TEXT("%dx%d"), Res.X, Res.Y));
	Root->SetNumberField(TEXT("passed"), Passed);
	Root->SetNumberField(TEXT("total"), Checks.Num());
	Root->SetNumberField(TEXT("seconds"), TotalTime);
	TArray<TSharedPtr<FJsonValue>> CheckArr;
	for (const FCheck& C : Checks)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), C.Name);
		O->SetBoolField(TEXT("pass"), C.bPass);
		O->SetStringField(TEXT("detail"), C.Detail);
		CheckArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("checks"), CheckArr);
	if (FrameTimes.Num() > 0)
	{
		TArray<float> Sorted = FrameTimes;
		Sorted.Sort();
		float Sum = 0.f;
		for (float F : FrameTimes)
		{
			Sum += F;
		}
		const float Avg = Sum / FrameTimes.Num();
		auto Pct = [&Sorted](float P) { return Sorted[FMath::Clamp(FMath::FloorToInt(P * (Sorted.Num() - 1)), 0, Sorted.Num() - 1)]; };
		// "1% low" = average of the slowest 1% of frames.
		const int32 N1 = FMath::Max(1, Sorted.Num() / 100);
		float Worst = 0.f;
		for (int32 i = Sorted.Num() - N1; i < Sorted.Num(); ++i)
		{
			Worst += Sorted[i];
		}
		Worst /= N1;
		auto AvgOf = [](const TArray<float>& A) { float S = 0.f; for (float V : A) { S += V; } return A.Num() ? S / A.Num() : 0.f; };
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("frames"), FrameTimes.Num());
		P->SetNumberField(TEXT("avgMs"), Avg);
		P->SetNumberField(TEXT("avgFps"), 1000.f / FMath::Max(0.01f, Avg));
		P->SetNumberField(TEXT("p50Ms"), Pct(0.5f));
		P->SetNumberField(TEXT("p95Ms"), Pct(0.95f));
		P->SetNumberField(TEXT("p99Ms"), Pct(0.99f));
		P->SetNumberField(TEXT("maxMs"), Sorted.Last());
		P->SetNumberField(TEXT("onePercentLowFps"), 1000.f / FMath::Max(0.01f, Worst));
		P->SetNumberField(TEXT("gameThreadMs"), AvgOf(GameMs));
		P->SetNumberField(TEXT("renderThreadMs"), AvgOf(RenderMs));
		P->SetNumberField(TEXT("gpuMs"), AvgOf(GpuMs));
		P->SetNumberField(TEXT("localPlayers"), GM->NumLocalPlayers());
		P->SetNumberField(TEXT("bikes"), GM->GetRace() ? GM->GetRace()->NumRacers() : 0);
		Root->SetObjectField(TEXT("performance"), P);
	}
	TArray<TSharedPtr<FJsonValue>> ShotArr;
	for (const FString& S : Screenshots)
	{
		ShotArr.Add(MakeShared<FJsonValueString>(S));
	}
	Root->SetArrayField(TEXT("screenshots"), ShotArr);
	FString Out;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, W);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / (Scenario + TEXT(".json"));
	FFileHelper::SaveStringToFile(Out, *Path);
	UE_LOG(LogHeatline, Display, TEXT("AUTOTEST done: %d/%d passed -> %s"), Passed, Checks.Num(), *Path);
	if (FParse::Param(FCommandLine::Get(), TEXT("mxquit")))
	{
		UKismetSystemLibrary::QuitGame(GM, nullptr, EQuitPreference::Quit, false);
	}
}
