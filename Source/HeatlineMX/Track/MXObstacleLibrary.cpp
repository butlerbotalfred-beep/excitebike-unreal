#include "Track/MXObstacleLibrary.h"
#include "Core/MXTuning.h"

namespace
{
	struct FColRow
	{
		float C;
		float R;
	};

	// Height profiles in NES units (columns, rows), taken from the decoded piece geometry
	// (docs/TRACK_MANIFEST.md, "Obstacle vocabulary") and smoothed into straight faces.
	const TArray<FColRow>& ProfileRampSmall() { static TArray<FColRow> P = {{0, 0}, {1.5f, 2}, {3, 0}}; return P; }
	const TArray<FColRow>& ProfileRampMedium() { static TArray<FColRow> P = {{0, 0}, {2.5f, 3}, {5, 0}}; return P; }
	const TArray<FColRow>& ProfileRampLarge() { static TArray<FColRow> P = {{0, 0}, {4.5f, 5}, {9, 0}}; return P; }
	const TArray<FColRow>& ProfileTableLow() { static TArray<FColRow> P = {{0, 0}, {2, 2}, {7, 2}, {9, 0}}; return P; }
	const TArray<FColRow>& ProfileRampSteep() { static TArray<FColRow> P = {{0, 0}, {2.5f, 5}, {5, 0}}; return P; }
	const TArray<FColRow>& ProfileRampSteepBack() { static TArray<FColRow> P = {{0, 0}, {3.8f, 4}, {6, 0}}; return P; }
	const TArray<FColRow>& ProfileRampSteepFace() { static TArray<FColRow> P = {{0, 0}, {1.8f, 4}, {6, 0}}; return P; }
	const TArray<FColRow>& ProfileKicker() { static TArray<FColRow> P = {{0, 0}, {2, 2.2f}, {2, 0}}; return P; }
	const TArray<FColRow>& ProfileFinish() { static TArray<FColRow> P = {{0, 0}, {2.5f, 3}, {9.5f, 3}, {12, 0}}; return P; }

	TArray<FMXProfilePoint> ToMetres(const TArray<FColRow>& In, float Mpc, float Mpr)
	{
		TArray<FMXProfilePoint> Out;
		Out.Reserve(In.Num());
		for (const FColRow& P : In)
		{
			Out.Add({P.C * Mpc, P.R * Mpr});
		}
		return Out;
	}

	int32 RunOr(const TArray<int32>& Runs, int32 Index, int32 Default)
	{
		return Runs.IsValidIndex(Index) ? FMath::Clamp(Runs[Index], 1, 60) : Default;
	}

