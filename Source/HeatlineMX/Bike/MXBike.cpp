#include "Bike/MXBike.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MXTuning.h"
#include "Track/MXTrackModel.h"
#include "Track/MXTrackActor.h"
#include "FX/MXMeshKit.h"
#include "FX/MXMaterials.h"
#include "FX/MXFXManager.h"
#include "Audio/MXEngineAudio.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// Bike-local key points (cm). Origin = rear tyre contact, +X forward, +Z up, +Y = right (camera side).
	const FVector RearAxlePos(0.f, 0.f, 35.f);
	const FVector FrontAxlePos(148.f, 0.f, 36.f);
	const FVector SwingPivotPos(58.f, 0.f, 44.f);
	const FVector HeadPos(118.f, 0.f, 100.f);
	const FVector GripPos(110.f, 37.f, 115.f);
	const FVector PegPos(54.f, 25.f, 46.f);
	constexpr float Wheelbase = 148.f;
	constexpr float TyreRadius = 35.f;

	constexpr float UpperArmLen = 29.f;
	constexpr float ForeArmLen = 31.f;
	constexpr float ThighLen = 45.f;
	constexpr float ShinLen = 47.f;
	constexpr float TorsoLen = 54.f;

	/** Two-bone IK: joint position for root A, target T, bone lengths L1/L2, bending towards Pole. */
	FVector SolveTwoBone(const FVector& A, const FVector& T, float L1, float L2, const FVector& PoleDir)
	{
		FVector D = T - A;
		float Dist = D.Size();
		const float MaxReach = (L1 + L2) * 0.999f;
		if (Dist > MaxReach)
		{
			D *= MaxReach / FMath::Max(Dist, 1e-3f);
			Dist = MaxReach;
		}
		Dist = FMath::Max(Dist, FMath::Abs(L1 - L2) + 0.5f);
		const FVector Dir = D.GetSafeNormal();
		const float CosA = FMath::Clamp((L1 * L1 + Dist * Dist - L2 * L2) / (2.f * L1 * Dist), -1.f, 1.f);
		const float SinA = FMath::Sqrt(FMath::Max(0.f, 1.f - CosA * CosA));
		FVector Bend = (PoleDir - Dir * FVector::DotProduct(PoleDir, Dir)).GetSafeNormal();
		if (Bend.IsNearlyZero())
		{
			Bend = FVector::UpVector;
		}
		return A + Dir * (L1 * CosA) + Bend * (L1 * SinA);
	}

	FLinearColor Col(float R, float G, float B, float A = 0.6f) { return FLinearColor(R, G, B, A); }
}

AMXBike::AMXBike()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	BikeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BikeRoot"));
	BikeRoot->SetupAttachment(Root);
	BikeRoot->SetUsingAbsoluteLocation(true);
	BikeRoot->SetUsingAbsoluteRotation(true);
	EngineAudio = CreateDefaultSubobject<UMXEngineAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(Root);
	AutoPossessAI = EAutoPossessAI::Disabled;
}

UStaticMeshComponent* AMXBike::NewPart(USceneComponent* Parent, FName Name)
{
	UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this, Name);
	P->SetMobility(EComponentMobility::Movable);
	P->SetupAttachment(Parent);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->SetCastShadow(true);
	P->RegisterComponent();
	AllParts.Add(P);
	return P;
}

void AMXBike::SetupRider(int32 InRacerIndex, int32 InColorIndex, const FString& InName, bool bInAI, int32 InPlayerSlot)
{
	RacerIndex = InRacerIndex;
	RacerName = InName;
	bIsAI = bInAI;
	PlayerSlot = InPlayerSlot;
	if (InitializedColor == InColorIndex && AllParts.Num() > 0)
	{
		return;
	}
	ColorIndex = InColorIndex;
	InitializedColor = InColorIndex;
	for (UStaticMeshComponent* P : AllParts)
	{
		if (P)
		{
			P->DestroyComponent();
		}
	}
	AllParts.Reset();
	BodyMaterial = MXMaterials::Get(EMXMat::Vertex);
	PaintMaterial = MXMaterials::Get(EMXMat::VertexGloss);
	BuildBikeMeshes();
	BuildRiderMeshes();
	if (EngineAudio)
	{
		EngineAudio->SetVoiceSeed(InRacerIndex);
	}
}

void AMXBike::MakeGhost()
{
	bIsGhost = true;
	UMaterialInterface* Ghost = MXMaterials::Get(EMXMat::Ghost);
	for (UStaticMeshComponent* P : AllParts)
	{
		if (P)
		{
			P->SetCastShadow(false);
			if (Ghost && MXMaterials::IsProjectMaterial(EMXMat::Ghost))
			{
				for (int32 s = 0; s < P->GetNumMaterials(); ++s)
				{
					P->SetMaterial(s, Ghost);
				}
			}
		}
	}
	if (EngineAudio)
	{
		EngineAudio->SetMuted(true);
	}
}

