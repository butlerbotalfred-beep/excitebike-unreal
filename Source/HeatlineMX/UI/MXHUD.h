#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MXHUD.generated.h"

class AMXRaceManager;

/**
 * Per-viewport race HUD drawn on the player's own split-screen canvas: position, lap, time,
 * a prominent temperature gauge, speed, landing-angle meter, course progress strip, messages.
 * Sized from the viewport height so it stays readable in a quarter of a 1080p screen.
 */
UCLASS()
class HEATLINEMX_API AMXHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Minimum text size in pixels (readability floor for quarter views). */
	UPROPERTY(EditAnywhere, Category = "HUD") float MinTextPx = 17.f;

private:
	void DrawRace(AMXRaceManager* Race, int32 Racer);
	void DrawHeatGauge(float X, float Y, float W, float H, float U, const struct FMXBikeState& S);
	void DrawLandingMeter(AMXRaceManager* Race, int32 Racer, float U);
	void DrawProgressStrip(AMXRaceManager* Race, int32 Racer, float U);
	float Px(float Size, float U) const { return FMath::Max(MinTextPx, Size * U); }
	float CoolFlash = 0.f;
};
