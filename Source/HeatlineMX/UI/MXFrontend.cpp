#include "UI/MXFrontend.h"
#include "UI/MXDraw.h"
#include "Race/MXGameMode.h"
#include "Race/MXRaceManager.h"
#include "Session/MXGameInstance.h"
#include "Track/MXCourseLibrary.h"
#include "Track/MXObstacleLibrary.h"
#include "Audio/MXEngineAudio.h"
#include "Core/MXTuning.h"
#include "Engine/Canvas.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	const FLinearColor Accent(1.f, 0.55f, 0.1f);
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.62f);
	const FLinearColor Dim(0.75f, 0.75f, 0.78f);

	FString ModeName(EMXRaceMode M)
	{
		switch (M)
		{
		case EMXRaceMode::TimeTrial: return TEXT("TIME TRIAL");
		case EMXRaceMode::RaceAI: return TEXT("RACE VS AI");
		case EMXRaceMode::Versus: return TEXT("LOCAL VERSUS");
		case EMXRaceMode::Championship: return TEXT("CHAMPIONSHIP");
		case EMXRaceMode::DesignerTest: return TEXT("TEST RIDE");
		default: return TEXT("RACE");
		}
	}

	FString DifficultyName(EMXAIDifficulty D)
	{
		switch (D)
		{
		case EMXAIDifficulty::Easy: return TEXT("Easy");
		case EMXAIDifficulty::Hard: return TEXT("Hard");
		default: return TEXT("Medium");
		}
	}
}

void UMXFrontend::Init(AMXGameMode* InGM)
{
	GM = InGM;
	UIAudio = NewObject<UMXEngineAudioComponent>(GM, TEXT("FrontendAudio"));
	UIAudio->SetUIMode(true);
	UIAudio->RegisterComponent();
	UIAudio->Start();
	Screen = EMXScreen::Title;
}

void UMXFrontend::Beep(uint8 Sfx)
{
	if (UIAudio)
	{
		UIAudio->PlayOneShot((EMXSfx)Sfx, 0.7f);
	}
}

void UMXFrontend::SetScreen(EMXScreen S)
{
	if (Screen == EMXScreen::MainMenu)
	{
		MainMenuIndex = MenuIndex; // other screens reuse MenuIndex; remember where the main menu was
	}
	PrevScreen = Screen;
	Screen = S;
	ScreenTime = 0.f;
	MenuIndex = (S == EMXScreen::MainMenu) ? MainMenuIndex : 0;
	ResultsIndex = 0;
	PauseIndex = 0;
	bCapturing = false;
}

void UMXFrontend::Tick(float DeltaSeconds)
{
	ScreenTime += DeltaSeconds;
	ToastTime = FMath::Max(0.f, ToastTime - DeltaSeconds);
	if (Screen == EMXScreen::Lobby)
	{
		if (LobbyReady())
		{
			if (LobbyCountdown < 0.f)
			{
				LobbyCountdown = 1.2f;
			}
			LobbyCountdown -= DeltaSeconds;
			if (LobbyCountdown <= 0.f)
			{
				LobbyCountdown = -1.f;
				SetScreen(EMXScreen::CourseSelect);
				if (!GM->GI()->PendingCourseId.IsEmpty())
				{
					SetCourseIndexById(GM->GI()->PendingCourseId);
					GM->GI()->PendingCourseId.Reset();
				}
				Beep((uint8)EMXSfx::MenuSelect);
			}
		}
		else
		{
			LobbyCountdown = -1.f;
		}
	}
}

int32 UMXFrontend::MaxPlayersForMode() const
{
	return (Mode == EMXRaceMode::TimeTrial || Mode == EMXRaceMode::RaceAI) ? 1 : 4;
}

bool UMXFrontend::LobbyReady() const
{
	const UMXGameInstance* G = GM->GI();
	int32 Joined = 0;
	for (const FMXPlayerSlot& S : G->Slots)
	{
		if (S.bJoined)
		{
			++Joined;
			if (!S.bReady)
			{
				return false;
			}
		}
	}
	const int32 MinPlayers = Mode == EMXRaceMode::Versus ? 2 : 1;
	return Joined >= MinPlayers;
}

void UMXFrontend::GoToLobby()
{
	bRaceActive = false;
	SetScreen(EMXScreen::Lobby);
}

void UMXFrontend::GoToMainMenu()
{
	bRaceActive = false;
	SetScreen(EMXScreen::MainMenu);
}

void UMXFrontend::OnRaceStarted()
{
	bRaceActive = true;
	SetScreen(EMXScreen::None);
}

void UMXFrontend::OnResults()
{
	SetScreen(EMXScreen::Results);
	Beep((uint8)EMXSfx::Fanfare);
}

void UMXFrontend::OnPauseChanged(bool bPaused, int32 BySlot)
{
	PausedBySlot = BySlot;
	if (bPaused)
	{
		SetScreen(EMXScreen::Pause);
	}
	else if (Screen == EMXScreen::Pause)
	{
		SetScreen(EMXScreen::None);
	}
}

void UMXFrontend::OnDeviceChanged(int32 Slot, bool bConnected)
{
	Toast = bConnected ? FString::Printf(TEXT("P%d controller connected"), Slot + 1) : FString::Printf(TEXT("P%d controller disconnected"), Slot + 1);
	ToastTime = 3.f;
	Beep(bConnected ? (uint8)EMXSfx::Join : (uint8)EMXSfx::Leave);
}

void UMXFrontend::SetCourseIndexById(const FString& Id)
{
	const TArray<FMXCourseEntry>& E = GM->GI()->GetCourses()->GetEntries();
	for (int32 i = 0; i < E.Num(); ++i)
	{
		if (E[i].Id == Id)
		{
			CourseIndex = i;
		}
	}
}

FString UMXFrontend::SelectedCourseId() const
{
	const TArray<FMXCourseEntry>& E = GM->GI()->GetCourses()->GetEntries();
	return E.IsValidIndex(CourseIndex) ? E[CourseIndex].Id : TEXT("nes-t1");
}

void UMXFrontend::StartSelectedRace()
{
	FMXRaceConfig C = GM->BuildConfig(Mode, SelectedCourseId(), Variant, LapsOverride);
	if (C.Racers.Num() == 0)
	{
		Toast = TEXT("Nobody has joined");
		ToastTime = 2.f;
		return;
	}
	Beep((uint8)EMXSfx::MenuSelect);
	if (Mode == EMXRaceMode::Championship)
	{
		GM->BeginChampionship(C);
	}
	else
	{
		GM->StartRace(C);
	}
}

