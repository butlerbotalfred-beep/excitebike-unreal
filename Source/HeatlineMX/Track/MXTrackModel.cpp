#include "Track/MXTrackModel.h"
#include "Core/MXTuning.h"
#include "HeatlineMX.h"

void FMXTrackModel::Build(const FMXTrackDefinition& InDef, EMXLayoutVariant InVariant, int32 InLaps, const UMXTrackStyle& Style)
{
	Def = InDef;
	Def.SortSegments();
	Variant = InVariant;
	Laps = FMath::Clamp(InLaps > 0 ? InLaps : Def.Laps, 1, 9);
	LaneWidth = Style.LaneWidth;
	VergeWidth = Style.VergeWidth;
	PreStart = Style.StartGridLength + 16.f;
	Pieces.Reset();
	Gates.Reset();
	MaxPieceLength = 0.f;
	bHasFinishDeck = false;

	// Resolve local placements for the chosen layout. The Challenge (qualifier) layout removes
	// main-only pieces and shifts everything after them back by their width, as the NES stream does.
	struct FLocal
	{
		int32 SegIndex;
		float Start;
		FMXPieceGeometry Geo;
	};
	TArray<FLocal> Local;
	float Removed = 0.f;
	for (int32 i = 0; i < Def.Segments.Num(); ++i)
	{
		const FMXSegment& Seg = Def.Segments[i];
		if (Seg.Type == EMXObstacleType::None)
		{
			continue;
		}
		if (Seg.bMainOnly && Variant == EMXLayoutVariant::Challenge)
		{
			Removed += Seg.LengthM > 0.f ? Seg.LengthM : FMXObstacleLibrary::ComputeLength(Seg.Type, Seg.Runs, 0.f, Style);
			continue;
		}
		FLocal L;
		L.SegIndex = i;
		L.Start = Seg.StartM - Removed;
		L.Geo = FMXObstacleLibrary::Build(Seg, Style);
		Local.Add(MoveTemp(L));
	}
	LapLength = FMath::Max(50.f, Def.LapLengthM - Removed);

	FinishLineLocal = LapLength;
	for (const FLocal& L : Local)
	{
		if (Def.Segments[L.SegIndex].Type == EMXObstacleType::FinishDeck && L.Geo.FinishLineX >= 0.f)
		{
			FinishLineLocal = L.Start + L.Geo.FinishLineX;
			bHasFinishDeck = true;
		}
	}

	for (int32 Lap = 0; Lap < Laps; ++Lap)
	{
		const float Base = Lap * LapLength;
		for (const FLocal& L : Local)
		{
			const FMXSegment& Seg = Def.Segments[L.SegIndex];
			// After the final lap line only the finish deck itself remains; the rest is run-out.
			if (Lap == Laps - 1 && L.Start > FinishLineLocal + 1.f)
			{
				continue;
			}
			FMXPlacedPiece P;
			P.SegmentIndex = L.SegIndex;
			P.Id = Laps > 1 ? FString::Printf(TEXT("%s@L%d"), *Seg.Id, Lap + 1) : Seg.Id;
			P.Type = Seg.Type;
			P.LaneMask = Seg.LaneMask;
			P.Lap = Lap;
			P.S0 = Base + L.Start;
			P.S1 = P.S0 + L.Geo.Length;
			P.Geo = L.Geo;
			MaxPieceLength = FMath::Max(MaxPieceLength, L.Geo.Length);
			Pieces.Add(MoveTemp(P));
		}
	}
	Pieces.StableSort([](const FMXPlacedPiece& A, const FMXPlacedPiece& B) { return A.S0 < B.S0; });
	TotalLen = RaceFinishS() + Style.RunOutLength;

	// Ordered gates: each lap is split into roughly CheckpointSpacing chunks ending on its lap line.
	float LapStart = 0.f;
	for (int32 Lap = 0; Lap < Laps; ++Lap)
	{
		const float Line = LapLineS(Lap);
		const float Span = FMath::Max(10.f, Line - LapStart);
		const int32 N = FMath::Max(2, FMath::RoundToInt(Span / FMath::Max(10.f, Style.CheckpointSpacing)));
		for (int32 g = 1; g <= N; ++g)
		{
			FMXGate Gate;
			Gate.S = LapStart + Span * (float)g / (float)N;
			Gate.Lap = Lap;
			Gate.bLapLine = (g == N);
			if (Gate.bLapLine)
			{
				Gate.S = Line;
			}
			Gates.Add(Gate);
		}
		LapStart = Line;
	}
	bBuilt = true;
}

