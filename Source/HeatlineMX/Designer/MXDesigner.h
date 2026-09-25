#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Track/MXTrackTypes.h"
#include "Track/MXTrackModel.h"
#include "Track/MXTrackValidator.h"
#include "UI/MXMenuTypes.h"
#include "MXDesigner.generated.h"

class AMXGameMode;
class UCanvas;
class UMXEngineAudioComponent;

enum class EMXDesignerMode : uint8
{
	Edit,
	Menu,
	NameEntry,
	LoadList,
	ConfirmNew
};

/**
 * In-game Track Designer (inspired by the NES Design mode): place/move/delete pieces on the lap,
 * choose lane coverage, cool strips and rough sections, lap count and length, undo/redo,
 * named local saves, validation (incl. a physics drive test), instant test ride, and racing a
 * saved track in split screen. Uses the same FMXTrackDefinition as the built-in courses.
 */
UCLASS()
class HEATLINEMX_API UMXDesigner : public UObject
{
	GENERATED_BODY()

public:
	void Init(AMXGameMode* InGM);
	void Open();
	void Close();
	/** Back from a test ride: restores the working track and camera. */
	void Resume();
	void Tick(float DeltaSeconds);
	void Draw(UCanvas* Canvas);
	void HandleAction(int32 DeviceKey, EMXMenuAction Action);
	void HandleAnyKey(int32 DeviceKey, const FKey& Key);
	void HandleTextChar(TCHAR Char);

	// ---- editing API (used by the UI and by the automated designer test) ----
	void NewTrack();
	int32 PlacePiece(EMXObstacleType Type, float StartM, int32 LaneMask = -1);
	bool MovePiece(int32 Index, float NewStart);
	bool DeletePiece(int32 Index);
	bool SetLaneMask(int32 Index, int32 Mask);
	void SetLaps(int32 Laps);
	void SetLapLength(float Meters);
	bool Undo();
	bool Redo();
	bool Save(const FString& Name, FString& OutError);
	bool Load(const FString& IdOrName, FString& OutError);
	void RunValidation(bool bDriveTest);
	bool AutoFix();
	int32 PieceAtCursor() const;

	const FMXTrackDefinition& GetDefinition() const { return Def; }
	const FMXValidationResult& GetValidation() const { return Validation; }
	int32 UndoDepth() const { return UndoStack.Num(); }
	int32 RedoDepth() const { return RedoStack.Num(); }
	float Cursor = 30.f;

private:
	void PushUndo();
	void MarkDirty();
	void RebuildPreview();
	void UpdateCamera();
	void Toast(const FString& Msg);
	void MenuConfirm();
	void Beep(uint8 Sfx);

	UPROPERTY() TObjectPtr<AMXGameMode> GM;
	UPROPERTY() TObjectPtr<UMXEngineAudioComponent> Audio;
	FMXTrackDefinition Def;
	FMXTrackModel Preview;
	TArray<FMXTrackDefinition> UndoStack;
	TArray<FMXTrackDefinition> RedoStack;
	FMXValidationResult Validation;
	EMXDesignerMode Mode = EMXDesignerMode::Edit;
	int32 PaletteIndex = 0;
	int32 LaneOption = 0;
	int32 Selected = INDEX_NONE;
	bool bSelectionUndoPushed = false;
	bool bDirty = false;
	bool bPreviewDirty = true;
	float RebuildDelay = 0.f;
	int32 MenuIndex = 0;
	int32 LoadIndex = 0;
	TArray<FString> LoadEntries;
	FString NameBuffer;
	int32 KeyGridIndex = 0;
	FString ToastText;
	float ToastTime = 0.f;
	float Zoom = 1.f;
	int32 OwnerDevice = MXDevice::None;
	bool bOpen = false;
	float Time = 0.f;
};
