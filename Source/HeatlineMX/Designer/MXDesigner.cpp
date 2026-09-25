#include "Designer/MXDesigner.h"
#include "Race/MXGameMode.h"
#include "Race/MXPlayerController.h"
#include "Track/MXTrackActor.h"
#include "Track/MXCourseLibrary.h"
#include "Track/MXObstacleLibrary.h"
#include "Session/MXGameInstance.h"
#include "Audio/MXEngineAudio.h"
#include "UI/MXDraw.h"
#include "UI/MXFrontend.h"
#include "Core/MXTuning.h"
#include "HeatlineMX.h"
#include "Engine/Canvas.h"

namespace
{
	const TCHAR* KeyGrid = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
	constexpr int32 KeyGridCols = 10;
	const FLinearColor Accent(1.f, 0.55f, 0.1f);
	const FLinearColor Dim(0.75f, 0.75f, 0.78f);

	enum EMenuItem
	{
		MI_Resume,
		MI_Test,
		MI_Save,
		MI_SaveAs,
		MI_Load,
		MI_New,
		MI_Laps,
		MI_Length,
		MI_Validate,
		MI_Race,
		MI_Exit,
		MI_Count
	};
}

void UMXDesigner::Init(AMXGameMode* InGM)
{
	GM = InGM;
	Audio = NewObject<UMXEngineAudioComponent>(GM, TEXT("DesignerAudio"));
	Audio->SetUIMode(true);
	Audio->RegisterComponent();
	Audio->Start();
	NewTrack();
}

void UMXDesigner::Beep(uint8 Sfx)
{
	if (Audio)
	{
		Audio->PlayOneShot((EMXSfx)Sfx, 0.6f);
	}
}

void UMXDesigner::Toast(const FString& Msg)
{
	ToastText = Msg;
	ToastTime = 2.5f;
	UE_LOG(LogHeatline, Log, TEXT("Designer: %s"), *Msg);
}

void UMXDesigner::NewTrack()
{
	Def = FMXTrackDefinition();
	Def.Id = TEXT("user:Untitled");
	Def.Name = TEXT("Untitled");
	Def.Author = TEXT("Local player");
	Def.Laps = 2;
	Def.LapLengthM = 400.f;
	FMXTrackValidator::EnsureFinishDeck(Def, MXTuning::Style());
	UndoStack.Reset();
	RedoStack.Reset();
	Selected = INDEX_NONE;
	Cursor = 30.f;
	bDirty = false;
	bPreviewDirty = true;
	RunValidation(false);
}

void UMXDesigner::Open()
{
	bOpen = true;
	Mode = EMXDesignerMode::Edit;
	OwnerDevice = GM->GI()->Slots[0].bJoined ? GM->GI()->Slots[0].DeviceKey : MXDevice::None;
	bPreviewDirty = true;
	RebuildPreview();
	UpdateCamera();
}

void UMXDesigner::Close()
{
	bOpen = false;
	if (AMXTrack* T = GM->GetTrackActor())
	{
		T->SetHighlight(-1.f, -1.f, 0);
	}
}

void UMXDesigner::Resume()
{
	bOpen = true;
	Mode = EMXDesignerMode::Edit;
	bPreviewDirty = true;
	RebuildPreview();
	UpdateCamera();
}

void UMXDesigner::PushUndo()
{
	UndoStack.Add(Def);
	if (UndoStack.Num() > 100)
	{
		UndoStack.RemoveAt(0);
	}
	RedoStack.Reset();
}

void UMXDesigner::MarkDirty()
{
	bDirty = true;
	bPreviewDirty = true;
	RebuildDelay = 0.12f;
}

int32 UMXDesigner::PlacePiece(EMXObstacleType Type, float StartM, int32 LaneMask)
{
	PushUndo();
	FMXSegment S = FMXObstacleLibrary::MakeDefault(Type, FMath::Max(0.f, StartM), MXTuning::Style());
	if (LaneMask > 0)
	{
		S.LaneMask = LaneMask;
	}
	S.Id = FString();
	Def.Segments.Add(S);
	Def.EnsureIds(TEXT("U"));
	const FString Id = Def.Segments.Last().Id;
	Def.SortSegments();
	MarkDirty();
	RunValidation(false);
	return Def.Segments.IndexOfByPredicate([&Id](const FMXSegment& X) { return X.Id == Id; });
}

bool UMXDesigner::MovePiece(int32 Index, float NewStart)
{
	if (!Def.Segments.IsValidIndex(Index) || Def.Segments[Index].Type == EMXObstacleType::FinishDeck)
	{
		return false;
	}
	Def.Segments[Index].StartM = FMath::Clamp(NewStart, 0.f, Def.LapLengthM);
	MarkDirty();
	return true;
}

bool UMXDesigner::DeletePiece(int32 Index)
{
	if (!Def.Segments.IsValidIndex(Index) || Def.Segments[Index].Type == EMXObstacleType::FinishDeck)
	{
		return false;
	}
	PushUndo();
	Def.Segments.RemoveAt(Index);
	Selected = INDEX_NONE;
	MarkDirty();
	RunValidation(false);
	return true;
}

