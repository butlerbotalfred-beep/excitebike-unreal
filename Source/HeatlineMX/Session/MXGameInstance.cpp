#include "Session/MXGameInstance.h"
#include "Session/MXGameUserSettings.h"
#include "Track/MXCourseLibrary.h"
#include "Core/MXTuning.h"
#include "HeatlineMX.h"
#include "Kismet/GameplayStatics.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"

namespace
{
	const TCHAR* SaveSlot = TEXT("HeatlineMX");
}

void UMXGameInstance::Init()
{
	Super::Init();
	if (UMXGameUserSettings* Settings = UMXGameUserSettings::Get())
	{
		Settings->FitWindowToScreen();
	}
	MXTuning::LoadAssets();
	Courses = NewObject<UMXCourseLibrary>(this);
	Courses->Initialize();
	if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
	{
		Save = Cast<UMXSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	}
	if (!Save)
	{
		Save = Cast<UMXSaveGame>(UGameplayStatics::CreateSaveGameObject(UMXSaveGame::StaticClass()));
	}
	while (Save->Profiles.Num() < 4)
	{
		Save->Profiles.Add(FMXControlProfile::Defaults(EMXControlScheme::Classic));
	}
	ResetLobby(false);
}

void UMXGameInstance::SaveNow()
{
	if (Save)
	{
		UGameplayStatics::SaveGameToSlot(Save, SaveSlot, 0);
	}
	ApplyVolume();
}

void UMXGameInstance::ApplyVolume() const
{
	if (Save && GEngine)
	{
		if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
		{
			Device->SetTransientPrimaryVolume(Save->Settings.MasterVolume);
		}
	}
}

int32 UMXGameInstance::NumJoined() const
{
	int32 N = 0;
	for (const FMXPlayerSlot& S : Slots)
	{
		N += S.bJoined ? 1 : 0;
	}
	return N;
}

int32 UMXGameInstance::SlotForDevice(int32 DeviceKey) const
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (Slots[i].bJoined && Slots[i].DeviceKey == DeviceKey)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool UMXGameInstance::IsColorTaken(int32 Color, int32 IgnoreSlot) const
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (i != IgnoreSlot && Slots[i].bJoined && Slots[i].ColorIndex == Color)
		{
			return true;
		}
	}
	return false;
}

int32 UMXGameInstance::FirstFreeColor(int32 Preferred, int32 IgnoreSlot) const
{
	for (int32 k = 0; k < MX::NumRiderColors; ++k)
	{
		const int32 C = (Preferred + k) % MX::NumRiderColors;
		if (!IsColorTaken(C, IgnoreSlot))
		{
			return C;
		}
	}
	return Preferred;
}

void UMXGameInstance::ResetLobby(bool bKeepDevices)
{
	for (int32 i = 0; i < 4; ++i)
	{
		FMXPlayerSlot& S = Slots[i];
		if (!bKeepDevices)
		{
			S = FMXPlayerSlot();
			S.ColorIndex = i;
		}
		S.bReady = false;
		S.Profile = ProfileFor(i);
	}
}

const FMXControlProfile& UMXGameInstance::ProfileFor(int32 Slot)
{
	static FMXControlProfile Fallback = FMXControlProfile::Defaults(EMXControlScheme::Classic);
	if (Save && Save->Profiles.IsValidIndex(Slot))
	{
		return Save->Profiles[Slot];
	}
	return Fallback;
}

void UMXGameInstance::StoreProfile(int32 Slot, const FMXControlProfile& Profile)
{
	if (Save && Save->Profiles.IsValidIndex(Slot))
	{
		Save->Profiles[Slot] = Profile;
		SaveNow();
	}
	if (Slot >= 0 && Slot < 4)
	{
		Slots[Slot].Profile = Profile;
	}
}

bool UMXGameInstance::SubmitResult(const FString& CourseId, EMXLayoutVariant Variant, int32 Laps, float Time, float BestLap, int32 Medal, const FMXGhostData* Ghost)
{
	if (!Save || Time <= 0.f)
	{
		return false;
	}
	const FString Key = UMXSaveGame::RecordKey(CourseId, Variant, Laps);
	FMXRecordEntry& R = Save->Records.FindOrAdd(Key);
	R.Runs++;
	bool bBest = false;
	if (R.BestTime < 0.f || Time < R.BestTime)
	{
		R.BestTime = Time;
		bBest = true;
		if (Ghost && Ghost->Frames.Num() > 10)
		{
			FMXGhostData G = *Ghost;
			G.CourseKey = Key;
			G.TotalTime = Time;
			Save->Ghosts.Add(Key, G);
		}
	}
	if (BestLap > 0.f && (R.BestLap < 0.f || BestLap < R.BestLap))
	{
		R.BestLap = BestLap;
	}
	R.Medal = FMath::Max(R.Medal, Medal);
	SaveNow();
	return bBest;
}

const FMXRecordEntry* UMXGameInstance::GetRecord(const FString& CourseId, EMXLayoutVariant Variant, int32 Laps) const
{
	return Save ? Save->Records.Find(UMXSaveGame::RecordKey(CourseId, Variant, Laps)) : nullptr;
}

const FMXGhostData* UMXGameInstance::GetGhost(const FString& CourseId, EMXLayoutVariant Variant, int32 Laps) const
{
	return Save ? Save->Ghosts.Find(UMXSaveGame::RecordKey(CourseId, Variant, Laps)) : nullptr;
}
