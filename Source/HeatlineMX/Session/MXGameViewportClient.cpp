#include "Session/MXGameViewportClient.h"
#include "Session/MXGameInstance.h"
#include "Race/MXGameMode.h"
#include "Race/MXPlayerController.h"
#include "HeatlineMX.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"

namespace
{
	/** Sends one key event to a player's controller (UE 5.6 moved this to FInputKeyEventArgs). */
	void SendKey(APlayerController* PC, FViewport* InViewport, FInputDeviceId Device, const FKey& Key, EInputEvent Event, float Amount, bool bGamepad)
	{
#if UE_VERSION_OLDER_THAN(5, 6, 0)
		PC->InputKey(FInputKeyParams(Key, Event, (double)Amount, bGamepad, Device));
#else
		PC->InputKey(FInputKeyEventArgs(InViewport, Device, Key, Event, Amount, false, FPlatformTime::Cycles64()));
#endif
	}

	/** Sends one analog sample to a player's controller. */
	void SendAxis(APlayerController* PC, FViewport* InViewport, FInputDeviceId Device, const FKey& Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
	{
#if UE_VERSION_OLDER_THAN(5, 6, 0)
		PC->InputKey(FInputKeyParams(Key, (double)Delta, DeltaTime, NumSamples, bGamepad, Device));
#else
		PC->InputKey(FInputKeyEventArgs(InViewport, Device, Key, Delta, DeltaTime, NumSamples, FPlatformTime::Cycles64()));
#endif
	}
}

UMXGameViewportClient::UMXGameViewportClient(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Three players: three equal quadrants (the fourth shows standings), not the engine's "favour top".
	if (SplitscreenInfo.IsValidIndex(ESplitScreenType::ThreePlayer_FavorTop))
	{
		TArray<FPerPlayerSplitscreenData>& D = SplitscreenInfo[ESplitScreenType::ThreePlayer_FavorTop].PlayerData;
		D.Reset();
		D.Add(FPerPlayerSplitscreenData(0.5f, 0.5f, 0.0f, 0.0f));
		D.Add(FPerPlayerSplitscreenData(0.5f, 0.5f, 0.5f, 0.0f));
		D.Add(FPerPlayerSplitscreenData(0.5f, 0.5f, 0.0f, 0.5f));
	}
}

void UMXGameViewportClient::Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
	Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
	ConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(this, &UMXGameViewportClient::OnDeviceConnectionChange);
}

void UMXGameViewportClient::BeginDestroy()
{
	if (ConnectionHandle.IsValid())
	{
		IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(ConnectionHandle);
		ConnectionHandle.Reset();
	}
	Super::BeginDestroy();
}

AMXGameMode* UMXGameViewportClient::GameMode() const
{
	UWorld* W = GetWorld();
	return W ? W->GetAuthGameMode<AMXGameMode>() : nullptr;
}

void UMXGameViewportClient::UpdateActiveSplitscreenType()
{
	const int32 N = GetGameInstance() ? GetGameInstance()->GetNumLocalPlayers() : 1;
	ESplitScreenType::Type Type = ESplitScreenType::None;
	if (N == 2)
	{
		const UMXGameInstance* GI = Cast<UMXGameInstance>(GetGameInstance());
		const bool bVertical = GI && GI->GetSave() && GI->GetSave()->Settings.TwoPlayerSplit == EMXSplitOrientation::Vertical;
		Type = bVertical ? ESplitScreenType::TwoPlayer_Vertical : ESplitScreenType::TwoPlayer_Horizontal;
	}
	else if (N == 3)
	{
		Type = ESplitScreenType::ThreePlayer_FavorTop; // remapped to quadrants in the constructor
	}
	else if (N >= 4)
	{
		Type = ESplitScreenType::FourPlayer_Grid;
	}
	ActiveSplitscreenType = Type;
}

bool UMXGameViewportClient::MapMenuKey(const FKey& K, EMXMenuAction& Out)
{
	if (K == EKeys::Up || K == EKeys::W || K == EKeys::Gamepad_DPad_Up) { Out = EMXMenuAction::Up; return true; }
	if (K == EKeys::Down || K == EKeys::S || K == EKeys::Gamepad_DPad_Down) { Out = EMXMenuAction::Down; return true; }
	if (K == EKeys::Left || K == EKeys::A || K == EKeys::Gamepad_DPad_Left) { Out = EMXMenuAction::Left; return true; }
	if (K == EKeys::Right || K == EKeys::D || K == EKeys::Gamepad_DPad_Right) { Out = EMXMenuAction::Right; return true; }
	if (K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::X || K == EKeys::J || K == EKeys::Gamepad_FaceButton_Bottom) { Out = EMXMenuAction::Confirm; return true; }
	if (K == EKeys::Escape || K == EKeys::BackSpace || K == EKeys::Gamepad_FaceButton_Right) { Out = EMXMenuAction::Back; return true; }
	if (K == EKeys::Gamepad_Special_Right || K == EKeys::P) { Out = EMXMenuAction::Start; return true; }
	if (K == EKeys::Tab || K == EKeys::Gamepad_FaceButton_Top) { Out = EMXMenuAction::Aux; return true; }
	if (K == EKeys::R || K == EKeys::Gamepad_FaceButton_Left) { Out = EMXMenuAction::Aux2; return true; }
	if (K == EKeys::Q || K == EKeys::Gamepad_LeftShoulder) { Out = EMXMenuAction::PageLeft; return true; }
	if (K == EKeys::E || K == EKeys::Gamepad_RightShoulder) { Out = EMXMenuAction::PageRight; return true; }
	return false;
}