void AMXBike::BuildBikeMeshes()
{
	const FLinearColor Paint = MX::RiderColor(ColorIndex);
	const FLinearColor PaintA(Paint.R, Paint.G, Paint.B, 0.25f);
	const FLinearColor White = Col(0.92f, 0.92f, 0.92f, 0.3f);
	const FLinearColor Black = Col(0.03f, 0.03f, 0.035f, 0.55f);
	const FLinearColor Metal = Col(0.55f, 0.56f, 0.6f, 0.9f);   // alpha -> metallic in the gloss material
	const FLinearColor Dark = Col(0.12f, 0.12f, 0.13f, 0.6f);
	const FLinearColor Gold = Col(0.85f, 0.62f, 0.2f, 0.9f);

	// ---------------- Chassis ----------------
	Chassis = NewObject<USceneComponent>(this, TEXT("Chassis"));
	Chassis->SetupAttachment(BikeRoot);
	Chassis->RegisterComponent();
	ChassisMesh = NewPart(Chassis, TEXT("ChassisMesh"));
	{
		FMXMeshBuffer B;
		// Frame tubes (two rails).
		for (float Y : {-7.f, 7.f})
		{
			B.AddTube({HeadPos + FVector(0, Y * 0.3f, 0), FVector(96, Y, 42), FVector(64, Y, 36), SwingPivotPos + FVector(0, Y, 0)}, 2.3f, 8, Metal);
			B.AddTube({HeadPos + FVector(0, Y * 0.3f, -4), FVector(72, Y, 88), FVector(8, Y * 0.7f, 92)}, 2.f, 8, Metal);
			B.AddTube({FVector(64, Y, 60), FVector(40, Y, 90)}, 1.8f, 6, Metal);
		}
		// Engine + cylinder head.
		B.AddBeveledBox(FVector(78, 0, 54), FVector(17, 11, 15), 4, FQuat::Identity, Dark);
		B.AddBeveledBox(FVector(92, 0, 72), FVector(8, 9, 8), 2, FQuat::Identity, Dark);
		B.AddCylinder(FVector(70, 13, 50), FVector(70, 15.5f, 50), 9, 9, 16, Metal, true); // clutch cover
		// Radiators.
		for (float Y : {-12.f, 12.f})
		{
			B.AddBox(FVector(102, Y, 80), FVector(4, 3, 11), FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-12.f)), Black);
		}
		// Seat.
		B.AddBeveledBox(FVector(50, 0, 99), FVector(34, 11, 4), 4, FQuat(FVector::YAxisVector, FMath::DegreesToRadians(3.f)), Black);
		// Rear fender.
		B.AddBox(FVector(2, 0, 89), FVector(40, 10, 1.2f), FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-9.f)), White);
		// Chain + sprocket (right side is the camera side on the exhaust... chain on the left).
		B.AddCylinder(FVector(0, -11, 35), FVector(0, -12, 35), 11, 11, 16, Metal, true);
		B.AddTube({FVector(0, -11.5f, 46), SwingPivotPos + FVector(8, -11.5f, 4)}, 0.9f, 4, Dark);
		B.AddTube({FVector(0, -11.5f, 24), SwingPivotPos + FVector(8, -11.5f, -8)}, 0.9f, 4, Dark);
		// Exhaust (right side).
		B.AddTube({FVector(100, 6, 68), FVector(113, 12, 57), FVector(104, 16, 44), FVector(74, 17, 47), FVector(52, 16, 62)}, 3.f, 10, Metal);
		B.AddCylinder(FVector(54, 16, 64), FVector(6, 16, 80), 5.5f, 5.f, 14, Col(0.18f, 0.18f, 0.2f, 0.8f), true);
		B.AddCylinder(FVector(6, 16, 80), FVector(1, 16, 81.5f), 3.5f, 3.f, 10, Metal, true);
		// Rear shock spring.
		B.AddCylinder(FVector(62, 0, 84), FVector(44, 0, 52), 3.4f, 3.4f, 10, Col(0.95f, 0.8f, 0.1f, 0.3f), true);
		// Footpegs.
		B.AddCylinder(PegPos + FVector(0, -52, 0), PegPos + FVector(0, -42, 0), 1.6f, 1.6f, 6, Metal, true);
		B.AddCylinder(PegPos + FVector(0, 0, 0), PegPos + FVector(0, -10, 0), 1.6f, 1.6f, 6, Metal, true);
		FMXMeshSections PartSections;
		PartSections.Add(B, PaintMaterial ? PaintMaterial.Get() : BodyMaterial.Get());

		FMXMeshBuffer P;
		// Tank + shrouds + side panels in the rider colour.
		P.AddEllipsoid(FVector(98, 0, 97), FVector(22, 13, 10), FQuat(FVector::YAxisVector, FMath::DegreesToRadians(8.f)), 14, 8, PaintA);
		const TArray<FVector2D> Shroud = {{78, 70}, {92, 104}, {118, 103}, {124, 90}, {104, 72}};
		P.AddExtrudedPolygon(Shroud, 14.f, 17.f, FTransform::Identity, PaintA);
		P.AddExtrudedPolygon(Shroud, -17.f, -14.f, FTransform::Identity, PaintA);
		const TArray<FVector2D> Panel = {{14, 80}, {55, 84}, {58, 96}, {12, 97}};
		P.AddExtrudedPolygon(Panel, 12.f, 14.5f, FTransform::Identity, White);
		P.AddExtrudedPolygon(Panel, -14.5f, -12.f, FTransform::Identity, White);
		// Colour stripe on the side panels.
		const TArray<FVector2D> Stripe = {{16, 86}, {54, 88}, {55, 91}, {15, 90}};
		P.AddExtrudedPolygon(Stripe, 14.4f, 14.9f, FTransform::Identity, PaintA);
		P.AddExtrudedPolygon(Stripe, -14.9f, -14.4f, FTransform::Identity, PaintA);
		PartSections.Add(P, PaintMaterial ? PaintMaterial.Get() : BodyMaterial.Get());
		MXMeshKit::ApplyToComponent(ChassisMesh, PartSections);
	}

	// ---------------- Front end ----------------
	ForkPivot = NewObject<USceneComponent>(this, TEXT("ForkPivot"));
	ForkPivot->SetupAttachment(Chassis);
	ForkPivot->SetRelativeLocation(HeadPos);
	ForkPivot->RegisterComponent();
	const FVector ForkDir = (FrontAxlePos - HeadPos).GetSafeNormal();
	const float ForkLen = (FrontAxlePos - HeadPos).Size();
	ForkUpper = NewPart(ForkPivot, TEXT("ForkUpper"));
	{
		FMXMeshBuffer B;
		for (float Y : {-9.f, 9.f})
		{
			B.AddCylinder(FVector(0, Y, 6), ForkDir * 38.f + FVector(0, Y, 0), 2.6f, 2.6f, 10, Gold, true);
		}
		B.AddBox(FVector(0, 0, 4), FVector(5, 13, 2.5f), FQuat::Identity, Metal);
		B.AddBox(ForkDir * 12.f, FVector(5, 13, 2.5f), FQuat::Identity, Metal);
		// Handlebar + grips.
		const FVector Bar = GripPos - HeadPos;
		B.AddTube({FVector(Bar.X, -Bar.Y, Bar.Z), FVector(Bar.X + 2, -12, Bar.Z - 4), FVector(Bar.X + 2, 12, Bar.Z - 4), FVector(Bar.X, Bar.Y, Bar.Z)}, 1.4f, 8, Metal);
		B.AddCylinder(FVector(Bar.X, Bar.Y - 11, Bar.Z), FVector(Bar.X, Bar.Y + 1, Bar.Z), 2.1f, 2.1f, 8, Black, true);
		B.AddCylinder(FVector(Bar.X, -Bar.Y - 1, Bar.Z), FVector(Bar.X, -Bar.Y + 11, Bar.Z), 2.1f, 2.1f, 8, Black, true);
		FMXMeshSections PartSections;
		PartSections.Add(B, PaintMaterial ? PaintMaterial.Get() : BodyMaterial.Get());
		// Front number plate.
		FMXMeshBuffer P;
		const TArray<FVector2D> Plate = {{8, -22}, {16, -2}, {14, 12}, {4, 16}, {0, -6}};
		P.AddExtrudedPolygon(Plate, -12.f, 12.f, FTransform::Identity, White);
		PartSections.Add(P, BodyMaterial);
		MXMeshKit::ApplyToComponent(ForkUpper, PartSections);
	}
	ForkSlider = NewObject<USceneComponent>(this, TEXT("ForkSlider"));
	ForkSlider->SetupAttachment(ForkPivot);
	ForkSlider->SetRelativeLocation(ForkDir * ForkLen);
	ForkSlider->RegisterComponent();
	ForkLower = NewPart(ForkSlider, TEXT("ForkLower"));
	{
		FMXMeshBuffer B;
		for (float Y : {-9.f, 9.f})
		{
			B.AddCylinder(FVector(0, Y, 0), -ForkDir * 34.f + FVector(0, Y, 0), 3.4f, 3.2f, 10, Black, true);
		}
		FMXMeshSections PartSections;
		PartSections.Add(B, BodyMaterial);
		// Front fender in the rider colour.
		FMXMeshBuffer P;
		const FQuat Tilt(FVector::YAxisVector, FMath::DegreesToRadians(-8.f));
		P.AddBox(FVector(6, 0, 40), FVector(26, 9, 1.2f), Tilt, PaintA);
		P.AddBox(FVector(-18, 0, 38), FVector(8, 9, 1.2f), FQuat(FVector::YAxisVector, FMath::DegreesToRadians(25.f)), PaintA);
		PartSections.Add(P, PaintMaterial ? PaintMaterial.Get() : BodyMaterial.Get());
		MXMeshKit::ApplyToComponent(ForkLower, PartSections);
	}
	FrontAxle = NewObject<USceneComponent>(this, TEXT("FrontAxle"));
	FrontAxle->SetupAttachment(ForkSlider);
	FrontAxle->RegisterComponent();
	FrontWheel = NewPart(FrontAxle, TEXT("FrontWheel"));

	// ---------------- Rear end ----------------
	SwingPivot = NewObject<USceneComponent>(this, TEXT("SwingPivot"));
	SwingPivot->SetupAttachment(Chassis);
	SwingPivot->SetRelativeLocation(SwingPivotPos);
	SwingPivot->RegisterComponent();
	Swingarm = NewPart(SwingPivot, TEXT("Swingarm"));
	{
		FMXMeshBuffer B;
		const FVector ToAxle = RearAxlePos - SwingPivotPos;
		for (float Y : {-9.f, 9.f})
		{
			B.AddCylinder(FVector(0, Y, 0), ToAxle + FVector(0, Y, 0), 3.6f, 2.6f, 8, Metal, true);
		}
		B.AddBox(FVector(-6, 0, 0), FVector(4, 10, 3), FQuat::Identity, Metal);
		FMXMeshSections PartSections;
		PartSections.Add(B, PaintMaterial ? PaintMaterial.Get() : BodyMaterial.Get());
		MXMeshKit::ApplyToComponent(Swingarm, PartSections);
	}
	RearAxle = NewObject<USceneComponent>(this, TEXT("RearAxle"));
	RearAxle->SetupAttachment(SwingPivot);
	RearAxle->SetRelativeLocation(RearAxlePos - SwingPivotPos);
	RearAxle->RegisterComponent();
	RearWheel = NewPart(RearAxle, TEXT("RearWheel"));

	// Wheels (identical geometry, built around the axle origin).
	for (UStaticMeshComponent* W : {FrontWheel.Get(), RearWheel.Get()})
	{
		FMXMeshBuffer T;
		T.AddTorus(FVector::ZeroVector, FVector::YAxisVector, 26.f, 8.6f, 32, 8, Col(0.04f, 0.04f, 0.045f, 0.9f), 1.7f);
		FMXMeshSections PartSections;
		PartSections.Add(T, BodyMaterial);
		FMXMeshBuffer R;
		R.AddTorus(FVector::ZeroVector, FVector::YAxisVector, 24.f, 1.8f, 32, 6, Metal, 0.f);
		R.AddCylinder(FVector(0, -7, 0), FVector(0, 7, 0), 5.f, 5.f, 12, Metal, true);
		for (int32 s = 0; s < 16; ++s)
		{
			const float A = 2.f * PI * s / 16.f;
			const FVector Rim(FMath::Cos(A) * 23.5f, 0.f, FMath::Sin(A) * 23.5f);
			const float Side = (s % 2) ? 5.f : -5.f;
			R.AddCylinder(FVector(0, Side, 0), Rim, 0.45f, 0.45f, 3, Metal, false);
		}
		if (W == FrontWheel)
		{
			R.AddCylinder(FVector(0, 7.5f, 0), FVector(0, 8.3f, 0), 12.f, 12.f, 20, Col(0.7f, 0.7f, 0.72f, 0.95f), true);
		}
		PartSections.Add(R, PaintMaterial ? PaintMaterial.Get() : BodyMaterial.Get());
		MXMeshKit::ApplyToComponent(W, PartSections);
	}
}

