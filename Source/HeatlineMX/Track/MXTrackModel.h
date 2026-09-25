#pragma once

#include "CoreMinimal.h"
#include "Core/MXTypes.h"
#include "Track/MXTrackTypes.h"
#include "Track/MXObstacleLibrary.h"

class UMXTrackStyle;

/** A piece placed on the unrolled race course (all laps). World s = metres from the start line. */
struct HEATLINEMX_API FMXPlacedPiece
{
	int32 SegmentIndex = INDEX_NONE;
	FString Id;
	EMXObstacleType Type = EMXObstacleType::None;
	int32 LaneMask = MX::AllLanes;
	int32 Lap = 0;
	float S0 = 0.f;
	float S1 = 0.f;
	FMXPieceGeometry Geo;
};

/** Ordered checkpoint gate. The last gate of each lap is the lap line; the final one is the finish. */
struct HEATLINEMX_API FMXGate
{
	float S = 0.f;
	int32 Lap = 0;
	bool bLapLine = false;
};

struct FMXBarrierHit
{
	const FMXPlacedPiece* Piece = nullptr;
	float S = 0.f;
	int32 LaneMask = 0;
	float Height = 0.f;
};

/**
 * Physics truth for a course: analytic ground height / slope / surface per lane, barriers, gates.
 * The track mesh is generated from the same data, so visuals and physics always agree.
 *
 * Coordinates: s along the track (+X in the world), y across (+Y = near side / lane 4), z up. Metres.
 */
class HEATLINEMX_API FMXTrackModel
{
public:
	void Build(const FMXTrackDefinition& InDef, EMXLayoutVariant InVariant, int32 InLaps, const UMXTrackStyle& Style);
	bool IsBuilt() const { return bBuilt; }

	// ----- lateral layout -----
	float LaneCenterY(int32 Lane) const;
	/** -1 = far verge, 0..3 = lanes, 4 = near verge. */
	int32 LaneIndexAt(float Y) const;
	int32 NearestLane(float Y) const;
	float TrackHalfWidth() const { return 2.f * LaneWidth + VergeWidth; }
	float LanesHalfWidth() const { return 2.f * LaneWidth; }

	// ----- ground -----
	float Height(float S, float Y) const;
	float HeightLane(float S, int32 Lane) const;
	float SlopeDeg(float S, float Y) const;
	EMXSurface SurfaceAt(float S, float Y) const;
	bool FindBarrierCrossing(float SFrom, float STo, float Y, FMXBarrierHit& Out) const;
	bool IsFlatSafe(float S, float Y, float HalfSpan) const;
	/** Safe flat ground at or BEFORE CrashS (never after): recovery must not gain progress. */
	float FindRecoveryS(float CrashS, float Y, float MaxBack) const;

	// ----- race layout -----
	float GetLapLength() const { return LapLength; }
	int32 GetLaps() const { return Laps; }
	float GetFinishLineLocal() const { return FinishLineLocal; }
	float LapLineS(int32 LapIndex) const { return LapIndex * LapLength + FinishLineLocal; }
	float RaceFinishS() const { return LapLineS(Laps - 1); }
	float TotalLength() const { return TotalLen; }
	float PreStartLength() const { return PreStart; }
	/** Number of lap lines at or behind S (0-based current lap, clamped). */
	int32 LapOfS(float S) const;
	const TArray<FMXGate>& GetGates() const { return Gates; }
	const TArray<FMXPlacedPiece>& GetPieces() const { return Pieces; }
	void PiecesInRange(float S0, float S1, TArray<const FMXPlacedPiece*>& Out) const;
	const FMXPlacedPiece* PieceAt(float S) const;
	bool HasFinishDeck() const { return bHasFinishDeck; }

	const FMXTrackDefinition& GetDefinition() const { return Def; }
	EMXLayoutVariant GetVariant() const { return Variant; }

	float LaneWidth = 3.2f;
	float VergeWidth = 2.2f;

private:
	int32 LowerBoundPiece(float S) const;

	FMXTrackDefinition Def;
	EMXLayoutVariant Variant = EMXLayoutVariant::Main;
	TArray<FMXPlacedPiece> Pieces;
	TArray<FMXGate> Gates;
	float LapLength = 600.f;
	float FinishLineLocal = 600.f;
	float TotalLen = 0.f;
	float PreStart = 40.f;
	float MaxPieceLength = 0.f;
	int32 Laps = 2;
	bool bHasFinishDeck = false;
	bool bBuilt = false;
};
