#include "Race/MXEnvironment.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"

AMXEnvironment::AMXEnvironment()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	// Low evening sun behind the camera side, raking across the ramps for readable shapes.
	Sun->SetWorldRotation(FRotator(-28.f, -125.f, 0.f));
	Sun->SetIntensity(7.5f);
	Sun->SetLightColor(FLinearColor(1.f, 0.86f, 0.7f));
	Sun->SetAtmosphereSunLight(true);
	Sun->SetDynamicShadowCascades(2);
	Sun->SetDynamicShadowDistanceMovableLight(9000.f);
	Sun->SetCastShadows(true);
	Sun->SetMobility(EComponentMobility::Movable);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(RootComponent);

	Sky = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	Sky->SetupAttachment(RootComponent);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->bRealTimeCapture = true;
	Sky->SetIntensity(1.1f);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetFogDensity(0.004f);
	Fog->SetFogHeightFalloff(0.1f);
	Fog->SetStartDistance(4000.f);

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(RootComponent);
	Post->bUnbound = true;
	FPostProcessSettings& S = Post->Settings;
	// Fixed exposure: identical brightness in every split-screen view, no adaptation pumping.
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 10.5f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.9f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.3f;
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.12f, 1.12f, 1.12f, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.f);
}

void AMXEnvironment::BeginPlay()
{
	Super::BeginPlay();
	if (Sky)
	{
		Sky->RecaptureSky();
	}
}