void AMXBike::BuildRiderMeshes()
{
	const FLinearColor Paint = MX::RiderColor(ColorIndex);
	const FLinearColor Jersey(Paint.R, Paint.G, Paint.B, 0.9f);
	const FLinearColor Pants(Paint.R * 0.35f + 0.05f, Paint.G * 0.35f + 0.05f, Paint.B * 0.35f + 0.05f, 0.9f);
	const FLinearColor WhiteC = Col(0.93f, 0.93f, 0.93f, 0.7f);
	const FLinearColor Black = Col(0.03f, 0.03f, 0.035f, 0.8f);
	const FLinearColor Visor = Col(0.02f, 0.02f, 0.03f, 0.05f);

	RiderRoot = NewObject<USceneComponent>(this, TEXT("RiderRoot"));
	RiderRoot->SetupAttachment(BikeRoot);
	RiderRoot->RegisterComponent();

	auto Limb = [&](FName Name, float Len, float R0, float R1, const FLinearColor& C, const FLinearColor& EndC, float EndSize, bool bBoot) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* P = NewPart(RiderRoot, Name);
		FMXMeshBuffer B;
		B.AddCylinder(FVector(0, 0, 0), FVector(Len, 0, 0), R0, R1, 8, C, true);
		B.AddEllipsoid(FVector::ZeroVector, FVector(R0 * 1.05f), FQuat::Identity, 8, 4, C);
		if (bBoot)
		{
			B.AddBeveledBox(FVector(Len - 4.f, 0, -2.f), FVector(8.f, 6.f, 13.f), 2.f, FQuat(FVector::YAxisVector, FMath::DegreesToRadians(90.f)), EndC);
			B.AddBox(FVector(Len + 2.f, 0, 9.f), FVector(4.f, 5.5f, 10.f), FQuat::Identity, EndC);
		}
		else if (EndSize > 0.f)
		{
			B.AddEllipsoid(FVector(Len, 0, 0), FVector(EndSize), FQuat::Identity, 8, 4, EndC);
		}
		FMXMeshSections PartSections;
		PartSections.Add(B, BodyMaterial);
		MXMeshKit::ApplyToComponent(P, PartSections);
		return P;
	};

	Pelvis = NewPart(RiderRoot, TEXT("Pelvis"));
	{
		FMXMeshBuffer B;
		B.AddEllipsoid(FVector::ZeroVector, FVector(13, 17, 11), FQuat::Identity, 10, 6, Pants);
		FMXMeshSections PartSections;
		PartSections.Add(B, BodyMaterial);
		MXMeshKit::ApplyToComponent(Pelvis, PartSections);
	}
	Torso = NewPart(RiderRoot, TEXT("Torso"));
	{
		// Built along +X (pelvis -> neck).
		FMXMeshBuffer B;
		B.AddEllipsoid(FVector(TorsoLen * 0.45f, 0, 0), FVector(TorsoLen * 0.55f, 18.f, 13.f), FQuat::Identity, 12, 8, Jersey);
		B.AddBox(FVector(TorsoLen * 0.62f, 0, 12.5f), FVector(10.f, 12.f, 1.f), FQuat::Identity, WhiteC); // number panel on the back
		B.AddEllipsoid(FVector(TorsoLen * 0.8f, 0, -9.f), FVector(8.f, 15.f, 5.f), FQuat::Identity, 8, 4, Black); // chest protector
		FMXMeshSections PartSections;
		PartSections.Add(B, BodyMaterial);
		MXMeshKit::ApplyToComponent(Torso, PartSections);
	}
	Head = NewPart(RiderRoot, TEXT("Head"));
	{
		FMXMeshBuffer B;
		B.AddEllipsoid(FVector(0, 0, 12), FVector(15.f, 13.f, 14.f), FQuat::Identity, 14, 9, Jersey);
		B.AddBox(FVector(0, 0, 25.5f), FVector(10.f, 2.5f, 1.2f), FQuat::Identity, WhiteC); // stripe
		B.AddBox(FVector(14, 0, 23), FVector(7.f, 11.f, 1.f), FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-16.f)), WhiteC); // peak
		B.AddBeveledBox(FVector(14, 0, 4), FVector(4.f, 9.f, 5.f), 2.f, FQuat::Identity, WhiteC); // chin guard
		FMXMeshSections PartSections;
		PartSections.Add(B, PaintMaterial ? PaintMaterial.Get() : BodyMaterial.Get());
		FMXMeshBuffer G;
		G.AddBox(FVector(13.5f, 0, 13), FVector(2.5f, 10.f, 4.f), FQuat::Identity, Visor); // goggles
		PartSections.Add(G, MXMaterials::Get(EMXMat::Visor));
		MXMeshKit::ApplyToComponent(Head, PartSections);
	}
	UpperArmL = Limb(TEXT("UpperArmL"), UpperArmLen, 5.5f, 4.8f, Jersey, Jersey, 0.f, false);
	UpperArmR = Limb(TEXT("UpperArmR"), UpperArmLen, 5.5f, 4.8f, Jersey, Jersey, 0.f, false);
	ForeArmL = Limb(TEXT("ForeArmL"), ForeArmLen, 4.6f, 3.8f, Jersey, Black, 4.5f, false);
	ForeArmR = Limb(TEXT("ForeArmR"), ForeArmLen, 4.6f, 3.8f, Jersey, Black, 4.5f, false);
	ThighL = Limb(TEXT("ThighL"), ThighLen, 8.f, 6.2f, Pants, Pants, 0.f, false);
	ThighR = Limb(TEXT("ThighR"), ThighLen, 8.f, 6.2f, Pants, Pants, 0.f, false);
	ShinL = Limb(TEXT("ShinL"), ShinLen, 6.2f, 5.f, WhiteC, WhiteC, 0.f, true);
	ShinR = Limb(TEXT("ShinR"), ShinLen, 6.2f, 5.f, WhiteC, WhiteC, 0.f, true);
}