void UMXFrontend::HandleAnyKey(int32 DeviceKey, const FKey& Key)
{
	if (Screen != EMXScreen::Controls || !bCapturing)
	{
		return;
	}
	UMXGameInstance* G = GM->GI();
	if (G->Slots[ControlsSlot].DeviceKey != DeviceKey || Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		return;
	}
	const EMXBindAction Act = (EMXBindAction)(ControlsIndex - 2);
	const bool bPad = Key.IsGamepadKey();
	FMXBindingSet& Set = bPad ? EditProfile.Gamepad : EditProfile.Keyboard;
	// Remove the key from other actions to avoid conflicts, then make it the primary binding.
	for (int32 a = 0; a < (int32)EMXBindAction::Count; ++a)
	{
		if (TArray<FKey>* Keys = UMXInputConfig::GetKeys(Set, (EMXBindAction)a))
		{
			Keys->Remove(Key);
		}
	}
	if (TArray<FKey>* Keys = UMXInputConfig::GetKeys(Set, Act))
	{
		Keys->Insert(Key, 0);
		if (Keys->Num() > 3)
		{
			Keys->SetNum(3);
		}
	}
	bCapturing = false;
	Beep((uint8)EMXSfx::MenuSelect);
}

void UMXFrontend::HandleAction(int32 DeviceKey, EMXMenuAction Action)
{
	UMXGameInstance* G = GM->GI();
	if (bCapturing)
	{
		if (Action == EMXMenuAction::Back || Action == EMXMenuAction::Start)
		{
			bCapturing = false;
		}
		return;
	}
	switch (Screen)
	{
	case EMXScreen::Title:
		if (Action == EMXMenuAction::Confirm || Action == EMXMenuAction::Start)
		{
			SetScreen(EMXScreen::MainMenu);
			Beep((uint8)EMXSfx::MenuSelect);
		}
		break;

	case EMXScreen::MainMenu:
	{
		const int32 Count = 7;
		if (Action == EMXMenuAction::Up) { MenuIndex = (MenuIndex + Count - 1) % Count; Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Down) { MenuIndex = (MenuIndex + 1) % Count; Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Back) { SetScreen(EMXScreen::Title); }
		else if (Action == EMXMenuAction::Confirm || Action == EMXMenuAction::Start)
		{
			Beep((uint8)EMXSfx::MenuSelect);
			auto JoinFirst = [G, DeviceKey]()
			{
				G->ResetLobby(false);
				FMXPlayerSlot& S = G->Slots[0];
				S.bJoined = true;
				S.DeviceKey = DeviceKey;
				S.ColorIndex = G->GetSave()->PreferredColors.IsValidIndex(0) ? G->GetSave()->PreferredColors[0] : 0;
				S.Profile = G->ProfileFor(0);
			};
			switch (MenuIndex)
			{
			case 0: Mode = EMXRaceMode::TimeTrial; Variant = EMXLayoutVariant::Challenge; JoinFirst(); SetScreen(EMXScreen::Lobby); break;
			case 1: Mode = EMXRaceMode::RaceAI; Variant = EMXLayoutVariant::Main; JoinFirst(); SetScreen(EMXScreen::Lobby); break;
			case 2: Mode = EMXRaceMode::Versus; Variant = EMXLayoutVariant::Main; JoinFirst(); SetScreen(EMXScreen::Lobby); break;
			case 3: Mode = EMXRaceMode::Championship; Variant = EMXLayoutVariant::Main; JoinFirst(); SetScreen(EMXScreen::Lobby); break;
			case 4: JoinFirst(); GM->EnterDesigner(); SetScreen(EMXScreen::None); break;
			case 5: SetScreen(EMXScreen::Options); break;
			case 6: UKismetSystemLibrary::QuitGame(GM, nullptr, EQuitPreference::Quit, false); break;
			default: break;
			}
		}
		break;
	}

	case EMXScreen::Lobby:
		LobbyAction(DeviceKey, Action);
		break;

	case EMXScreen::Controls:
		if (G->Slots[ControlsSlot].DeviceKey == DeviceKey)
		{
			ControlsAction(Action);
		}
		break;

	case EMXScreen::CourseSelect:
	{
		const int32 Count = G->GetCourses()->GetEntries().Num();
		if (Mode == EMXRaceMode::Championship)
		{
			if (Action == EMXMenuAction::Confirm || Action == EMXMenuAction::Start) { StartSelectedRace(); }
			else if (Action == EMXMenuAction::Back) { SetScreen(EMXScreen::Lobby); }
			break;
		}
		if (Action == EMXMenuAction::Up) { CourseIndex = (CourseIndex + Count - 1) % FMath::Max(1, Count); Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Down) { CourseIndex = (CourseIndex + 1) % FMath::Max(1, Count); Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Left || Action == EMXMenuAction::Right)
		{
			Variant = Variant == EMXLayoutVariant::Main ? EMXLayoutVariant::Challenge : EMXLayoutVariant::Main;
			Beep((uint8)EMXSfx::MenuMove);
		}
		else if (Action == EMXMenuAction::PageLeft) { LapsOverride = (LapsOverride + 9) % 10; Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::PageRight) { LapsOverride = (LapsOverride + 1) % 10; Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Confirm || Action == EMXMenuAction::Start) { StartSelectedRace(); }
		else if (Action == EMXMenuAction::Back) { SetScreen(EMXScreen::Lobby); }
		break;
	}

	case EMXScreen::Options:
	{
		FMXSettings& S = G->GetSave()->Settings;
		const int32 Count = 7;
		const int32 Dir = Action == EMXMenuAction::Right ? 1 : (Action == EMXMenuAction::Left ? -1 : 0);
		if (Action == EMXMenuAction::Up) { OptionsIndex = (OptionsIndex + Count - 1) % Count; }
		else if (Action == EMXMenuAction::Down) { OptionsIndex = (OptionsIndex + 1) % Count; }
		else if (Dir != 0 || (Action == EMXMenuAction::Confirm && OptionsIndex != 6))
		{
			const int32 D = Dir != 0 ? Dir : 1;
			switch (OptionsIndex)
			{
			case 0: S.TwoPlayerSplit = S.TwoPlayerSplit == EMXSplitOrientation::Horizontal ? EMXSplitOrientation::Vertical : EMXSplitOrientation::Horizontal; break;
			case 1: S.AIDifficulty = (EMXAIDifficulty)(((int32)S.AIDifficulty + 3 + D) % 3); break;
			case 2: S.AICountRace = FMath::Clamp(S.AICountRace + D, 1, 7); break;
			case 3: S.AICountVersus = FMath::Clamp(S.AICountVersus + D, 0, 4); break;
			case 4: S.bLandingMeter = !S.bLandingMeter; break;
			case 5: S.MasterVolume = FMath::Clamp(S.MasterVolume + D * 0.1f, 0.f, 1.f); break;
			default: break;
			}
			Beep((uint8)EMXSfx::MenuMove);
		}
		else if (Action == EMXMenuAction::Back || (Action == EMXMenuAction::Confirm && OptionsIndex == 6))
		{
			G->SaveNow();
			SetScreen(EMXScreen::MainMenu);
		}
		break;
	}

	case EMXScreen::Results:
	{
		const FMXRaceConfig& C = GM->GetRace()->GetConfig();
		TArray<int32> Actions; // 0 rematch, 1 course select, 2 lobby, 3 next race, 4 standings, 5 back to designer
		if (C.Mode == EMXRaceMode::Championship) { Actions = {3, 4, 2}; }
		else if (C.Mode == EMXRaceMode::DesignerTest) { Actions = {0, 5}; }
		else { Actions = {0, 1, 2}; }
		if (Action == EMXMenuAction::Up) { ResultsIndex = (ResultsIndex + Actions.Num() - 1) % Actions.Num(); Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Down) { ResultsIndex = (ResultsIndex + 1) % Actions.Num(); Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Confirm || Action == EMXMenuAction::Start)
		{
			Beep((uint8)EMXSfx::MenuSelect);
			switch (Actions[FMath::Clamp(ResultsIndex, 0, Actions.Num() - 1)])
			{
			case 0: GM->Rematch(); break;
			case 1: GM->GetRace()->Teardown(); GM->StartAttract(); SetScreen(EMXScreen::CourseSelect); break;
			case 2: GM->ReturnToLobby(); break;
			case 3: SetScreen(EMXScreen::ChampTable); break;
			case 4: SetScreen(EMXScreen::ChampTable); break;
			case 5: GM->EndDesignerTest(); SetScreen(EMXScreen::None); break;
			default: break;
			}
		}
		else if (Action == EMXMenuAction::Back)
		{
			if (C.Mode == EMXRaceMode::DesignerTest) { GM->EndDesignerTest(); SetScreen(EMXScreen::None); }
		}
		break;
	}

	case EMXScreen::ChampTable:
		if (Action == EMXMenuAction::Confirm || Action == EMXMenuAction::Start)
		{
			Beep((uint8)EMXSfx::MenuSelect);
			GM->NextChampionshipRace();
		}
		else if (Action == EMXMenuAction::Back)
		{
			SetScreen(EMXScreen::Results);
		}
		break;

	case EMXScreen::Pause:
	{
		const bool bDesigner = GM->GetState() == EMXAppState::DesignerTest;
		const int32 Count = 3;
		if (GM->LostDeviceSlot() != INDEX_NONE)
		{
			break; // waiting for a controller (handled by the game mode)
		}
		if (Action == EMXMenuAction::Up) { PauseIndex = (PauseIndex + Count - 1) % Count; Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Down) { PauseIndex = (PauseIndex + 1) % Count; Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Start || Action == EMXMenuAction::Back || (Action == EMXMenuAction::Confirm && PauseIndex == 0))
		{
			GM->SetPaused(false);
		}
		else if (Action == EMXMenuAction::Confirm && PauseIndex == 1)
		{
			GM->Rematch();
		}
		else if (Action == EMXMenuAction::Confirm && PauseIndex == 2)
		{
			if (bDesigner)
			{
				GM->EndDesignerTest();
				SetScreen(EMXScreen::None);
			}
			else
			{
				GM->ReturnToLobby();
			}
		}
		break;
	}

	case EMXScreen::None:
	default:
		break;
	}
}

