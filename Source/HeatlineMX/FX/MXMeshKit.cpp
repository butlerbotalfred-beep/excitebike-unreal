#include "FX/MXMeshKit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// If faces render inside-out on a new engine version, flip this one switch.
	constexpr bool bFlipWinding = false;
}

void FMXMeshBuffer::Reset()
{
	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	UVs.Reset();
	Colors.Reset();
	Tangents.Reset();
}

int32 FMXMeshBuffer::AddVertex(const FVector& P, const FVector& N, const FVector2D& UV, const FLinearColor& C)
{
	const int32 I = Vertices.Add(P);
	Normals.Add(N.GetSafeNormal());
	UVs.Add(UV);
	Colors.Add(C);
	// Tangent perpendicular to the normal (good enough for flat-shaded content).
	FVector T = FVector::CrossProduct(FVector::UpVector, N);
	if (T.SizeSquared() < 1e-4)
	{
		T = FVector::CrossProduct(FVector::ForwardVector, N);
	}
	Tangents.Add(FProcMeshTangent(T.GetSafeNormal(), false));
	return I;
}

void FMXMeshBuffer::AddTri(int32 A, int32 B, int32 C)
{
	if (bFlipWinding)
	{
		Triangles.Add(A);
		Triangles.Add(B);
		Triangles.Add(C);
	}
	else
	{
		// Callers pass A,B,C with (B-A)x(C-A) = outward normal. Unreal's own box generator
		// (UKismetProceduralMeshLibrary::GenerateBoxMesh) winds front faces the opposite way, so emit A,C,B.
		Triangles.Add(A);
		Triangles.Add(C);
		Triangles.Add(B);
	}
}

void FMXMeshBuffer::AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FLinearColor& Col, const FVector2D& UVScale)
{
	const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
	const int32 I0 = AddVertex(A, N, FVector2D(0, 0) * UVScale, Col);
	const int32 I1 = AddVertex(B, N, FVector2D(1, 0) * UVScale, Col);
	const int32 I2 = AddVertex(C, N, FVector2D(1, 1) * UVScale, Col);
	const int32 I3 = AddVertex(D, N, FVector2D(0, 1) * UVScale, Col);
	AddTri(I0, I1, I2);
	AddTri(I0, I2, I3);
}

void FMXMeshBuffer::AddBox(const FVector& Center, const FVector& H, const FQuat& Rot, const FLinearColor& Col)
{
	auto P = [&](float X, float Y, float Z) { return Center + Rot.RotateVector(FVector(X * H.X, Y * H.Y, Z * H.Z)); };
	// Faces listed counter-clockwise when seen from outside (right-handed sense) -> outward normals.
	AddQuad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Col);      // top (+Z)
	AddQuad(P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), P(-1, -1, -1), Col);  // bottom
	AddQuad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), Col);      // +X
	AddQuad(P(-1, 1, -1), P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), Col);  // -X
	AddQuad(P(1, 1, -1), P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), Col);      // +Y
	AddQuad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), Col);  // -Y
}

void FMXMeshBuffer::AddBeveledBox(const FVector& Center, const FVector& H, float Bevel, const FQuat& Rot, const FLinearColor& Col)
{
	// Octagonal prism along Z: chamfer the four vertical edges, then cap.
	const float Bx = FMath::Min(Bevel, H.X * 0.49f);
	const float By = FMath::Min(Bevel, H.Y * 0.49f);
	TArray<FVector2D> Ring = {
		{H.X, -H.Y + By}, {H.X, H.Y - By}, {H.X - Bx, H.Y}, {-H.X + Bx, H.Y},
		{-H.X, H.Y - By}, {-H.X, -H.Y + By}, {-H.X + Bx, -H.Y}, {H.X - Bx, -H.Y}
	};
	auto P = [&](const FVector2D& XY, float Z) { return Center + Rot.RotateVector(FVector(XY.X, XY.Y, Z)); };
	const int32 N = Ring.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Ring[i];
		const FVector2D& B = Ring[(i + 1) % N];
		AddQuad(P(A, -H.Z), P(B, -H.Z), P(B, H.Z), P(A, H.Z), Col);
	}
	// Caps (fan).
	const FVector Up = Rot.RotateVector(FVector::UpVector);
	const int32 CTop = AddVertex(Center + Up * H.Z, Up, FVector2D(0.5f, 0.5f), Col);
	const int32 CBot = AddVertex(Center - Up * H.Z, -Up, FVector2D(0.5f, 0.5f), Col);
	TArray<int32> Top, Bot;
	for (int32 i = 0; i < N; ++i)
	{
		Top.Add(AddVertex(P(Ring[i], H.Z), Up, FVector2D(0, 0), Col));
		Bot.Add(AddVertex(P(Ring[i], -H.Z), -Up, FVector2D(0, 0), Col));
	}
	for (int32 i = 0; i < N; ++i)
	{
		AddTri(CTop, Top[i], Top[(i + 1) % N]);
		AddTri(CBot, Bot[(i + 1) % N], Bot[i]);
	}
}