void AMXBike::PlaceLimb(UStaticMeshComponent* Part, const FVector& From, const FVector& To)
{
	if (!Part)
	{
		return;
	}
	const FVector Dir = (To - From).GetSafeNormal();
	Part->SetRelativeLocationAndRotation(From, Dir.IsNearlyZero() ? FRotator::ZeroRotator : Dir.Rotation());
}

void AMXBike::OnSimStep(const FMXTrackModel& Track)
{
	PendingEvents |= State.Events;
}

void AMXBike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PoseTime += DeltaSeconds;
	UpdatePose(DeltaSeconds);

	// Engine audio from the simulation.
	if (EngineAudio && !bIsGhost)
	{
		const UMXBikeTuning& T = MXTuning::Bike();
		const float V = State.ForwardSpeed();
		EngineAudio->SetEngineParams(V / FMath::Max(1.f, T.MaxSpeedTurbo), State.ThrottleApplied, State.bTurboActive,
			State.Heat / FMath::Max(1.f, T.HeatOverheat), State.IsAirborne(), State.Phase == EMXBikePhase::Stalled || !State.IsRiding());
		if (PendingEvents & EMXBikeEvent::Landed)
		{
			EngineAudio->PlayOneShot(State.LastLanding == EMXLandingGrade::Crash ? EMXSfx::Crash : EMXSfx::Land, FMath::Clamp(State.SuspensionImpulse / 12.f, 0.2f, 1.f));
		}
		if (PendingEvents & EMXBikeEvent::Crashed)
		{
			EngineAudio->PlayOneShot(EMXSfx::Crash, 1.f);
		}
		if (PendingEvents & EMXBikeEvent::CoolStrip)
		{
			EngineAudio->PlayOneShot(EMXSfx::Cool, 1.f);
		}
		if (PendingEvents & EMXBikeEvent::Overheat)
		{
			EngineAudio->PlayOneShot(EMXSfx::Overheat, 1.f);
		}
		if (PendingEvents & EMXBikeEvent::HeatWarning)
		{
			EngineAudio->PlayOneShot(EMXSfx::Warning, 0.7f);
		}
		if (PendingEvents & (EMXBikeEvent::BarrierHop | EMXBikeEvent::Bumped))
		{
			EngineAudio->PlayOneShot(EMXSfx::Bump, 0.6f);
		}
	}
	SpawnFX(DeltaSeconds, State);
	PendingEvents = 0;
}