void UMXFrontend::LobbyAction(int32 DeviceKey, EMXMenuAction Action)
{
	UMXGameInstance* G = GM->GI();
	const int32 Slot = G->SlotForDevice(DeviceKey);
	if (Slot == INDEX_NONE)
	{
		if (Action == EMXMenuAction::Confirm || Action == EMXMenuAction::Start)
		{
			if (G->NumJoined() >= MaxPlayersForMode())
			{
				Toast = FString::Printf(TEXT("%s is for one rider - use LOCAL VERSUS for more"), *ModeName(Mode));
				ToastTime = 2.5f;
				return;
			}
			for (int32 i = 0; i < 4; ++i)
			{
				FMXPlayerSlot& S = G->Slots[i];
				if (!S.bJoined)
				{
					S.bJoined = true;
					S.DeviceKey = DeviceKey;
					S.bReady = false;
					S.bDeviceLost = false;
					S.Profile = G->ProfileFor(i);
					S.ColorIndex = G->FirstFreeColor(i, i);
					Beep((uint8)EMXSfx::Join);
					break;
				}
			}
		}
		else if (Action == EMXMenuAction::Back && G->NumJoined() == 0)
		{
			SetScreen(EMXScreen::MainMenu);
		}
		return;
	}
	FMXPlayerSlot& S = G->Slots[Slot];
	FMXSettings& Settings = G->GetSave()->Settings;
	switch (Action)
	{
	case EMXMenuAction::Left:
	case EMXMenuAction::Right:
		if (!S.bReady)
		{
			const int32 Dir = Action == EMXMenuAction::Right ? 1 : -1;
			int32 C = S.ColorIndex;
			for (int32 k = 0; k < MX::NumRiderColors; ++k)
			{
				C = (C + Dir + MX::NumRiderColors) % MX::NumRiderColors;
				if (!G->IsColorTaken(C, Slot))
				{
					break;
				}
			}
			S.ColorIndex = C;
			Beep((uint8)EMXSfx::MenuMove);
		}
		break;
	case EMXMenuAction::Confirm:
		S.bReady = !S.bReady;
		Beep(S.bReady ? (uint8)EMXSfx::MenuSelect : (uint8)EMXSfx::MenuMove);
		if (S.bReady)
		{
			G->GetSave()->PreferredColors.SetNumZeroed(4);
			G->GetSave()->PreferredColors[Slot] = S.ColorIndex;
		}
		break;
	case EMXMenuAction::Back:
		if (S.bReady)
		{
			S.bReady = false;
		}
		else
		{
			S.bJoined = false;
			S.DeviceKey = MXDevice::None;
			Beep((uint8)EMXSfx::Leave);
			if (G->NumJoined() == 0)
			{
				SetScreen(EMXScreen::MainMenu);
			}
		}
		break;
	case EMXMenuAction::Aux:
		if (!S.bReady)
		{
			const EMXControlScheme NewScheme = S.Profile.Scheme == EMXControlScheme::Classic ? EMXControlScheme::Modern : EMXControlScheme::Classic;
			const bool bAssist = S.Profile.bLandingAssist;
			S.Profile = FMXControlProfile::Defaults(NewScheme);
			S.Profile.bLandingAssist = bAssist;
			G->StoreProfile(Slot, S.Profile);
			Beep((uint8)EMXSfx::MenuMove);
		}
		break;
	case EMXMenuAction::Aux2:
		if (!S.bReady)
		{
			ControlsSlot = Slot;
			ControlsIndex = 0;
			EditProfile = S.Profile;
			SetScreen(EMXScreen::Controls);
		}
		break;
	case EMXMenuAction::PageLeft:
	case EMXMenuAction::PageRight:
	{
		const int32 D = Action == EMXMenuAction::PageRight ? 1 : -1;
		if (Mode == EMXRaceMode::RaceAI)
		{
			Settings.AICountRace = FMath::Clamp(Settings.AICountRace + D, 1, 7);
		}
		else if (Mode == EMXRaceMode::Versus || Mode == EMXRaceMode::Championship)
		{
			Settings.AICountVersus = FMath::Clamp(Settings.AICountVersus + D, 0, 4);
		}
		Beep((uint8)EMXSfx::MenuMove);
		break;
	}
	case EMXMenuAction::Start:
		if (LobbyReady())
		{
			LobbyCountdown = 0.01f;
		}
		break;
	default:
		break;
	}
}

