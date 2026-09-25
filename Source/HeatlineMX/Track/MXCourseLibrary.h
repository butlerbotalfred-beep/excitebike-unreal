#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Track/MXTrackTypes.h"
#include "MXCourseLibrary.generated.h"

/** Entry in course lists (built-in NES courses, the handling test strip, user tracks). */
USTRUCT()
struct FMXCourseEntry
{
	GENERATED_BODY()

	UPROPERTY() FString Id;
	UPROPERTY() FString DisplayName;
	UPROPERTY() bool bBuiltIn = false;
	UPROPERTY() bool bUser = false;
	UPROPERTY() bool bTestStrip = false;
	UPROPERTY() int32 NesTrack = 0;
};

/**
 * Loads built-in courses (Data Assets under /Game/HeatlineMX/Courses, falling back to the
 * Content/Courses/nes_t<n>.json manifest) and manages named user tracks saved as JSON under Saved/Tracks.
 */
UCLASS()
class HEATLINEMX_API UMXCourseLibrary : public UObject
{
	GENERATED_BODY()

public:
	void Initialize();

	const TArray<FMXCourseEntry>& GetEntries() const { return Entries; }
	/** Built-in championship order (NES courses 1..5). */
	TArray<FString> ChampionshipOrder() const;

	bool GetCourse(const FString& Id, FMXTrackDefinition& Out) const;
	const FMXTrackDefinition* FindBuiltIn(const FString& Id) const;

	// ---- user tracks ----
	void RefreshUserTracks();
	bool SaveUserTrack(FMXTrackDefinition Def, FString& OutError);
	bool LoadUserTrack(const FString& Name, FMXTrackDefinition& Out, FString& OutError) const;
	bool DeleteUserTrack(const FString& Name);
	TArray<FString> GetUserTrackNames() const;
	static FString UserTracksDir();
	static FString SanitizeName(const FString& Name);

	/** Handling benchmark strip: one of every piece with generous spacing (build step 1). */
	static FMXTrackDefinition MakeTestStrip();

	/**
	 * Time-trial medal targets for a course, layout and lap count. They come from the definition's explicit
	 * values (Challenge layout at its own lap count) or else from the reference rider's measured time on
	 * exactly this layout (cached per content).
	 */
	static void GetMedalTimes(const FMXTrackDefinition& Def, EMXLayoutVariant Variant, int32 Laps, float& OutGold, float& OutSilver, float& OutBronze);

private:
	void RebuildEntries();

	UPROPERTY() TArray<FMXTrackDefinition> BuiltIn;
	UPROPERTY() TArray<FMXCourseEntry> Entries;
	TArray<FString> UserNames;
};
