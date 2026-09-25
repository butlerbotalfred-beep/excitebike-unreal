#include "Race/MXPlayerController.h"
#include "Race/MXGameMode.h"
#include "Bike/MXBike.h"
#include "Core/MXTuning.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

AMXPlayerController::AMXPlayerController()
{
	PlayerCameraManagerClass = AMXPlayerCameraManager::StaticClass();
	bShowMouseCursor = false;
	bAutoManageActiveCameraTarget = false;
}

void AMXPlayerController::ApplyProfile(const FMXControlProfile& InProfile)
{
	Profile = InProfile;
	ULocalPlayer* LP = GetLocalPlayer();
	if (!LP)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
	{
		Sub->ClearAllMappings();
		Context = UMXInputConfig::Get()->BuildContext(Profile, this);
		Sub->AddMappingContext(Context, 0);
	}
}

void AMXPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		UMXInputConfig* Cfg = UMXInputConfig::Get();
		EIC->BindAction(Cfg->Accelerate, ETriggerEvent::Started, this, &AMXPlayerController::OnAccelerateStarted);
		EIC->BindAction(Cfg->Pause, ETriggerEvent::Started, this, &AMXPlayerController::OnPausePressed);
	}
}

void AMXPlayerController::OnAccelerateStarted()
{
	++MashCounter;
	if (AMXBike* Bike = Cast<AMXBike>(GetPawn()))
	{
		Bike->CurrentInput.MashPresses++;
	}
}

void AMXPlayerController::OnPausePressed()
{
	if (AMXGameMode* GM = GetWorld()->GetAuthGameMode<AMXGameMode>())
	{
		GM->RequestPause(Slot);
	}
}

void AMXPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UEnhancedPlayerInput* EPI = Cast<UEnhancedPlayerInput>(PlayerInput);
	if (!EPI)
	{
		return;
	}
	UMXInputConfig* Cfg = UMXInputConfig::Get();
	FMXBikeInput In;
	In.Throttle = FMath::Clamp(EPI->GetActionValue(Cfg->Accelerate).Get<float>(), 0.f, 1.f);
	In.bTurbo = EPI->GetActionValue(Cfg->Turbo).Get<bool>();
	In.Steer = FMath::Clamp(EPI->GetActionValue(Cfg->Steer).Get<float>(), -1.f, 1.f);
	In.Pitch = FMath::Clamp(EPI->GetActionValue(Cfg->Pitch).Get<float>(), -1.f, 1.f);
	In.bLaneTaps = Profile.Scheme == EMXControlScheme::Classic;
	LastSampled = In;
	if (AMXBike* Bike = Cast<AMXBike>(GetPawn()))
	{
		const int32 Mash = Bike->CurrentInput.MashPresses;
		Bike->CurrentInput = In;
		Bike->CurrentInput.MashPresses = Mash;
	}
}

// ------------------------------------------------------------------------------------------------

AMXPlayerCameraManager::AMXPlayerCameraManager()
{
	DefaultFOV = 58.f;
	ViewPitchMin = -89.f;
	ViewPitchMax = 89.f;
}

EMXViewShape AMXPlayerCameraManager::ComputeShape() const
{
	const APlayerController* PC = GetOwningPlayerController();
	const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LP || !LP->ViewportClient)
	{
		return EMXViewShape::Full;
	}
	FVector2D VP(1920.f, 1080.f);
	LP->ViewportClient->GetViewportSize(VP);
	const float W = LP->Size.X * VP.X;
	const float H = FMath::Max(1.f, LP->Size.Y * (float)VP.Y);
	const float Aspect = W / H;
	ViewAspect = Aspect;
	if (Aspect > 2.4f)
	{
		return EMXViewShape::Wide;
	}
	if (Aspect < 1.25f)
	{
		return EMXViewShape::Tall;
	}
	if (LP->Size.X < 0.9f && LP->Size.Y < 0.9f)
	{
		return EMXViewShape::Quarter;
	}
	return EMXViewShape::Full;
}

void AMXPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	AMXPlayerController* PC = Cast<AMXPlayerController>(GetOwningPlayerController());
	if (!PC)
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		return;
	}
	Shape = ComputeShape();
	const UMXCameraTuning& CT = MXTuning::Camera();
	const bool bDesigner = PC->CamMode == EMXCamMode::Designer;
	const FMXCameraShapeTuning& T = bDesigner ? CT.Designer : CT.ForShape(Shape);

	FVector Bike(0.f, 0.f, 0.f);
	float Speed = 0.f;
	float BikeHeight = 0.f;
	float BikeY = 0.f;
	if (bDesigner)
	{
		Bike = FVector(PC->DesignerS * 100.f, 0.f, 0.f);
	}
	else if (AMXBike* B = PC->FocusBike.Get())
	{
		Bike = B->GetVisualLocation();
		Speed = B->State.ForwardSpeed();
		BikeHeight = Bike.Z / 100.f;
		BikeY = Bike.Y / 100.f;
		if (B->State.bFinished)
		{
			Speed = FMath::Min(Speed, 12.f);
		}
	}

	const float Zoom = bDesigner ? FMath::Clamp(PC->DesignerZoom, 0.4f, 3.f) : 1.f;
	// The point the camera frames: locked to the bike along the track; lane and height follow partially.
	const FVector DesiredAnchor(Bike.X, T.LateralFollow * BikeY * 100.f, T.HeightFollow * BikeHeight * 100.f + 100.f);
	if (!bInit || PC->bSnapCamera)
	{
		Anchor = DesiredAnchor;
		bInit = true;
		PC->bSnapCamera = false;
	}
	else
	{
		const float Dt = FMath::Min(DeltaTime, 0.05f);
		const float KY = 1.f - FMath::Exp(-Dt / FMath::Max(0.01f, T.FollowLag));
		const float KZ = 1.f - FMath::Exp(-Dt / FMath::Max(0.01f, T.HeightLag));
		Anchor.X = DesiredAnchor.X;
		Anchor.Y = FMath::Lerp(Anchor.Y, DesiredAnchor.Y, KY);
		Anchor.Z = FMath::Lerp(Anchor.Z, DesiredAnchor.Z, KZ);
	}

	// Fixed orientation: look across the track (towards -Y), turned towards travel by Yaw, pitched down.
	const FRotator Rot(-T.Pitch, -90.f + T.Yaw, 0.f);
	const FRotationMatrix M(Rot);
	const FVector Fwd = M.GetScaledAxis(EAxis::X);
	const FVector Right = M.GetScaledAxis(EAxis::Y);
	const FVector Up = M.GetScaledAxis(EAxis::Z);
	const float TanH = FMath::Tan(FMath::DegreesToRadians(T.FOV * 0.5f));
	const float TanV = TanH / FMath::Max(0.2f, ViewAspect);
	const float SpeedFrac = FMath::Clamp(Speed / FMath::Max(1.f, MXTuning::Bike().MaxSpeedTurbo), 0.f, 1.f);
	const float SX = T.ScreenX - T.SpeedScreenShift * SpeedFrac;
	const FVector Ray = (Fwd + Right * ((SX - 0.5f) * 2.f * TanH) + Up * ((0.5f - T.ScreenY) * 2.f * TanV)).GetSafeNormal();
	CamPos = Anchor - Ray * T.Distance * 100.f * Zoom;

	// Small shake on hard landings / crashes for the followed bike.
	if (AMXBike* B = PC->FocusBike.Get())
	{
		if (B->State.Events & EMXBikeEvent::Crashed)
		{
			Shake = 1.f;
		}
		else if ((B->State.Events & EMXBikeEvent::Landed) && B->State.SuspensionImpulse > 10.f)
		{
			Shake = FMath::Max(Shake, 0.4f);
		}
	}
	Shake = FMath::Max(0.f, Shake - DeltaTime * 3.f);
	const FVector ShakeOffset = Shake > 0.f ? FVector(0.f, 0.f, FMath::Sin(GetWorld()->GetTimeSeconds() * 60.f) * 12.f * Shake) : FVector::ZeroVector;

	OutVT.POV.Location = CamPos + ShakeOffset;
	OutVT.POV.Rotation = Rot;
	OutVT.POV.FOV = T.FOV;

	// Visible look-ahead: how far along the rider's own lane the track stays inside the view (it recedes
	// towards the top right), in seconds at the current speed.
	float AheadM = 0.f;
	for (float Ahead = 1.f; Ahead <= 200.f; Ahead += 1.f)
	{
		const FVector P(Bike.X + Ahead * 100.f, BikeY * 100.f, Bike.Z);
		const FVector V = P - CamPos;
		const float Zc = FVector::DotProduct(V, Fwd);
		if (Zc <= 1.f)
		{
			break;
		}
		const float SXp = 0.5f + FVector::DotProduct(V, Right) / (Zc * 2.f * TanH);
		const float SYp = 0.5f - FVector::DotProduct(V, Up) / (Zc * 2.f * TanV);
		if (SXp > 1.f || SYp < 0.f)
		{
			break;
		}
		AheadM = Ahead;
	}
	VisibleLookAheadSeconds = AheadM / FMath::Max(5.f, Speed);
}

