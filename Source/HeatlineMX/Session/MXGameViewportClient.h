#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Engine/GameViewportClient.h"
#include "UI/MXMenuTypes.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "MXGameViewportClient.generated.h"

class AMXGameMode;

/**
 * Routes every input device to the local player that joined with it (keyboard + up to 4 gamepads),
 * turns keys/sticks into menu actions outside races, draws the full-screen overlay, and provides the
 * 3-player quadrant layout (standings in the fourth quadrant) and the 2P orientation option.
 * The autotest injects synthetic events through the same routing path (InjectKey/InjectAxis).
 */
UCLASS()
class HEATLINEMX_API UMXGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	UMXGameViewportClient(const FObjectInitializer& ObjectInitializer);

	virtual void Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
	virtual void BeginDestroy() override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
#if UE_VERSION_OLDER_THAN(5, 6, 0)
	virtual bool InputAxis(FViewport* InViewport, FInputDeviceId InputDevice, FKey Key, float Delta, float DeltaTime, int32 NumSamples = 1, bool bGamepad = false) override;
#else
	// UE 5.6 moved analog input to FInputKeyEventArgs (the value is in AmountDepressed).
	virtual bool InputAxis(const FInputKeyEventArgs& Args) override;
#endif
	virtual bool InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character) override;
	virtual void PostRender(UCanvas* Canvas) override;
	virtual void UpdateActiveSplitscreenType() override;
	virtual void Tick(float DeltaTime) override;

	// ---- synthetic input (autotest) through the real routing path ----
	void InjectKey(int32 DeviceKey, const FKey& Key, EInputEvent Event, float Amount = 1.f);
	void InjectAxis(int32 DeviceKey, const FKey& Key, float Value);
	void InjectConnection(int32 DeviceKey, bool bConnected);

	int32 RoutedToPlayers = 0;
	int32 RoutedToMenus = 0;
	TSet<int32> SeenDevices;

private:
	AMXGameMode* GameMode() const;
	bool RouteKey(int32 DeviceKey, const FKey& Key, EInputEvent Event, float Amount, bool bGamepad, FInputDeviceId DeviceId);
	bool RouteAxis(int32 DeviceKey, const FKey& Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad, FInputDeviceId DeviceId);
	static bool MapMenuKey(const FKey& Key, EMXMenuAction& Out);
	void OnDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device);

	struct FStickState
	{
		FVector2D Value = FVector2D::ZeroVector;
		int32 DirX = 0;
		int32 DirY = 0;
		float Repeat = 0.f;
	};
	TMap<int32, FStickState> Sticks;
	FDelegateHandle ConnectionHandle;
};
