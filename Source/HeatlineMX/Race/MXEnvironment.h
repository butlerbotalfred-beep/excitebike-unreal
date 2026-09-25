#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MXEnvironment.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;

/** Floodlit-dusk stadium lighting, sized for a four-camera budget (no Lumen, 2-cascade shadows). */
UCLASS()
class HEATLINEMX_API AMXEnvironment : public AActor
{
	GENERATED_BODY()

public:
	AMXEnvironment();
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<USkyLightComponent> Sky;
	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<UPostProcessComponent> Post;
};