void UMXFrontend::ControlsAction(EMXMenuAction Action)
{
	UMXGameInstance* G = GM->GI();
	const int32 Count = 10; // scheme, assist, 6 actions, reset, done
	switch (Action)
	{
	case EMXMenuAction::Up: ControlsIndex = (ControlsIndex + Count - 1) % Count; Beep((uint8)EMXSfx::MenuMove); break;
	case EMXMenuAction::Down: ControlsIndex = (ControlsIndex + 1) % Count; Beep((uint8)EMXSfx::MenuMove); break;
	case EMXMenuAction::Left:
	case EMXMenuAction::Right:
	case EMXMenuAction::Confirm:
		if (ControlsIndex == 0)
		{
			const bool bAssist = EditProfile.bLandingAssist;
			EditProfile = FMXControlProfile::Defaults(EditProfile.Scheme == EMXControlScheme::Classic ? EMXControlScheme::Modern : EMXControlScheme::Classic);
			EditProfile.bLandingAssist = bAssist;
		}
		else if (ControlsIndex == 1)
		{
			EditProfile.bLandingAssist = !EditProfile.bLandingAssist;
		}
		else if (ControlsIndex >= 2 && ControlsIndex <= 7 && Action == EMXMenuAction::Confirm)
		{
			bCapturing = true;
		}
		else if (ControlsIndex == 8 && Action == EMXMenuAction::Confirm)
		{
			const bool bAssist = EditProfile.bLandingAssist;
			EditProfile = FMXControlProfile::Defaults(EditProfile.Scheme);
			EditProfile.bLandingAssist = bAssist;
		}
		else if (ControlsIndex == 9 && Action == EMXMenuAction::Confirm)
		{
			G->StoreProfile(ControlsSlot, EditProfile);
			SetScreen(EMXScreen::Lobby);
		}
		Beep((uint8)EMXSfx::MenuMove);
		break;
	case EMXMenuAction::Back:
		G->StoreProfile(ControlsSlot, EditProfile);
		SetScreen(EMXScreen::Lobby);
		break;
	default:
		break;
	}
}

// ------------------------------------------------------------------------------------------------
// Drawing
// ------------------------------------------------------------------------------------------------

void UMXFrontend::Draw(UCanvas* C)
{
	const float U = FMath::Max(0.6f, C->ClipY / 1080.f);
	switch (Screen)
	{
	case EMXScreen::Title: DrawTitle(C, U); break;
	case EMXScreen::MainMenu: DrawMainMenu(C, U); break;
	case EMXScreen::Lobby: DrawLobby(C, U); break;
	case EMXScreen::Controls: DrawControls(C, U); break;
	case EMXScreen::CourseSelect: DrawCourseSelect(C, U); break;
	case EMXScreen::Options: DrawOptions(C, U); break;
	case EMXScreen::Results: DrawResults(C, U); break;
	case EMXScreen::Pause: DrawPause(C, U); break;
	case EMXScreen::ChampTable: DrawChampTable(C, U); break;
	default: break;
	}
	if (ToastTime > 0.f && !Toast.IsEmpty())
	{
		const float A = FMath::Clamp(ToastTime, 0.f, 1.f);
		MXDraw::Text(C, Toast, C->ClipX * 0.5f, C->ClipY * 0.93f, 30.f * U, FLinearColor(1.f, 0.9f, 0.4f, A), 0.5f, 0.5f);
	}
}

void UMXFrontend::DrawList(UCanvas* C, const TArray<FString>& Items, int32 Selected, float X, float Y, float Size, float U)
{
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const bool bSel = i == Selected;
		if (bSel)
		{
			const FVector2D Sz = MXDraw::Measure(Items[i], Size);
			MXDraw::Rect(C, X - 18.f * U, Y + i * Size * 1.45f - 4.f * U, Sz.X + 36.f * U, Size * 1.3f + 8.f * U, FLinearColor(Accent.R, Accent.G, Accent.B, 0.85f));
		}
		MXDraw::Text(C, Items[i], X, Y + i * Size * 1.45f, Size, bSel ? FLinearColor::Black : FLinearColor::White, 0.f, 0.f, true, !bSel);
	}
}

void UMXFrontend::DrawTitle(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	MXDraw::Rect(C, 0.f, H * 0.18f, W, H * 0.36f, FLinearColor(0.f, 0.f, 0.f, 0.55f));
	const FVector2D Sz = MXDraw::Text(C, TEXT("HEATLINE"), W * 0.5f, H * 0.3f, 150.f * U, FLinearColor::White, 0.5f, 0.5f);
	MXDraw::Text(C, TEXT("MX"), W * 0.5f + Sz.X * 0.5f + 12.f * U, H * 0.3f - 30.f * U, 70.f * U, Accent, 0.f, 0.5f);
	MXDraw::Text(C, TEXT("Lanes. Ramps. Pitch. Heat.  A modern take on NES Excitebike."), W * 0.5f, H * 0.42f, 30.f * U, Dim, 0.5f, 0.5f, false);
	if (FMath::Fmod(ScreenTime, 1.f) < 0.65f)
	{
		MXDraw::Text(C, TEXT("PRESS  A  /  ENTER"), W * 0.5f, H * 0.72f, 44.f * U, Accent, 0.5f, 0.5f);
	}
	MXDraw::Text(C, TEXT("1-4 players  -  gamepads or keyboard + gamepads"), W * 0.5f, H * 0.8f, 24.f * U, Dim, 0.5f, 0.5f, false);
}