	TMap<EMXObstacleType, FMXObstacleInfo> BuildInfos()
	{
		TMap<EMXObstacleType, FMXObstacleInfo> M;
		auto Add = [&M](EMXObstacleType T, const TCHAR* Letter, const TCHAR* Desc, float Cols, int32 Mask,
			TArray<int32> Options, bool bRamp, FLinearColor Col, TArray<int32> Runs = {}, bool bVar = false, bool bPlace = true)
		{
			FMXObstacleInfo I;
			I.Type = T;
			I.Letter = Letter;
			I.Name = MX::ObstacleName(T);
			I.Description = Desc;
			I.DefaultCols = Cols;
			I.DefaultLaneMask = Mask;
			I.LaneOptions = MoveTemp(Options);
			I.bRamp = bRamp;
			I.UiColor = Col;
			I.DefaultRuns = MoveTemp(Runs);
			I.bVariableLength = bVar;
			I.bDesignerPlaceable = bPlace;
			M.Add(T, I);
		};
		const FLinearColor RampCol(0.85f, 0.55f, 0.25f);
		Add(EMXObstacleType::RampSmall, TEXT("A"), TEXT("Short kicker, 1.2 m tall. Chain them into rhythm sections."), 3, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::RampMedium, TEXT("B"), TEXT("Medium triangle ramp, 1.8 m."), 5, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::RampLarge, TEXT("C"), TEXT("Large triangle ramp, 3 m. Big air."), 9, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::TableLow, TEXT("D"), TEXT("Low table-top: case it or clear it."), 9, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::RampSteep, TEXT("E"), TEXT("Steep, tall ramp with a sharp lip."), 5, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::RampSteepBack, TEXT("F"), TEXT("Long take-off, steep landing side."), 6, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::RampSteepFace, TEXT("G"), TEXT("Steep take-off, long landing side."), 6, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::Kicker, TEXT("H"), TEXT("Jump ramp with a sheer drop behind it."), 2, MX::AllLanes, {MX::AllLanes}, true, RampCol);
		Add(EMXObstacleType::Barrier, TEXT("I/J"), TEXT("Low barrier: wheelie over it or slow down."), 1, MX::MaskFromLanes({1, 2}),
			{MX::MaskFromLanes({1, 2}), MX::MaskFromLanes({3, 4}), MX::MaskFromLanes({2, 3}), MX::AllLanes}, false, FLinearColor(0.95f, 0.95f, 0.95f));
		Add(EMXObstacleType::Mud, TEXT("K/L"), TEXT("Mud: heavy slowdown, worse with turbo."), 3, MX::MaskFromLanes({1, 3}),
			{MX::MaskFromLanes({1, 3}), MX::MaskFromLanes({2, 4}), MX::MaskFromLanes({1, 2}), MX::MaskFromLanes({3, 4}), MX::MaskFromLanes({1}), MX::MaskFromLanes({2}), MX::MaskFromLanes({3}), MX::MaskFromLanes({4})},
			false, FLinearColor(0.35f, 0.22f, 0.1f));
		Add(EMXObstacleType::CoolStrip, TEXT("M/N"), TEXT("Cool strip: resets engine heat if you ride over it."), 2, MX::MaskFromLanes({1}),
			{MX::MaskFromLanes({1}), MX::MaskFromLanes({4}), MX::MaskFromLanes({2}), MX::MaskFromLanes({3})}, false, FLinearColor(0.2f, 0.7f, 1.f));
		Add(EMXObstacleType::Grass, TEXT("O/P/Q"), TEXT("Rough ground (track missing): speed cap, no turbo."), 12, MX::AllLanes,
			{MX::AllLanes, MX::MaskFromLanes({1, 2}), MX::MaskFromLanes({3, 4}), MX::MaskFromLanes({2, 3})}, false, FLinearColor(0.3f, 0.7f, 0.2f), {10}, true);
		Add(EMXObstacleType::Mountain, TEXT("R"), TEXT("Two-step mesa: climb, plateau, big drop."), 26, MX::AllLanes, {MX::AllLanes}, true, RampCol, {6, 6});
		Add(EMXObstacleType::PlatformDeck, TEXT("S"), TEXT("Ramp to a high deck over lanes 1-2; lanes 3-4 drop into mud."), 27, MX::AllLanes, {MX::AllLanes}, true, RampCol, {3, 4, 4});
		Add(EMXObstacleType::FinishDeck, TEXT("FIN"), TEXT("Finish deck. The lap line is on top."), 12, MX::AllLanes, {MX::AllLanes}, true, FLinearColor(1.f, 1.f, 1.f), {}, false, false);
		return M;
	}
}

float FMXPieceGeometry::HeightAt(int32 Lane, float X) const
{
	Lane = FMath::Clamp(Lane, 0, MX::NumLanes - 1);
	const TArray<FMXProfilePoint>& P = LaneProfile[Lane];
	if (P.Num() < 2 || X < 0.f || X >= Length)
	{
		return 0.f;
	}
	// Find the segment [i, i+1] with P[i].X <= X < P[i+1].X (skipping vertical faces).
	for (int32 i = 0; i + 1 < P.Num(); ++i)
	{
		const FMXProfilePoint& A = P[i];
		const FMXProfilePoint& B = P[i + 1];
		if (B.X <= A.X)
		{
			continue;
		}
		if (X >= A.X && X < B.X)
		{
			const float T = (X - A.X) / (B.X - A.X);
			return FMath::Lerp(A.H, B.H, T);
		}
	}
	// Past the last point: use the last height (profiles end at 0 normally).
	return X >= P.Last().X ? P.Last().H : P[0].H;
}

float FMXPieceGeometry::SlopeDegAt(int32 Lane, float X) const
{
	Lane = FMath::Clamp(Lane, 0, MX::NumLanes - 1);
	const TArray<FMXProfilePoint>& P = LaneProfile[Lane];
	if (P.Num() < 2 || X < 0.f || X >= Length)
	{
		return 0.f;
	}
	for (int32 i = 0; i + 1 < P.Num(); ++i)
	{
		const FMXProfilePoint& A = P[i];
		const FMXProfilePoint& B = P[i + 1];
		if (B.X <= A.X)
		{
			continue;
		}
		if (X >= A.X && X < B.X)
		{
			return FMath::RadiansToDegrees(FMath::Atan2(B.H - A.H, B.X - A.X));
		}
	}
	return 0.f;
}

