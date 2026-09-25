#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/MXTypes.h"
#include "UI/MXMenuTypes.h"
#include "Session/MXInputConfig.h"
#include "MXFrontend.generated.h"

class AMXGameMode;
class UCanvas;
class UMXEngineAudioComponent;

enum class EMXScreen : uint8
{
	None,
	Title,
	MainMenu,
	Lobby,
	Controls,
	CourseSelect,
	Options,
	Results,
	Pause,
	ChampTable
};

/** Canvas-drawn menus driven by device-tagged menu actions from the viewport client. */
UCLASS()
class HEATLINEMX_API UMXFrontend : public UObject
{
	GENERATED_BODY()

public:
	void Init(AMXGameMode* InGM);
	void Tick(float DeltaSeconds);
	void Draw(UCanvas* Canvas);
	void HandleAction(int32 DeviceKey, EMXMenuAction Action);
	void HandleAnyKey(int32 DeviceKey, const FKey& Key);

	void OnRaceStarted();
	void OnResults();
	void OnPauseChanged(bool bPaused, int32 BySlot);
	void OnDeviceChanged(int32 Slot, bool bConnected);
	void GoToLobby();
	void GoToMainMenu();

	EMXScreen GetScreen() const { return Screen; }
	/** Cursor of the current list screen (main menu, options...), for the autotest. */
	int32 GetMenuIndex() const { return MenuIndex; }
	EMXRaceMode GetMode() const { return Mode; }
	/** Autotest helpers. */
	int32 GetCourseIndex() const { return CourseIndex; }
	void SetCourseIndexById(const FString& Id);
	FString SelectedCourseId() const;

private:
	void SetScreen(EMXScreen S);
	void Beep(uint8 Sfx);
	void StartSelectedRace();
	void DrawTitle(UCanvas* C, float U);
	void DrawMainMenu(UCanvas* C, float U);
	void DrawLobby(UCanvas* C, float U);
	void DrawControls(UCanvas* C, float U);
	void DrawCourseSelect(UCanvas* C, float U);
	void DrawOptions(UCanvas* C, float U);
	void DrawResults(UCanvas* C, float U);
	void DrawPause(UCanvas* C, float U);
	void DrawChampTable(UCanvas* C, float U);
	void DrawList(UCanvas* C, const TArray<FString>& Items, int32 Selected, float X, float Y, float Size, float U);
	void LobbyAction(int32 DeviceKey, EMXMenuAction Action);
	void ControlsAction(EMXMenuAction Action);
	bool LobbyReady() const;
	int32 MaxPlayersForMode() const;

	UPROPERTY() TObjectPtr<AMXGameMode> GM;
	UPROPERTY() TObjectPtr<UMXEngineAudioComponent> UIAudio;

	EMXScreen Screen = EMXScreen::Title;
	EMXScreen PrevScreen = EMXScreen::None;
	EMXRaceMode Mode = EMXRaceMode::RaceAI;
	int32 MenuIndex = 0;
	int32 MainMenuIndex = 0;
	int32 CourseIndex = 0;
	EMXLayoutVariant Variant = EMXLayoutVariant::Main;
	int32 LapsOverride = 0;
	int32 ResultsIndex = 0;
	int32 PauseIndex = 0;
	int32 OptionsIndex = 0;
	float ScreenTime = 0.f;
	float LobbyCountdown = -1.f;
	// Controls screen
	int32 ControlsSlot = 0;
	int32 ControlsIndex = 0;
	bool bCapturing = false;
	FMXControlProfile EditProfile;
	FString Toast;
	float ToastTime = 0.f;
	int32 PausedBySlot = -1;
	bool bRaceActive = false;
};