float FMXTrackModel::LaneCenterY(int32 Lane) const
{
	return (-1.5f + (float)FMath::Clamp(Lane, 0, MX::NumLanes - 1)) * LaneWidth;
}

int32 FMXTrackModel::LaneIndexAt(float Y) const
{
	const float Edge = LanesHalfWidth();
	if (Y < -Edge)
	{
		return -1;
	}
	if (Y >= Edge)
	{
		return MX::NumLanes;
	}
	return FMath::Clamp(FMath::FloorToInt((Y + Edge) / LaneWidth), 0, MX::NumLanes - 1);
}

int32 FMXTrackModel::NearestLane(float Y) const
{
	return FMath::Clamp(LaneIndexAt(Y), 0, MX::NumLanes - 1);
}

int32 FMXTrackModel::LowerBoundPiece(float S) const
{
	// First piece whose S0 > S - MaxPieceLength (candidates that may overlap S).
	const float Key = S - MaxPieceLength - 0.01f;
	int32 Lo = 0;
	int32 Hi = Pieces.Num();
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Pieces[Mid].S0 < Key)
		{
			Lo = Mid + 1;
		}
		else
		{
			Hi = Mid;
		}
	}
	return Lo;
}

float FMXTrackModel::HeightLane(float S, int32 Lane) const
{
	float H = 0.f;
	for (int32 i = LowerBoundPiece(S); i < Pieces.Num(); ++i)
	{
		const FMXPlacedPiece& P = Pieces[i];
		if (P.S0 > S)
		{
			break;
		}
		if (S < P.S1)
		{
			H = FMath::Max(H, P.Geo.HeightAt(Lane, S - P.S0));
		}
	}
	return H;
}

float FMXTrackModel::Height(float S, float Y) const
{
	return HeightLane(S, NearestLane(Y));
}

float FMXTrackModel::SlopeDeg(float S, float Y) const
{
	const int32 Lane = NearestLane(Y);
	float BestH = 0.f;
	float Slope = 0.f;
	for (int32 i = LowerBoundPiece(S); i < Pieces.Num(); ++i)
	{
		const FMXPlacedPiece& P = Pieces[i];
		if (P.S0 > S)
		{
			break;
		}
		if (S < P.S1)
		{
			const float H = P.Geo.HeightAt(Lane, S - P.S0);
			if (H >= BestH)
			{
				BestH = H;
				Slope = P.Geo.SlopeDegAt(Lane, S - P.S0);
			}
		}
	}
	return Slope;
}

EMXSurface FMXTrackModel::SurfaceAt(float S, float Y) const
{
	const int32 LaneIdx = LaneIndexAt(Y);
	const bool bVerge = LaneIdx < 0 || LaneIdx >= MX::NumLanes;
	const int32 Lane = FMath::Clamp(LaneIdx, 0, MX::NumLanes - 1);
	EMXSurface Result = bVerge ? EMXSurface::Verge : EMXSurface::Dirt;
	bool bRaised = false;
	for (int32 i = LowerBoundPiece(S); i < Pieces.Num(); ++i)
	{
		const FMXPlacedPiece& P = Pieces[i];
		if (P.S0 > S)
		{
			break;
		}
		if (S >= P.S1)
		{
			continue;
		}
		const float X = S - P.S0;
		if (P.Geo.HasProfile(Lane) && P.Geo.HeightAt(Lane, X) > 0.05f)
		{
			bRaised = true;
		}
		if (!bVerge)
		{
			for (const FMXSurfaceSpan& Span : P.Geo.Surfaces)
			{
				if (X >= Span.X0 && X < Span.X1 && MX::LaneInMask(Span.LaneMask, Lane))
				{
					// Cool strips win over mud/grass if pieces ever overlap.
					if (Result != EMXSurface::Cool)
					{
						Result = Span.Surface;
					}
				}
			}
		}
	}
	if (bRaised && (Result == EMXSurface::Verge || Result == EMXSurface::Dirt))
	{
		return EMXSurface::Deck;
	}
	return Result;
}

