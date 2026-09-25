#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

/**
 * Tiny procedural geometry kit. All content in Heatline MX (bikes, riders, track, dressing) is built
 * from code so the project needs no imported art. Colours live in vertex colours.
 */
struct HEATLINEMX_API FMXMeshBuffer
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	int32 Num() const { return Vertices.Num(); }
	void Reset();
	bool IsEmpty() const { return Triangles.Num() == 0; }

	int32 AddVertex(const FVector& P, const FVector& N, const FVector2D& UV, const FLinearColor& C);
	/** Triangle with outward normal (B-A)x(C-A). */
	void AddTri(int32 A, int32 B, int32 C);
	/** Quad A,B,C,D in order around the face; flat shaded with the given normal. */
	void AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FLinearColor& Col, const FVector2D& UVScale = FVector2D(1, 1));

	void AddBox(const FVector& Center, const FVector& HalfExtents, const FQuat& Rot, const FLinearColor& Col);
	/** Rounded-ish box: box with chamfered vertical edges (cheap bevel). */
	void AddBeveledBox(const FVector& Center, const FVector& HalfExtents, float Bevel, const FQuat& Rot, const FLinearColor& Col);
	void AddCylinder(const FVector& A, const FVector& B, float RadiusA, float RadiusB, int32 Segments, const FLinearColor& Col, bool bCapped = true);
	void AddTorus(const FVector& Center, const FVector& Axis, float MajorR, float MinorR, int32 Segs, int32 Sides, const FLinearColor& Col, float KnobAmount = 0.f);
	void AddEllipsoid(const FVector& Center, const FVector& Radii, const FQuat& Rot, int32 Segs, int32 Rings, const FLinearColor& Col);
	void AddTube(const TArray<FVector>& Path, float Radius, int32 Segments, const FLinearColor& Col);
	/** Extrudes a closed 2D polygon (in the XZ plane, counter-clockwise) along Y between Y0 and Y1. */
	void AddExtrudedPolygon(const TArray<FVector2D>& PolyXZ, float Y0, float Y1, const FTransform& Xf, const FLinearColor& Col);
	void Append(const FMXMeshBuffer& Other, const FTransform& Xf);

	/** Pushes the buffer into a section of a procedural mesh component (no collision). */
	void ToSection(UProceduralMeshComponent* PMC, int32 Section, UMaterialInterface* Mat) const;
};

class UStaticMesh;
class UStaticMeshComponent;

/** Mesh sections (one material slot each) collected before building a static mesh. */
struct HEATLINEMX_API FMXMeshSections
{
	struct FSection
	{
		FMXMeshBuffer Buffer;
		UMaterialInterface* Material = nullptr;
	};
	TArray<FSection> Sections;

	void Add(const FMXMeshBuffer& Buf, UMaterialInterface* Mat)
	{
		if (!Buf.IsEmpty())
		{
			Sections.Add({Buf, Mat});
		}
	}
	bool IsEmpty() const { return Sections.Num() == 0; }
};

namespace MXMeshKit
{
	/** Colour helpers. */
	HEATLINEMX_API FLinearColor Darken(const FLinearColor& C, float K);
	HEATLINEMX_API FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T);

	/** Builds a real UStaticMesh at runtime (used for instanced props: crowd, bales, lights). */
	HEATLINEMX_API UStaticMesh* BuildStaticMesh(const FMXMeshBuffer& Buf, UMaterialInterface* Mat, UObject* Outer, FName Name);
	/** Builds a UStaticMesh with one material slot per section. */
	HEATLINEMX_API UStaticMesh* BuildStaticMesh(const FMXMeshSections& Sections, UObject* Outer, FName Name);
	/**
	 * Builds the sections into a static mesh owned by the component and assigns it. Static meshes keep cached
	 * draw commands, so they cost far less render-thread time per view than procedural mesh sections.
	 */
	HEATLINEMX_API void ApplyToComponent(UStaticMeshComponent* Comp, const FMXMeshSections& Sections);

	/** Cheap deterministic hash noise in [0,1). */
	inline float Hash01(int32 A, int32 B = 0)
	{
		uint32 H = (uint32)A * 374761393u + (uint32)B * 668265263u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return (float)((H ^ (H >> 16)) & 0xFFFFFF) / 16777216.f;
	}
}
