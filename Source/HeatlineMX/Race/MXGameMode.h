#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Race/MXRaceTypes.h"
#include "Track/MXTrackTypes.h"
#include "UI/MXMenuTypes.h"
#include "MXGameMode.generated.h"

class AMXRaceManager;
class AMXTrack;
class AMXEnvironment;
class AMXPlayerController;
class UMXFrontend;
class UMXDesigner;
class UMXGameInstance;
class UMXAutoTest;
class UCanvas;

/** Result row prepared for the results screen. */
USTRUCT()
struct FMXResultRow
{
	GENERATED_BODY()

	UPROPERTY() int32 Racer = 0;
	UPROPERTY() int32 Position = 0;
	UPROPERTY() FString Name;
	UPROPERTY() int32 ColorIndex = 0;
	UPROPERTY() float Time = -1.f;
	UPROPERTY() float BestLap = -1.f;
	UPROPERTY() bool bHuman = false;
	UPROPERTY() bool bEstimated = false;
	UPROPERTY() int32 Medal = 0;
	UPROPERTY() bool bNewRecord = false;
	UPROPERTY() int32 Points = 0;
	UPROPERTY() int32 Crashes = 0;
	UPROPERTY() int32 Perfects = 0;
};

/**
 * Orchestrates the whole app in one persistent map: attract race + menus, races (1-4 local
 * players via native local players and split screen), results/rematch, championship and the
 * Track Designer.
 */
UCLASS()
class HEATLINEMX_API AMXGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMXGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override {}

	// ---- accessors ----
	AMXRaceManager* GetRace() const { return Race; }
	AMXTrack* GetTrackActor() const { return TrackActor; }
	UMXFrontend* GetFrontend() const { return Frontend; }
	UMXDesigner* GetDesigner() const { return Designer; }
	UMXGameInstance* GI() const;
	EMXAppState GetState() const { return State; }
	bool IsRaceHUDVisible() const;
	bool SlotUsesKeyboard(int32 Slot) const;
	bool IsPaused() const { return bPaused; }
	const TArray<FMXResultRow>& GetResults() const { return Results; }

	// ---- flow ----
	void StartAttract();
	bool StartRace(const FMXRaceConfig& Config);
	void Rematch();
	void NextChampionshipRace();
	void ReturnToLobby();
	void ReturnToMainMenu();
	void RequestPause(int32 Slot);
	void SetPaused(bool bInPaused, int32 BySlot = -1);
	void EnterDesigner();
	void ExitDesigner();
	bool StartDesignerTest(const FMXTrackDefinition& Def);
	void EndDesignerTest();
	/** Builds a race config for the current lobby + mode + course selection. */
	FMXRaceConfig BuildConfig(EMXRaceMode Mode, const FString& CourseId, EMXLayoutVariant Variant, int32 Laps) const;
	void BeginChampionship(const FMXRaceConfig& Template);

	// ---- local players ----
	void SetupLocalPlayers(const TArray<int32>& Slots);
	void RemoveExtraLocalPlayers();
	AMXPlayerController* GetPCForSlot(int32 Slot) const;
	AMXPlayerController* GetPCForDevice(int32 DeviceKey) const;
	int32 NumLocalPlayers() const;

	// ---- input from the viewport client ----
	/** True when gameplay input should go to the players' controllers. */
	bool WantsRaceInput() const;
	void HandleMenuAction(int32 DeviceKey, EMXMenuAction Action);
	void HandleAnyKey(int32 DeviceKey, const FKey& Key);
	void HandleTextChar(int32 DeviceKey, TCHAR Char);
	void HandleDeviceConnection(int32 DeviceKey, bool bConnected);
	int32 LostDeviceSlot() const;

	/** Full-screen overlay (menus, results, designer UI, 3P standings). */
	void DrawOverlay(UCanvas* Canvas);

	UPROPERTY() TObjectPtr<UMXAutoTest> AutoTest;

	/** Presentation classes (Blueprint subclasses set these; C++ classes are the fallback). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Classes") TSubclassOf<class AMXBike> BikeClass;

private:
	void OnRaceOver();
	void ComputeResults();
	void PossessBikes();

	UPROPERTY() TObjectPtr<AMXRaceManager> Race;
	UPROPERTY() TObjectPtr<AMXTrack> TrackActor;
	UPROPERTY() TObjectPtr<AMXEnvironment> Environment;
	UPROPERTY() TObjectPtr<UMXFrontend> Frontend;
	UPROPERTY() TObjectPtr<UMXDesigner> Designer;
	UPROPERTY() TArray<FMXResultRow> Results;

	EMXAppState State = EMXAppState::Frontend;
	bool bPaused = false;
	int32 PausedBy = -1;
	bool bAttract = false;
	FMXTrackDefinition DesignerTestDef;
	TArray<int32> LocalIndexToSlot;
	float AttractLeaderTimer = 0.f;
};