void AMXBike::UpdatePose(float DeltaSeconds)
{
	const FMXBikeState& Cur = State;
	const FMXBikeState& Prev = PrevState;
	const bool bSamePhase = Cur.Phase == Prev.Phase;
	const float A = bSamePhase ? FMath::Clamp(InterpAlpha, 0.f, 1.f) : 1.f;
	const float S = FMath::Lerp(Prev.VisualS(), Cur.VisualS(), A);
	const float Y = FMath::Lerp(Prev.Y, Cur.Y, A);
	const float Z = FMath::Lerp(Prev.Z, Cur.Z, A);
	const float VY = Cur.VY;

	// Suspension: spring-damper excited by landings and hard terrain transitions.
	const float Impulse = (PendingEvents & EMXBikeEvent::Landed) ? Cur.SuspensionImpulse * LandingCompressionGain : 0.f;
	FrontVel += Impulse * 1.1f;
	RearVel += Impulse * 1.3f;
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	FrontVel += (-SuspensionStiffness * FrontComp - SuspensionDamping * FrontVel) * Dt;
	RearVel += (-SuspensionStiffness * RearComp - SuspensionDamping * RearVel) * Dt;
	FrontComp = FMath::Clamp(FrontComp + FrontVel * Dt, -4.f, 18.f);
	RearComp = FMath::Clamp(RearComp + RearVel * Dt, -4.f, 16.f);
	const float Sag = Cur.IsAirborne() ? -3.f : 2.f;

	float Pitch = 0.f;
	FVector Center = AMXTrack::ToWorld(S, Y, Z);
	float Roll = FMath::Clamp(-VY * LeanPerLateralSpeed, -16.f, 16.f);
	float Yaw = FMath::RadiansToDegrees(FMath::Atan2(VY, FMath::Max(3.f, Cur.ForwardSpeed()))) * 0.6f;
	const bool bCrashed = Cur.Phase == EMXBikePhase::Crashed;
	const bool bRecover = Cur.Phase == EMXBikePhase::Recovering;

	if (Cur.IsAirborne())
	{
		Pitch = FMath::Lerp(Prev.Pitch, Cur.Pitch, A);
	}
	else if (bCrashed || bRecover)
	{
		Pitch = Cur.SlopeDeg;
	}
	else
	{
		Pitch = FMath::Lerp(Prev.SlopeDeg + Prev.Wheelie, Cur.SlopeDeg + Cur.Wheelie, A);
	}

	if (bCrashed)
	{
		// Bike slides on its side.
		CrashAnimTime += DeltaSeconds;
		Roll = FMath::Lerp(0.f, -82.f, FMath::Clamp(CrashAnimTime * 3.f, 0.f, 1.f));
		Yaw = FMath::Sin(CrashAnimTime * 3.f) * 25.f * FMath::Exp(-CrashAnimTime);
		Center = AMXTrack::ToWorld(Cur.CrashS + Cur.VisualSlide * 0.75f, Y, Z) + FVector(0, 0, 18.f);
	}
	else if (bRecover)
	{
		// Bike is stood back up and wheeled back to the (never forward) recovery point.
		CrashAnimTime += DeltaSeconds;
		const float T = FMath::Clamp(Cur.RecoveryElapsed / FMath::Max(0.3f, MXTuning::Bike().RecoveryBase), 0.f, 1.f);
		const float SlideS = Cur.CrashS + Cur.VisualSlide * 0.75f;
		const float Sx = FMath::Lerp(SlideS, Cur.RecoveryS, FMath::SmoothStep(0.f, 1.f, T));
		const float Yx = FMath::Lerp(Cur.Y, Cur.RecoveryY, T);
		Center = AMXTrack::ToWorld(Sx, Yx, 0.f);
		Center.Z = Z * 100.f;
		Roll = FMath::Lerp(-82.f, 0.f, FMath::SmoothStep(0.f, 0.35f, T));
		Yaw = 0.f;
		Pitch = 0.f;
	}
	else
	{
		CrashAnimTime = 0.f;
	}

	VisualCenter = Center;
	VisualPitch = Pitch;
	VisualRoll = FMath::FInterpTo(VisualRoll, Roll, DeltaSeconds, bCrashed ? 30.f : 10.f);
	VisualYaw = FMath::FInterpTo(VisualYaw, Yaw, DeltaSeconds, 10.f);

	// Root at the rear tyre contact; grounded wheelies pivot about it, airborne rotation about the centre.
	const FRotator Rot(Pitch, VisualYaw, VisualRoll);
	const FQuat Q = Rot.Quaternion();
	FVector Rear;
	if (!Cur.IsAirborne() && !bCrashed && !bRecover)
	{
		const FQuat SlopeQ = FRotator(Cur.SlopeDeg, VisualYaw, 0.f).Quaternion();
		Rear = Center - SlopeQ.RotateVector(FVector(Wheelbase * 0.5f, 0, 0));
	}
	else
	{
		Rear = Center - Q.RotateVector(FVector(Wheelbase * 0.5f, 0, 0));
	}
	BikeRoot->SetWorldLocationAndRotation(Rear + Q.RotateVector(FVector(0, 0, -Sag)), Q);
	SetActorLocation(Center, false);

	// Wheel spin + suspension parts.
	const float DDist = FMath::Max(0.f, Cur.Distance - LastDistance);
	LastDistance = Cur.Distance;
	if (!bCrashed)
	{
		WheelAngleR += FMath::RadiansToDegrees(DDist * 100.f / TyreRadius);
		WheelAngleF += FMath::RadiansToDegrees(DDist * 100.f / TyreRadius);
	}
	if (FrontWheel)
	{
		FrontWheel->SetRelativeRotation(FRotator(-WheelAngleF, 0.f, 0.f));
	}
	if (RearWheel)
	{
		RearWheel->SetRelativeRotation(FRotator(-WheelAngleR, 0.f, 0.f));
	}
	if (ForkSlider)
	{
		const FVector ForkDir = (FrontAxlePos - HeadPos).GetSafeNormal();
		const float ForkLen = (FrontAxlePos - HeadPos).Size();
		ForkSlider->SetRelativeLocation(ForkDir * (ForkLen - FMath::Max(0.f, FrontComp + Sag)));
	}
	if (SwingPivot)
	{
		const float SwingLen = (RearAxlePos - SwingPivotPos).Size();
		const float Deg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp((RearComp + Sag) / SwingLen, -0.5f, 0.5f)));
		SwingPivot->SetRelativeRotation(FRotator(-Deg, 0.f, 0.f));
	}

	UpdateRiderPose(DeltaSeconds, Cur);

	// Invulnerability blink after remounting (readable "you are protected" feedback).
	const bool bBlink = Cur.GhostTimer > 0.f && Cur.IsRiding() && FMath::Fmod(PoseTime, 0.2f) < 0.07f;
	if (!bIsGhost)
	{
		BikeRoot->SetVisibility(!bBlink, true);
	}
	LastPhase = Cur.Phase;
}

