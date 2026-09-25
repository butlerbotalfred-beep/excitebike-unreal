#pragma once

#include "CoreMinimal.h"
#include "Track/MXTrackTypes.h"

class UMXTrackStyle;

enum class EMXIssueSeverity : uint8
{
	Info,
	Warning,
	Error
};

struct HEATLINEMX_API FMXTrackIssue
{
	EMXIssueSeverity Severity = EMXIssueSeverity::Info;
	FString Message;
	FString SegmentId;
	float S = -1.f;
};

struct HEATLINEMX_API FMXValidationResult
{
	TArray<FMXTrackIssue> Issues;
	bool HasErrors() const;
	int32 Count(EMXIssueSeverity Sev) const;
	FString Summary() const;
};

class HEATLINEMX_API FMXTrackValidator
{
public:
	static constexpr float MinLapLength = 150.f;
	static constexpr float MaxLapLength = 1500.f;
	static constexpr float StartZone = 12.f;

	/** Static checks: boundaries, overlaps, finish/checkpoint setup, blocked landings, heat. */
	static FMXValidationResult Validate(const FMXTrackDefinition& Def, const UMXTrackStyle& Style);

	/**
	 * Drive-test: runs the real bike simulation with several autopilot strategies over one lap and
	 * reports sections where every strategy crashes or stalls (impossible / disconnected sections).
	 */
	static void DriveTest(const FMXTrackDefinition& Def, FMXValidationResult& InOut);

	/**
	 * Race time of the reference rider (Hard AI skill, no random mistakes, turbo managed, lane 2 start)
	 * over this layout and number of laps, using the real bike simulation. Medal targets are derived from
	 * it, so they match the layout actually raced. Returns < 0 if the reference rider can't finish.
	 */
	static float ReferenceTime(const FMXTrackDefinition& Def, EMXLayoutVariant Variant, int32 Laps);

	/** Adds or moves the finish deck so it closes the lap. Returns true if the definition changed. */
	static bool EnsureFinishDeck(FMXTrackDefinition& Def, const UMXTrackStyle& Style);
};