void UMXFrontend::DrawMainMenu(UCanvas* C, float U)
{
	const float H = C->ClipY;
	MXDraw::Rect(C, 0.f, 0.f, 620.f * U, H, Panel);
	MXDraw::Text(C, TEXT("HEATLINE MX"), 60.f * U, 60.f * U, 64.f * U, FLinearColor::White);
	const TArray<FString> Items = {TEXT("TIME TRIAL"), TEXT("RACE VS AI"), TEXT("LOCAL VERSUS"), TEXT("CHAMPIONSHIP"), TEXT("TRACK DESIGNER"), TEXT("OPTIONS"), TEXT("QUIT")};
	DrawList(C, Items, MenuIndex, 70.f * U, 220.f * U, 44.f * U, U);
	static const TCHAR* Help[] = {
		TEXT("Solo against the clock and your best ghost. Medals per course."),
		TEXT("You against five AI riders (difficulty in Options)."),
		TEXT("2-4 players split screen, optional AI riders."),
		TEXT("All five courses in a row, points per finish."),
		TEXT("Build, test and save your own courses."),
		TEXT("Split layout, AI, landing meter, volume."),
		TEXT("Exit the game."),
	};
	MXDraw::TextWrap(C, Help[FMath::Clamp(MenuIndex, 0, 6)], 70.f * U, H - 160.f * U, 24.f * U, 620.f * U - 100.f * U, Dim, false);
	MXDraw::Text(C, TEXT("A / Enter select    B / Esc back"), 70.f * U, H - 70.f * U, 20.f * U, Dim, 0.f, 0.f, false);
}

void UMXFrontend::DrawLobby(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	UMXGameInstance* G = GM->GI();
	MXDraw::Rect(C, 0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	MXDraw::Text(C, ModeName(Mode), W * 0.5f, 50.f * U, 56.f * U, FLinearColor::White, 0.5f, 0.f);
	MXDraw::Text(C, MaxPlayersForMode() == 1 ? TEXT("One rider") : TEXT("Press A / Enter on any controller or keyboard to join"), W * 0.5f, 118.f * U, 26.f * U, Dim, 0.5f, 0.f, false);

	const float CardW = (W - 100.f * U) / 4.f - 20.f * U;
	const float CardH = H * 0.58f;
	const float CardY = 180.f * U;
	for (int32 i = 0; i < 4; ++i)
	{
		const FMXPlayerSlot& S = G->Slots[i];
		const float X = 50.f * U + i * (CardW + 26.6f * U);
		const bool bAllowed = i < MaxPlayersForMode();
		MXDraw::Rect(C, X, CardY, CardW, CardH, FLinearColor(0.05f, 0.06f, 0.08f, bAllowed ? 0.85f : 0.35f));
		MXDraw::Text(C, FString::Printf(TEXT("P%d"), i + 1), X + 20.f * U, CardY + 16.f * U, 44.f * U, S.bJoined ? MX::RiderColor(S.ColorIndex) : Dim);
		if (!bAllowed)
		{
			continue;
		}
		if (!S.bJoined)
		{
			if (FMath::Fmod(ScreenTime + i * 0.2f, 1.2f) < 0.8f)
			{
				MXDraw::Text(C, TEXT("PRESS A"), X + CardW * 0.5f, CardY + CardH * 0.45f, 34.f * U, Accent, 0.5f, 0.5f);
				MXDraw::Text(C, TEXT("TO JOIN"), X + CardW * 0.5f, CardY + CardH * 0.45f + 40.f * U, 26.f * U, Dim, 0.5f, 0.5f);
			}
			continue;
		}
		const FLinearColor Col = MX::RiderColor(S.ColorIndex);
		float Y = CardY + 80.f * U;
		MXDraw::Text(C, S.bDeviceLost ? TEXT("DISCONNECTED") : MXDevice::Label(S.DeviceKey), X + 20.f * U, Y, 22.f * U, S.bDeviceLost ? FLinearColor(1.f, 0.3f, 0.2f) : Dim, 0.f, 0.f, false);
		Y += 40.f * U;
		// Colour swatch + bike silhouette block.
		MXDraw::Rect(C, X + 20.f * U, Y, CardW - 40.f * U, 90.f * U, FLinearColor(Col.R, Col.G, Col.B, 0.95f));
		MXDraw::Text(C, TEXT("<"), X + 30.f * U, Y + 45.f * U, 40.f * U, S.bReady ? FLinearColor(1, 1, 1, 0.2f) : FLinearColor::White, 0.f, 0.5f);
		MXDraw::Text(C, TEXT(">"), X + CardW - 30.f * U, Y + 45.f * U, 40.f * U, S.bReady ? FLinearColor(1, 1, 1, 0.2f) : FLinearColor::White, 1.f, 0.5f);
		MXDraw::Text(C, MX::RiderColorName(S.ColorIndex), X + CardW * 0.5f, Y + 45.f * U, 30.f * U, FLinearColor::White, 0.5f, 0.5f);
		Y += 110.f * U;
		MXDraw::Text(C, S.Profile.Scheme == EMXControlScheme::Classic ? TEXT("CLASSIC controls") : TEXT("MODERN controls"), X + 20.f * U, Y, 24.f * U, FLinearColor::White);
		Y += 32.f * U;
		const TArray<FString> Lines = UMXInputConfig::DescribeLayout(S.Profile, !S.UsesKeyboard());
		for (int32 L = 1; L < Lines.Num() - 1; ++L)
		{
			MXDraw::TextFit(C, Lines[L], X + 20.f * U, Y, 17.f * U, CardW - 40.f * U, Dim, 0.f, 0.f, false);
			Y += MXDraw::LineHeight(17.f * U, false);
		}
		MXDraw::Text(C, S.Profile.bLandingAssist ? TEXT("Landing assist: ON") : TEXT("Landing assist: OFF"), X + 20.f * U, Y, 17.f * U, Dim, 0.f, 0.f, false);
		// Ready banner.
		const float RY = CardY + CardH - 70.f * U;
		MXDraw::Rect(C, X + 20.f * U, RY, CardW - 40.f * U, 50.f * U, S.bReady ? FLinearColor(0.1f, 0.7f, 0.25f, 0.95f) : FLinearColor(0.25f, 0.25f, 0.28f, 0.9f));
		MXDraw::Text(C, S.bReady ? TEXT("READY!") : TEXT("A = READY"), X + CardW * 0.5f, RY + 25.f * U, 28.f * U, FLinearColor::White, 0.5f, 0.5f);
	}
	const FMXSettings& Settings = G->GetSave()->Settings;
	FString Footer;
	if (Mode == EMXRaceMode::RaceAI)
	{
		Footer = FString::Printf(TEXT("AI riders: %d (LB/RB or Q/E)    AI: %s"), Settings.AICountRace, *DifficultyName(Settings.AIDifficulty));
	}
	else if (Mode == EMXRaceMode::Versus || Mode == EMXRaceMode::Championship)
	{
		Footer = FString::Printf(TEXT("Extra AI riders: %d (LB/RB or Q/E)    AI: %s    2P split: %s"), Settings.AICountVersus, *DifficultyName(Settings.AIDifficulty),
			Settings.TwoPlayerSplit == EMXSplitOrientation::Horizontal ? TEXT("top/bottom") : TEXT("side by side"));
	}
	else
	{
		Footer = TEXT("Solo run against the clock and your best ghost");
	}
	MXDraw::Text(C, Footer, W * 0.5f, CardY + CardH + 30.f * U, 26.f * U, FLinearColor::White, 0.5f, 0.f, false);
	MXDraw::Text(C, TEXT("Left/Right colour    Y/Tab switch Classic/Modern    X/R remap controls    A ready    B leave"), W * 0.5f, H - 60.f * U, 22.f * U, Dim, 0.5f, 0.f, false);
	if (LobbyCountdown > 0.f)
	{
		MXDraw::Text(C, TEXT("ALL READY!"), W * 0.5f, H * 0.5f, 90.f * U, Accent, 0.5f, 0.5f);
	}
}

void UMXFrontend::DrawControls(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	UMXGameInstance* G = GM->GI();
	const FMXPlayerSlot& S = G->Slots[ControlsSlot];
	const bool bPad = !S.UsesKeyboard();
	MXDraw::Rect(C, 0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, 0.75f));
	MXDraw::Text(C, FString::Printf(TEXT("P%d CONTROLS  (%s)"), ControlsSlot + 1, *MXDevice::Label(S.DeviceKey)), 80.f * U, 50.f * U, 48.f * U, MX::RiderColor(S.ColorIndex));
	TArray<FString> Items;
	Items.Add(FString::Printf(TEXT("Layout:  %s"), EditProfile.Scheme == EMXControlScheme::Classic ? TEXT("CLASSIC (screen-relative)") : TEXT("MODERN (rider-relative)")));
	Items.Add(FString::Printf(TEXT("Landing assist:  %s"), EditProfile.bLandingAssist ? TEXT("ON (bike eases toward the landing slope)") : TEXT("OFF")));
	FMXBindingSet& Set = bPad ? EditProfile.Gamepad : EditProfile.Keyboard;
	for (int32 a = 0; a < (int32)EMXBindAction::Count; ++a)
	{
		const TArray<FKey>* Keys = UMXInputConfig::GetKeys(Set, (EMXBindAction)a);
		FString Label = UMXInputConfig::ActionLabel((EMXBindAction)a, EditProfile.Scheme);
		if (bCapturing && ControlsIndex == a + 2)
		{
			Items.Add(FString::Printf(TEXT("%s:  PRESS A NEW %s..."), *Label, bPad ? TEXT("BUTTON") : TEXT("KEY")));
		}
		else
		{
			Items.Add(FString::Printf(TEXT("%s:  %s"), *Label, Keys ? *UMXInputConfig::KeysLabel(*Keys) : TEXT("-")));
		}
	}
	Items.Add(TEXT("Reset to defaults"));
	Items.Add(TEXT("Done"));
	DrawList(C, Items, ControlsIndex, 90.f * U, 150.f * U, 30.f * U, U);
	float Y = H - 240.f * U;
	for (const FString& L : UMXInputConfig::DescribeLayout(EditProfile, bPad))
	{
		MXDraw::Text(C, L, 90.f * U, Y, 22.f * U, Dim, 0.f, 0.f, false);
		Y += 30.f * U;
	}
	if (bPad)
	{
		MXDraw::Text(C, EditProfile.Scheme == EMXControlScheme::Classic ? TEXT("Left stick also works: up/down lanes, left/right pitch.") : TEXT("Left stick: steer left/right, lean back/forward."), 90.f * U, Y, 20.f * U, Dim, 0.f, 0.f, false);
	}
}

