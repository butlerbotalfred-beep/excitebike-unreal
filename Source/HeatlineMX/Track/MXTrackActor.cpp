#include "Track/MXTrackActor.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MXTuning.h"
#include "FX/MXMeshKit.h"
#include "FX/MXMaterials.h"
#include "HeatlineMX.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	enum EProp
	{
		PropBale,
		PropSpectator,
		PropPole,
		PropLightHead,
		PropFlag,
		PropCount
	};

	constexpr float ChunkLength = 40.f;
	constexpr float SampleStep = 0.5f;

	FLinearColor Jitter(const FLinearColor& C, float Amount, int32 A, int32 B)
	{
		const float K = 1.f + (MXMeshKit::Hash01(A, B) - 0.5f) * 2.f * Amount;
		return FLinearColor(C.R * K, C.G * K, C.B * K, C.A);
	}
}

AMXTrack::AMXTrack()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
	Highlight = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Highlight"));
	Highlight->SetMobility(EComponentMobility::Movable);
	Highlight->SetupAttachment(Root);
	Highlight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Highlight->SetCastShadow(false);
}

void AMXTrack::Clear()
{
	for (UStaticMeshComponent* C : Chunks)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	Chunks.Reset();
	for (UStaticMeshComponent* C : Gantries)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	Gantries.Reset();
	for (UHierarchicalInstancedStaticMeshComponent* C : Instanced)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	Instanced.Reset();
	if (Ground)
	{
		Ground->DestroyComponent();
		Ground = nullptr;
	}
	if (Highlight)
	{
		Highlight->ClearAllMeshSections();
	}
}