void AMXBike::UpdateRiderPose(float DeltaSeconds, const FMXBikeState& S)
{
	if (!RiderRoot || !Torso)
	{
		return;
	}
	const bool bCrashed = S.Phase == EMXBikePhase::Crashed;
	const bool bRecover = S.Phase == EMXBikePhase::Recovering;

	// Target lean from pitch input (visible body language for float vs dive).
	float TargetLean = FMath::Clamp(CurrentInput.Pitch, -1.f, 1.f);
	if (S.Phase == EMXBikePhase::Grounded && S.Wheelie > 5.f)
	{
		TargetLean = 1.f;
	}
	RiderLean = FMath::FInterpTo(RiderLean, TargetLean, DeltaSeconds, 8.f);
	const float Compression = FMath::Clamp(FMath::Max(FrontComp, RearComp) / 14.f, 0.f, 1.f);
	RiderCrouch = FMath::FInterpTo(RiderCrouch, Compression, DeltaSeconds, 14.f);
	const bool bSeated = S.Phase == EMXBikePhase::Stalled || (S.IsRiding() && S.ForwardSpeed() < 3.f);

	if (bCrashed || bRecover)
	{
		// Rider detached: tumble along the ground, then run back to the bike.
		RiderRoot->SetUsingAbsoluteLocation(true);
		RiderRoot->SetUsingAbsoluteRotation(true);
		const float T = CrashAnimTime;
		FVector P;
		FRotator R;
		if (bCrashed)
		{
			const float Hop = FMath::Max(0.f, FMath::Sin(FMath::Min(T, 0.6f) / 0.6f * PI)) * 90.f;
			P = AMXTrack::ToWorld(S.CrashS + S.VisualSlide, S.Y + 0.6f, S.Z) + FVector(0, 0, 25.f + Hop);
			CrashRiderSpin += DeltaSeconds * FMath::Max(90.f, S.Speed * 40.f);
			R = FRotator(-CrashRiderSpin, 0.f, FMath::Sin(T * 5.f) * 30.f);
			CrashRiderStart = P;
		}
		else
		{
			// Run back: interpolate from where the tumble ended to beside the bike.
			const float Rt = FMath::Clamp(S.RecoveryElapsed / FMath::Max(0.3f, MXTuning::Bike().RecoveryBase), 0.f, 1.f);
			const FVector Goal = VisualCenter + FVector(-20.f, 60.f, 55.f);
			P = FMath::Lerp(CrashRiderStart, Goal, FMath::SmoothStep(0.f, 0.8f, Rt));
			P.Z = FMath::Lerp(CrashRiderStart.Z, Goal.Z, Rt) + FMath::Abs(FMath::Sin(PoseTime * 14.f)) * 6.f;
			const FVector Dir = (Goal - CrashRiderStart).GetSafeNormal2D();
			R = FRotator(0.f, Dir.IsNearlyZero() ? 180.f : Dir.Rotation().Yaw, 0.f);
			CrashRiderSpin = 0.f;
		}
		RiderRoot->SetWorldLocationAndRotation(P, R);
		// Local pose around the pelvis (origin): limbs splayed while tumbling, running legs after.
		const float Run = bRecover ? FMath::Sin(PoseTime * 14.f) : 0.f;
		const FVector Hip(0, 0, 0);
		const FVector Neck = FVector(0, 0, TorsoLen);
		Pelvis->SetRelativeLocationAndRotation(Hip, FRotator::ZeroRotator);
		PlaceLimb(Torso, Hip, Neck);
		Head->SetRelativeLocationAndRotation(Neck, FRotator::ZeroRotator);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const FVector Sh = Neck + FVector(0, Side * 19.f, -6.f);
			const FVector Hand = bRecover ? Sh + FVector(Run * Side * 20.f, Side * 10.f, -45.f) : Sh + FVector(10.f, Side * 45.f, 15.f);
			const FVector Elbow = SolveTwoBone(Sh, Hand, UpperArmLen, ForeArmLen, FVector(-1, Side, 0));
			PlaceLimb(Side < 0 ? UpperArmL : UpperArmR, Sh, Elbow);
			PlaceLimb(Side < 0 ? ForeArmL : ForeArmR, Elbow, Hand);
			const FVector HipS = Hip + FVector(0, Side * 10.f, 0);
			const FVector Foot = bRecover ? HipS + FVector(-Run * Side * 30.f, Side * 4.f, -85.f) : HipS + FVector(-20.f, Side * 35.f, -70.f);
			const FVector Knee = SolveTwoBone(HipS, Foot, ThighLen, ShinLen, FVector(1, Side * 0.3f, 0));
			PlaceLimb(Side < 0 ? ThighL : ThighR, HipS, Knee);
			PlaceLimb(Side < 0 ? ShinL : ShinR, Knee, Foot);
		}
		return;
	}

	RiderRoot->SetUsingAbsoluteLocation(false);
	RiderRoot->SetUsingAbsoluteRotation(false);
	RiderRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

	// Hip position blends between attack, lean-back, tuck and seated stances (bike-local cm).
	const FVector Attack(48.f, 0.f, 121.f);
	const FVector Back(33.f, 0.f, 112.f);
	const FVector Tuck(61.f, 0.f, 124.f);
	const FVector Seated(46.f, 0.f, 101.f);
	FVector Hip = Attack;
	float TorsoDeg = 38.f;
	if (RiderLean > 0.f)
	{
		Hip = FMath::Lerp(Attack, Back, RiderLean);
		TorsoDeg = FMath::Lerp(38.f, 12.f, RiderLean);
	}
	else
	{
		Hip = FMath::Lerp(Attack, Tuck, -RiderLean);
		TorsoDeg = FMath::Lerp(38.f, 56.f, -RiderLean);
	}
	if (bSeated)
	{
		Hip = Seated;
		TorsoDeg = 14.f;
	}
	if (S.bTurboActive)
	{
		TorsoDeg += 6.f;
		Hip.X += 3.f;
	}
	Hip.Z -= RiderCrouch * 16.f + FrontComp * 0.4f;
	// Tiny bounce with engine vibration / rough ground.
	Hip.Z += FMath::Sin(PoseTime * 31.f) * (S.Surface == EMXSurface::Dirt ? 0.4f : 1.4f);

	const float TorsoRad = FMath::DegreesToRadians(TorsoDeg);
	const FVector Neck = Hip + FVector(FMath::Sin(TorsoRad) * TorsoLen, 0.f, FMath::Cos(TorsoRad) * TorsoLen);
	Pelvis->SetRelativeLocationAndRotation(Hip, FRotator(-TorsoDeg * 0.3f, 0.f, 0.f));
	PlaceLimb(Torso, Hip, Neck);
	// Head looks down-track.
	Head->SetRelativeLocationAndRotation(Neck + FVector(1.f, 0.f, 1.f), FRotator(-FMath::Clamp(S.IsAirborne() ? S.Pitch * 0.3f : 0.f, -20.f, 20.f), 0.f, 0.f));

	// Grips follow the fork (which doesn't steer here) and suspension.
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const FVector Shoulder = Neck + FVector(-4.f, Side * 19.f, -7.f);
		const FVector Grip(GripPos.X, Side * GripPos.Y, GripPos.Z);
		const FVector Elbow = SolveTwoBone(Shoulder, Grip, UpperArmLen, ForeArmLen, FVector(-0.3f, Side * 1.f, 0.4f));
		PlaceLimb(Side < 0 ? UpperArmL : UpperArmR, Shoulder, Elbow);
		PlaceLimb(Side < 0 ? ForeArmL : ForeArmR, Elbow, Grip);

		const FVector HipS = Hip + FVector(0.f, Side * 10.f, -3.f);
		const FVector Peg(PegPos.X, Side * PegPos.Y, PegPos.Z + 6.f);
		const FVector Knee = SolveTwoBone(HipS, Peg, ThighLen, ShinLen, FVector(1.f, Side * 0.35f, 0.2f));
		PlaceLimb(Side < 0 ? ThighL : ThighR, HipS, Knee);
		PlaceLimb(Side < 0 ? ShinL : ShinR, Knee, Peg);
	}
}