void UMXFrontend::DrawCourseSelect(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	UMXGameInstance* G = GM->GI();
	const TArray<FMXCourseEntry>& E = G->GetCourses()->GetEntries();
	MXDraw::Rect(C, 0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, 0.5f));
	MXDraw::Text(C, Mode == EMXRaceMode::Championship ? TEXT("CHAMPIONSHIP") : TEXT("SELECT COURSE"), 80.f * U, 50.f * U, 52.f * U, FLinearColor::White);
	if (Mode == EMXRaceMode::Championship)
	{
		const TArray<FString> Order = G->GetCourses()->ChampionshipOrder();
		TArray<FString> Items;
		for (int32 i = 0; i < Order.Num(); ++i)
		{
			Items.Add(FString::Printf(TEXT("Race %d:  Course %d"), i + 1, i + 1));
		}
		DrawList(C, Items, -1, 90.f * U, 170.f * U, 36.f * U, U);
		MXDraw::Text(C, TEXT("Points 10-8-6-5-4-3-2-1.  Main-race layouts, 2 laps each."), 90.f * U, 170.f * U + Items.Num() * 52.f * U + 20.f * U, 24.f * U, Dim, 0.f, 0.f, false);
		MXDraw::Text(C, TEXT("A START    B BACK"), 90.f * U, H - 70.f * U, 26.f * U, Accent);
		return;
	}
	TArray<FString> Items;
	for (const FMXCourseEntry& Entry : E)
	{
		Items.Add(Entry.bUser ? FString::Printf(TEXT("%s  (yours)"), *Entry.DisplayName) : Entry.DisplayName);
	}
	DrawList(C, Items, CourseIndex, 90.f * U, 150.f * U, 32.f * U, U);

	// Details panel.
	FMXTrackDefinition Def;
	if (E.IsValidIndex(CourseIndex) && G->GetCourses()->GetCourse(E[CourseIndex].Id, Def))
	{
		const float PX = W * 0.46f;
		float Y = 150.f * U;
		MXDraw::Rect(C, PX - 30.f * U, Y - 20.f * U, W * 0.5f, H * 0.66f, Panel);
		MXDraw::Text(C, Def.Name, PX, Y, 44.f * U, FLinearColor::White);
		Y += 64.f * U;
		const bool bHasVariants = Def.HasMainOnlyPieces();
		const EMXLayoutVariant V = bHasVariants ? Variant : EMXLayoutVariant::Main;
		const int32 Laps = LapsOverride > 0 ? LapsOverride : Def.Laps;
		MXDraw::Text(C, FString::Printf(TEXT("Lap %.0f m    Laps %d%s"), Def.LapLengthFor(V), Laps, LapsOverride > 0 ? TEXT(" (custom)") : TEXT("")), PX, Y, 26.f * U, FLinearColor::White, 0.f, 0.f, false);
		Y += 38.f * U;
		if (bHasVariants)
		{
			MXDraw::Text(C, FString::Printf(TEXT("< Layout: %s >"), V == EMXLayoutVariant::Main ? TEXT("MAIN RACE (all NES pieces)") : TEXT("CHALLENGE (NES qualifier)")), PX, Y, 26.f * U, Accent);
			Y += 38.f * U;
		}
		int32 Ramps = 0, Mud = 0, Cool = 0, Barriers = 0;
		for (const FMXSegment& S : Def.Segments)
		{
			if (S.bMainOnly && V == EMXLayoutVariant::Challenge) { continue; }
			Ramps += FMXObstacleLibrary::IsRamp(S.Type) ? 1 : 0;
			Mud += S.Type == EMXObstacleType::Mud ? 1 : 0;
			Cool += S.Type == EMXObstacleType::CoolStrip ? 1 : 0;
			Barriers += S.Type == EMXObstacleType::Barrier ? 1 : 0;
		}
		MXDraw::Text(C, FString::Printf(TEXT("Ramps %d   Mud %d   Barriers %d   Cool strips %d"), Ramps, Mud, Barriers, Cool), PX, Y, 24.f * U, Dim, 0.f, 0.f, false);
		Y += 36.f * U;
		if (!Def.SourceNote.IsEmpty())
		{
			MXDraw::Text(C, Def.NesTrack > 0 ? FString::Printf(TEXT("Translated from NES Excitebike track %d"), Def.NesTrack) : Def.SourceNote, PX, Y, 22.f * U, Dim, 0.f, 0.f, false);
			Y += 34.f * U;
		}
		if (const FMXRecordEntry* R = G->GetRecord(Def.Id, V, Laps))
		{
			static const TCHAR* Medals[] = {TEXT(""), TEXT("  BRONZE"), TEXT("  SILVER"), TEXT("  GOLD")};
			MXDraw::Text(C, FString::Printf(TEXT("Record %s   Best lap %s%s"), *MX::FormatRaceTime(R->BestTime), *MX::FormatRaceTime(R->BestLap), Medals[FMath::Clamp(R->Medal, 0, 3)]), PX, Y, 26.f * U, FLinearColor(1.f, 0.9f, 0.4f));
			Y += 38.f * U;
		}
		if (Mode == EMXRaceMode::TimeTrial)
		{
			float Gold, Silver, Bronze;
			UMXCourseLibrary::GetMedalTimes(Def, V, Laps, Gold, Silver, Bronze);
			MXDraw::Text(C, FString::Printf(TEXT("Medals  Gold %s  Silver %s  Bronze %s"), *MX::FormatRaceTime(Gold), *MX::FormatRaceTime(Silver), *MX::FormatRaceTime(Bronze)), PX, Y, 22.f * U, Dim, 0.f, 0.f, false);
		}
	}
	MXDraw::Text(C, TEXT("A START    Left/Right layout    LB/RB (Q/E) laps    B BACK"), 90.f * U, H - 70.f * U, 24.f * U, Accent);
}

