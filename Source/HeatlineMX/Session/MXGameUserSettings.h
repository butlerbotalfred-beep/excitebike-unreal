#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "MXGameUserSettings.generated.h"

/**
 * Window settings. The game runs in a window: on macOS the engine's default (full screen) froze at launch, with
 * the game thread waiting in FMacWindow::WaitForFullScreenTransition for a transition that never finished.
 *
 * The engine sizes the first window from config before any game code runs, so Config/DefaultGameUserSettings.ini
 * holds the first-launch values under this class's section. Using a section of our own also means settings saved
 * by earlier builds, which asked for full screen, are ignored.
 */
UCLASS()
class HEATLINEMX_API UMXGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static UMXGameUserSettings* Get();

	virtual void SetToDefaults() override;
	virtual void LoadSettings(bool bForceReload = false) override;

	/** Shrinks the window when it doesn't fit the main display's work area (call once the game window exists). */
	void FitWindowToScreen();

	static const FIntPoint DefaultWindowSize;
};