void AMXTrack::Build(const FMXTrackModel& Model, bool bDressing)
{
	Clear();
	const double StartTime = FPlatformTime::Seconds();
	const float S0 = -Model.PreStartLength();
	const float S1 = Model.TotalLength();
	int32 Index = 0;
	for (float C = S0; C < S1; C += ChunkLength)
	{
		BuildChunk(Model, C, FMath::Min(S1, C + ChunkLength), Index++);
	}

	// Infield ground on both sides with mowing stripes.
	const UMXTrackStyle& Style = MXTuning::Style();
	FMXMeshBuffer G;
	const float HW = Model.TrackHalfWidth();
	const float GA = S0 - 150.f;
	const float GB = S1 + 150.f;
	int32 Stripe = 0;
	for (float S = GA; S < GB; S += 12.f, ++Stripe)
	{
		const float E = FMath::Min(GB, S + 12.f);
		const FLinearColor Near = (Stripe % 2) ? Style.InfieldColor : MXMeshKit::Darken(Style.InfieldColor, 0.82f);
		// Near side (camera side), -0.02 so the track surface always wins.
		G.AddQuad(AMXTrack::ToWorld(S, HW + 0.3f, -0.02f), AMXTrack::ToWorld(E, HW + 0.3f, -0.02f),
			AMXTrack::ToWorld(E, HW + 400.f, -0.02f), AMXTrack::ToWorld(S, HW + 400.f, -0.02f), Near);
		// Far side (under the stands).
		const FLinearColor Far = MXMeshKit::Darken(Style.InfieldColor, (Stripe % 2) ? 0.7f : 0.62f);
		G.AddQuad(AMXTrack::ToWorld(S, -HW - 400.f, -0.02f), AMXTrack::ToWorld(E, -HW - 400.f, -0.02f),
			AMXTrack::ToWorld(E, -HW - 0.3f, -0.02f), AMXTrack::ToWorld(S, -HW - 0.3f, -0.02f), Far);
	}
	FMXMeshSections GroundSections;
	GroundSections.Add(G, MXMaterials::Get(EMXMat::Vertex));
	Ground = AddTrackMesh(TEXT("Ground"), GroundSections);

	BuildGantry(Model, 0.f, true, 0);
	for (int32 Lap = 0; Lap < Model.GetLaps(); ++Lap)
	{
		BuildGantry(Model, Model.LapLineS(Lap), false, Lap + 1);
	}
	if (bDressing)
	{
		BuildDressing(Model);
	}
	UE_LOG(LogHeatline, Log, TEXT("Track built: %d chunks, %.1f m, %.1f ms"), Chunks.Num(), S1 - S0, (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void AMXTrack::BuildChunk(const FMXTrackModel& Model, float C0, float C1, int32 ChunkIndex)
{
	const UMXTrackStyle& Style = MXTuning::Style();
	const float L = Model.LaneWidth;
	const float HW = Model.TrackHalfWidth();

	// ---- Sample positions: regular grid plus every profile breakpoint (sharp ramp edges). ----
	TArray<float> Samples;
	for (float S = C0; S < C1; S += SampleStep)
	{
		Samples.Add(S);
	}
	Samples.Add(C1);
	TArray<const FMXPlacedPiece*> Pieces;
	Model.PiecesInRange(C0 - 0.1f, C1 + 0.1f, Pieces);
	for (const FMXPlacedPiece* P : Pieces)
	{
		for (int32 Lane = 0; Lane < MX::NumLanes; ++Lane)
		{
			for (const FMXProfilePoint& Pt : P->Geo.LaneProfile[Lane])
			{
				const float S = P->S0 + Pt.X;
				if (S > C0 && S < C1)
				{
					Samples.Add(S - 0.01f);
					Samples.Add(S + 0.01f);
				}
			}
		}
		for (const FMXSurfaceSpan& Span : P->Geo.Surfaces)
		{
			for (float X : {Span.X0, Span.X1})
			{
				const float S = P->S0 + X;
				if (S > C0 && S < C1)
				{
					Samples.Add(S + 0.005f);
				}
			}
		}
	}
	Samples.Sort();
	TArray<float> Clean;
	for (float S : Samples)
	{
		if (Clean.Num() == 0 || S - Clean.Last() > 0.004f)
		{
			Clean.Add(S);
		}
	}
	Samples = MoveTemp(Clean);

	// ---- Strips across the track: far verge, 3 sub-strips per lane (edge | rut | edge), near verge. ----
	struct FStrip
	{
		float Y0, Y1;
		int32 Lane;     // profile lane
		bool bVerge;
		bool bRut;
	};
	TArray<FStrip> Strips;
	Strips.Add({-HW, -2.f * L, 0, true, false});
	for (int32 Lane = 0; Lane < MX::NumLanes; ++Lane)
	{
		const float A = -2.f * L + Lane * L;
		Strips.Add({A, A + L * 0.38f, Lane, false, false});
		Strips.Add({A + L * 0.38f, A + L * 0.62f, Lane, false, true});
		Strips.Add({A + L * 0.62f, A + L, Lane, false, false});
	}
	Strips.Add({2.f * L, HW, MX::NumLanes - 1, true, false});

	const int32 NS = Samples.Num();
	TArray<TArray<float>> H;
	H.SetNum(MX::NumLanes);
	for (int32 Lane = 0; Lane < MX::NumLanes; ++Lane)
	{
		H[Lane].SetNum(NS);
		for (int32 i = 0; i < NS; ++i)
		{
			H[Lane][i] = Model.HeightLane(Samples[i], Lane);
		}
	}

	FMXMeshBuffer B;      // lit dirt / ramps / lines / barriers
	FMXMeshBuffer Cool;   // emissive chevrons

	for (int32 k = 0; k < Strips.Num(); ++k)
	{
		const FStrip& St = Strips[k];
		const float YMid = 0.5f * (St.Y0 + St.Y1);
		for (int32 i = 0; i + 1 < NS; ++i)
		{
			const float Sa = Samples[i];
			const float Sb = Samples[i + 1];
			const float Ha = H[St.Lane][i];
			const float Hb = H[St.Lane][i + 1];
			const float SMid = 0.5f * (Sa + Sb);
			const EMXSurface Surf = Model.SurfaceAt(SMid, St.bVerge ? (St.Y0 < 0 ? -HW + 0.1f : HW - 0.1f) : YMid);
			const bool bRaised = FMath::Max(Ha, Hb) > 0.04f;
			FLinearColor Col;
			switch (Surf)
			{
			case EMXSurface::Mud: Col = Style.MudColor; Col.A = 0.2f; break;
			case EMXSurface::Grass: Col = Jitter(Style.GrassColor, 0.12f, FMath::FloorToInt(SMid * 2.f), k); Col.A = 1.f; break;
			case EMXSurface::Verge: Col = Jitter(Style.VergeColor, 0.1f, FMath::FloorToInt(SMid), k); Col.A = 1.f; break;
			default:
				Col = bRaised ? Style.RampColor : ((FMath::FloorToInt(SMid / 6.f) % 2) ? Style.DirtColor : Style.DirtColorAlt);
				Col = Jitter(Col, 0.07f, FMath::FloorToInt(SMid * 2.f), k);
				if (St.bRut && !bRaised)
				{
					Col = MXMeshKit::Darken(Col, 0.8f);
				}
				Col.A = 0.9f;
				break;
			}
			B.AddQuad(AMXTrack::ToWorld(Sa, St.Y0, Ha), AMXTrack::ToWorld(Sb, St.Y0, Hb),
				AMXTrack::ToWorld(Sb, St.Y1, Hb), AMXTrack::ToWorld(Sa, St.Y1, Ha), Col);
		}
	}

	// ---- Side faces: near skirt (faces the camera) and steps between lanes of different height. ----
	const FLinearColor SideCol = MXMeshKit::Darken(Style.RampColor, 0.72f);
	for (int32 i = 0; i + 1 < NS; ++i)
	{
		const float Sa = Samples[i];
		const float Sb = Samples[i + 1];
		const float Ha = H[MX::NumLanes - 1][i];
		const float Hb = H[MX::NumLanes - 1][i + 1];
		if (FMath::Max(Ha, Hb) > 0.02f)
		{
			B.AddQuad(AMXTrack::ToWorld(Sa, HW, Ha), AMXTrack::ToWorld(Sb, HW, Hb), AMXTrack::ToWorld(Sb, HW, 0.f), AMXTrack::ToWorld(Sa, HW, 0.f), SideCol);
		}
		for (int32 Lane = 0; Lane + 1 < MX::NumLanes; ++Lane)
		{
			const float FarA = H[Lane][i], FarB = H[Lane][i + 1];
			const float NearA = H[Lane + 1][i], NearB = H[Lane + 1][i + 1];
			if (FarA > NearA + 0.05f || FarB > NearB + 0.05f)
			{
				const float Y = -2.f * L + (Lane + 1) * L;
				B.AddQuad(AMXTrack::ToWorld(Sa, Y, FarA), AMXTrack::ToWorld(Sb, Y, FarB), AMXTrack::ToWorld(Sb, Y, NearB), AMXTrack::ToWorld(Sa, Y, NearA), SideCol);
			}
		}
	}

	auto HeightAt = [&Model](float S, float Y) { return Model.Height(S, Y) + 0.015f; };

	// ---- Lane lines: dashed between lanes, solid on the track edges. ----
	const FLinearColor LineCol(Style.LaneLineColor.R, Style.LaneLineColor.G, Style.LaneLineColor.B, 0.8f);
	for (int32 Boundary = 0; Boundary <= MX::NumLanes; ++Boundary)
	{
		const float Y = -2.f * L + Boundary * L;
		const bool bEdge = Boundary == 0 || Boundary == MX::NumLanes;
		const float W = bEdge ? 0.12f : 0.08f;
		const float Ys = Boundary == MX::NumLanes ? Y - 0.02f : Y + 0.02f;
		for (float S = FMath::FloorToFloat(C0 / 3.f) * 3.f; S < C1; S += 3.f)
		{
			const float A = FMath::Max(C0, S);
			const float E = FMath::Min(C1, S + (bEdge ? 3.f : 1.6f));
			if (E <= A)
			{
				continue;
			}
			for (float Sa = A; Sa < E - 0.001f; Sa += 0.5f)
			{
				const float Sb = FMath::Min(E, Sa + 0.5f);
				B.AddQuad(AMXTrack::ToWorld(Sa, Ys - W, HeightAt(Sa, Ys)), AMXTrack::ToWorld(Sb, Ys - W, HeightAt(Sb, Ys)),
					AMXTrack::ToWorld(Sb, Ys + W, HeightAt(Sb, Ys)), AMXTrack::ToWorld(Sa, Ys + W, HeightAt(Sa, Ys)), LineCol);
			}
		}
	}

	// ---- Per-piece details: mud puddles, cool chevrons, barriers, finish checkers. ----
	for (const FMXPlacedPiece* P : Pieces)
	{
		if (P->S1 < C0 || P->S0 >= C1)
		{
			continue;
		}
		// Only the chunk that owns the piece start draws the details (no duplicates).
		if (P->S0 < C0 || P->S0 >= C1)
		{
			continue;
		}
		for (const FMXSurfaceSpan& Span : P->Geo.Surfaces)
		{
			for (int32 Lane = 0; Lane < MX::NumLanes; ++Lane)
			{
				if (!MX::LaneInMask(Span.LaneMask, Lane))
				{
					continue;
				}
				const float Yc = Model.LaneCenterY(Lane);
				const float Sa = P->S0 + Span.X0;
				const float Sb = P->S0 + Span.X1;
				if (Span.Surface == EMXSurface::Mud)
				{
					// Glossy puddle blob.
					const int32 Seg = 14;
					const FVector2D C((Sa + Sb) * 0.5f, Yc);
					const FVector2D R((Sb - Sa) * 0.55f + 0.3f, L * 0.46f);
					const float Hc = HeightAt(C.X, C.Y) + 0.01f;
					const FLinearColor MudCol(Style.MudColor.R * 0.8f, Style.MudColor.G * 0.8f, Style.MudColor.B * 0.8f, 0.05f);
					const int32 Center = B.AddVertex(AMXTrack::ToWorld(C.X, C.Y, Hc), FVector::UpVector, FVector2D(0.5f, 0.5f), MudCol);
					TArray<int32> Ring;
					for (int32 s = 0; s <= Seg; ++s)
					{
						const float A = 2.f * PI * s / Seg;
						const float Wob = 0.85f + 0.15f * MXMeshKit::Hash01(s % Seg, Lane + P->SegmentIndex * 7);
						const float Sx = C.X + FMath::Cos(A) * R.X * Wob;
						const float Sy = C.Y + FMath::Sin(A) * R.Y * Wob;
						Ring.Add(B.AddVertex(AMXTrack::ToWorld(Sx, Sy, HeightAt(Sx, Sy) + 0.01f), FVector::UpVector, FVector2D(0, 0), MudCol));
					}
					for (int32 s = 0; s < Seg; ++s)
					{
						B.AddTri(Center, Ring[s], Ring[s + 1]);
					}
				}
				else if (Span.Surface == EMXSurface::Cool)
				{
					// Chevrons pointing down-track (">>"), like the NES cool zone arrows.
					const int32 NumChev = FMath::Max(2, FMath::RoundToInt((Sb - Sa) / 0.9f));
					const float Step = (Sb - Sa) / NumChev;
					const FLinearColor CC = Style.CoolColor;
					for (int32 c = 0; c < NumChev; ++c)
					{
						const float Base = Sa + c * Step + Step * 0.15f;
						const float Tip = Base + Step * 0.6f;
						const float Half = L * 0.36f;
						const float T = 0.22f * Step;
						const float Hz = HeightAt(Base, Yc) + 0.02f;
						// Two slanted bars forming a chevron.
						// Vertex order keeps (B-A)x(C-A) pointing up.
						Cool.AddQuad(AMXTrack::ToWorld(Base, Yc - Half, Hz), AMXTrack::ToWorld(Base + T, Yc - Half, Hz),
							AMXTrack::ToWorld(Tip + T, Yc, Hz), AMXTrack::ToWorld(Tip, Yc, Hz), CC);
						Cool.AddQuad(AMXTrack::ToWorld(Tip, Yc, Hz), AMXTrack::ToWorld(Tip + T, Yc, Hz),
							AMXTrack::ToWorld(Base + T, Yc + Half, Hz), AMXTrack::ToWorld(Base, Yc + Half, Hz), CC);
					}
					// Glowing border plate.
					const float Hz = HeightAt(Sa, Yc) + 0.012f;
					const FLinearColor Plate(CC.R * 0.25f, CC.G * 0.25f, CC.B * 0.3f, 0.3f);
					B.AddQuad(AMXTrack::ToWorld(Sa, Yc - L * 0.47f, Hz), AMXTrack::ToWorld(Sb, Yc - L * 0.47f, Hz),
						AMXTrack::ToWorld(Sb, Yc + L * 0.47f, Hz), AMXTrack::ToWorld(Sa, Yc + L * 0.47f, Hz), Plate);
				}
			}
		}
		for (const FMXBarrierSpec& Bar : P->Geo.Barriers)
		{
			// Low striped log across the covered lanes, on little feet.
			int32 First = -1, Last = -1;
			for (int32 Lane = 0; Lane < MX::NumLanes; ++Lane)
			{
				if (MX::LaneInMask(Bar.LaneMask, Lane))
				{
					First = First < 0 ? Lane : First;
					Last = Lane;
				}
			}
			if (First < 0)
			{
				continue;
			}
			const float S = P->S0 + Bar.X;
			const float Ya = -2.f * L + First * L + 0.15f;
			const float Yb = -2.f * L + (Last + 1) * L - 0.15f;
			const float Z = Bar.Height * 0.55f;
			const float R = Bar.Height * 0.32f;
			int32 Seg = 0;
			for (float Y = Ya; Y < Yb - 0.01f; Y += 0.5f, ++Seg)
			{
				const float Y2 = FMath::Min(Yb, Y + 0.5f);
				const FLinearColor C = (Seg % 2) ? FLinearColor(0.9f, 0.9f, 0.9f, 0.6f) : FLinearColor(0.85f, 0.08f, 0.05f, 0.6f);
				B.AddCylinder(AMXTrack::ToWorld(S, Y, Z), AMXTrack::ToWorld(S, Y2, Z), R * 100.f, R * 100.f, 10, C, Seg == 0 || Y2 >= Yb);
			}
			for (float Y : {Ya + 0.2f, (Ya + Yb) * 0.5f, Yb - 0.2f})
			{
				B.AddBox(AMXTrack::ToWorld(S, Y, Z * 0.5f), FVector(12.f, 8.f, Z * 50.f), FQuat::Identity, FLinearColor(0.2f, 0.2f, 0.22f, 0.8f));
			}
		}
		if (P->Type == EMXObstacleType::FinishDeck && P->Geo.FinishLineX >= 0.f)
		{
			const float S = P->S0 + P->Geo.FinishLineX;
			const int32 Cells = 24;
			const float Y0 = -HW;
			const float Cell = 2.f * HW / Cells;
			for (int32 Row = 0; Row < 2; ++Row)
			{
				for (int32 c = 0; c < Cells; ++c)
				{
					const bool bBlack = (c + Row) % 2 == 0;
					const FLinearColor C = bBlack ? FLinearColor(0.02f, 0.02f, 0.02f, 0.6f) : FLinearColor(0.95f, 0.95f, 0.95f, 0.6f);
					const float Sa = S - 0.5f + Row * 0.5f;
					const float Ya = Y0 + c * Cell;
					const float Hz = Model.HeightLane(S, 1) + 0.02f;
					B.AddQuad(AMXTrack::ToWorld(Sa, Ya, Hz), AMXTrack::ToWorld(Sa + 0.5f, Ya, Hz), AMXTrack::ToWorld(Sa + 0.5f, Ya + Cell, Hz), AMXTrack::ToWorld(Sa, Ya + Cell, Hz), C);
				}
			}
		}
	}
	// Start line (s = 0) and a grid box per starting slot.
	if (0.f >= C0 && 0.f < C1)
	{
		for (int32 c = 0; c < 20; ++c)
		{
			const float Cell = 2.f * 2.f * L / 20.f;
			const float Ya = -2.f * L + c * Cell;
			B.AddQuad(AMXTrack::ToWorld(-0.3f, Ya, 0.02f), AMXTrack::ToWorld(0.3f, Ya, 0.02f), AMXTrack::ToWorld(0.3f, Ya + Cell, 0.02f), AMXTrack::ToWorld(-0.3f, Ya + Cell, 0.02f),
				(c % 2) ? FLinearColor(0.95f, 0.95f, 0.95f, 0.6f) : FLinearColor(0.05f, 0.05f, 0.05f, 0.6f));
		}
	}

	FMXMeshSections Sections;
	Sections.Add(B, MXMaterials::Get(EMXMat::Vertex));
	Sections.Add(Cool, MXMaterials::Get(EMXMat::CoolStrip));
	Chunks.Add(AddTrackMesh(*FString::Printf(TEXT("Chunk_%d"), ChunkIndex), Sections));
}

UStaticMeshComponent* AMXTrack::AddTrackMesh(FName Name, const FMXMeshSections& Sections)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
	C->SetMobility(EComponentMobility::Static);
	C->SetupAttachment(RootComponent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// A Static component's mesh can only be set before it is registered.
	MXMeshKit::ApplyToComponent(C, Sections);
	C->RegisterComponent();
	return C;
}

void AMXTrack::BuildGantry(const FMXTrackModel& Model, float S, bool bStart, int32 Index)
{
	const float HW = Model.TrackHalfWidth();
	const float Base = Model.HeightLane(S, 0);
	FMXMeshBuffer B;
	const FLinearColor Frame(0.12f, 0.12f, 0.14f, 0.5f);
	const float Top = Base + 7.5f;
	for (float Y : {-HW - 1.2f, HW + 1.2f})
	{
		B.AddBox(AMXTrack::ToWorld(S, Y, (Top) * 0.5f), FVector(30.f, 30.f, Top * 50.f), FQuat::Identity, Frame);
	}
	B.AddBox(AMXTrack::ToWorld(S, 0.f, Top), FVector(35.f, (HW + 1.5f) * 100.f, 35.f), FQuat::Identity, Frame);
	// Banner under the crossbar: checkered for lap lines, red/white for the start.
	const int32 Cells = 20;
	const float W = 2.f * (HW + 0.8f) / Cells;
	for (int32 Row = 0; Row < 2; ++Row)
	{
		for (int32 c = 0; c < Cells; ++c)
		{
			FLinearColor C;
			if (bStart)
			{
				C = ((c + Row) % 2) ? FLinearColor(0.9f, 0.05f, 0.05f, 0.5f) : FLinearColor(0.95f, 0.95f, 0.95f, 0.5f);
			}
			else
			{
				C = ((c + Row) % 2) ? FLinearColor(0.02f, 0.02f, 0.02f, 0.5f) : FLinearColor(0.95f, 0.95f, 0.95f, 0.5f);
			}
			const float Ya = -(HW + 0.8f) + c * W;
			const float Za = Top - 0.4f - (Row + 1) * 0.6f;
			// Faces +Y (towards the camera side).
			B.AddQuad(AMXTrack::ToWorld(S + 0.25f, Ya, Za), AMXTrack::ToWorld(S + 0.25f, Ya + W, Za), AMXTrack::ToWorld(S + 0.25f, Ya + W, Za + 0.6f), AMXTrack::ToWorld(S + 0.25f, Ya, Za + 0.6f), C);
			B.AddQuad(AMXTrack::ToWorld(S - 0.25f, Ya + W, Za), AMXTrack::ToWorld(S - 0.25f, Ya, Za), AMXTrack::ToWorld(S - 0.25f, Ya, Za + 0.6f), AMXTrack::ToWorld(S - 0.25f, Ya + W, Za + 0.6f), C);
		}
	}
	FMXMeshSections Sections;
	Sections.Add(B, MXMaterials::Get(EMXMat::Vertex));
	// Light bar on the crossbar.
	FMXMeshBuffer L;
	for (int32 i = -4; i <= 4; ++i)
	{
		L.AddBox(AMXTrack::ToWorld(S + 0.4f, i * 1.6f, Top - 0.1f), FVector(8.f, 30.f, 12.f), FQuat::Identity, FLinearColor::White);
	}
	if (!GantryLightMID)
	{
		GantryLightMID = MXMaterials::MakeEmissive(this, FLinearColor(1.f, 0.85f, 0.55f), 25.f);
	}
	Sections.Add(L, GantryLightMID ? (UMaterialInterface*)GantryLightMID : MXMaterials::Get(EMXMat::Emissive));
	Gantries.Add(AddTrackMesh(*FString::Printf(TEXT("Gantry_%d"), Index), Sections));
}

UStaticMesh* AMXTrack::GetPropMesh(int32 Which)
{
	if (PropMeshes.Num() == PropCount && PropMeshes[Which])
	{
		return PropMeshes[Which];
	}
	PropMeshes.SetNum(PropCount);
	UMaterialInterface* VMat = MXMaterials::Get(EMXMat::Vertex);
	FMXMeshBuffer B;
	switch (Which)
	{
	case PropBale:
	{
		const FLinearColor Straw(0.78f, 0.62f, 0.25f, 1.f);
		B.AddBeveledBox(FVector(0, 0, 28), FVector(60, 35, 28), 8, FQuat::Identity, Straw);
		B.AddBox(FVector(-20, 0, 28), FVector(3, 36, 29), FQuat::Identity, MXMeshKit::Darken(Straw, 0.6f));
		B.AddBox(FVector(20, 0, 28), FVector(3, 36, 29), FQuat::Identity, MXMeshKit::Darken(Straw, 0.6f));
		PropMeshes[Which] = MXMeshKit::BuildStaticMesh(B, VMat, this, TEXT("SM_Bale"));
		break;
	}
	case PropSpectator:
	{
		// Seated/standing fan: torso, head, arms (vertex colour grey; crowd material tints per instance).
		const FLinearColor Shirt(0.8f, 0.8f, 0.8f, 1.f);
		const FLinearColor Skin(0.75f, 0.55f, 0.42f, 1.f);
		B.AddCylinder(FVector(0, 0, 0), FVector(0, 0, 95), 18, 20, 6, MXMeshKit::Darken(Shirt, 0.35f), true);
		B.AddCylinder(FVector(0, 0, 95), FVector(0, 0, 150), 21, 17, 6, Shirt, true);
		B.AddEllipsoid(FVector(0, 0, 168), FVector(11, 11, 13), FQuat::Identity, 6, 4, Skin);
		B.AddCylinder(FVector(0, -20, 145), FVector(0, -26, 175), 5, 5, 4, Shirt, true);
		B.AddCylinder(FVector(0, 20, 145), FVector(0, 26, 175), 5, 5, 4, Shirt, true);
		PropMeshes[Which] = MXMeshKit::BuildStaticMesh(B, MXMaterials::IsProjectMaterial(EMXMat::Crowd) ? MXMaterials::Get(EMXMat::Crowd) : VMat, this, TEXT("SM_Spectator"));
		break;
	}
	case PropPole:
	{
		const FLinearColor Steel(0.35f, 0.36f, 0.4f, 0.5f);
		B.AddCylinder(FVector(0, 0, 0), FVector(0, 0, 2800), 45, 30, 10, Steel, true);
		B.AddBox(FVector(0, 0, 2800), FVector(60, 420, 170), FQuat::Identity, MXMeshKit::Darken(Steel, 0.5f));
		PropMeshes[Which] = MXMeshKit::BuildStaticMesh(B, VMat, this, TEXT("SM_LightPole"));
		break;
	}
	case PropLightHead:
	{
		for (int32 r = 0; r < 3; ++r)
		{
			for (int32 c = 0; c < 6; ++c)
			{
				B.AddBox(FVector(65, -350 + c * 140, 2700 + r * 100), FVector(6, 55, 38), FQuat::Identity, FLinearColor::White);
			}
		}
		if (!LightPanelMID)
		{
			LightPanelMID = MXMaterials::MakeEmissive(this, FLinearColor(1.f, 0.95f, 0.85f), 60.f);
		}
		PropMeshes[Which] = MXMeshKit::BuildStaticMesh(B, LightPanelMID ? (UMaterialInterface*)LightPanelMID : VMat, this, TEXT("SM_LightHead"));
		break;
	}
	case PropFlag:
	{
		B.AddCylinder(FVector(0, 0, 0), FVector(0, 0, 900), 6, 5, 6, FLinearColor(0.8f, 0.8f, 0.82f, 0.4f), true);
		PropMeshes[Which] = MXMeshKit::BuildStaticMesh(B, VMat, this, TEXT("SM_FlagPole"));
		break;
	}
	default:
		break;
	}
	return PropMeshes[Which];
}

UHierarchicalInstancedStaticMeshComponent* AMXTrack::MakeHISM(UStaticMesh* Mesh, FName Name, bool bCastShadow)
{
	UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, Name);
	H->SetupAttachment(RootComponent);
	H->SetStaticMesh(Mesh);
	H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	H->SetCastShadow(bCastShadow);
	H->SetCullDistances(0, 60000);
	H->RegisterComponent();
	Instanced.Add(H);
	return H;
}

void AMXTrack::BuildDressing(const FMXTrackModel& Model)
{
	const UMXTrackStyle& Style = MXTuning::Style();
	const float HW = Model.TrackHalfWidth();
	const float S0 = -Model.PreStartLength() - 60.f;
	const float S1 = Model.TotalLength() + 60.f;

	// Hay bales lining both edges.
	if (UStaticMesh* Bale = GetPropMesh(PropBale))
	{
		UHierarchicalInstancedStaticMeshComponent* H = MakeHISM(Bale, TEXT("Bales"), true);
		TArray<FTransform> Xf;
		for (float S = S0; S < S1; S += 2.6f)
		{
			for (float Y : {-HW - 0.7f, HW + 0.7f})
			{
				const float Z = Model.Height(S, Y > 0 ? HW - 0.2f : -HW + 0.2f);
				Xf.Add(FTransform(FRotator(0, (MXMeshKit::Hash01(FMath::FloorToInt(S * 3), Y > 0) - 0.5f) * 8.f, 0), AMXTrack::ToWorld(S, Y, Z)));
			}
		}
		H->AddInstances(Xf, false);
	}

	// Grandstands on the far side: tiers built as long boxes per 100 m, with a front wall.
	{
		FMXMeshBuffer B;
		const float Front = -HW - 4.f;
		const int32 Tiers = 6;
		const float Depth = 2.1f;
		const float Rise = 1.05f;
		for (float S = S0; S < S1; S += 100.f)
		{
			const float E = FMath::Min(S1, S + 100.f);
			const float Mid = 0.5f * (S + E);
			const float Half = 0.5f * (E - S) * 100.f;
			// Front wall with alternating advertising panels.
			for (float P = S; P < E; P += 10.f)
			{
				const int32 K = FMath::FloorToInt(P / 10.f);
				static const FLinearColor Panels[] = {
					FLinearColor(0.9f, 0.1f, 0.05f, 0.5f), FLinearColor(0.05f, 0.2f, 0.8f, 0.5f), FLinearColor(0.95f, 0.75f, 0.05f, 0.5f),
					FLinearColor(0.95f, 0.95f, 0.95f, 0.5f), FLinearColor(0.1f, 0.6f, 0.2f, 0.5f)
				};
				const FLinearColor C = Panels[((K % 5) + 5) % 5];
				B.AddQuad(AMXTrack::ToWorld(P, Front + 0.05f, 0.f), AMXTrack::ToWorld(FMath::Min(E, P + 10.f), Front + 0.05f, 0.f),
					AMXTrack::ToWorld(FMath::Min(E, P + 10.f), Front + 0.05f, 1.3f), AMXTrack::ToWorld(P, Front + 0.05f, 1.3f), C);
			}
			for (int32 t = 0; t < Tiers; ++t)
			{
				const float Y = Front - Depth * (t + 0.5f);
				const float Z = 1.3f + Rise * t;
				B.AddBox(AMXTrack::ToWorld(Mid, Y, Z * 0.5f), FVector(Half, Depth * 50.f, Z * 50.f), FQuat::Identity,
					(t % 2) ? FLinearColor(0.32f, 0.33f, 0.37f, 0.8f) : FLinearColor(0.27f, 0.28f, 0.31f, 0.8f));
			}
			// Roof canopy.
			const float RoofY = Front - Depth * Tiers * 0.5f;
			B.AddBox(AMXTrack::ToWorld(Mid, RoofY, 1.3f + Rise * Tiers + 5.5f), FVector(Half, Depth * Tiers * 55.f, 20.f), FQuat::Identity, FLinearColor(0.15f, 0.16f, 0.2f, 0.7f));
			B.AddBox(AMXTrack::ToWorld(Mid, Front - Depth * Tiers - 0.3f, (1.3f + Rise * Tiers + 5.5f) * 0.5f), FVector(Half, 30.f, (1.3f + Rise * Tiers + 5.5f) * 50.f), FQuat::Identity, FLinearColor(0.2f, 0.2f, 0.24f, 0.7f));
		}
		FMXMeshSections StandSections;
		StandSections.Add(B, MXMaterials::Get(EMXMat::Vertex));
		Chunks.Add(AddTrackMesh(TEXT("Stands"), StandSections));

		// Crowd.
		if (UStaticMesh* Spec = GetPropMesh(PropSpectator))
		{
			UHierarchicalInstancedStaticMeshComponent* H = MakeHISM(Spec, TEXT("Crowd"), false);
			H->SetNumCustomDataFloats(4);
			const float Density = FMath::Clamp((float)Style.CrowdDensity / 3.f, 0.2f, 1.5f);
			int32 N = 0;
			for (int32 t = 0; t < Tiers; ++t)
			{
				const float Y = Front - Depth * (t + 0.55f);
				const float Z = 1.3f + Rise * t;
				for (float S = S0; S < S1; S += 0.8f / Density)
				{
					const int32 Key = FMath::FloorToInt(S * 10.f);
					if (MXMeshKit::Hash01(Key, t) < 0.18f)
					{
						continue;
					}
					const float Jx = (MXMeshKit::Hash01(Key, t + 11) - 0.5f) * 0.4f;
					const float Scale = 0.9f + MXMeshKit::Hash01(Key, t + 23) * 0.2f;
					const FTransform X(FRotator(0, -90.f + (MXMeshKit::Hash01(Key, t + 31) - 0.5f) * 40.f, 0), AMXTrack::ToWorld(S + Jx, Y, Z), FVector(Scale));
					const int32 I = H->AddInstance(X, false);
					const float Hue = MXMeshKit::Hash01(Key, t + 41);
					const FLinearColor Shirt = FLinearColor::MakeFromHSV8((uint8)(Hue * 255.f), 170, 220);
					H->SetCustomDataValue(I, 0, Shirt.R, false);
					H->SetCustomDataValue(I, 1, Shirt.G, false);
					H->SetCustomDataValue(I, 2, Shirt.B, false);
					H->SetCustomDataValue(I, 3, MXMeshKit::Hash01(Key, t + 53), false);
					++N;
				}
			}
			H->MarkRenderStateDirty();
			UE_LOG(LogHeatline, Log, TEXT("Crowd: %d spectators"), N);
		}
	}

	// Floodlight towers behind the stands.
	if (UStaticMesh* Pole = GetPropMesh(PropPole))
	{
		UHierarchicalInstancedStaticMeshComponent* HP = MakeHISM(Pole, TEXT("LightPoles"), true);
		UHierarchicalInstancedStaticMeshComponent* HL = MakeHISM(GetPropMesh(PropLightHead), TEXT("LightHeads"), false);
		for (float S = S0 + 40.f; S < S1; S += 110.f)
		{
			const FTransform X(FRotator(0.f, 0.f, 0.f), AMXTrack::ToWorld(S, -HW - 22.f, 0.f));
			HP->AddInstance(X, false);
			HL->AddInstance(X, false);
		}
		HP->MarkRenderStateDirty();
		HL->MarkRenderStateDirty();
	}
}

void AMXTrack::SetHighlight(float S0, float S1, int32 LaneMask)
{
	Highlight->ClearAllMeshSections();
	if (S0 < 0.f || S1 <= S0)
	{
		return;
	}
	FMXMeshBuffer B;
	const UMXTrackStyle& Style = MXTuning::Style();
	const float L = Style.LaneWidth;
	for (int32 Lane = 0; Lane < MX::NumLanes; ++Lane)
	{
		if (!MX::LaneInMask(LaneMask, Lane))
		{
			continue;
		}
		const float Ya = -2.f * L + Lane * L + 0.1f;
		const float Yb = Ya + L - 0.2f;
		// Glowing frame posts at the corners (visible from the designer camera).
		for (float S : {S0, S1})
		{
			for (float Y : {Ya, Yb})
			{
				B.AddBox(AMXTrack::ToWorld(S, Y, 1.5f), FVector(8.f, 8.f, 150.f), FQuat::Identity, FLinearColor::White);
			}
		}
		B.AddBox(AMXTrack::ToWorld((S0 + S1) * 0.5f, Ya, 3.f), FVector((S1 - S0) * 50.f, 5.f, 5.f), FQuat::Identity, FLinearColor::White);
		B.AddBox(AMXTrack::ToWorld((S0 + S1) * 0.5f, Yb, 3.f), FVector((S1 - S0) * 50.f, 5.f, 5.f), FQuat::Identity, FLinearColor::White);
	}
	UMaterialInstanceDynamic* MID = MXMaterials::MakeEmissive(this, FLinearColor(1.f, 0.8f, 0.1f), 8.f);
	B.ToSection(Highlight, 0, MID ? (UMaterialInterface*)MID : MXMaterials::Get(EMXMat::Vertex));
}
