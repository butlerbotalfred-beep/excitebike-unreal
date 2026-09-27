#include "Session/MXGameUserSettings.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"

const FIntPoint UMXGameUserSettings::DefaultWindowSize(1600, 900);

UMXGameUserSettings* UMXGameUserSettings::Get()
{
	return GEngine ? Cast<UMXGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UMXGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	FullscreenMode = LastConfirmedFullscreenMode = PreferredFullscreenMode = EWindowMode::Windowed;
	ResolutionSizeX = LastUserConfirmedResolutionSizeX = DefaultWindowSize.X;
	ResolutionSizeY = LastUserConfirmedResolutionSizeY = DefaultWindowSize.Y;
}

void UMXGameUserSettings::LoadSettings(bool bForceReload)
{
	Super::LoadSettings(bForceReload);
#if PLATFORM_MAC
	// The game has no full-screen option; keep it off on macOS even if a settings file asks for it.
	FullscreenMode = LastConfirmedFullscreenMode = PreferredFullscreenMode = EWindowMode::Windowed;
#endif
}

void UMXGameUserSettings::FitWindowToScreen()
{
	if (GetFullscreenMode() != EWindowMode::Windowed || !FSlateApplication::IsInitialized())
	{
		return;
	}
	FDisplayMetrics Metrics;
	FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
	const FPlatformRect& Work = Metrics.PrimaryDisplayWorkAreaRect;
	// Leave room for the title bar and a small margin.
	const int32 MaxW = Work.Right - Work.Left - 20;
	const int32 MaxH = Work.Bottom - Work.Top - 40;
	const FIntPoint Current = GetScreenResolution();
	if (MaxW <= 0 || MaxH <= 0 || (Current.X <= MaxW && Current.Y <= MaxH))
	{
		return;
	}
	// The largest 16:9 window that fits.
	const int32 H = FMath::Min(MaxH, MaxW * 9 / 16) & ~1;
	SetScreenResolution(FIntPoint((H * 16 / 9) & ~1, H));
	ApplyResolutionSettings(true); // command-line sizes (autotests) still win
	SaveSettings();
}