const FMXObstacleInfo& FMXObstacleLibrary::Info(EMXObstacleType Type)
{
	static const TMap<EMXObstacleType, FMXObstacleInfo> Infos = BuildInfos();
	static const FMXObstacleInfo Empty;
	const FMXObstacleInfo* Found = Infos.Find(Type);
	return Found ? *Found : Empty;
}

const TArray<EMXObstacleType>& FMXObstacleLibrary::DesignerPalette()
{
	static const TArray<EMXObstacleType> Palette = {
		EMXObstacleType::RampSmall, EMXObstacleType::RampMedium, EMXObstacleType::RampLarge, EMXObstacleType::TableLow,
		EMXObstacleType::RampSteep, EMXObstacleType::RampSteepBack, EMXObstacleType::RampSteepFace, EMXObstacleType::Kicker,
		EMXObstacleType::Barrier, EMXObstacleType::Mud, EMXObstacleType::CoolStrip, EMXObstacleType::Grass,
		EMXObstacleType::Mountain, EMXObstacleType::PlatformDeck
	};
	return Palette;
}

float FMXObstacleLibrary::ComputeLength(EMXObstacleType Type, const TArray<int32>& Runs, float GrassLengthM, const UMXTrackStyle& Style)
{
	const float Mpc = Style.MetersPerColumn;
	switch (Type)
	{
	case EMXObstacleType::Grass:
		if (GrassLengthM > 0.f)
		{
			return GrassLengthM;
		}
		return (RunOr(Runs, 0, 10) + 2) * Mpc;
	case EMXObstacleType::Mountain:
		return (2 + RunOr(Runs, 0, 6) + 2 + RunOr(Runs, 1, 6) + 10) * Mpc;
	case EMXObstacleType::PlatformDeck:
		return (8 + RunOr(Runs, 0, 3) + 3 + RunOr(Runs, 1, 4) + 3 + RunOr(Runs, 2, 4) + 1 + 1) * Mpc;
	default:
		return Info(Type).DefaultCols * Mpc;
	}
}

FMXSegment FMXObstacleLibrary::MakeDefault(EMXObstacleType Type, float StartM, const UMXTrackStyle& Style)
{
	const FMXObstacleInfo& I = Info(Type);
	FMXSegment S;
	S.Type = Type;
	S.StartM = StartM;
	S.LaneMask = I.DefaultLaneMask;
	S.Runs = I.DefaultRuns;
	S.LengthM = ComputeLength(Type, S.Runs, 0.f, Style);
	return S;
}

FString FMXObstacleLibrary::LaneMaskLabel(int32 Mask)
{
	FString Out;
	for (int32 L = 0; L < MX::NumLanes; ++L)
	{
		if (MX::LaneInMask(Mask, L))
		{
			if (!Out.IsEmpty())
			{
				Out += TEXT(",");
			}
			Out += FString::FromInt(L + 1);
		}
	}
	return Mask == MX::AllLanes ? TEXT("all") : Out;
}