bool UMXDesigner::SetLaneMask(int32 Index, int32 Mask)
{
	if (!Def.Segments.IsValidIndex(Index) || Mask <= 0)
	{
		return false;
	}
	Def.Segments[Index].LaneMask = Mask & MX::AllLanes;
	MarkDirty();
	return true;
}

void UMXDesigner::SetLaps(int32 Laps)
{
	PushUndo();
	Def.Laps = FMath::Clamp(Laps, 1, 9);
	MarkDirty();
}

void UMXDesigner::SetLapLength(float Meters)
{
	PushUndo();
	Def.LapLengthM = FMath::Clamp(Meters, FMXTrackValidator::MinLapLength, FMXTrackValidator::MaxLapLength);
	// Keep the finish deck closing the lap.
	const int32 Fin = Def.Segments.IndexOfByPredicate([](const FMXSegment& S) { return S.Type == EMXObstacleType::FinishDeck; });
	if (Fin != INDEX_NONE)
	{
		Def.Segments[Fin].StartM = Def.LapLengthM - Def.Segments[Fin].LengthM - 4.f;
	}
	Def.SortSegments();
	MarkDirty();
	RunValidation(false);
}

bool UMXDesigner::Undo()
{
	if (UndoStack.Num() == 0)
	{
		return false;
	}
	RedoStack.Add(Def);
	Def = UndoStack.Pop();
	Selected = INDEX_NONE;
	MarkDirty();
	RunValidation(false);
	return true;
}

bool UMXDesigner::Redo()
{
	if (RedoStack.Num() == 0)
	{
		return false;
	}
	UndoStack.Add(Def);
	Def = RedoStack.Pop();
	Selected = INDEX_NONE;
	MarkDirty();
	RunValidation(false);
	return true;
}

bool UMXDesigner::Save(const FString& Name, FString& OutError)
{
	FMXTrackDefinition Copy = Def;
	Copy.Name = UMXCourseLibrary::SanitizeName(Name);
	if (!GM->GI()->GetCourses()->SaveUserTrack(Copy, OutError))
	{
		return false;
	}
	Def.Name = Copy.Name;
	Def.Id = FString::Printf(TEXT("user:%s"), *Copy.Name);
	bDirty = false;
	return true;
}

bool UMXDesigner::Load(const FString& IdOrName, FString& OutError)
{
	FMXTrackDefinition Loaded;
	UMXCourseLibrary* Lib = GM->GI()->GetCourses();
	const FString Id = IdOrName.StartsWith(TEXT("user:")) || Lib->FindBuiltIn(IdOrName) ? IdOrName : FString::Printf(TEXT("user:%s"), *IdOrName);
	if (!Lib->GetCourse(Id, Loaded))
	{
		OutError = FString::Printf(TEXT("Could not load %s"), *IdOrName);
		return false;
	}
	if (Loaded.bBuiltIn)
	{
		// Built-in courses open as a copy (main-race layout).
		Loaded.bBuiltIn = false;
		Loaded.Name = Loaded.Name + TEXT(" copy");
		Loaded.Id = FString::Printf(TEXT("user:%s"), *Loaded.Name);
		for (FMXSegment& S : Loaded.Segments)
		{
			S.bMainOnly = false;
		}
	}
	Def = Loaded;
	UndoStack.Reset();
	RedoStack.Reset();
	Selected = INDEX_NONE;
	Cursor = 30.f;
	bDirty = false;
	MarkDirty();
	RunValidation(false);
	return true;
}

void UMXDesigner::RunValidation(bool bDriveTest)
{
	Validation = FMXTrackValidator::Validate(Def, MXTuning::Style());
	if (bDriveTest && !Validation.HasErrors())
	{
		FMXTrackValidator::DriveTest(Def, Validation);
	}
}

bool UMXDesigner::AutoFix()
{
	PushUndo();
	bool bChanged = FMXTrackValidator::EnsureFinishDeck(Def, MXTuning::Style());
	// Pull pieces out of the start zone and inside the lap.
	for (FMXSegment& S : Def.Segments)
	{
		if (S.Type == EMXObstacleType::FinishDeck)
		{
			continue;
		}
		const float Len = S.LengthM > 0.f ? S.LengthM : FMXObstacleLibrary::ComputeLength(S.Type, S.Runs, 0.f, MXTuning::Style());
		const float Clamped = FMath::Clamp(S.StartM, FMXTrackValidator::StartZone, FMath::Max(FMXTrackValidator::StartZone, Def.LapLengthM - Len - 20.f));
		if (!FMath::IsNearlyEqual(Clamped, S.StartM))
		{
			S.StartM = Clamped;
			bChanged = true;
		}
	}
	Def.SortSegments();
	// Resolve overlaps by pushing later pieces forward.
	for (int32 i = 1; i < Def.Segments.Num(); ++i)
	{
		FMXSegment& Prev = Def.Segments[i - 1];
		FMXSegment& Cur = Def.Segments[i];
		const bool bSurfPrev = Prev.Type == EMXObstacleType::Mud || Prev.Type == EMXObstacleType::CoolStrip || Prev.Type == EMXObstacleType::Grass || Prev.Type == EMXObstacleType::Barrier;
		const bool bSurfCur = Cur.Type == EMXObstacleType::Mud || Cur.Type == EMXObstacleType::CoolStrip || Cur.Type == EMXObstacleType::Grass || Cur.Type == EMXObstacleType::Barrier;
		const float PrevEnd = Prev.StartM + Prev.LengthM;
		if (Cur.StartM < PrevEnd && !(bSurfPrev && bSurfCur && (Prev.LaneMask & Cur.LaneMask) == 0) && Cur.Type != EMXObstacleType::FinishDeck)
		{
			Cur.StartM = PrevEnd + 1.f;
			bChanged = true;
		}
	}
	bChanged |= FMXTrackValidator::EnsureFinishDeck(Def, MXTuning::Style());
	if (!bChanged)
	{
		UndoStack.Pop();
	}
	MarkDirty();
	RunValidation(false);
	return bChanged;
}

