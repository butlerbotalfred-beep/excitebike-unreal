#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Session/MXInputConfig.h"
#include "MXPlayerController.generated.h"

class AMXBike;
class UInputMappingContext;

/** What a player's camera looks at. */
enum class EMXCamMode : uint8
{
	FollowBike,
	Designer,
	Attract
};

/** Local player: Enhanced Input -> FMXBikeInput for the possessed bike. One per split-screen view. */
UCLASS()
class HEATLINEMX_API AMXPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMXPlayerController();

	void ApplyProfile(const FMXControlProfile& InProfile);
	const FMXControlProfile& GetProfile() const { return Profile; }

	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Local player slot (0..3) this controller serves. */
	int32 Slot = 0;

	// Camera focus (read by AMXPlayerCameraManager).
	EMXCamMode CamMode = EMXCamMode::Attract;
	TWeakObjectPtr<AMXBike> FocusBike;
	float DesignerS = 0.f;
	float DesignerZoom = 1.f;
	/** Set true on the first frame after a focus change so the camera snaps instead of gliding. */
	bool bSnapCamera = true;

	/** Last raw input sampled (debug / autotest verification). */
	FMXBikeInput LastSampled;
	int32 MashCounter = 0;

private:
	void OnAccelerateStarted();
	void OnPausePressed();

	FMXControlProfile Profile;
	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
};

/** Elevated three-quarter side-follow camera, tuned per viewport shape. */
UCLASS()
class HEATLINEMX_API AMXPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AMXPlayerCameraManager();
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	/** Current view shape (derived from the split-screen viewport). */
	EMXViewShape GetViewShape() const { return Shape; }
	/** Seconds of track visible ahead of the bike at its current speed (reported by the camera test). */
	float VisibleLookAheadSeconds = 0.f;

private:
	EMXViewShape ComputeShape() const;
	FVector CamPos = FVector::ZeroVector;
	/** Smoothed point the camera frames (the bike, with partial lane / height follow). */
	FVector Anchor = FVector::ZeroVector;
	/** Width / height of this player's view (from the split-screen layout). */
	mutable float ViewAspect = 16.f / 9.f;
	float Shake = 0.f;
	EMXViewShape Shape = EMXViewShape::Full;
	bool bInit = false;
};