void AMXBike::SpawnFX(float DeltaSeconds, const FMXBikeState& S)
{
	if (bIsGhost)
	{
		return;
	}
	AMXFXManager* FX = AMXFXManager::Get(GetWorld());
	if (!FX)
	{
		return;
	}
	const FVector Rear = BikeRoot->GetComponentLocation();
	const float V = S.ForwardSpeed();
	if (S.Phase == EMXBikePhase::Grounded && V > 4.f)
	{
		// Roost from the rear wheel: more with throttle/turbo, darker in mud, green bits on grass.
		DustAccumulator += DeltaSeconds * DustRate * (6.f + V * 0.9f) * (0.35f + S.ThrottleApplied + (S.bTurboActive ? 0.8f : 0.f));
		while (DustAccumulator > 1.f)
		{
			DustAccumulator -= 1.f;
			EMXPuff Kind = EMXPuff::Dust;
			if (S.Surface == EMXSurface::Mud)
			{
				Kind = EMXPuff::Mud;
			}
			else if (S.Surface == EMXSurface::Grass || S.Surface == EMXSurface::Verge)
			{
				Kind = EMXPuff::Grass;
			}
			FX->SpawnPuff(Kind, Rear + FVector(-10.f, 0.f, 8.f), FVector(-V * 18.f, FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(80.f, 260.f)), FMath::FRandRange(0.8f, 1.3f));
		}
	}
	if (PendingEvents & EMXBikeEvent::Landed)
	{
		const float Strength = FMath::Clamp(S.SuspensionImpulse / 10.f, 0.3f, 1.5f);
		for (int32 i = 0; i < 10; ++i)
		{
			FX->SpawnPuff(EMXPuff::Dust, Rear + FVector(FMath::FRandRange(0.f, 150.f), FMath::FRandRange(-60.f, 60.f), 10.f),
				FVector(FMath::FRandRange(-300.f, 300.f), FMath::FRandRange(-300.f, 300.f), FMath::FRandRange(50.f, 200.f)) * Strength, 1.3f * Strength);
		}
	}
	if (PendingEvents & EMXBikeEvent::Crashed)
	{
		for (int32 i = 0; i < 24; ++i)
		{
			FX->SpawnPuff(EMXPuff::Dust, GetActorLocation(), FVector(FMath::FRandRange(-200.f, 600.f), FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(50.f, 400.f)), 1.8f);
		}
	}
	if (PendingEvents & EMXBikeEvent::CoolStrip)
	{
		for (int32 i = 0; i < 16; ++i)
		{
			FX->SpawnPuff(EMXPuff::Steam, Rear + FVector(FMath::FRandRange(20.f, 120.f), FMath::FRandRange(-40.f, 40.f), 40.f),
				FVector(FMath::FRandRange(-100.f, 100.f), FMath::FRandRange(-100.f, 100.f), FMath::FRandRange(100.f, 300.f)), 1.2f);
		}
	}
	const UMXBikeTuning& T = MXTuning::Bike();
	if ((S.Heat > T.HeatWarning || S.Phase == EMXBikePhase::Stalled) && FMath::FRand() < DeltaSeconds * (S.Phase == EMXBikePhase::Stalled ? 30.f : 10.f))
	{
		const FVector Engine = BikeRoot->GetComponentTransform().TransformPosition(FVector(80.f, 0.f, 70.f));
		FX->SpawnPuff(S.Phase == EMXBikePhase::Stalled ? EMXPuff::Smoke : EMXPuff::Steam, Engine, FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), 160.f), 1.f);
	}
}
