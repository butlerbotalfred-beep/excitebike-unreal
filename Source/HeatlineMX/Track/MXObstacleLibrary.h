#pragma once

#include "CoreMinimal.h"
#include "Core/MXTypes.h"
#include "Track/MXTrackTypes.h"

class UMXTrackStyle;

/** Point on a height profile in piece-local metres. Two points with equal X form a vertical face. */
struct FMXProfilePoint
{
	float X = 0.f;
	float H = 0.f;
};

struct FMXSurfaceSpan
{
	float X0 = 0.f;
	float X1 = 0.f;
	EMXSurface Surface = EMXSurface::Dirt;
	int32 LaneMask = MX::AllLanes;
};

struct FMXBarrierSpec
{
	float X = 0.f;
	int32 LaneMask = 0;
	float Height = 0.45f;
};

/** Fully resolved geometry of one piece. Lanes with an empty profile are flat ground. */
struct HEATLINEMX_API FMXPieceGeometry
{
	float Length = 0.f;
	TArray<FMXProfilePoint> LaneProfile[MX::NumLanes];
	TArray<FMXSurfaceSpan> Surfaces;
	TArray<FMXBarrierSpec> Barriers;
	/** Piece-local X of the lap/finish line (FinishDeck), else -1. */
	float FinishLineX = -1.f;
	/** Tallest point of the piece (for culling / visuals). */
	float MaxHeight = 0.f;

	/** Height of lane (0..3) at local X. Outside [0,Length] returns 0. */
	float HeightAt(int32 Lane, float X) const;
	/** Surface slope in degrees (positive = uphill in the direction of travel). */
	float SlopeDegAt(int32 Lane, float X) const;
	bool HasProfile(int32 Lane) const { return LaneProfile[Lane].Num() >= 2; }
};

struct HEATLINEMX_API FMXObstacleInfo
{
	EMXObstacleType Type = EMXObstacleType::None;
	FString Letter;
	FString Name;
	FString Description;
	/** Default length in NES columns (composites: computed from runs). */
	float DefaultCols = 0.f;
	int32 DefaultLaneMask = MX::AllLanes;
	/** Lane coverage options offered in the Track Designer. */
	TArray<int32> LaneOptions;
	TArray<int32> DefaultRuns;
	bool bRamp = false;
	bool bVariableLength = false;
	bool bDesignerPlaceable = true;
	FLinearColor UiColor = FLinearColor::White;
};

class HEATLINEMX_API FMXObstacleLibrary
{
public:
	static const FMXObstacleInfo& Info(EMXObstacleType Type);
	static const TArray<EMXObstacleType>& DesignerPalette();

	/** Builds geometry for a placed segment with the given scale. */
	static FMXPieceGeometry Build(const FMXSegment& Seg, const UMXTrackStyle& Style);

	/** Length in metres a segment of this type/runs occupies at the given scale. */
	static float ComputeLength(EMXObstacleType Type, const TArray<int32>& Runs, float GrassLengthM, const UMXTrackStyle& Style);

	/** Creates a new segment with defaults (designer). */
	static FMXSegment MakeDefault(EMXObstacleType Type, float StartM, const UMXTrackStyle& Style);

	static bool IsRamp(EMXObstacleType Type) { return Info(Type).bRamp; }
	static FString LaneMaskLabel(int32 Mask);
};