bool UMXGameViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (EventArgs.Key.IsMouseButton() || EventArgs.Key.IsTouch())
	{
		return Super::InputKey(EventArgs);
	}
	// Let the console key reach the engine.
	if (EventArgs.Key == EKeys::Tilde)
	{
		return Super::InputKey(EventArgs);
	}
	const bool bGamepad = EventArgs.Key.IsGamepadKey();
	const int32 Dev = bGamepad ? EventArgs.InputDevice.GetId() : MXDevice::Keyboard;
	return RouteKey(Dev, EventArgs.Key, EventArgs.Event, EventArgs.AmountDepressed, bGamepad, EventArgs.InputDevice);
}

static bool IsMouseAxis(const FKey& Key)
{
	return Key.IsMouseButton() || Key == EKeys::MouseX || Key == EKeys::MouseY || Key == EKeys::MouseWheelAxis;
}

#if UE_VERSION_OLDER_THAN(5, 6, 0)
bool UMXGameViewportClient::InputAxis(FViewport* InViewport, FInputDeviceId InputDevice, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{
	if (IsMouseAxis(Key))
	{
		return Super::InputAxis(InViewport, InputDevice, Key, Delta, DeltaTime, NumSamples, bGamepad);
	}
	const bool bPad = bGamepad || Key.IsGamepadKey();
	return RouteAxis(bPad ? InputDevice.GetId() : MXDevice::Keyboard, Key, Delta, DeltaTime, NumSamples, bPad, InputDevice);
}
#else
bool UMXGameViewportClient::InputAxis(const FInputKeyEventArgs& Args)
{
	if (IsMouseAxis(Args.Key))
	{
		return Super::InputAxis(Args);
	}
	const bool bPad = Args.IsGamepad() || Args.Key.IsGamepadKey();
	return RouteAxis(bPad ? Args.InputDevice.GetId() : MXDevice::Keyboard, Args.Key, Args.AmountDepressed, Args.DeltaTime, Args.NumSamples, bPad,
		Args.InputDevice);
}
#endif

bool UMXGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	if (AMXGameMode* GM = GameMode())
	{
		GM->HandleTextChar(MXDevice::Keyboard, Character);
	}
	return true;
}

bool UMXGameViewportClient::RouteKey(int32 Dev, const FKey& Key, EInputEvent Event, float Amount, bool bGamepad, FInputDeviceId DeviceId)
{
	AMXGameMode* GM = GameMode();
	if (!GM)
	{
		return false;
	}
	SeenDevices.Add(Dev);
	if (Event == IE_Pressed)
	{
		GM->HandleAnyKey(Dev, Key);
	}
	const bool bPauseKey = Key == EKeys::Gamepad_Special_Right || Key == EKeys::Escape || Key == EKeys::P;
	if (GM->WantsRaceInput())
	{
		if (bPauseKey)
		{
			if (Event == IE_Pressed)
			{
				const UMXGameInstance* GI = GM->GI();
				const int32 Slot = GI ? GI->SlotForDevice(Dev) : INDEX_NONE;
				if (Slot != INDEX_NONE)
				{
					GM->RequestPause(Slot);
				}
			}
			return true;
		}
		if (AMXPlayerController* PC = GM->GetPCForDevice(Dev))
		{
			SendKey(PC, Viewport, DeviceId, Key, Event, Amount, bGamepad);
			++RoutedToPlayers;
		}
		// Devices that didn't join are ignored during races.
		return true;
	}
	// Menus (frontend, pause, results, designer).
	if (Event == IE_Pressed || Event == IE_Repeat)
	{
		EMXMenuAction Action;
		if (MapMenuKey(Key, Action))
		{
			GM->HandleMenuAction(Dev, Action);
			++RoutedToMenus;
		}
	}
	// Keep controllers' key-up state clean when a race resumes.
	if (Event == IE_Released)
	{
		if (AMXPlayerController* PC = GM->GetPCForDevice(Dev))
		{
			SendKey(PC, Viewport, DeviceId, Key, Event, 0.f, bGamepad);
		}
	}
	return true;
}

