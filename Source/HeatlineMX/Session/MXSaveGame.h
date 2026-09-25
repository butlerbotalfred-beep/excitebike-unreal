#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Race/MXRaceTypes.h"
#include "Session/MXInputConfig.h"
#include "MXSaveGame.generated.h"

USTRUCT()
struct FMXRecordEntry
{
	GENERATED_BODY()

	UPROPERTY() float BestTime = -1.f;
	UPROPERTY() float BestLap = -1.f;
	/** 0 none, 1 bronze, 2 silver, 3 gold (time trial only; independent of race positions). */
	UPROPERTY() int32 Medal = 0;
	UPROPERTY() int32 Runs = 0;
};

USTRUCT()
struct FMXSettings
{
	GENERATED_BODY()

	UPROPERTY() EMXSplitOrientation TwoPlayerSplit = EMXSplitOrientation::Horizontal;
	UPROPERTY() EMXAIDifficulty AIDifficulty = EMXAIDifficulty::Medium;
	UPROPERTY() int32 AICountRace = 5;
	UPROPERTY() int32 AICountVersus = 2;
	UPROPERTY() float MasterVolume = 0.8f;
	UPROPERTY() bool bLandingMeter = true;
};

UCLASS()
class HEATLINEMX_API UMXSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() int32 Version = 1;
	UPROPERTY() TMap<FString, FMXRecordEntry> Records;
	UPROPERTY() TMap<FString, FMXGhostData> Ghosts;
	UPROPERTY() TArray<FMXControlProfile> Profiles;
	UPROPERTY() FMXSettings Settings;
	UPROPERTY() TArray<int32> PreferredColors;

	static FString RecordKey(const FString& CourseId, EMXLayoutVariant Variant, int32 Laps)
	{
		return FString::Printf(TEXT("%s|%s|%d"), *CourseId, Variant == EMXLayoutVariant::Main ? TEXT("main") : TEXT("challenge"), Laps);
	}
};