FMXPieceGeometry FMXObstacleLibrary::Build(const FMXSegment& Seg, const UMXTrackStyle& Style)
{
	const float Mpc = Style.MetersPerColumn;
	const float Mpr = Style.MetersPerRow;
	FMXPieceGeometry G;
	G.Length = Seg.LengthM > 0.f ? Seg.LengthM : ComputeLength(Seg.Type, Seg.Runs, 0.f, Style);

	auto SetAllLanes = [&G](const TArray<FMXProfilePoint>& P)
	{
		for (int32 L = 0; L < MX::NumLanes; ++L)
		{
			G.LaneProfile[L] = P;
		}
	};
	auto ScaleToLength = [&G](TArray<FMXProfilePoint>& P)
	{
		// Fixed shapes: stretch to the stored length (lets the designer/scale differ without breaking shape).
		if (P.Num() >= 2 && P.Last().X > KINDA_SMALL_NUMBER)
		{
			const float K = G.Length / P.Last().X;
			for (FMXProfilePoint& Pt : P)
			{
				Pt.X *= K;
			}
		}
	};

	switch (Seg.Type)
	{
	case EMXObstacleType::RampSmall:
	case EMXObstacleType::RampMedium:
	case EMXObstacleType::RampLarge:
	case EMXObstacleType::TableLow:
	case EMXObstacleType::RampSteep:
	case EMXObstacleType::RampSteepBack:
	case EMXObstacleType::RampSteepFace:
	case EMXObstacleType::Kicker:
	case EMXObstacleType::FinishDeck:
	{
		const TArray<FColRow>* Src = nullptr;
		switch (Seg.Type)
		{
		case EMXObstacleType::RampSmall: Src = &ProfileRampSmall(); break;
		case EMXObstacleType::RampMedium: Src = &ProfileRampMedium(); break;
		case EMXObstacleType::RampLarge: Src = &ProfileRampLarge(); break;
		case EMXObstacleType::TableLow: Src = &ProfileTableLow(); break;
		case EMXObstacleType::RampSteep: Src = &ProfileRampSteep(); break;
		case EMXObstacleType::RampSteepBack: Src = &ProfileRampSteepBack(); break;
		case EMXObstacleType::RampSteepFace: Src = &ProfileRampSteepFace(); break;
		case EMXObstacleType::Kicker: Src = &ProfileKicker(); break;
		default: Src = &ProfileFinish(); break;
		}
		TArray<FMXProfilePoint> P = ToMetres(*Src, Mpc, Mpr);
		ScaleToLength(P);
		SetAllLanes(P);
		if (Seg.Type == EMXObstacleType::FinishDeck)
		{
			G.FinishLineX = G.Length * 0.5f;
		}
		break;
	}
	case EMXObstacleType::Barrier:
	{
		G.Barriers.Add({0.5f * G.Length, Seg.LaneMask, MXTuning::Bike().BarrierHeight});
		break;
	}
	case EMXObstacleType::Mud:
		G.Surfaces.Add({0.f, G.Length, EMXSurface::Mud, Seg.LaneMask});
		break;
	case EMXObstacleType::CoolStrip:
		G.Surfaces.Add({0.f, G.Length, EMXSurface::Cool, Seg.LaneMask});
		break;
	case EMXObstacleType::Grass:
		G.Surfaces.Add({0.f, G.Length, EMXSurface::Grass, Seg.LaneMask});
		break;
	case EMXObstacleType::Mountain:
	{
		const float A = (float)RunOr(Seg.Runs, 0, 6);
		const float B = (float)RunOr(Seg.Runs, 1, 6);
		const float D0 = 2.f + A + 2.f + B;
		const TArray<FColRow> Cr = {
			{0, 0}, {1.5f, 2}, {2.f + A, 2}, {2.f + A + 1.6f, 6}, {D0 + 2.5f, 6}, {D0 + 6.5f, 2}, {D0 + 8.5f, 2}, {D0 + 10.f, 0}
		};
		TArray<FMXProfilePoint> P = ToMetres(Cr, Mpc, Mpr);
		ScaleToLength(P);
		SetAllLanes(P);
		break;
	}
	case EMXObstacleType::PlatformDeck:
	{
		const float R1 = (float)RunOr(Seg.Runs, 0, 3);
		const float R2 = (float)RunOr(Seg.Runs, 1, 4);
		const float R3 = (float)RunOr(Seg.Runs, 2, 4);
		const float Total = 8.f + R1 + 3.f + R2 + 3.f + R3 + 1.f + 1.f;
		const float DeckEnd = Total - 1.f;
		// Lanes 1-2: full-width ramp up to the deck, deck continues, sheer drop at the end.
		const TArray<FColRow> Upper = {{0, 0}, {3.f, 6}, {DeckEnd, 6}, {DeckEnd, 0}, {Total, 0}};
		// Lanes 3-4: same ramp, short top, slope back down to ground level (mud under the deck).
		const TArray<FColRow> Lower = {{0, 0}, {3.f, 6}, {5.f, 6}, {8.f, 0}, {Total, 0}};
		TArray<FMXProfilePoint> PU = ToMetres(Upper, Mpc, Mpr);
		TArray<FMXProfilePoint> PL = ToMetres(Lower, Mpc, Mpr);
		ScaleToLength(PU);
		ScaleToLength(PL);
		G.LaneProfile[0] = PU;
		G.LaneProfile[1] = PU;
		G.LaneProfile[2] = PL;
		G.LaneProfile[3] = PL;
		const float K = G.Length / (Total * Mpc);
		const float Mud1 = (8.f + R1) * Mpc * K;
		const float Mud2 = (8.f + R1 + 3.f + R2) * Mpc * K;
		const float MudLen = 3.f * Mpc * K;
		G.Surfaces.Add({Mud1, Mud1 + MudLen, EMXSurface::Mud, MX::MaskFromLanes({3, 4})});
		G.Surfaces.Add({Mud2, Mud2 + MudLen, EMXSurface::Mud, MX::MaskFromLanes({3, 4})});
		break;
	}
	default:
		break;
	}

	for (int32 L = 0; L < MX::NumLanes; ++L)
	{
		for (const FMXProfilePoint& P : G.LaneProfile[L])
		{
			G.MaxHeight = FMath::Max(G.MaxHeight, P.H);
		}
	}
	return G;
}
