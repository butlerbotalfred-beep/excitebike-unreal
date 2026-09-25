#pragma once

#include "CoreMinimal.h"
#include "Core/MXTypes.h"
#include "Engine/DataAsset.h"
#include "MXTrackTypes.generated.h"

/**
 * One obstacle placement on a lap. Shared by built-in (NES-translated) courses and user tracks.
 * Lane mask: bit 0 = lane 1 (far side) ... bit 3 = lane 4 (near side).
 */
USTRUCT(BlueprintType)
struct HEATLINEMX_API FMXSegment
{
	GENERATED_BODY()

	/** Stable identifier (e.g. "T1.O07" for NES courses, "U-12" for user pieces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") FString Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") EMXObstacleType Type = EMXObstacleType::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") int32 LaneMask = 0xF;
	/** Start along the lap (metres from the lap start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") float StartM = 0.f;
	/** Length (metres). Fixed-shape pieces derive it; Grass uses it directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") float LengthM = 0.f;
	/** Composite piece parameters in NES columns (mountain plateaus, platform deck runs, grass run). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") TArray<int32> Runs;
	/** NES main-race-only piece (omitted from the Challenge/qualifier layout). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") bool bMainOnly = false;
	/** Provenance, e.g. "NES T1 col 70 $0D (L)". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segment") FString SourceRef;
};

USTRUCT(BlueprintType)
struct HEATLINEMX_API FMXTrackDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") FString Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") FString Author;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") bool bBuiltIn = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") int32 Laps = 2;
	/** Length of one lap in metres (main layout for NES courses). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") float LapLengthM = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") TArray<FMXSegment> Segments;
	/** 1..5 for NES translations, 0 otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") int32 NesTrack = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") FString SourceNote;
	/** Time-trial medal targets (seconds, whole race, Challenge layout). 0 = derive automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") float MedalGold = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") float MedalSilver = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") float MedalBronze = 0.f;

	bool HasMainOnlyPieces() const
	{
		for (const FMXSegment& S : Segments) { if (S.bMainOnly) { return true; } }
		return false;
	}
	void SortSegments();
	/** Keeps ids unique ("U-<n>" for user pieces without one). */
	void EnsureIds(const FString& Prefix);
	/** Lap length of the requested layout (Challenge removes main-only pieces and shifts the rest). */
	float LapLengthFor(EMXLayoutVariant Variant) const;
};

/** Data Asset wrapper so built-in courses are assembled/edited as regular Unreal assets. */
UCLASS(BlueprintType)
class HEATLINEMX_API UMXCourseAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") FMXTrackDefinition Definition;
	/** Order in course lists / championship. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") int32 SortOrder = 0;
};

namespace MXTrackIO
{
	/** Parses both the manifest schema (heatline.course/1) and saved user tracks (heatline.track/1). */
	HEATLINEMX_API bool FromJsonString(const FString& Json, FMXTrackDefinition& Out, FString& OutError);
	HEATLINEMX_API FString ToJsonString(const FMXTrackDefinition& Def);
	HEATLINEMX_API bool LoadFile(const FString& Path, FMXTrackDefinition& Out, FString& OutError);
	HEATLINEMX_API bool SaveFile(const FString& Path, const FMXTrackDefinition& Def, FString& OutError);
}
