#include "UI/MXHUD.h"
#include "UI/MXDraw.h"
#include "Race/MXGameMode.h"
#include "Race/MXRaceManager.h"
#include "Race/MXPlayerController.h"
#include "Bike/MXBike.h"
#include "Core/MXTuning.h"
#include "Session/MXInputConfig.h"
#include "Session/MXGameInstance.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"

void AMXHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	AMXGameMode* GM = GetWorld()->GetAuthGameMode<AMXGameMode>();
	AMXPlayerController* PC = Cast<AMXPlayerController>(PlayerOwner);
	if (!GM || !PC || !GM->IsRaceHUDVisible())
	{
		return;
	}
	AMXRaceManager* Race = GM->GetRace();
	if (!Race || Race->NumRacers() == 0)
	{
		return;
	}
	const int32 Racer = Race->RacerForSlot(PC->Slot);
	if (Racer == INDEX_NONE)
	{
		return;
	}
	DrawRace(Race, Racer);
}

void AMXHUD::DrawRace(AMXRaceManager* Race, int32 Racer)
{
	UCanvas* C = Canvas;
	const float W = C->ClipX;
	const float H = C->ClipY;
	const float U = FMath::Max(0.7f, H / 720.f);
	const float M = 16.f * U;
	AMXBike* Bike = Race->GetBike(Racer);
	if (!Bike)
	{
		return;
	}
	const FMXBikeState& S = Bike->State;
	const FMXRacerProgress& P = Race->GetProgress(Racer);
	const FMXRaceConfig& Cfg = Race->GetConfig();
	const FMXRacerConfig& RC = Cfg.Racers[Racer];
	const FLinearColor RiderCol = MX::RiderColor(RC.ColorIndex);
	const int32 Laps = Race->GetTrack().GetLaps();
	const float Time = Race->GetRaceTime();

	// ---- Top-left: player tag + position + lap ----
	MXDraw::Rect(C, 0.f, 0.f, 8.f * U, H, FLinearColor(RiderCol.R, RiderCol.G, RiderCol.B, 0.9f)); // colour edge identifies the view
	const bool bSolo = Race->NumRacers() == 1;
	float Y = M;
	// Default names are just "P<n>"; show the rider colour instead so players can find their bike.
	const FString Tag = FString::Printf(TEXT("P%d"), RC.PlayerSlot + 1);
	const FString Label = RC.Name == Tag ? MX::RiderColorName(RC.ColorIndex).ToUpper() : RC.Name;
	MXDraw::Text(C, FString::Printf(TEXT("%s  %s"), *Tag, *Label), M + 8.f * U, Y, Px(18.f, U), RiderCol);
	Y += Px(22.f, U);
	if (!bSolo)
	{
		const FVector2D Sz = MXDraw::Text(C, MXDraw::Ordinal(P.Position), M + 8.f * U, Y, Px(64.f, U), FLinearColor::White);
		MXDraw::Text(C, FString::Printf(TEXT("/%d"), Race->NumRacers()), M + 12.f * U + Sz.X, Y + Sz.Y * 0.38f, Px(28.f, U), FLinearColor(0.85f, 0.85f, 0.85f));
		Y += Sz.Y;
	}
	const int32 LapShown = FMath::Clamp(P.LapsCompleted + 1, 1, Laps);
	MXDraw::Text(C, P.bFinished ? TEXT("FINISHED") : FString::Printf(TEXT("LAP %d/%d"), LapShown, Laps), M + 8.f * U, Y, Px(30.f, U), FLinearColor(1.f, 0.9f, 0.35f));

	// ---- Top-right: time, best lap, ghost delta ----
	const float ShownTime = P.bFinished ? P.FinishTime : Time;
	MXDraw::Text(C, MX::FormatRaceTime(ShownTime), W - M, M, Px(36.f, U), FLinearColor::White, 1.f);
	float TY = M + MXDraw::LineHeight(Px(36.f, U)) * 0.85f;
	if (P.BestLap > 0.f)
	{
		MXDraw::Text(C, FString::Printf(TEXT("BEST LAP %s"), *MX::FormatRaceTime(P.BestLap)), W - M, TY, Px(20.f, U), FLinearColor(0.8f, 0.9f, 1.f), 1.f);
		TY += MXDraw::LineHeight(Px(20.f, U)) * 0.9f;
	}
	if (Cfg.Mode == EMXRaceMode::TimeTrial && Race->GetGhostTotal() > 0.f)
	{
		MXDraw::Text(C, FString::Printf(TEXT("GHOST BEST %s"), *MX::FormatRaceTime(Race->GetGhostTotal())), W - M, TY, Px(20.f, U), FLinearColor(0.7f, 0.85f, 1.f), 1.f);
	}

	// ---- Bottom: heat gauge (prominent) ----
	const float GaugeW = FMath::Clamp(W * 0.46f, 260.f, 900.f);
	const float GaugeH = FMath::Max(20.f, 28.f * U);
	const float GaugeX = (W - GaugeW) * 0.5f;
	const float GaugeY = H - M - GaugeH;
	DrawHeatGauge(GaugeX, GaugeY, GaugeW, GaugeH, U, S);

	// ---- Bottom-left: speed ----
	const float Kmh = S.ForwardSpeed() * 3.6f;
	const FVector2D NumSz = MXDraw::Text(C, FString::Printf(TEXT("%d"), FMath::RoundToInt(Kmh)), M + 8.f * U, H - M, Px(44.f, U), FLinearColor::White, 0.f, 1.f);
	// The font box includes line spacing; the digits' tops sit about 20 % below it.
	MXDraw::Text(C, TEXT("KM/H"), M + 10.f * U + NumSz.X, H - M - NumSz.Y * 0.2f, Px(15.f, U), FLinearColor(0.8f, 0.8f, 0.8f), 0.f, 1.f);

	// ---- Progress strip (all riders along the race distance) ----
	if (!bSolo || Cfg.Mode == EMXRaceMode::TimeTrial)
	{
		DrawProgressStrip(Race, Racer, U);
	}

	// ---- Countdown / GO ----
	if (Race->GetPhase() == EMXRacePhase::Countdown || (Race->GetPhase() == EMXRacePhase::Racing && Time < 0.8f))
	{
		const float Cd = Race->GetCountdown();
		const FString Txt = Race->GetPhase() == EMXRacePhase::Countdown ? FString::FromInt(FMath::Max(1, FMath::CeilToInt(Cd))) : TEXT("GO!");
		MXDraw::Text(C, Txt, W * 0.5f, H * 0.36f, Px(130.f, U), Race->GetPhase() == EMXRacePhase::Countdown ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(0.3f, 1.f, 0.4f), 0.5f, 0.5f);
	}

	// ---- Controls reminder at the start ----
	if (Race->GetPhase() != EMXRacePhase::Finished && (Race->GetPhase() != EMXRacePhase::Racing || Time < 6.f))
	{
		const AMXPlayerController* PC = Cast<AMXPlayerController>(PlayerOwner);
		if (PC && RC.IsHuman())
		{
			const TArray<FString> Lines = UMXInputConfig::DescribeLayout(PC->GetProfile(), !GetWorld()->GetAuthGameMode<AMXGameMode>()->SlotUsesKeyboard(PC->Slot));
			const float Sz = Px(17.f, U);
			const float LH = MXDraw::LineHeight(Sz, false) * 0.92f;
			const float PanelW = FMath::Min(W - 2.f * M, GaugeW * 1.3f);
			float LY = GaugeY - Px(10.f, U) - Lines.Num() * LH;
			MXDraw::Rect(C, W * 0.5f - PanelW * 0.5f, LY - 4.f * U, PanelW, Lines.Num() * LH + 8.f * U, FLinearColor(0.f, 0.f, 0.f, 0.5f));
			for (const FString& L : Lines)
			{
				MXDraw::TextFit(C, L, W * 0.5f, LY, Sz, PanelW - 16.f * U, FLinearColor(0.95f, 0.95f, 0.95f), 0.5f, 0.f, false);
				LY += LH;
			}
		}
	}

	// ---- Messages (landing grades, crash reasons, laps) ----
	const TArray<FMXHudMessage>& Msgs = Race->GetMessages(Racer);
	float MY = H * 0.24f;
	for (int32 i = Msgs.Num() - 1; i >= 0; --i)
	{
		const FMXHudMessage& Msg = Msgs[i];
		const float Age = Time - Msg.Time;
		if (Age > Msg.Duration)
		{
			continue;
		}
		const float Alpha = FMath::Clamp((Msg.Duration - Age) * 3.f, 0.f, 1.f);
		const float Pop = 1.f + FMath::Max(0.f, 0.25f - Age) * 1.2f;
		FLinearColor Col = Msg.Color;
		Col.A = Alpha;
		const FVector2D Sz = MXDraw::Text(C, Msg.Text, W * 0.5f, MY, Px(34.f * Msg.Scale * Pop, U), Col, 0.5f, 0.f);
		MY += Sz.Y + 4.f * U;
	}

	// ---- Crash recovery prompt ----
	if (S.Phase == EMXBikePhase::Recovering || S.Phase == EMXBikePhase::Crashed)
	{
		const UMXBikeTuning& T = MXTuning::Bike();
		const float Frac = S.Phase == EMXBikePhase::Recovering ? FMath::Clamp(1.f - S.RecoveryTimer / FMath::Max(0.1f, T.RecoveryBase), 0.f, 1.f) : 0.f;
		const float BW = GaugeW * 0.6f;
		const float BX = (W - BW) * 0.5f;
		const float BY = H * 0.62f;
		MXDraw::Rect(C, BX, BY, BW, 14.f * U, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		MXDraw::Rect(C, BX, BY, BW * Frac, 14.f * U, FLinearColor(1.f, 1.f, 1.f, 0.9f));
		MXDraw::Text(C, TEXT("MASH ACCELERATE"), W * 0.5f, BY - 4.f * U, Px(22.f, U), FLinearColor::White, 0.5f, 1.f);
	}

	// ---- Airborne landing meter (option) ----
	const UMXGameInstance* GI = Cast<UMXGameInstance>(GetGameInstance());
	if (!GI || !GI->GetSave() || GI->GetSave()->Settings.bLandingMeter)
	{
		DrawLandingMeter(Race, Racer, U);
	}

	// ---- Finish banner ----
	if (P.bFinished && !P.bEstimated)
	{
		const FString Place = bSolo ? TEXT("FINISH") : FString::Printf(TEXT("%s PLACE"), *MXDraw::Ordinal(P.FinishOrder));
		MXDraw::Text(C, Place, W * 0.5f, H * 0.5f, Px(64.f, U), P.FinishOrder == 1 ? FLinearColor(1.f, 0.85f, 0.15f) : FLinearColor::White, 0.5f, 0.5f);
		MXDraw::Text(C, MX::FormatRaceTime(P.FinishTime), W * 0.5f, H * 0.5f + Px(46.f, U), Px(30.f, U), FLinearColor::White, 0.5f, 0.f);
	}
}

void AMXHUD::DrawHeatGauge(float X, float Y, float W, float H, float U, const FMXBikeState& S)
{
	UCanvas* C = Canvas;
	const UMXBikeTuning& T = MXTuning::Bike();
	const float Heat01 = FMath::Clamp(S.Heat / FMath::Max(1.f, T.HeatOverheat), 0.f, 1.f);
	const bool bStalled = S.Phase == EMXBikePhase::Stalled;
	const float TimeNow = GetWorld()->GetTimeSeconds();
	if (S.Events & EMXBikeEvent::CoolStrip)
	{
		CoolFlash = 1.f;
	}
	CoolFlash = FMath::Max(0.f, CoolFlash - GetWorld()->GetDeltaSeconds() * 1.6f);

	// Label.
	const float LabelSize = Px(22.f, U);
	MXDraw::Text(C, TEXT("HEAT"), X - 10.f * U, Y + H * 0.5f, LabelSize, FLinearColor::White, 1.f, 0.5f);
	// Back plate + frame.
	MXDraw::Rect(C, X - 3.f * U, Y - 3.f * U, W + 6.f * U, H + 6.f * U, FLinearColor(0.f, 0.f, 0.f, 0.65f));
	// Fill with segmented look.
	const int32 Segs = 20;
	const float SegW = W / Segs;
	for (int32 i = 0; i < Segs; ++i)
	{
		const float SegStart = (float)i / Segs;
		if (SegStart >= Heat01)
		{
			MXDraw::Rect(C, X + i * SegW + 1.f, Y + 1.f, SegW - 2.f, H - 2.f, FLinearColor(0.2f, 0.2f, 0.22f, 0.6f));
			continue;
		}
		FLinearColor Col = MXDraw::HeatColor(SegStart + 0.5f / Segs);
		if (bStalled && FMath::Fmod(TimeNow, 0.3f) < 0.15f)
		{
			Col = FLinearColor(1.f, 1.f, 1.f, 1.f);
		}
		const float Frac = FMath::Clamp((Heat01 - SegStart) * Segs, 0.f, 1.f);
		MXDraw::Rect(C, X + i * SegW + 1.f, Y + 1.f, (SegW - 2.f) * Frac, H - 2.f, Col);
	}
	// Warning threshold tick.
	const float WarnX = X + W * (T.HeatWarning / FMath::Max(1.f, T.HeatOverheat));
	MXDraw::Rect(C, WarnX - 1.5f * U, Y - 6.f * U, 3.f * U, H + 12.f * U, FLinearColor(1.f, 0.3f, 0.2f, 0.9f));
	if (CoolFlash > 0.f)
	{
		MXDraw::Frame(C, X - 5.f * U, Y - 5.f * U, W + 10.f * U, H + 10.f * U, 4.f * U, FLinearColor(0.3f, 0.8f, 1.f, CoolFlash));
	}
	// Right side: turbo lamp / overheat countdown.
	if (bStalled)
	{
		MXDraw::Text(C, FString::Printf(TEXT("OVERHEAT %.1fs"), FMath::Max(0.f, S.StallTimer)), X + W + 12.f * U, Y + H * 0.5f, Px(22.f, U), FLinearColor(1.f, 0.25f, 0.15f), 0.f, 0.5f);
	}
	else
	{
		const bool bTurbo = S.bTurboActive;
		MXDraw::Text(C, TEXT("TURBO"), X + W + 12.f * U, Y + H * 0.5f, Px(22.f, U), bTurbo ? FLinearColor(1.f, 0.55f, 0.1f) : FLinearColor(0.4f, 0.4f, 0.4f, 0.8f), 0.f, 0.5f);
	}
	if (Heat01 >= T.HeatWarning / T.HeatOverheat && !bStalled && FMath::Fmod(TimeNow, 0.4f) < 0.25f)
	{
		MXDraw::Text(C, TEXT("HOT - EASE OFF OR FIND A COOL STRIP"), X + W * 0.5f, Y - 8.f * U, Px(18.f, U), FLinearColor(1.f, 0.35f, 0.2f), 0.5f, 1.f);
	}
}

void AMXHUD::DrawLandingMeter(AMXRaceManager* Race, int32 Racer, float U)
{
	AMXBike* Bike = Race->GetBike(Racer);
	if (!Bike || !Bike->State.IsAirborne() || Bike->State.AirTime < 0.12f || Bike->State.bBounce)
	{
		return;
	}
	FVector2D Screen;
	if (!PlayerOwner->ProjectWorldLocationToScreen(Bike->GetVisualLocation() + FVector(0.f, 0.f, 260.f), Screen, true))
	{
		return;
	}
	const UMXBikeTuning& T = MXTuning::Bike();
	const FMXFlightPrediction Pred = FMXBikeSim::PredictFlight(Bike->State, Bike->CurrentInput.Pitch, Race->GetTrack(), T, 2.5f);
	if (!Pred.bLands)
	{
		return;
	}
	const float R = 44.f * U;
	const FVector2D Ctr = Screen;
	// Clean window around the landing surface angle (green), perfect window (bright).
	auto Dir = [](float Deg) { const float Rad = FMath::DegreesToRadians(Deg); return FVector2D(FMath::Cos(Rad), -FMath::Sin(Rad)); };
	for (float D = T.CleanMin; D <= T.CleanMax; D += 2.5f)
	{
		const bool bPerfect = FMath::Abs(D) <= T.PerfectTolerance;
		MXDraw::Line(Canvas, Ctr + Dir(Pred.SurfaceDeg + D) * R * 0.55f, Ctr + Dir(Pred.SurfaceDeg + D) * R, 3.f * U,
			bPerfect ? FLinearColor(0.3f, 1.f, 0.4f, 0.9f) : FLinearColor(0.2f, 0.6f, 0.25f, 0.6f));
	}
	// The bike's current pitch.
	const float Pitch = Bike->State.Pitch;
	const float Delta = Pitch - Pred.SurfaceDeg;
	const EMXLandingGrade G = FMXBikeSim::ClassifyLanding(Delta, Pred.SurfaceDeg, T);
	const FLinearColor NeedleCol = G == EMXLandingGrade::Crash ? FLinearColor(1.f, 0.2f, 0.15f) : (G == EMXLandingGrade::Wobble ? FLinearColor(1.f, 0.75f, 0.2f) : FLinearColor::White);
	MXDraw::Line(Canvas, Ctr - Dir(Pitch) * R * 0.3f, Ctr + Dir(Pitch) * R * 1.15f, 4.f * U, NeedleCol);
}

void AMXHUD::DrawProgressStrip(AMXRaceManager* Race, int32 Racer, float U)
{
	UCanvas* C = Canvas;
	const float W = C->ClipX;
	const float StripW = W * 0.36f;
	const float X0 = (W - StripW) * 0.5f;
	const float Y0 = 16.f * U + 8.f * U;
	const float Len = FMath::Max(1.f, Race->GetTrack().RaceFinishS());
	MXDraw::Rect(C, X0, Y0, StripW, 4.f * U, FLinearColor(1.f, 1.f, 1.f, 0.5f));
	for (int32 Lap = 0; Lap < Race->GetTrack().GetLaps(); ++Lap)
	{
		const float Lx = X0 + StripW * FMath::Clamp(Race->GetTrack().LapLineS(Lap) / Len, 0.f, 1.f);
		MXDraw::Rect(C, Lx - 1.f, Y0 - 6.f * U, 2.f, 16.f * U, FLinearColor(1.f, 1.f, 1.f, 0.8f));
	}
	for (int32 i = 0; i < Race->NumRacers(); ++i)
	{
		const AMXBike* B = Race->GetBike(i);
		if (!B)
		{
			continue;
		}
		const float F = FMath::Clamp(B->State.S / Len, 0.f, 1.f);
		const float Sz = (i == Racer ? 12.f : 8.f) * U;
		const FLinearColor Col = MX::RiderColor(Race->GetConfig().Racers[i].ColorIndex);
		MXDraw::Rect(C, X0 + StripW * F - Sz * 0.5f, Y0 + 2.f * U - Sz * 0.5f, Sz, Sz, FLinearColor(Col.R, Col.G, Col.B, i == Racer ? 1.f : 0.85f));
	}
}
