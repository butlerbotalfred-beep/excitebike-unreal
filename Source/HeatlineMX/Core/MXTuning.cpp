#include "Core/MXTuning.h"
#include "HeatlineMX.h"
#include "UObject/SoftObjectPath.h"

UMXCameraTuning::UMXCameraTuning()
{
	// Full screen 16:9.
	Full.Distance = 26.f; Full.FOV = 60.f; Full.Pitch = 18.f; Full.Yaw = 27.f; Full.ScreenX = 0.33f; Full.ScreenY = 0.62f;
	Full.SpeedScreenShift = 0.07f; Full.HeightFollow = 0.35f; Full.LateralFollow = 0.3f;

	// 2P top/bottom: each view is ~3.5:1, so the view is wide and low; the bike can sit further left.
	Wide.Distance = 22.f; Wide.FOV = 84.f; Wide.Pitch = 13.f; Wide.Yaw = 18.f; Wide.ScreenX = 0.30f; Wide.ScreenY = 0.60f;
	Wide.SpeedScreenShift = 0.05f; Wide.HeightFollow = 0.45f; Wide.LateralFollow = 0.25f;

	// 2P side by side: ~0.9:1. Pull back, look down more, turn further towards travel.
	Tall.Distance = 36.f; Tall.FOV = 58.f; Tall.Pitch = 22.f; Tall.Yaw = 20.f; Tall.ScreenX = 0.46f; Tall.ScreenY = 0.64f;
	Tall.SpeedScreenShift = 0.08f; Tall.HeightFollow = 0.3f; Tall.LateralFollow = 0.5f;

	// Quadrant (3P/4P) at 960x540: same shape as full screen, a touch closer and wider.
	Quarter.Distance = 24.f; Quarter.FOV = 64.f; Quarter.Pitch = 18.f; Quarter.Yaw = 28.f; Quarter.ScreenX = 0.29f; Quarter.ScreenY = 0.62f;
	Quarter.SpeedScreenShift = 0.07f; Quarter.HeightFollow = 0.35f; Quarter.LateralFollow = 0.3f;

	// Track Designer: straight side view centred on the cursor.
	Designer.Distance = 52.f; Designer.FOV = 55.f; Designer.Pitch = 26.f; Designer.Yaw = 0.f; Designer.ScreenX = 0.5f; Designer.ScreenY = 0.5f;
	Designer.SpeedScreenShift = 0.f; Designer.HeightFollow = 0.f; Designer.LateralFollow = 0.f; Designer.FollowLag = 0.08f;
}

const FMXCameraShapeTuning& UMXCameraTuning::ForShape(EMXViewShape Shape) const
{
	switch (Shape)
	{
	case EMXViewShape::Wide: return Wide;
	case EMXViewShape::Tall: return Tall;
	case EMXViewShape::Quarter: return Quarter;
	default: return Full;
	}
}

UMXAITuning::UMXAITuning()
{
	Easy.ReactionDelay = 0.35f; Easy.LandingErrorDeg = 14.f; Easy.TurboHeatLimit = 60.f; Easy.TurboResumeMargin = 25.f; Easy.bPlansCoolStrips = false;
	Easy.LaneHorizon = 25.f; Easy.bUsesFlightControl = false; Easy.MashRate = 4.f;
	Easy.MistakeRate = 0.12f; Easy.bChasersAllowed = false; Easy.GreedyTurboRate = 0.03f;

	Medium.ReactionDelay = 0.2f; Medium.LandingErrorDeg = 7.f; Medium.TurboHeatLimit = 80.f; Medium.TurboResumeMargin = 15.f; Medium.bPlansCoolStrips = false;
	Medium.LaneHorizon = 45.f; Medium.bUsesFlightControl = true; Medium.MashRate = 7.f;
	Medium.MistakeRate = 0.05f; Medium.bChasersAllowed = true; Medium.GreedyTurboRate = 0.01f;

	Hard.ReactionDelay = 0.1f; Hard.LandingErrorDeg = 3.f; Hard.TurboHeatLimit = 94.f; Hard.TurboResumeMargin = 4.f; Hard.bPlansCoolStrips = true;
	Hard.LaneHorizon = 70.f; Hard.bUsesFlightControl = true; Hard.MashRate = 10.f;
	Hard.MistakeRate = 0.015f; Hard.bChasersAllowed = true; Hard.GreedyTurboRate = 0.001f;
}

const FMXAISkill& UMXAITuning::ForDifficulty(EMXAIDifficulty D) const
{
	switch (D)
	{
	case EMXAIDifficulty::Easy: return Easy;
	case EMXAIDifficulty::Hard: return Hard;
	default: return Medium;
	}
}

namespace MXTuning
{
	static TWeakObjectPtr<UMXBikeTuning> GBike;
	static TWeakObjectPtr<UMXCameraTuning> GCamera;
	static TWeakObjectPtr<UMXAITuning> GAI;
	static TWeakObjectPtr<UMXTrackStyle> GStyle;
	static bool GTriedLoad = false;

	template <typename T>
	static T* TryLoad(const TCHAR* Path)
	{
		const FSoftObjectPath SoftPath(Path);
		UObject* Obj = SoftPath.TryLoad();
		T* Typed = Cast<T>(Obj);
		if (Typed)
		{
			Typed->AddToRoot();
			UE_LOG(LogHeatline, Log, TEXT("Tuning asset loaded: %s"), Path);
		}
		return Typed;
	}

	void LoadAssets()
	{
		if (GTriedLoad)
		{
			return;
		}
		GTriedLoad = true;
		GBike = TryLoad<UMXBikeTuning>(TEXT("/Game/HeatlineMX/Data/DA_BikeTuning.DA_BikeTuning"));
		GCamera = TryLoad<UMXCameraTuning>(TEXT("/Game/HeatlineMX/Data/DA_CameraTuning.DA_CameraTuning"));
		GAI = TryLoad<UMXAITuning>(TEXT("/Game/HeatlineMX/Data/DA_AITuning.DA_AITuning"));
		GStyle = TryLoad<UMXTrackStyle>(TEXT("/Game/HeatlineMX/Data/DA_TrackStyle.DA_TrackStyle"));
	}

	const UMXBikeTuning& Bike()
	{
		return GBike.IsValid() ? *GBike.Get() : *GetDefault<UMXBikeTuning>();
	}
	const UMXCameraTuning& Camera()
	{
		return GCamera.IsValid() ? *GCamera.Get() : *GetDefault<UMXCameraTuning>();
	}
	const UMXAITuning& AI()
	{
		return GAI.IsValid() ? *GAI.Get() : *GetDefault<UMXAITuning>();
	}
	const UMXTrackStyle& Style()
	{
		return GStyle.IsValid() ? *GStyle.Get() : *GetDefault<UMXTrackStyle>();
	}
}