void UMXFrontend::DrawOptions(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	const FMXSettings& S = GM->GI()->GetSave()->Settings;
	MXDraw::Rect(C, 0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	MXDraw::Text(C, TEXT("OPTIONS"), 80.f * U, 50.f * U, 56.f * U, FLinearColor::White);
	const TArray<FString> Items = {
		FString::Printf(TEXT("Two-player split:  %s"), S.TwoPlayerSplit == EMXSplitOrientation::Horizontal ? TEXT("TOP / BOTTOM") : TEXT("SIDE BY SIDE")),
		FString::Printf(TEXT("AI difficulty:  %s"), *DifficultyName(S.AIDifficulty)),
		FString::Printf(TEXT("AI riders in Race vs AI:  %d"), S.AICountRace),
		FString::Printf(TEXT("AI riders in Versus:  %d"), S.AICountVersus),
		FString::Printf(TEXT("Landing meter:  %s"), S.bLandingMeter ? TEXT("ON") : TEXT("OFF")),
		FString::Printf(TEXT("Volume:  %d%%"), FMath::RoundToInt(S.MasterVolume * 100.f)),
		TEXT("Back")
	};
	DrawList(C, Items, OptionsIndex, 90.f * U, 170.f * U, 36.f * U, U);
	MXDraw::Text(C, TEXT("AI difficulty changes skill (reaction, landing precision, heat and line choice), never top speed."), 90.f * U, H - 110.f * U, 22.f * U, Dim, 0.f, 0.f, false);
}

void UMXFrontend::DrawResults(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	const TArray<FMXResultRow>& Rows = GM->GetResults();
	const FMXRaceConfig& Cfg = GM->GetRace()->GetConfig();
	const float PW = FMath::Min(W * 0.8f, 1400.f * U);
	const float PX = (W - PW) * 0.5f;
	const float PY = H * 0.12f;
	const float RowH = FMath::Min(56.f * U, (H * 0.55f) / FMath::Max(1, Rows.Num()));
	MXDraw::Rect(C, PX, PY, PW, H * 0.78f, FLinearColor(0.02f, 0.03f, 0.05f, 0.9f));
	MXDraw::Text(C, Cfg.Mode == EMXRaceMode::TimeTrial ? TEXT("TIME TRIAL RESULT") : TEXT("RACE RESULTS"), PX + PW * 0.5f, PY + 24.f * U, 48.f * U, FLinearColor::White, 0.5f, 0.f);
	float Y = PY + 100.f * U;
	MXDraw::Text(C, TEXT("POS"), PX + 40.f * U, Y, 20.f * U, Dim, 0.f, 0.f, false);
	MXDraw::Text(C, TEXT("RIDER"), PX + 170.f * U, Y, 20.f * U, Dim, 0.f, 0.f, false);
	MXDraw::Text(C, TEXT("TIME"), PX + PW * 0.52f, Y, 20.f * U, Dim, 0.f, 0.f, false);
	MXDraw::Text(C, TEXT("BEST LAP"), PX + PW * 0.68f, Y, 20.f * U, Dim, 0.f, 0.f, false);
	MXDraw::Text(C, Cfg.Mode == EMXRaceMode::Championship ? TEXT("PTS") : (Cfg.Mode == EMXRaceMode::TimeTrial ? TEXT("MEDAL") : TEXT("")), PX + PW * 0.84f, Y, 20.f * U, Dim, 0.f, 0.f, false);
	Y += 34.f * U;
	static const TCHAR* Medals[] = {TEXT("-"), TEXT("BRONZE"), TEXT("SILVER"), TEXT("GOLD")};
	for (const FMXResultRow& R : Rows)
	{
		const FLinearColor Col = MX::RiderColor(R.ColorIndex);
		MXDraw::Rect(C, PX + 20.f * U, Y + 6.f * U, 8.f * U, RowH - 12.f * U, Col);
		MXDraw::Text(C, MXDraw::Ordinal(R.Position), PX + 40.f * U, Y + RowH * 0.5f, 32.f * U, R.Position == 1 ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor::White, 0.f, 0.5f);
		MXDraw::Text(C, R.Name + (R.bHuman ? TEXT("") : TEXT("  AI")), PX + 170.f * U, Y + RowH * 0.5f, 30.f * U, R.bHuman ? Col : FLinearColor(0.85f, 0.85f, 0.85f), 0.f, 0.5f);
		MXDraw::Text(C, R.bEstimated ? FString::Printf(TEXT("%s est."), *MX::FormatRaceTime(R.Time)) : MX::FormatRaceTime(R.Time), PX + PW * 0.52f, Y + RowH * 0.5f, 28.f * U, FLinearColor::White, 0.f, 0.5f);
		MXDraw::Text(C, MX::FormatRaceTime(R.BestLap), PX + PW * 0.68f, Y + RowH * 0.5f, 26.f * U, Dim, 0.f, 0.5f);
		if (Cfg.Mode == EMXRaceMode::Championship)
		{
			MXDraw::Text(C, FString::Printf(TEXT("+%d"), R.Points), PX + PW * 0.84f, Y + RowH * 0.5f, 28.f * U, Accent, 0.f, 0.5f);
		}
		else if (Cfg.Mode == EMXRaceMode::TimeTrial)
		{
			MXDraw::Text(C, Medals[FMath::Clamp(R.Medal, 0, 3)], PX + PW * 0.84f, Y + RowH * 0.5f, 26.f * U, R.Medal == 3 ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor::White, 0.f, 0.5f);
		}
		if (R.bNewRecord)
		{
			MXDraw::Text(C, TEXT("NEW RECORD"), PX + PW * 0.52f, Y + RowH * 0.5f + 20.f * U, 16.f * U, FLinearColor(0.3f, 1.f, 0.5f), 0.f, 0.f);
		}
		if (R.bHuman)
		{
			MXDraw::Text(C, FString::Printf(TEXT("%d perfect, %d crash"), R.Perfects, R.Crashes), PX + 170.f * U, Y + RowH * 0.5f + 20.f * U, 16.f * U, Dim, 0.f, 0.f, false);
		}
		Y += RowH;
	}
	TArray<FString> Items;
	if (Cfg.Mode == EMXRaceMode::Championship)
	{
		const FMXChampionship& Ch = GM->GI()->Championship;
		Items = {Ch.RaceIndex + 1 >= Ch.Courses.Num() ? TEXT("FINAL STANDINGS") : TEXT("STANDINGS & NEXT RACE"), TEXT("STANDINGS"), TEXT("QUIT TO LOBBY")};
	}
	else if (Cfg.Mode == EMXRaceMode::DesignerTest)
	{
		Items = {TEXT("RIDE AGAIN"), TEXT("BACK TO DESIGNER")};
	}
	else
	{
		Items = {TEXT("REMATCH"), TEXT("CHANGE COURSE"), TEXT("BACK TO LOBBY")};
	}
	DrawList(C, Items, ResultsIndex, PX + 60.f * U, PY + H * 0.78f - Items.Num() * 46.f * U - 30.f * U, 30.f * U, U);
}

void UMXFrontend::DrawPause(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	MXDraw::Rect(C, 0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, 0.55f));
	const int32 Lost = GM->LostDeviceSlot();
	if (Lost != INDEX_NONE)
	{
		MXDraw::Text(C, FString::Printf(TEXT("P%d CONTROLLER DISCONNECTED"), Lost + 1), W * 0.5f, H * 0.4f, 52.f * U, FLinearColor(1.f, 0.35f, 0.25f), 0.5f, 0.5f);
		MXDraw::Text(C, TEXT("Reconnect it, or press A on a free controller to take over this rider."), W * 0.5f, H * 0.5f, 28.f * U, FLinearColor::White, 0.5f, 0.5f, false);
		return;
	}
	// A solid panel keeps the menu readable over the players' HUDs in split screen.
	const float PW = 560.f * U;
	const float PH = 380.f * U;
	MXDraw::Rect(C, W * 0.5f - PW * 0.5f, H * 0.5f - PH * 0.5f, PW, PH, FLinearColor(0.05f, 0.06f, 0.08f, 0.92f));
	MXDraw::Text(C, FString::Printf(TEXT("PAUSED%s"), PausedBySlot >= 0 ? *FString::Printf(TEXT(" (P%d)"), PausedBySlot + 1) : TEXT("")), W * 0.5f, H * 0.5f - PH * 0.5f + 20.f * U, 56.f * U, FLinearColor::White, 0.5f, 0.f);
	const bool bDesigner = GM->GetState() == EMXAppState::DesignerTest;
	const TArray<FString> Items = {TEXT("RESUME"), TEXT("RESTART"), bDesigner ? TEXT("BACK TO DESIGNER") : TEXT("QUIT TO LOBBY")};
	const float X = W * 0.5f - 180.f * U;
	DrawList(C, Items, PauseIndex, X, H * 0.5f - PH * 0.5f + 130.f * U, 40.f * U, U);
}