void FMXMeshBuffer::AddCylinder(const FVector& A, const FVector& B, float RA, float RB, int32 Segments, const FLinearColor& Col, bool bCapped)
{
	const FVector Axis = (B - A).GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		return;
	}
	FVector U = FVector::CrossProduct(Axis, FMath::Abs(Axis.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	FVector V = FVector::CrossProduct(Axis, U);
	Segments = FMath::Max(3, Segments);
	TArray<int32> RingA, RingB;
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float Ang = 2.f * PI * i / Segments;
		const FVector Dir = U * FMath::Cos(Ang) + V * FMath::Sin(Ang);
		const float U01 = (float)i / Segments;
		RingA.Add(AddVertex(A + Dir * RA, Dir, FVector2D(U01, 0), Col));
		RingB.Add(AddVertex(B + Dir * RB, Dir, FVector2D(U01, 1), Col));
	}
	for (int32 i = 0; i < Segments; ++i)
	{
		AddTri(RingA[i], RingA[i + 1], RingB[i + 1]);
		AddTri(RingA[i], RingB[i + 1], RingB[i]);
	}
	if (bCapped)
	{
		const int32 CA = AddVertex(A, -Axis, FVector2D(0.5f, 0.5f), Col);
		const int32 CB = AddVertex(B, Axis, FVector2D(0.5f, 0.5f), Col);
		TArray<int32> CapA, CapB;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float Ang = 2.f * PI * i / Segments;
			const FVector Dir = U * FMath::Cos(Ang) + V * FMath::Sin(Ang);
			CapA.Add(AddVertex(A + Dir * RA, -Axis, FVector2D(0, 0), Col));
			CapB.Add(AddVertex(B + Dir * RB, Axis, FVector2D(0, 0), Col));
		}
		for (int32 i = 0; i < Segments; ++i)
		{
			AddTri(CA, CapA[i + 1], CapA[i]);
			AddTri(CB, CapB[i], CapB[i + 1]);
		}
	}
}

