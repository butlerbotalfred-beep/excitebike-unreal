#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "Core/MXTypes.h"
#include "MXInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/** Remappable digital bindings for one device kind (analog sticks are fixed per scheme). */
USTRUCT(BlueprintType)
struct FMXBindingSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FKey> Accelerate;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FKey> Turbo;
	/** Toward lane 1 (far side / screen up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FKey> SteerFar;
	/** Toward lane 4 (near side / screen down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FKey> SteerNear;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FKey> NoseUp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FKey> NoseDown;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FKey> Pause;
};

/** Per-player control profile: scheme + both binding sets. Stored in the save game. */
USTRUCT(BlueprintType)
struct FMXControlProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) EMXControlScheme Scheme = EMXControlScheme::Classic;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FMXBindingSet Keyboard;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FMXBindingSet Gamepad;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bLandingAssist = true;

	static FMXControlProfile Defaults(EMXControlScheme Scheme);
};

/** Remappable action ids (for the controls screen). */
enum class EMXBindAction : uint8
{
	Accelerate,
	Turbo,
	SteerFar,
	SteerNear,
	NoseUp,
	NoseDown,
	Count
};

/**
 * Enhanced Input actions and mapping contexts created at runtime (no assets needed), so the same
 * native Enhanced Input pipeline handles every local player with its own profile.
 */
UCLASS()
class HEATLINEMX_API UMXInputConfig : public UObject
{
	GENERATED_BODY()

public:
	static UMXInputConfig* Get();

	UPROPERTY() TObjectPtr<UInputAction> Accelerate;
	UPROPERTY() TObjectPtr<UInputAction> Turbo;
	UPROPERTY() TObjectPtr<UInputAction> Steer;
	UPROPERTY() TObjectPtr<UInputAction> Pitch;
	UPROPERTY() TObjectPtr<UInputAction> Pause;

	/** Builds a mapping context for a profile (keyboard + gamepad keys; routing picks the device). */
	UInputMappingContext* BuildContext(const FMXControlProfile& Profile, UObject* Outer);

	static FString ActionLabel(EMXBindAction A, EMXControlScheme Scheme);
	static TArray<FKey>* GetKeys(FMXBindingSet& Set, EMXBindAction A);
	static FString KeysLabel(const TArray<FKey>& Keys);
	/** Short display name for one key (A/B/X/Y, LB/RB, LT/RT, D-pad up...; keyboard keys as the engine names them). */
	static FString KeyName(const FKey& Key);
	/** Short human text describing the selected layout (shown in the lobby and at race start). */
	static TArray<FString> DescribeLayout(const FMXControlProfile& Profile, bool bGamepad);

private:
	void CreateActions();
};
