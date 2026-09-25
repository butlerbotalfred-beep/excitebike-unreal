#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

enum class EMXMat : uint8
{
	Vertex,        // lit, base colour = vertex colour, roughness from vertex alpha
	VertexGloss,   // lit, glossy (bike plastics / paint), metallic from vertex alpha
	Emissive,      // unlit glow, colour param "Color", intensity param "Intensity"
	CoolStrip,     // animated blue chevrons (emissive, panning)
	Ghost,         // translucent fresnel ghost bike
	Particle,      // translucent camera-facing sprite (per-instance custom data: alpha, size)
	Crowd,         // instanced spectators with per-instance colour and bobbing
	Visor,         // glossy dark glass
	Count
};

/** Loads project materials (created by Tools/build_content.py) with engine fallbacks. */
namespace MXMaterials
{
	HEATLINEMX_API UMaterialInterface* Get(EMXMat Which);
	HEATLINEMX_API bool IsProjectMaterial(EMXMat Which);
	/** Dynamic instance of an emissive material with a colour. */
	HEATLINEMX_API UMaterialInstanceDynamic* MakeEmissive(UObject* Outer, const FLinearColor& Color, float Intensity);
}
