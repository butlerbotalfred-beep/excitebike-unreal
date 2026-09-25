#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/MXMenuTypes.h"
#include "MXAutoTest.generated.h"

class AMXGameMode;
class UMXGameViewportClient;
class UCanvas;

/**
 * In-game end-to-end tests driven through the REAL input path: synthetic key/axis events for
 * virtual devices are injected into the viewport client, routed to local players, processed by
 * Enhanced Input, and drive the bikes. (This is simulated input, not a physical-controller test.)
 *
 *   -mxautotest=loop      join 4 players, independence checks, race, crash+recover, finish,
 *                         rematch, controller disconnect/reconnect, return to lobby
 *   -mxautotest=perf      4 humans (autopilot) + 4 AI, 1080p, frame-time capture
 *   -mxautotest=designer  designer edit/undo/save/load/validate/test ride/split-screen race
 *   -mxautotest=shots     screenshot tour (1P/2P/3P/4P layouts, menus)
 * Results: Saved/AutoTest/<scenario>.json (+ screenshots). Add -mxquit to exit when done.
 */
UCLASS()
class HEATLINEMX_API UMXAutoTest : public UObject
{
	GENERATED_BODY()

public:
	void Start(AMXGameMode* InGM, const FString& InScenario);
	void Tick(float DeltaSeconds);
	void Draw(UCanvas* Canvas);
	void OnRaceOver();

private:
	struct FCheck
	{
		FString Name;
		bool bPass = false;
		FString Detail;
	};

	UMXGameViewportClient* VC() const;
	void Press(int32 Device, const FKey& Key);
	void Menu(int32 Device, EMXMenuAction Action);
	void Check(const FString& Name, bool bPass, const FString& Detail = FString());
	void Shot(const FString& Name);
	void Finish();
	void DriveHumans(float DeltaSeconds);
	void Next(int32 NewStep, float Wait = 0.f);
	void TickLoop(float DeltaSeconds);
	void TickPerf(float DeltaSeconds);
	void TickDesigner(float DeltaSeconds);
	void TickShots(float DeltaSeconds);
	void TickMenus(float DeltaSeconds);
	/** Records a 4-player split-screen clip, one screenshot per frame (run with -benchmark -fps=30). */
	void TickDemo(float DeltaSeconds);
	/** Moves the current list cursor to Target with Up/Down presses (the main menu remembers its position). */
	void MenuSelect(int32 Device, int32 Target);

	UPROPERTY() TObjectPtr<AMXGameMode> GM;
	FString Scenario;
	TArray<FCheck> Checks;
	TArray<FString> Screenshots;
	TArray<float> FrameTimes;
	TArray<float> GameMs, RenderMs, GpuMs;
	int32 Step = 0;
	float StepTime = 0.f;
	float Wait = 0.f;
	float TotalTime = 0.f;
	bool bDone = false;
	bool bRaceOver = false;
	int32 RaceOverCount = 0;
	// Virtual devices (not real hardware ids).
	static constexpr int32 PadBase = 1001;
	// Autopilot key state per slot to generate press/release edges.
	bool bTurboDown[4] = {false, false, false, false};
	int32 SteerDir[4] = {0, 0, 0, 0};
	float MashTimer[4] = {0.f, 0.f, 0.f, 0.f};
	bool bAutopilot = false;
	int32 SabotageSlot = -1;
	float SabotageAfter = 1000.f;
	bool bSabotaged = false;
	bool bSawCrash = false;
	bool bSawRecover = false;
	float CrashS = 0.f;
	float CrashValidS = 0.f;
	float RecoverS = 0.f;
	// Independence probes.
	float IndepHeat[4] = {0, 0, 0, 0};
	int32 IndepLaneStart[4] = {0, 0, 0, 0};
	float IndepMaxPitch[4] = {0, 0, 0, 0};
	float IndepSpeed[4] = {0, 0, 0, 0};
	FString DesignerTrackName;
};