int32 UMXDesigner::PieceAtCursor() const
{
	for (int32 i = 0; i < Def.Segments.Num(); ++i)
	{
		const FMXSegment& S = Def.Segments[i];
		if (Cursor >= S.StartM - 0.5f && Cursor <= S.StartM + FMath::Max(1.f, S.LengthM) + 0.5f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UMXDesigner::RebuildPreview()
{
	if (!bPreviewDirty || !GM->GetTrackActor())
	{
		return;
	}
	bPreviewDirty = false;
	Preview.Build(Def, EMXLayoutVariant::Main, 1, MXTuning::Style());
	GM->GetTrackActor()->Build(Preview, false);
}

void UMXDesigner::UpdateCamera()
{
	if (AMXPlayerController* PC = GM->GetPCForSlot(0))
	{
		PC->CamMode = EMXCamMode::Designer;
		PC->FocusBike = nullptr;
		PC->DesignerS = Cursor;
		PC->DesignerZoom = Zoom;
	}
	if (AMXTrack* T = GM->GetTrackActor())
	{
		if (Def.Segments.IsValidIndex(Selected))
		{
			const FMXSegment& S = Def.Segments[Selected];
			T->SetHighlight(S.StartM, S.StartM + FMath::Max(1.f, S.LengthM), S.LaneMask);
		}
		else
		{
			const EMXObstacleType Type = FMXObstacleLibrary::DesignerPalette()[PaletteIndex];
			const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(Type);
			const int32 Mask = Info.LaneOptions.IsValidIndex(LaneOption) ? Info.LaneOptions[LaneOption] : Info.DefaultLaneMask;
			const float Len = FMXObstacleLibrary::ComputeLength(Type, Info.DefaultRuns, 0.f, MXTuning::Style());
			T->SetHighlight(Cursor, Cursor + Len, Mask);
		}
	}
}

void UMXDesigner::Tick(float DeltaSeconds)
{
	Time += DeltaSeconds;
	ToastTime = FMath::Max(0.f, ToastTime - DeltaSeconds);
	if (!bOpen)
	{
		return;
	}
	if (bPreviewDirty)
	{
		RebuildDelay -= DeltaSeconds;
		if (RebuildDelay <= 0.f)
		{
			RebuildPreview();
		}
	}
	UpdateCamera();
}

void UMXDesigner::HandleAnyKey(int32 DeviceKey, const FKey& Key)
{
	if (Mode == EMXDesignerMode::NameEntry && DeviceKey == MXDevice::Keyboard)
	{
		if (Key == EKeys::BackSpace && NameBuffer.Len() > 0)
		{
			NameBuffer.LeftChopInline(1);
		}
		else if (Key == EKeys::Enter)
		{
			FString Err;
			if (Save(NameBuffer, Err))
			{
				Toast(FString::Printf(TEXT("Saved \"%s\""), *Def.Name));
				Mode = EMXDesignerMode::Edit;
			}
			else
			{
				Toast(Err);
			}
		}
	}
}

void UMXDesigner::HandleTextChar(TCHAR Char)
{
	if (Mode != EMXDesignerMode::NameEntry)
	{
		return;
	}
	if ((FChar::IsAlnum(Char) || Char == TEXT(' ') || Char == TEXT('-') || Char == TEXT('_')) && NameBuffer.Len() < 24)
	{
		NameBuffer.AppendChar(Char);
	}
}

void UMXDesigner::MenuConfirm()
{
	UMXGameInstance* G = GM->GI();
	switch (MenuIndex)
	{
	case MI_Resume:
		Mode = EMXDesignerMode::Edit;
		break;
	case MI_Test:
		RunValidation(false);
		if (Validation.HasErrors())
		{
			Toast(TEXT("Fix the errors first (Validate & Auto-fix)"));
			break;
		}
		Mode = EMXDesignerMode::Edit;
		bOpen = false;
		GM->StartDesignerTest(Def);
		break;
	case MI_Save:
		if (Def.Name.IsEmpty() || Def.Name == TEXT("Untitled"))
		{
			NameBuffer = Def.Name == TEXT("Untitled") ? FString() : Def.Name;
			Mode = EMXDesignerMode::NameEntry;
		}
		else
		{
			FString Err;
			Toast(Save(Def.Name, Err) ? FString::Printf(TEXT("Saved \"%s\""), *Def.Name) : Err);
			Mode = EMXDesignerMode::Edit;
		}
		break;
	case MI_SaveAs:
		NameBuffer = Def.Name == TEXT("Untitled") ? FString() : Def.Name;
		KeyGridIndex = 0;
		Mode = EMXDesignerMode::NameEntry;
		break;
	case MI_Load:
		LoadEntries.Reset();
		for (const FMXCourseEntry& E : G->GetCourses()->GetEntries())
		{
			LoadEntries.Add(E.Id);
		}
		LoadIndex = 0;
		Mode = EMXDesignerMode::LoadList;
		break;
	case MI_New:
		Mode = bDirty ? EMXDesignerMode::ConfirmNew : EMXDesignerMode::Edit;
		if (!bDirty)
		{
			NewTrack();
		}
		break;
	case MI_Validate:
		AutoFix();
		RunValidation(true);
		Toast(Validation.Summary());
		break;
	case MI_Race:
	{
		RunValidation(false);
		if (Validation.HasErrors())
		{
			Toast(TEXT("Fix the errors first"));
			break;
		}
		FString Err;
		if (!Save(Def.Name == TEXT("Untitled") ? TEXT("My Track") : Def.Name, Err))
		{
			Toast(Err);
			break;
		}
		G->PendingCourseId = Def.Id;
		Close();
		GM->ReturnToLobby();
		break;
	}
	case MI_Exit:
		GM->ExitDesigner();
		break;
	default:
		break;
	}
}

void UMXDesigner::HandleAction(int32 DeviceKey, EMXMenuAction Action)
{
	if (!bOpen)
	{
		return;
	}
	const TArray<EMXObstacleType>& Palette = FMXObstacleLibrary::DesignerPalette();
	switch (Mode)
	{
	case EMXDesignerMode::Edit:
	{
		if (Def.Segments.IsValidIndex(Selected))
		{
			FMXSegment& S = Def.Segments[Selected];
			switch (Action)
			{
			case EMXMenuAction::Left:
			case EMXMenuAction::Right:
				if (!bSelectionUndoPushed)
				{
					PushUndo();
					bSelectionUndoPushed = true;
				}
				MovePiece(Selected, S.StartM + (Action == EMXMenuAction::Right ? 1.f : -1.f));
				Cursor = Def.Segments[Selected].StartM;
				break;
			case EMXMenuAction::Up:
			case EMXMenuAction::Down:
			{
				const int32 D = Action == EMXMenuAction::Up ? 1 : -1;
				PushUndo();
				if (S.Type == EMXObstacleType::Grass)
				{
					S.LengthM = FMath::Clamp(S.LengthM + D * 2.5f, 5.f, 120.f);
				}
				else if (S.Type == EMXObstacleType::Mountain || S.Type == EMXObstacleType::PlatformDeck)
				{
					if (S.Runs.Num() == 0)
					{
						S.Runs = FMXObstacleLibrary::Info(S.Type).DefaultRuns;
					}
					S.Runs[0] = FMath::Clamp(S.Runs[0] + D, 1, 20);
					S.LengthM = FMXObstacleLibrary::ComputeLength(S.Type, S.Runs, 0.f, MXTuning::Style());
				}
				MarkDirty();
				RunValidation(false);
				break;
			}
			case EMXMenuAction::Aux:
			{
				const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(S.Type);
				if (Info.LaneOptions.Num() > 1)
				{
					PushUndo();
					int32 Idx = Info.LaneOptions.IndexOfByKey(S.LaneMask);
					Idx = (Idx + 1) % Info.LaneOptions.Num();
					SetLaneMask(Selected, Info.LaneOptions[Idx]);
					RunValidation(false);
				}
				break;
			}
			case EMXMenuAction::Aux2:
				if (DeletePiece(Selected))
				{
					Beep((uint8)EMXSfx::Leave);
					Toast(TEXT("Deleted"));
				}
				break;
			case EMXMenuAction::Confirm:
			case EMXMenuAction::Back:
				Def.SortSegments();
				Selected = INDEX_NONE;
				bSelectionUndoPushed = false;
				RunValidation(false);
				break;
			case EMXMenuAction::Start:
				Selected = INDEX_NONE;
				Mode = EMXDesignerMode::Menu;
				MenuIndex = 0;
				break;
			default:
				break;
			}
			break;
		}
		switch (Action)
		{
		case EMXMenuAction::Left: Cursor = FMath::Max(0.f, Cursor - 1.f); break;
		case EMXMenuAction::Right: Cursor = FMath::Min(Def.LapLengthM, Cursor + 1.f); break;
		case EMXMenuAction::Up: PaletteIndex = (PaletteIndex + Palette.Num() - 1) % Palette.Num(); LaneOption = 0; Beep((uint8)EMXSfx::MenuMove); break;
		case EMXMenuAction::Down: PaletteIndex = (PaletteIndex + 1) % Palette.Num(); LaneOption = 0; Beep((uint8)EMXSfx::MenuMove); break;
		case EMXMenuAction::Aux:
		{
			const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(Palette[PaletteIndex]);
			LaneOption = Info.LaneOptions.Num() > 0 ? (LaneOption + 1) % Info.LaneOptions.Num() : 0;
			break;
		}
		case EMXMenuAction::Confirm:
		{
			const int32 Hit = PieceAtCursor();
			if (Hit != INDEX_NONE)
			{
				Selected = Hit;
				bSelectionUndoPushed = false;
				Beep((uint8)EMXSfx::MenuSelect);
			}
			else
			{
				const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(Palette[PaletteIndex]);
				const int32 Mask = Info.LaneOptions.IsValidIndex(LaneOption) ? Info.LaneOptions[LaneOption] : Info.DefaultLaneMask;
				Selected = PlacePiece(Palette[PaletteIndex], Cursor, Mask);
				bSelectionUndoPushed = true;
				Beep((uint8)EMXSfx::Join);
			}
			break;
		}
		case EMXMenuAction::PageLeft:
			if (Undo()) { Toast(TEXT("Undo")); }
			break;
		case EMXMenuAction::PageRight:
			if (Redo()) { Toast(TEXT("Redo")); }
			break;
		case EMXMenuAction::Aux2:
			Zoom = Zoom > 1.5f ? 0.6f : (Zoom > 0.9f ? 2.f : 1.f);
			break;
		case EMXMenuAction::Back:
		case EMXMenuAction::Start:
			Mode = EMXDesignerMode::Menu;
			MenuIndex = 0;
			break;
		default:
			break;
		}
		break;
	}
	case EMXDesignerMode::Menu:
		if (Action == EMXMenuAction::Up) { MenuIndex = (MenuIndex + MI_Count - 1) % MI_Count; Beep((uint8)EMXSfx::MenuMove); }
		else if (Action == EMXMenuAction::Down) { MenuIndex = (MenuIndex + 1) % MI_Count; Beep((uint8)EMXSfx::MenuMove); }
		else if ((Action == EMXMenuAction::Left || Action == EMXMenuAction::Right) && MenuIndex == MI_Laps)
		{
			SetLaps(Def.Laps + (Action == EMXMenuAction::Right ? 1 : -1));
		}
		else if ((Action == EMXMenuAction::Left || Action == EMXMenuAction::Right) && MenuIndex == MI_Length)
		{
			SetLapLength(Def.LapLengthM + (Action == EMXMenuAction::Right ? 25.f : -25.f));
		}
		else if (Action == EMXMenuAction::Confirm) { MenuConfirm(); Beep((uint8)EMXSfx::MenuSelect); }
		else if (Action == EMXMenuAction::Back || Action == EMXMenuAction::Start) { Mode = EMXDesignerMode::Edit; }
		break;
	case EMXDesignerMode::NameEntry:
	{
		const int32 N = FCString::Strlen(KeyGrid);
		const int32 Total = N + 2; // + DEL + OK
		if (Action == EMXMenuAction::Left) { KeyGridIndex = (KeyGridIndex + Total - 1) % Total; }
		else if (Action == EMXMenuAction::Right) { KeyGridIndex = (KeyGridIndex + 1) % Total; }
		else if (Action == EMXMenuAction::Up) { KeyGridIndex = (KeyGridIndex + Total - KeyGridCols) % Total; }
		else if (Action == EMXMenuAction::Down) { KeyGridIndex = (KeyGridIndex + KeyGridCols) % Total; }
		else if (Action == EMXMenuAction::Confirm && DeviceKey != MXDevice::Keyboard)
		{
			if (KeyGridIndex < N)
			{
				if (NameBuffer.Len() < 24) { NameBuffer.AppendChar(KeyGrid[KeyGridIndex]); }
			}
			else if (KeyGridIndex == N)
			{
				NameBuffer.LeftChopInline(FMath::Min(1, NameBuffer.Len()));
			}
			else
			{
				FString Err;
				if (Save(NameBuffer.IsEmpty() ? TEXT("My Track") : NameBuffer, Err))
				{
					Toast(FString::Printf(TEXT("Saved \"%s\""), *Def.Name));
					Mode = EMXDesignerMode::Edit;
				}
				else
				{
					Toast(Err);
				}
			}
		}
		else if (Action == EMXMenuAction::Back && DeviceKey != MXDevice::Keyboard)
		{
			Mode = EMXDesignerMode::Menu;
		}
		else if (Action == EMXMenuAction::Start)
		{
			FString Err;
			if (Save(NameBuffer.IsEmpty() ? TEXT("My Track") : NameBuffer, Err))
			{
				Toast(FString::Printf(TEXT("Saved \"%s\""), *Def.Name));
				Mode = EMXDesignerMode::Edit;
			}
		}
		break;
	}
	case EMXDesignerMode::LoadList:
		if (Action == EMXMenuAction::Up) { LoadIndex = (LoadIndex + LoadEntries.Num() - 1) % FMath::Max(1, LoadEntries.Num()); }
		else if (Action == EMXMenuAction::Down) { LoadIndex = (LoadIndex + 1) % FMath::Max(1, LoadEntries.Num()); }
		else if (Action == EMXMenuAction::Confirm && LoadEntries.IsValidIndex(LoadIndex))
		{
			FString Err;
			Toast(Load(LoadEntries[LoadIndex], Err) ? FString::Printf(TEXT("Loaded %s"), *Def.Name) : Err);
			Mode = EMXDesignerMode::Edit;
		}
		else if (Action == EMXMenuAction::Aux2 && LoadEntries.IsValidIndex(LoadIndex) && LoadEntries[LoadIndex].StartsWith(TEXT("user:")))
		{
			GM->GI()->GetCourses()->DeleteUserTrack(LoadEntries[LoadIndex].RightChop(5));
			Toast(TEXT("Deleted saved track"));
			Mode = EMXDesignerMode::Menu;
		}
		else if (Action == EMXMenuAction::Back) { Mode = EMXDesignerMode::Menu; }
		break;
	case EMXDesignerMode::ConfirmNew:
		if (Action == EMXMenuAction::Confirm) { NewTrack(); Mode = EMXDesignerMode::Edit; Toast(TEXT("New track")); }
		else if (Action == EMXMenuAction::Back) { Mode = EMXDesignerMode::Menu; }
		break;
	}
}

void UMXDesigner::Draw(UCanvas* C)
{
	if (!bOpen || !C)
	{
		return;
	}
	const float W = C->ClipX;
	const float H = C->ClipY;
	const float U = FMath::Max(0.6f, H / 1080.f);
	const TArray<EMXObstacleType>& Palette = FMXObstacleLibrary::DesignerPalette();

	// ---- Top bar ----
	MXDraw::Rect(C, 0.f, 0.f, W, 86.f * U, FLinearColor(0.f, 0.f, 0.f, 0.7f));
	MXDraw::TextFit(C, FString::Printf(TEXT("TRACK DESIGNER  -  %s%s"), *Def.Name, bDirty ? TEXT(" *") : TEXT("")), 30.f * U, 6.f * U, 30.f * U, W * 0.62f, FLinearColor::White);
	MXDraw::Text(C, FString::Printf(TEXT("Lap %.0f m   Laps %d   Pieces %d   Cursor %.0f m   Undo %d / Redo %d"), Def.LapLengthM, Def.Laps, Def.Segments.Num(), Cursor, UndoStack.Num(), RedoStack.Num()),
		30.f * U, 6.f * U + MXDraw::LineHeight(30.f * U) * 0.9f, 20.f * U, Dim, 0.f, 0.f, false);
	const FLinearColor VCol = Validation.HasErrors() ? FLinearColor(1.f, 0.35f, 0.25f) : (Validation.Count(EMXIssueSeverity::Warning) > 0 ? FLinearColor(1.f, 0.8f, 0.3f) : FLinearColor(0.4f, 1.f, 0.5f));
	MXDraw::Text(C, Validation.Summary(), W - 30.f * U, 30.f * U, 28.f * U, VCol, 1.f, 0.f);

	// ---- Lane map of the whole lap (top-down) ----
	const float MapX = 30.f * U;
	const float MapW = W - 60.f * U;
	const float MapY = 100.f * U;
	const float LaneH = 14.f * U;
	MXDraw::Rect(C, MapX, MapY, MapW, LaneH * 4.f, FLinearColor(0.35f, 0.2f, 0.1f, 0.85f));
	const float Scale = MapW / FMath::Max(1.f, Def.LapLengthM);
	for (int32 i = 0; i < Def.Segments.Num(); ++i)
	{
		const FMXSegment& S = Def.Segments[i];
		const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(S.Type);
		const float X0 = MapX + S.StartM * Scale;
		const float WX = FMath::Max(3.f, S.LengthM * Scale);
		for (int32 L = 0; L < MX::NumLanes; ++L)
		{
			if (MX::LaneInMask(S.LaneMask, L))
			{
				FLinearColor Col = Info.UiColor;
				Col.A = i == Selected ? 1.f : 0.85f;
				MXDraw::Rect(C, X0, MapY + L * LaneH + 1.f, WX, LaneH - 2.f, Col);
			}
		}
		if (i == Selected)
		{
			MXDraw::Frame(C, X0 - 2.f, MapY - 3.f, WX + 4.f, LaneH * 4.f + 6.f, 2.f, FLinearColor::White);
		}
	}
	MXDraw::Rect(C, MapX + Cursor * Scale - 1.5f, MapY - 8.f * U, 3.f, LaneH * 4.f + 16.f * U, FLinearColor(1.f, 1.f, 0.2f, 1.f));
	for (const FMXTrackIssue& Issue : Validation.Issues)
	{
		if (Issue.S >= 0.f)
		{
			MXDraw::Text(C, TEXT("!"), MapX + Issue.S * Scale, MapY + LaneH * 4.f + 2.f * U, 22.f * U,
				Issue.Severity == EMXIssueSeverity::Error ? FLinearColor(1.f, 0.3f, 0.2f) : FLinearColor(1.f, 0.8f, 0.3f), 0.5f, 0.f);
		}
	}

	// ---- Palette (bottom) ----
	const float PY = H - 150.f * U;
	MXDraw::Rect(C, 0.f, PY - 10.f * U, W, 160.f * U, FLinearColor(0.f, 0.f, 0.f, 0.7f));
	const float Cell = (W - 60.f * U) / Palette.Num();
	for (int32 i = 0; i < Palette.Num(); ++i)
	{
		const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(Palette[i]);
		const float X = 30.f * U + i * Cell;
		const bool bSel = i == PaletteIndex && !Def.Segments.IsValidIndex(Selected);
		MXDraw::Rect(C, X + 3.f, PY, Cell - 6.f, 60.f * U, bSel ? FLinearColor(Accent.R, Accent.G, Accent.B, 0.95f) : FLinearColor(0.15f, 0.15f, 0.18f, 0.9f));
		MXDraw::Text(C, Info.Letter, X + Cell * 0.5f, PY + 30.f * U, 26.f * U, bSel ? FLinearColor::Black : Info.UiColor, 0.5f, 0.5f, true, !bSel);
	}
	FString Line1, Line2;
	if (Def.Segments.IsValidIndex(Selected))
	{
		const FMXSegment& S = Def.Segments[Selected];
		const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(S.Type);
		Line1 = FString::Printf(TEXT("SELECTED: %s (%s)  at %.1f m  lanes %s  length %.1f m%s"), *Info.Name, *Info.Letter, S.StartM, *FMXObstacleLibrary::LaneMaskLabel(S.LaneMask), S.LengthM,
			S.SourceRef.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  [%s]"), *S.SourceRef));
		Line2 = TEXT("Left/Right move   Up/Down length/size   Y/Tab lanes   X/R delete   A/B done   Start menu");
	}
	else
	{
		const FMXObstacleInfo& Info = FMXObstacleLibrary::Info(Palette[PaletteIndex]);
		const int32 Mask = Info.LaneOptions.IsValidIndex(LaneOption) ? Info.LaneOptions[LaneOption] : Info.DefaultLaneMask;
		Line1 = FString::Printf(TEXT("PLACE: %s (%s)  lanes %s  -  %s"), *Info.Name, *Info.Letter, *FMXObstacleLibrary::LaneMaskLabel(Mask), *Info.Description);
		Line2 = TEXT("Left/Right cursor   Up/Down piece   Y/Tab lanes   A place/select   LB/RB (Q/E) undo/redo   X/R zoom   B/Start menu");
	}
	MXDraw::TextFit(C, Line1, 30.f * U, PY + 70.f * U, 22.f * U, W - 60.f * U, FLinearColor::White, 0.f, 0.f, false);
	MXDraw::TextFit(C, Line2, 30.f * U, PY + 70.f * U + MXDraw::LineHeight(22.f * U, false) * 0.95f, 19.f * U, W - 60.f * U, Dim, 0.f, 0.f, false);

	// ---- Validation list (right) ----
	float VY = 190.f * U;
	for (int32 i = 0; i < FMath::Min(6, Validation.Issues.Num()); ++i)
	{
		const FMXTrackIssue& Is = Validation.Issues[i];
		const FLinearColor Col = Is.Severity == EMXIssueSeverity::Error ? FLinearColor(1.f, 0.35f, 0.25f) : (Is.Severity == EMXIssueSeverity::Warning ? FLinearColor(1.f, 0.8f, 0.3f) : Dim);
		MXDraw::Text(C, Is.S >= 0.f ? FString::Printf(TEXT("%.0f m: %s"), Is.S, *Is.Message) : Is.Message, W - 30.f * U, VY, 20.f * U, Col, 1.f, 0.f, false);
		VY += 28.f * U;
	}

	// ---- Menus / dialogs ----
	if (Mode == EMXDesignerMode::Menu)
	{
		const TArray<FString> Items = {
			TEXT("RESUME EDITING"), TEXT("TEST RIDE"), TEXT("SAVE"), TEXT("SAVE AS..."), TEXT("LOAD..."), TEXT("NEW TRACK"),
			FString::Printf(TEXT("< LAPS: %d >"), Def.Laps), FString::Printf(TEXT("< LAP LENGTH: %.0f m >"), Def.LapLengthM),
			TEXT("VALIDATE & AUTO-FIX (drive test)"), TEXT("RACE THIS TRACK (split screen)"), TEXT("EXIT DESIGNER")
		};
		const float MX0 = W * 0.5f - 300.f * U;
		const float MY0 = H * 0.2f;
		MXDraw::Rect(C, MX0 - 30.f * U, MY0 - 30.f * U, 660.f * U, Items.Num() * 46.f * U + 60.f * U, FLinearColor(0.02f, 0.03f, 0.05f, 0.94f));
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			const bool bSel = i == MenuIndex;
			if (bSel)
			{
				MXDraw::Rect(C, MX0 - 12.f * U, MY0 + i * 46.f * U - 4.f * U, 624.f * U, 42.f * U, FLinearColor(Accent.R, Accent.G, Accent.B, 0.9f));
			}
			MXDraw::Text(C, Items[i], MX0, MY0 + i * 46.f * U, 30.f * U, bSel ? FLinearColor::Black : FLinearColor::White, 0.f, 0.f, true, !bSel);
		}
	}
	else if (Mode == EMXDesignerMode::NameEntry)
	{
		const float BX = W * 0.5f - 420.f * U;
		const float BY = H * 0.25f;
		MXDraw::Rect(C, BX, BY, 840.f * U, 460.f * U, FLinearColor(0.02f, 0.03f, 0.05f, 0.95f));
		MXDraw::Text(C, TEXT("TRACK NAME"), W * 0.5f, BY + 20.f * U, 32.f * U, FLinearColor::White, 0.5f, 0.f);
		MXDraw::Text(C, NameBuffer + (FMath::Fmod(Time, 1.f) < 0.5f ? TEXT("_") : TEXT(" ")), W * 0.5f, BY + 70.f * U, 40.f * U, Accent, 0.5f, 0.f);
		const int32 N = FCString::Strlen(KeyGrid);
		for (int32 i = 0; i < N + 2; ++i)
		{
			const int32 Row = i / KeyGridCols;
			const int32 Col = i % KeyGridCols;
			const float KX = BX + 40.f * U + Col * 76.f * U;
			const float KY = BY + 150.f * U + Row * 56.f * U;
			const bool bSel = i == KeyGridIndex;
			const FString Label = i < N ? (KeyGrid[i] == TEXT(' ') ? FString(TEXT("SPC")) : FString(1, &KeyGrid[i])) : (i == N ? TEXT("DEL") : TEXT("OK"));
			MXDraw::Rect(C, KX, KY, 66.f * U, 46.f * U, bSel ? FLinearColor(Accent.R, Accent.G, Accent.B, 0.95f) : FLinearColor(0.2f, 0.2f, 0.24f, 0.9f));
			MXDraw::Text(C, Label, KX + 33.f * U, KY + 23.f * U, 22.f * U, bSel ? FLinearColor::Black : FLinearColor::White, 0.5f, 0.5f);
		}
		MXDraw::Text(C, TEXT("Type on the keyboard (Enter saves) or use the grid (A type, Start saves, B cancel)"), W * 0.5f, BY + 420.f * U, 18.f * U, Dim, 0.5f, 0.f, false);
	}
	else if (Mode == EMXDesignerMode::LoadList)
	{
		const float BX = W * 0.5f - 360.f * U;
		const float BY = H * 0.18f;
		MXDraw::Rect(C, BX, BY, 720.f * U, FMath::Min(H * 0.7f, (LoadEntries.Num() + 2) * 44.f * U + 40.f * U), FLinearColor(0.02f, 0.03f, 0.05f, 0.95f));
		MXDraw::Text(C, TEXT("LOAD (built-in courses open as a copy)   X/R delete saved"), BX + 20.f * U, BY + 16.f * U, 22.f * U, Dim, 0.f, 0.f, false);
		for (int32 i = 0; i < LoadEntries.Num(); ++i)
		{
			const bool bSel = i == LoadIndex;
			const float Y = BY + 60.f * U + i * 44.f * U;
			if (bSel)
			{
				MXDraw::Rect(C, BX + 10.f * U, Y - 4.f * U, 700.f * U, 40.f * U, FLinearColor(Accent.R, Accent.G, Accent.B, 0.9f));
			}
			const FString Label = LoadEntries[i].StartsWith(TEXT("user:")) ? LoadEntries[i].RightChop(5) : LoadEntries[i];
			MXDraw::Text(C, Label, BX + 30.f * U, Y, 28.f * U, bSel ? FLinearColor::Black : FLinearColor::White, 0.f, 0.f, true, !bSel);
		}
	}
	else if (Mode == EMXDesignerMode::ConfirmNew)
	{
		MXDraw::Rect(C, W * 0.3f, H * 0.4f, W * 0.4f, H * 0.16f, FLinearColor(0.02f, 0.03f, 0.05f, 0.95f));
		MXDraw::Text(C, TEXT("Discard unsaved changes?  A yes   B no"), W * 0.5f, H * 0.48f, 30.f * U, FLinearColor::White, 0.5f, 0.5f);
	}
	if (ToastTime > 0.f)
	{
		MXDraw::Text(C, ToastText, W * 0.5f, H * 0.62f, 30.f * U, FLinearColor(1.f, 0.9f, 0.4f, FMath::Clamp(ToastTime, 0.f, 1.f)), 0.5f, 0.5f);
	}
}
