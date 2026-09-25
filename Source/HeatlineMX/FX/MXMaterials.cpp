#include "FX/MXMaterials.h"
#include "HeatlineMX.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/SoftObjectPath.h"

namespace MXMaterials
{
	static TWeakObjectPtr<UMaterialInterface> GCache[(int32)EMXMat::Count];
	static bool GIsProject[(int32)EMXMat::Count] = {};
	static bool GTried[(int32)EMXMat::Count] = {};

	static const TCHAR* ProjectPath(EMXMat W)
	{
		switch (W)
		{
		case EMXMat::Vertex: return TEXT("/Game/HeatlineMX/Materials/M_MX_Vertex.M_MX_Vertex");
		case EMXMat::VertexGloss: return TEXT("/Game/HeatlineMX/Materials/M_MX_VertexGloss.M_MX_VertexGloss");
		case EMXMat::Emissive: return TEXT("/Game/HeatlineMX/Materials/M_MX_Emissive.M_MX_Emissive");
		case EMXMat::CoolStrip: return TEXT("/Game/HeatlineMX/Materials/M_MX_CoolStrip.M_MX_CoolStrip");
		case EMXMat::Ghost: return TEXT("/Game/HeatlineMX/Materials/M_MX_Ghost.M_MX_Ghost");
		case EMXMat::Particle: return TEXT("/Game/HeatlineMX/Materials/M_MX_Particle.M_MX_Particle");
		case EMXMat::Crowd: return TEXT("/Game/HeatlineMX/Materials/M_MX_Crowd.M_MX_Crowd");
		case EMXMat::Visor: return TEXT("/Game/HeatlineMX/Materials/M_MX_Visor.M_MX_Visor");
		default: return TEXT("");
		}
	}

	UMaterialInterface* Get(EMXMat Which)
	{
		const int32 I = (int32)Which;
		if (GCache[I].IsValid())
		{
			return GCache[I].Get();
		}
		UMaterialInterface* M = nullptr;
		if (!GTried[I])
		{
			GTried[I] = true;
			M = Cast<UMaterialInterface>(FSoftObjectPath(ProjectPath(Which)).TryLoad());
			GIsProject[I] = M != nullptr;
			if (!M)
			{
				UE_LOG(LogHeatline, Warning, TEXT("Material %s missing; using engine fallback (run Tools/build_content.py)."), ProjectPath(Which));
			}
		}
		if (!M)
		{
			static const TCHAR* Fallbacks[] = {
				TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"),
				TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
				TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"),
			};
			for (const TCHAR* F : Fallbacks)
			{
				M = Cast<UMaterialInterface>(FSoftObjectPath(F).TryLoad());
				if (M)
				{
					break;
				}
			}
		}
		if (M)
		{
			M->AddToRoot();
			GCache[I] = M;
		}
		return M;
	}

	bool IsProjectMaterial(EMXMat Which)
	{
		Get(Which);
		return GIsProject[(int32)Which];
	}

	UMaterialInstanceDynamic* MakeEmissive(UObject* Outer, const FLinearColor& Color, float Intensity)
	{
		UMaterialInterface* Base = Get(EMXMat::Emissive);
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		return MID;
	}
}
