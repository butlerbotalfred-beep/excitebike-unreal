#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MXFXManager.generated.h"

class UInstancedStaticMeshComponent;

enum class EMXPuff : uint8
{
	Dust,
	Mud,
	Grass,
	Steam,
	Smoke
};

/**
 * Lightweight CPU particle system for the whole race: one instanced quad mesh, billboarded per view
 * by the particle material (so every split-screen camera sees them face-on). Budgeted for 4 views.
 */
UCLASS()
class HEATLINEMX_API AMXFXManager : public AActor
{
	GENERATED_BODY()

public:
	AMXFXManager();

	static AMXFXManager* Get(UWorld* World);

	void SpawnPuff(EMXPuff Kind, const FVector& Location, const FVector& Velocity, float Scale);
	virtual void Tick(float DeltaSeconds) override;
	void ClearAll();

	int32 ActiveCount() const { return Active; }

	/** Hard cap (performance budget for 4 viewports). */
	UPROPERTY(EditAnywhere, Category = "FX") int32 MaxParticles = 1800;

private:
	struct FParticle
	{
		FVector Pos;
		FVector Vel;
		float Age = 0.f;
		float Life = 1.f;
		float Size0 = 30.f;
		float Size1 = 90.f;
		float Gravity = 0.f;
		float Drag = 1.f;
		FLinearColor Color;
	};
	void EnsureComponent();

	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ISM;
	TArray<FParticle> Particles;
	int32 Active = 0;
	int32 InstanceCount = 0;
	static TWeakObjectPtr<AMXFXManager> Instance;
};