void FMXMeshBuffer::AddTorus(const FVector& Center, const FVector& InAxis, float MajorR, float MinorR, int32 Segs, int32 Sides, const FLinearColor& Col, float KnobAmount)
{
	const FVector Axis = InAxis.GetSafeNormal();
	const FVector U = FVector::CrossProduct(Axis, FMath::Abs(Axis.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	const FVector V = FVector::CrossProduct(Axis, U);
	TArray<TArray<int32>> Grid;
	Grid.SetNum(Segs + 1);
	for (int32 i = 0; i <= Segs; ++i)
	{
		const float A = 2.f * PI * i / Segs;
		const FVector Radial = U * FMath::Cos(A) + V * FMath::Sin(A);
		const FVector RingCenter = Center + Radial * MajorR;
		for (int32 j = 0; j <= Sides; ++j)
		{
			const float B = 2.f * PI * j / Sides;
			const FVector N = Radial * FMath::Cos(B) + Axis * FMath::Sin(B);
			// Knobbly tread: bumps on the outer face, alternating around the circumference.
			float R = MinorR;
			if (KnobAmount > 0.f && FMath::Cos(B) > 0.3f && (i % 2 == 0))
			{
				R += KnobAmount * FMath::Cos(B);
			}
			Grid[i].Add(AddVertex(RingCenter + N * R, N, FVector2D((float)i / Segs, (float)j / Sides), Col));
		}
	}
	for (int32 i = 0; i < Segs; ++i)
	{
		for (int32 j = 0; j < Sides; ++j)
		{
			AddTri(Grid[i][j], Grid[i + 1][j], Grid[i + 1][j + 1]);
			AddTri(Grid[i][j], Grid[i + 1][j + 1], Grid[i][j + 1]);
		}
	}
}

void FMXMeshBuffer::AddEllipsoid(const FVector& Center, const FVector& Radii, const FQuat& Rot, int32 Segs, int32 Rings, const FLinearColor& Col)
{
	TArray<TArray<int32>> Grid;
	Grid.SetNum(Rings + 1);
	for (int32 r = 0; r <= Rings; ++r)
	{
		const float Phi = PI * r / Rings; // 0 top .. PI bottom
		for (int32 s = 0; s <= Segs; ++s)
		{
			const float Theta = 2.f * PI * s / Segs;
			const FVector Unit(FMath::Sin(Phi) * FMath::Cos(Theta), FMath::Sin(Phi) * FMath::Sin(Theta), FMath::Cos(Phi));
			const FVector Local = Unit * Radii;
			const FVector N = Rot.RotateVector(FVector(Unit.X / FMath::Max(0.01f, Radii.X), Unit.Y / FMath::Max(0.01f, Radii.Y), Unit.Z / FMath::Max(0.01f, Radii.Z)).GetSafeNormal());
			Grid[r].Add(AddVertex(Center + Rot.RotateVector(Local), N, FVector2D((float)s / Segs, (float)r / Rings), Col));
		}
	}
	for (int32 r = 0; r < Rings; ++r)
	{
		for (int32 s = 0; s < Segs; ++s)
		{
			AddTri(Grid[r][s], Grid[r + 1][s], Grid[r + 1][s + 1]);
			AddTri(Grid[r][s], Grid[r + 1][s + 1], Grid[r][s + 1]);
		}
	}
}

void FMXMeshBuffer::AddTube(const TArray<FVector>& Path, float Radius, int32 Segments, const FLinearColor& Col)
{
	for (int32 i = 0; i + 1 < Path.Num(); ++i)
	{
		AddCylinder(Path[i], Path[i + 1], Radius, Radius, Segments, Col, i == 0 || i + 2 == Path.Num());
		if (i > 0)
		{
			AddEllipsoid(Path[i], FVector(Radius), FQuat::Identity, Segments, FMath::Max(3, Segments / 2), Col);
		}
	}
}

void FMXMeshBuffer::AddExtrudedPolygon(const TArray<FVector2D>& Poly, float Y0, float Y1, const FTransform& Xf, const FLinearColor& Col)
{
	const int32 N = Poly.Num();
	if (N < 3)
	{
		return;
	}
	auto P = [&](const FVector2D& XZ, float Y) { return Xf.TransformPosition(FVector(XZ.X, Y, XZ.Y)); };
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Poly[i];
		const FVector2D& B = Poly[(i + 1) % N];
		AddQuad(P(A, Y1), P(B, Y1), P(B, Y0), P(A, Y0), Col);
	}
	// Caps via ear-less fan from the centroid (polygons used here are convex or star-shaped).
	FVector2D Centroid(0, 0);
	for (const FVector2D& V : Poly)
	{
		Centroid += V;
	}
	Centroid /= (float)N;
	const FVector NY1 = Xf.TransformVectorNoScale(FVector(0, 1, 0));
	const FVector NY0 = -NY1;
	const int32 C1 = AddVertex(P(Centroid, Y1), NY1, FVector2D(0.5f, 0.5f), Col);
	const int32 C0 = AddVertex(P(Centroid, Y0), NY0, FVector2D(0.5f, 0.5f), Col);
	TArray<int32> R1, R0;
	for (int32 i = 0; i < N; ++i)
	{
		R1.Add(AddVertex(P(Poly[i], Y1), NY1, FVector2D(0, 0), Col));
		R0.Add(AddVertex(P(Poly[i], Y0), NY0, FVector2D(0, 0), Col));
	}
	for (int32 i = 0; i < N; ++i)
	{
		AddTri(C1, R1[(i + 1) % N], R1[i]);
		AddTri(C0, R0[i], R0[(i + 1) % N]);
	}
}

void FMXMeshBuffer::Append(const FMXMeshBuffer& O, const FTransform& Xf)
{
	const int32 Base = Vertices.Num();
	for (int32 i = 0; i < O.Vertices.Num(); ++i)
	{
		Vertices.Add(Xf.TransformPosition(O.Vertices[i]));
		Normals.Add(Xf.TransformVectorNoScale(O.Normals[i]));
		UVs.Add(O.UVs[i]);
		Colors.Add(O.Colors[i]);
		Tangents.Add(FProcMeshTangent(Xf.TransformVectorNoScale(O.Tangents[i].TangentX), false));
	}
	for (int32 T : O.Triangles)
	{
		Triangles.Add(Base + T);
	}
}

void FMXMeshBuffer::ToSection(UProceduralMeshComponent* PMC, int32 Section, UMaterialInterface* Mat) const
{
	if (!PMC)
	{
		return;
	}
	PMC->CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	if (Mat)
	{
		PMC->SetMaterial(Section, Mat);
	}
}

namespace MXMeshKit
{
	FLinearColor Darken(const FLinearColor& C, float K)
	{
		return FLinearColor(C.R * K, C.G * K, C.B * K, C.A);
	}

	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return FLinearColor::LerpUsingHSV(A, B, T);
	}

	UStaticMesh* BuildStaticMesh(const FMXMeshBuffer& Buf, UMaterialInterface* Mat, UObject* Outer, FName Name)
	{
		FMXMeshSections S;
		S.Add(Buf, Mat);
		return BuildStaticMesh(S, Outer, Name);
	}

	UStaticMesh* BuildStaticMesh(const FMXMeshSections& Sections, UObject* Outer, FName Name)
	{
		if (Sections.IsEmpty())
		{
			return nullptr;
		}
		FMeshDescription MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();

		TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attr.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);

		int32 NumVerts = 0;
		int32 NumTris = 0;
		for (const FMXMeshSections::FSection& Sec : Sections.Sections)
		{
			NumVerts += Sec.Buffer.Vertices.Num();
			NumTris += Sec.Buffer.Triangles.Num() / 3;
		}
		MD.ReserveNewVertices(NumVerts);
		MD.ReserveNewVertexInstances(NumVerts);
		MD.ReserveNewTriangles(NumTris);

		UStaticMesh* SM = NewObject<UStaticMesh>(Outer, Name);
		TArray<FVertexInstanceID> Instances;
		for (int32 SecIndex = 0; SecIndex < Sections.Sections.Num(); ++SecIndex)
		{
			const FMXMeshSections::FSection& Sec = Sections.Sections[SecIndex];
			const FMXMeshBuffer& Buf = Sec.Buffer;
			const FName Slot(*FString::Printf(TEXT("Mat%d"), SecIndex));
			const FPolygonGroupID PG = MD.CreatePolygonGroup();
			Attr.GetPolygonGroupMaterialSlotNames()[PG] = Slot;
			FStaticMaterial StaticMat(Sec.Material, Slot);
			// Runtime meshes skip the editor's UV density pass; without this, texture streaming hits an ensure
			// in packaged builds. The materials are untextured, so any density will do.
			StaticMat.UVChannelData = FMeshUVChannelInfo(1.f);
			SM->GetStaticMaterials().Add(StaticMat);

			Instances.Reset(Buf.Vertices.Num());
			for (int32 i = 0; i < Buf.Vertices.Num(); ++i)
			{
				const FVertexID V = MD.CreateVertex();
				Positions[V] = FVector3f(Buf.Vertices[i]);
				const FVertexInstanceID VI = MD.CreateVertexInstance(V);
				Normals[VI] = FVector3f(Buf.Normals[i]);
				Tangents[VI] = FVector3f(Buf.Tangents[i].TangentX);
				Signs[VI] = 1.f;
				// The static mesh builder sRGB-encodes vertex colours into bytes, but the materials read vertex
				// colour bytes as-is (as the procedural mesh path stored them). Pre-linearise so the stored bytes
				// equal the intended values.
				const FLinearColor Pre = FLinearColor::FromSRGBColor(Buf.Colors[i].ToFColor(false));
				Colors[VI] = FVector4f(Pre.R, Pre.G, Pre.B, Buf.Colors[i].A);
				UVs.Set(VI, 0, FVector2f(Buf.UVs[i]));
				Instances.Add(VI);
			}
			for (int32 t = 0; t + 2 < Buf.Triangles.Num(); t += 3)
			{
				TArray<FVertexInstanceID, TInlineAllocator<3>> Tri;
				Tri.Add(Instances[Buf.Triangles[t]]);
				Tri.Add(Instances[Buf.Triangles[t + 1]]);
				Tri.Add(Instances[Buf.Triangles[t + 2]]);
				MD.CreateTriangle(PG, Tri);
			}
		}

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		TArray<const FMeshDescription*> Descs;
		Descs.Add(&MD);
		SM->BuildFromMeshDescriptions(Descs, Params);
		return SM;
	}

	void ApplyToComponent(UStaticMeshComponent* Comp, const FMXMeshSections& Sections)
	{
		if (!Comp)
		{
			return;
		}
		UStaticMesh* SM = BuildStaticMesh(Sections, Comp, MakeUniqueObjectName(Comp, UStaticMesh::StaticClass(), TEXT("SM")));
		Comp->SetStaticMesh(SM);
	}
}