bool UMXGameViewportClient::RouteAxis(int32 Dev, const FKey& Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad, FInputDeviceId DeviceId)
{
	AMXGameMode* GM = GameMode();
	if (!GM)
	{
		return false;
	}
	SeenDevices.Add(Dev);
	if (GM->WantsRaceInput())
	{
		if (AMXPlayerController* PC = GM->GetPCForDevice(Dev))
		{
			SendAxis(PC, Viewport, DeviceId, Key, Delta, DeltaTime, NumSamples, bGamepad);
			++RoutedToPlayers;
		}
		return true;
	}
	// Left stick drives menus like a d-pad (edge + repeat handled in Tick).
	if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY)
	{
		FStickState& S = Sticks.FindOrAdd(Dev);
		if (Key == EKeys::Gamepad_LeftX)
		{
			S.Value.X = Delta;
		}
		else
		{
			S.Value.Y = Delta;
		}
	}
	// Always pass zeroed axes to the controller so nothing sticks when the race resumes.
	if (FMath::IsNearlyZero(Delta))
	{
		if (AMXPlayerController* PC = GM->GetPCForDevice(Dev))
		{
			SendAxis(PC, Viewport, DeviceId, Key, 0.f, DeltaTime, NumSamples, bGamepad);
		}
	}
	return true;
}

void UMXGameViewportClient::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	AMXGameMode* GM = GameMode();
	if (!GM || GM->WantsRaceInput())
	{
		return;
	}
	for (TPair<int32, FStickState>& Pair : Sticks)
	{
		FStickState& S = Pair.Value;
		const int32 DX = S.Value.X > 0.6f ? 1 : (S.Value.X < -0.6f ? -1 : 0);
		const int32 DY = S.Value.Y > 0.6f ? 1 : (S.Value.Y < -0.6f ? -1 : 0);
		const bool bChanged = DX != S.DirX || DY != S.DirY;
		S.Repeat -= DeltaTime;
		if ((bChanged && (DX != 0 || DY != 0)) || ((DX != 0 || DY != 0) && S.Repeat <= 0.f))
		{
			if (DY > 0) { GM->HandleMenuAction(Pair.Key, EMXMenuAction::Up); }
			else if (DY < 0) { GM->HandleMenuAction(Pair.Key, EMXMenuAction::Down); }
			else if (DX > 0) { GM->HandleMenuAction(Pair.Key, EMXMenuAction::Right); }
			else if (DX < 0) { GM->HandleMenuAction(Pair.Key, EMXMenuAction::Left); }
			S.Repeat = bChanged ? 0.4f : 0.12f;
		}
		S.DirX = DX;
		S.DirY = DY;
	}
}

void UMXGameViewportClient::PostRender(UCanvas* Canvas)
{
	Super::PostRender(Canvas);
	// The engine's debug-draw pass leaves this canvas sized to the last player's view rectangle (it only
	// resets it after PostRender); the overlay (menus, 3P standings quadrant) needs the whole window.
	if (Canvas && Viewport)
	{
		const FIntPoint Size = Viewport->GetSizeXY();
		Canvas->Init(Size.X, Size.Y, nullptr, Canvas->Canvas);
	}
	if (AMXGameMode* GM = GameMode())
	{
		GM->DrawOverlay(Canvas);
	}
}

void UMXGameViewportClient::OnDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device)
{
	const bool bConnected = NewState == EInputDeviceConnectionState::Connected;
	UE_LOG(LogHeatline, Log, TEXT("Input device %d %s"), Device.GetId(), bConnected ? TEXT("connected") : TEXT("disconnected"));
	if (AMXGameMode* GM = GameMode())
	{
		GM->HandleDeviceConnection(Device.GetId(), bConnected);
	}
}

void UMXGameViewportClient::InjectKey(int32 DeviceKey, const FKey& Key, EInputEvent Event, float Amount)
{
	RouteKey(DeviceKey, Key, Event, Amount, DeviceKey >= 0, FInputDeviceId::CreateFromInternalId(FMath::Max(0, DeviceKey)));
}

void UMXGameViewportClient::InjectAxis(int32 DeviceKey, const FKey& Key, float Value)
{
	RouteAxis(DeviceKey, Key, Value, 1.f / 60.f, 1, DeviceKey >= 0, FInputDeviceId::CreateFromInternalId(FMath::Max(0, DeviceKey)));
}

void UMXGameViewportClient::InjectConnection(int32 DeviceKey, bool bConnected)
{
	if (AMXGameMode* GM = GameMode())
	{
		GM->HandleDeviceConnection(DeviceKey, bConnected);
	}
}
