#include "FX/MXFXManager.h"
#include "FX/MXMeshKit.h"
#include "FX/MXMaterials.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

TWeakObjectPtr<AMXFXManager> AMXFXManager::Instance;

AMXFXManager::AMXFXManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AMXFXManager* AMXFXManager::Get(UWorld* World)
{
	if (Instance.IsValid() && Instance->GetWorld() == World)
	{
		return Instance.Get();
	}
	if (!World)
	{
		return nullptr;
	}
	if (TActorIterator<AMXFXManager> It(World); It)
	{
		Instance = *It;
		return *It;
	}
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMXFXManager* M = World->SpawnActor<AMXFXManager>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	Instance = M;
	return M;
}

void AMXFXManager::EnsureComponent()
{
	if (ISM)
	{
		return;
	}
	// Unit quad in the XZ plane facing +Y (camera side); the material billboards it per view.
	FMXMeshBuffer B;
	B.AddQuad(FVector(-50, 0, -50), FVector(50, 0, -50), FVector(50, 0, 50), FVector(-50, 0, 50), FLinearColor::White);
	// AddQuad makes the face point -Y for this order; add the +Y face too so fallback materials show it.
	B.AddQuad(FVector(50, 0, -50), FVector(-50, 0, -50), FVector(-50, 0, 50), FVector(50, 0, 50), FLinearColor::White);
	UStaticMesh* Quad = MXMeshKit::BuildStaticMesh(B, MXMaterials::Get(EMXMat::Particle), this, TEXT("SM_ParticleQuad"));
	ISM = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Particles"));
	ISM->SetupAttachment(RootComponent);
	ISM->SetStaticMesh(Quad);
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCastShadow(false);
	ISM->RegisterComponent();
	ISM->SetNumCustomDataFloats(4);
	Particles.SetNum(MaxParticles);
	InstanceCount = 0;
}

void AMXFXManager::SpawnPuff(EMXPuff Kind, const FVector& Location, const FVector& Velocity, float Scale)
{
	EnsureComponent();
	if (Active >= MaxParticles)
	{
		return;
	}
	FParticle& P = Particles[Active++];
	P.Pos = Location;
	P.Vel = Velocity;
	P.Age = 0.f;
	switch (Kind)
	{
	case EMXPuff::Dust:
		P.Life = FMath::FRandRange(0.7f, 1.3f);
		P.Size0 = 25.f * Scale;
		P.Size1 = 140.f * Scale;
		P.Gravity = -60.f;
		P.Drag = 2.2f;
		P.Color = FLinearColor(0.55f, 0.38f, 0.22f, 0.55f);
		break;
	case EMXPuff::Mud:
		P.Life = FMath::FRandRange(0.5f, 0.8f);
		P.Size0 = 12.f * Scale;
		P.Size1 = 30.f * Scale;
		P.Gravity = 900.f;
		P.Drag = 0.6f;
		P.Color = FLinearColor(0.08f, 0.05f, 0.02f, 0.95f);
		break;
	case EMXPuff::Grass:
		P.Life = FMath::FRandRange(0.4f, 0.7f);
		P.Size0 = 8.f * Scale;
		P.Size1 = 14.f * Scale;
		P.Gravity = 700.f;
		P.Drag = 1.2f;
		P.Color = FLinearColor(0.15f, 0.45f, 0.08f, 0.95f);
		break;
	case EMXPuff::Steam:
		P.Life = FMath::FRandRange(0.8f, 1.4f);
		P.Size0 = 20.f * Scale;
		P.Size1 = 110.f * Scale;
		P.Gravity = -120.f;
		P.Drag = 1.5f;
		P.Color = FLinearColor(0.75f, 0.9f, 1.f, 0.5f);
		break;
	case EMXPuff::Smoke:
		P.Life = FMath::FRandRange(1.2f, 2.f);
		P.Size0 = 25.f * Scale;
		P.Size1 = 160.f * Scale;
		P.Gravity = -90.f;
		P.Drag = 1.1f;
		P.Color = FLinearColor(0.12f, 0.12f, 0.13f, 0.6f);
		break;
	}
}

void AMXFXManager::ClearAll()
{
	Active = 0;
	if (ISM)
	{
		ISM->ClearInstances();
	}
	InstanceCount = 0;
}

void AMXFXManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!ISM)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	for (int32 i = 0; i < Active;)
	{
		FParticle& P = Particles[i];
		P.Age += Dt;
		if (P.Age >= P.Life)
		{
			Particles[i] = Particles[--Active];
			continue;
		}
		P.Vel.Z -= P.Gravity * Dt;
		P.Vel *= FMath::Exp(-P.Drag * Dt);
		P.Pos += P.Vel * Dt;
		if (P.Pos.Z < 2.f && P.Gravity > 0.f)
		{
			P.Pos.Z = 2.f;
			P.Vel = FVector::ZeroVector;
		}
		++i;
	}

	// Grow the instance pool to the number of live particles; extra instances are parked at zero scale.
	if (InstanceCount < Active)
	{
		TArray<FTransform> Add;
		Add.Init(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector), Active - InstanceCount);
		ISM->AddInstances(Add, false);
		InstanceCount = Active;
	}
	TArray<FTransform> Xf;
	Xf.SetNum(InstanceCount);
	for (int32 i = 0; i < InstanceCount; ++i)
	{
		if (i < Active)
		{
			const FParticle& P = Particles[i];
			const float T = P.Age / P.Life;
			const float Size = FMath::Lerp(P.Size0, P.Size1, FMath::Sqrt(T)) / 100.f;
			Xf[i] = FTransform(FRotator::ZeroRotator, P.Pos, FVector(Size));
			const float Alpha = P.Color.A * (1.f - T) * FMath::Min(1.f, T * 8.f);
			ISM->SetCustomDataValue(i, 0, P.Color.R, false);
			ISM->SetCustomDataValue(i, 1, P.Color.G, false);
			ISM->SetCustomDataValue(i, 2, P.Color.B, false);
			ISM->SetCustomDataValue(i, 3, Alpha, false);
		}
		else
		{
			Xf[i] = FTransform(FRotator::ZeroRotator, FVector(0, 0, -10000), FVector::ZeroVector);
		}
	}
	if (InstanceCount > 0)
	{
		ISM->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
	}
}
