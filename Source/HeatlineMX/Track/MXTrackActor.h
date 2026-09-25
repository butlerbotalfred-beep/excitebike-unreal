#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Track/MXTrackModel.h"
#include "MXTrackActor.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;

/**
 * Builds the visible course from an FMXTrackModel: dirt surface with ramps (per-lane heights),
 * lane lines, mud, grass, animated cool strips, barriers, finish decks and gantries, hay bales,
 * grandstands with an instanced crowd, and floodlight towers. Chunked for per-view culling.
 */
UCLASS()
class HEATLINEMX_API AMXTrack : public AActor
{
	GENERATED_BODY()

public:
	AMXTrack();

	/** Rebuilds all geometry. bDressing=false skips stands/crowd (designer preview). */
	void Build(const FMXTrackModel& Model, bool bDressing = true);
	void Clear();

	/** Highlights a lap-local range (designer selection), <0 hides. */
	void SetHighlight(float S0, float S1, int32 LaneMask);

	static FVector ToWorld(float S, float Y, float Z) { return FVector(S * 100.0, Y * 100.0, Z * 100.0); }

private:
	void BuildChunk(const FMXTrackModel& Model, float C0, float C1, int32 ChunkIndex);
	void BuildDressing(const FMXTrackModel& Model);
	void BuildGantry(const FMXTrackModel& Model, float S, bool bStart, int32 Index);
	/** Builds the sections into a static-mobility mesh component under the root (the track never moves). */
	UStaticMeshComponent* AddTrackMesh(FName Name, const struct FMXMeshSections& Sections);
	UStaticMesh* GetPropMesh(int32 Which);
	UHierarchicalInstancedStaticMeshComponent* MakeHISM(UStaticMesh* Mesh, FName Name, bool bCastShadow);

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Chunks;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Gantries;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Instanced;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ground;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Highlight;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> PropMeshes;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LightPanelMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GantryLightMID;
};
