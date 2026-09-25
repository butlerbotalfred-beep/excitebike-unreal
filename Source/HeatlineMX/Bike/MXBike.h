#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Bike/MXBikeSim.h"
#include "MXBike.generated.h"

class UStaticMeshComponent;
class UMXEngineAudioComponent;
class FMXTrackModel;
class UMaterialInterface;

/** Visual/audio presentation of one rider. The simulation state lives here but is stepped by AMXRaceManager. */
UCLASS(Blueprintable)
class HEATLINEMX_API AMXBike : public APawn
{
	GENERATED_BODY()

public:
	AMXBike();

	/** Builds the procedural bike + rider in the given colour. */
	void SetupRider(int32 InRacerIndex, int32 InColorIndex, const FString& InName, bool bInAI, int32 InPlayerSlot);
	/** Ghost bikes are translucent and silent. */
	void MakeGhost();

	virtual void Tick(float DeltaSeconds) override;

	/** Called by the race manager after each fixed step (events drive audio/FX). */
	void OnSimStep(const FMXTrackModel& Track);
	/** Render interpolation between PrevState and State. */
	void SetInterpolation(float Alpha) { InterpAlpha = Alpha; }

	FVector GetVisualLocation() const { return VisualCenter; }
	float GetVisualPitch() const { return VisualPitch; }

	// ---- simulation data (owned here, stepped by the race manager) ----
	FMXBikeState State;
	FMXBikeState PrevState;
	FMXBikeInput CurrentInput;
	FMXBikeInput LastInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider") int32 RacerIndex = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider") int32 ColorIndex = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider") FString RacerName;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider") bool bIsAI = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider") int32 PlayerSlot = -1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider") bool bIsGhost = false;

	/** Tuning knobs for presentation (Blueprint subclasses can override). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Presentation") float SuspensionStiffness = 260.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Presentation") float SuspensionDamping = 18.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Presentation") float LandingCompressionGain = 2.4f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Presentation") float LeanPerLateralSpeed = 2.2f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Presentation") float DustRate = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<USceneComponent> BikeRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UMXEngineAudioComponent> EngineAudio;

	/** Accumulated events since the last presentation tick. */
	uint32 PendingEvents = 0;

private:
	void BuildBikeMeshes();
	void BuildRiderMeshes();
	UStaticMeshComponent* NewPart(USceneComponent* Parent, FName Name);
	void UpdatePose(float DeltaSeconds);
	void UpdateRiderPose(float DeltaSeconds, const FMXBikeState& S);
	void PlaceLimb(UStaticMeshComponent* Part, const FVector& From, const FVector& To);
	void SpawnFX(float DeltaSeconds, const FMXBikeState& S);

	int32 InitializedColor = -1;
	float InterpAlpha = 1.f;
	FVector VisualCenter = FVector::ZeroVector;
	float VisualPitch = 0.f;
	float VisualRoll = 0.f;
	float VisualYaw = 0.f;
	float FrontComp = 0.f, FrontVel = 0.f;
	float RearComp = 0.f, RearVel = 0.f;
	float WheelAngleF = 0.f, WheelAngleR = 0.f;
	float LastDistance = 0.f;
	float RiderLean = 0.f;       // -1 tucked .. +1 leaning back
	float RiderCrouch = 0.f;
	float PoseTime = 0.f;
	float CrashAnimTime = 0.f;
	float DustAccumulator = 0.f;
	float FlickerTime = 0.f;
	FVector CrashRiderStart = FVector::ZeroVector;
	float CrashRiderSpin = 0.f;
	EMXBikePhase LastPhase = EMXBikePhase::Grounded;

	// Bike parts
	UPROPERTY() TObjectPtr<USceneComponent> Chassis;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ChassisMesh;
	UPROPERTY() TObjectPtr<USceneComponent> ForkPivot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ForkUpper;
	UPROPERTY() TObjectPtr<USceneComponent> ForkSlider;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ForkLower;
	UPROPERTY() TObjectPtr<USceneComponent> FrontAxle;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FrontWheel;
	UPROPERTY() TObjectPtr<USceneComponent> SwingPivot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Swingarm;
	UPROPERTY() TObjectPtr<USceneComponent> RearAxle;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> RearWheel;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Shock;

	// Rider parts
	UPROPERTY() TObjectPtr<USceneComponent> RiderRoot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pelvis;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> UpperArmL;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> UpperArmR;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ForeArmL;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ForeArmR;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ThighL;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ThighR;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ShinL;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ShinR;

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> AllParts;
	UPROPERTY() TObjectPtr<UMaterialInterface> BodyMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> PaintMaterial;
};