void UMXFrontend::DrawChampTable(UCanvas* C, float U)
{
	const float W = C->ClipX;
	const float H = C->ClipY;
	const FMXChampionship& Ch = GM->GI()->Championship;
	MXDraw::Rect(C, 0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, 0.8f));
	const bool bFinal = Ch.RaceIndex + 1 >= Ch.Courses.Num();
	MXDraw::Text(C, bFinal ? TEXT("CHAMPIONSHIP FINAL STANDINGS") : FString::Printf(TEXT("CHAMPIONSHIP AFTER RACE %d / %d"), Ch.RaceIndex + 1, Ch.Courses.Num()), W * 0.5f, 70.f * U, 48.f * U, FLinearColor::White, 0.5f, 0.f);
	TArray<int32> Order;
	for (int32 i = 0; i < Ch.Points.Num(); ++i)
	{
		if (Ch.Names.IsValidIndex(i) && !Ch.Names[i].IsEmpty())
		{
			Order.Add(i);
		}
	}
	Order.Sort([&Ch](int32 A, int32 B) { return Ch.Points[A] > Ch.Points[B]; });
	float Y = 170.f * U;
	for (int32 k = 0; k < Order.Num(); ++k)
	{
		const int32 i = Order[k];
		const FLinearColor Col = MX::RiderColor(Ch.Colors.IsValidIndex(i) ? Ch.Colors[i] : 0);
		MXDraw::Rect(C, W * 0.25f, Y + 6.f * U, 10.f * U, 44.f * U, Col);
		MXDraw::Text(C, MXDraw::Ordinal(k + 1), W * 0.27f, Y + 28.f * U, 34.f * U, k == 0 ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor::White, 0.f, 0.5f);
		MXDraw::Text(C, Ch.Names[i], W * 0.36f, Y + 28.f * U, 32.f * U, Col, 0.f, 0.5f);
		MXDraw::Text(C, FString::Printf(TEXT("%d pts"), Ch.Points[i]), W * 0.72f, Y + 28.f * U, 32.f * U, FLinearColor::White, 1.f, 0.5f);
		Y += 60.f * U;
	}
	MXDraw::Text(C, bFinal ? TEXT("A  BACK TO LOBBY") : TEXT("A  NEXT RACE"), W * 0.5f, H - 90.f * U, 34.f * U, Accent, 0.5f, 0.f);
}
