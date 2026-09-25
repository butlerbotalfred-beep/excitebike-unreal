#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Race/MXRaceTypes.h"
#include "Session/MXInputConfig.h"
#include "Session/MXSaveGame.h"
#include "MXGameInstance.generated.h"

class UMXCourseLibrary;

/** Lobby slot for one local player. */
USTRUCT()
struct FMXPlayerSlot
{
	GENERATED_BODY()

	UPROPERTY() bool bJoined = false;
	/** Device key: -1 = keyboard, otherwise the gamepad's FInputDeviceId. INDEX_NONE-1 = none. */
	UPROPERTY() int32 DeviceKey = -2;
	UPROPERTY() int32 ColorIndex = 0;
	UPROPERTY() bool bReady = false;
	UPROPERTY() bool bDeviceLost = false;
	UPROPERTY() FMXControlProfile Profile;

	bool UsesKeyboard() const { return DeviceKey == -1; }
};

/** Championship progress. */
USTRUCT()
struct FMXChampionship
{
	GENERATED_BODY()

	UPROPERTY() bool bActive = false;
	UPROPERTY() TArray<FString> Courses;
	UPROPERTY() int32 RaceIndex = 0;
	/** Points per championship entrant (index = FMXRacerConfig::ChampionshipId). */
	UPROPERTY() TArray<int32> Points;
	UPROPERTY() TArray<FString> Names;
	UPROPERTY() TArray<int32> Colors;
	UPROPERTY() TArray<int32> LastPositions;

	static int32 PointsFor(int32 Position)
	{
		static const int32 Table[] = {10, 8, 6, 5, 4, 3, 2, 1};
		return (Position >= 1 && Position <= 8) ? Table[Position - 1] : 0;
	}
};

UCLASS()
class HEATLINEMX_API UMXGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UMXCourseLibrary* GetCourses() const { return Courses; }
	UMXSaveGame* GetSave() const { return Save; }
	void SaveNow();
	/** Applies the master volume setting to the main audio device. */
	void ApplyVolume() const;

	FMXPlayerSlot Slots[4];
	FMXChampionship Championship;
	FMXRaceConfig LastRace;
	/** Course preselected by the designer's "race this track". */
	FString PendingCourseId;

	int32 NumJoined() const;
	int32 SlotForDevice(int32 DeviceKey) const;
	int32 FirstFreeColor(int32 Preferred, int32 IgnoreSlot) const;
	bool IsColorTaken(int32 Color, int32 IgnoreSlot) const;
	void ResetLobby(bool bKeepDevices);
	const FMXControlProfile& ProfileFor(int32 Slot);
	void StoreProfile(int32 Slot, const FMXControlProfile& Profile);

	/** Records (time trial / race best times + medals). Returns true if it was a new best. */
	bool SubmitResult(const FString& CourseId, EMXLayoutVariant Variant, int32 Laps, float Time, float BestLap, int32 Medal, const FMXGhostData* Ghost);
	const FMXRecordEntry* GetRecord(const FString& CourseId, EMXLayoutVariant Variant, int32 Laps) const;
	const FMXGhostData* GetGhost(const FString& CourseId, EMXLayoutVariant Variant, int32 Laps) const;

private:
	UPROPERTY() TObjectPtr<UMXCourseLibrary> Courses;
	UPROPERTY() TObjectPtr<UMXSaveGame> Save;
};