bool FMXTrackModel::FindBarrierCrossing(float SFrom, float STo, float Y, FMXBarrierHit& Out) const
{
	if (STo <= SFrom)
	{
		return false;
	}
	const int32 LaneIdx = LaneIndexAt(Y);
	if (LaneIdx < 0 || LaneIdx >= MX::NumLanes)
	{
		return false;
	}
	for (int32 i = LowerBoundPiece(SFrom); i < Pieces.Num(); ++i)
	{
		const FMXPlacedPiece& P = Pieces[i];
		if (P.S0 > STo)
		{
			break;
		}
		for (const FMXBarrierSpec& B : P.Geo.Barriers)
		{
			const float BS = P.S0 + B.X;
			if (BS > SFrom && BS <= STo && MX::LaneInMask(B.LaneMask, LaneIdx))
			{
				Out.Piece = &P;
				Out.S = BS;
				Out.LaneMask = B.LaneMask;
				Out.Height = B.Height;
				return true;
			}
		}
	}
	return false;
}

bool FMXTrackModel::IsFlatSafe(float S, float Y, float HalfSpan) const
{
	for (int32 i = LowerBoundPiece(S - HalfSpan); i < Pieces.Num(); ++i)
	{
		const FMXPlacedPiece& P = Pieces[i];
		if (P.S0 > S + HalfSpan)
		{
			break;
		}
		if (P.S1 < S - HalfSpan)
		{
			continue;
		}
		// Any raised geometry, barrier, or non-dirt surface in the rider's lane nearby is unsafe.
		if (P.Geo.MaxHeight > 0.02f || P.Geo.Barriers.Num() > 0)
		{
			return false;
		}
		const int32 Lane = NearestLane(Y);
		for (const FMXSurfaceSpan& Span : P.Geo.Surfaces)
		{
			const float A = P.S0 + Span.X0;
			const float B = P.S0 + Span.X1;
			if (B >= S - HalfSpan && A <= S + HalfSpan && MX::LaneInMask(Span.LaneMask, Lane) && Span.Surface != EMXSurface::Cool)
			{
				return false;
			}
		}
	}
	return true;
}

float FMXTrackModel::FindRecoveryS(float CrashS, float Y, float MaxBack) const
{
	const float MinS = -PreStart + 4.f;
	for (float Back = 0.f; Back <= MaxBack; Back += 0.5f)
	{
		const float S = CrashS - Back;
		if (S < MinS)
		{
			break;
		}
		if (IsFlatSafe(S, Y, 3.f))
		{
			return S;
		}
	}
	// Fallback: in front of whatever piece we crashed on, never beyond the crash point.
	if (const FMXPlacedPiece* P = PieceAt(CrashS))
	{
		return FMath::Max(MinS, FMath::Min(CrashS, P->S0 - 3.f));
	}
	return FMath::Max(MinS, CrashS);
}

int32 FMXTrackModel::LapOfS(float S) const
{
	int32 Count = 0;
	for (int32 Lap = 0; Lap < Laps; ++Lap)
	{
		if (S >= LapLineS(Lap))
		{
			++Count;
		}
	}
	return FMath::Min(Count, Laps - 1);
}

void FMXTrackModel::PiecesInRange(float S0, float S1, TArray<const FMXPlacedPiece*>& Out) const
{
	Out.Reset();
	for (int32 i = LowerBoundPiece(S0); i < Pieces.Num(); ++i)
	{
		const FMXPlacedPiece& P = Pieces[i];
		if (P.S0 > S1)
		{
			break;
		}
		if (P.S1 >= S0)
		{
			Out.Add(&P);
		}
	}
}

const FMXPlacedPiece* FMXTrackModel::PieceAt(float S) const
{
	for (int32 i = LowerBoundPiece(S); i < Pieces.Num(); ++i)
	{
		const FMXPlacedPiece& P = Pieces[i];
		if (P.S0 > S)
		{
			break;
		}
		if (S < P.S1)
		{
			return &P;
		}
	}
	return nullptr;
}
