#include "Core/MXTypes.h"

namespace MX
{
	FString ObstacleName(EMXObstacleType T)
	{
		switch (T)
		{
		case EMXObstacleType::RampSmall: return TEXT("Small ramp");
		case EMXObstacleType::RampMedium: return TEXT("Medium ramp");
		case EMXObstacleType::RampLarge: return TEXT("Large ramp");
		case EMXObstacleType::TableLow: return TEXT("Low table-top");
		case EMXObstacleType::RampSteep: return TEXT("Steep ramp");
		case EMXObstacleType::RampSteepBack: return TEXT("Ramp, steep back");
		case EMXObstacleType::RampSteepFace: return TEXT("Ramp, steep face");
		case EMXObstacleType::Kicker: return TEXT("Kicker");
		case EMXObstacleType::Barrier: return TEXT("Barrier");
		case EMXObstacleType::Mud: return TEXT("Mud");
		case EMXObstacleType::CoolStrip: return TEXT("Cool strip");
		case EMXObstacleType::Grass: return TEXT("Rough / grass");
		case EMXObstacleType::Mountain: return TEXT("Mountain");
		case EMXObstacleType::PlatformDeck: return TEXT("Platform deck");
		case EMXObstacleType::FinishDeck: return TEXT("Finish deck");
		default: return TEXT("None");
		}
	}

	FString ObstacleToString(EMXObstacleType T)
	{
		switch (T)
		{
		case EMXObstacleType::RampSmall: return TEXT("RampSmall");
		case EMXObstacleType::RampMedium: return TEXT("RampMedium");
		case EMXObstacleType::RampLarge: return TEXT("RampLarge");
		case EMXObstacleType::TableLow: return TEXT("TableLow");
		case EMXObstacleType::RampSteep: return TEXT("RampSteep");
		case EMXObstacleType::RampSteepBack: return TEXT("RampSteepBack");
		case EMXObstacleType::RampSteepFace: return TEXT("RampSteepFace");
		case EMXObstacleType::Kicker: return TEXT("Kicker");
		case EMXObstacleType::Barrier: return TEXT("Barrier");
		case EMXObstacleType::Mud: return TEXT("Mud");
		case EMXObstacleType::CoolStrip: return TEXT("CoolStrip");
		case EMXObstacleType::Grass: return TEXT("Grass");
		case EMXObstacleType::Mountain: return TEXT("Mountain");
		case EMXObstacleType::PlatformDeck: return TEXT("PlatformDeck");
		case EMXObstacleType::FinishDeck: return TEXT("FinishDeck");
		default: return TEXT("None");
		}
	}

	EMXObstacleType ObstacleFromString(const FString& S)
	{
		for (int32 i = 1; i < (int32)EMXObstacleType::Count; ++i)
		{
			const EMXObstacleType T = (EMXObstacleType)i;
			if (S.Equals(ObstacleToString(T), ESearchCase::IgnoreCase))
			{
				return T;
			}
		}
		return EMXObstacleType::None;
	}

	FLinearColor RiderColor(int32 ColorIndex)
	{
		static const FLinearColor Colors[NumRiderColors] = {
			FLinearColor(0.85f, 0.06f, 0.05f),  // red (the NES player bike)
			FLinearColor(0.05f, 0.30f, 0.95f),  // blue
			FLinearColor(0.98f, 0.72f, 0.02f),  // yellow
			FLinearColor(0.05f, 0.75f, 0.20f),  // green
			FLinearColor(0.95f, 0.35f, 0.02f),  // orange
			FLinearColor(0.55f, 0.10f, 0.85f),  // purple
			FLinearColor(0.02f, 0.80f, 0.85f),  // cyan
			FLinearColor(0.92f, 0.20f, 0.60f),  // pink
		};
		return Colors[((ColorIndex % NumRiderColors) + NumRiderColors) % NumRiderColors];
	}

	FString RiderColorName(int32 ColorIndex)
	{
		static const TCHAR* Names[NumRiderColors] = {
			TEXT("Red"), TEXT("Blue"), TEXT("Yellow"), TEXT("Green"),
			TEXT("Orange"), TEXT("Purple"), TEXT("Cyan"), TEXT("Pink")
		};
		return Names[((ColorIndex % NumRiderColors) + NumRiderColors) % NumRiderColors];
	}

	FString FormatRaceTime(float Seconds)
	{
		if (Seconds < 0.f || !FMath::IsFinite(Seconds))
		{
			return TEXT("--:--.--");
		}
		const int32 TotalHundredths = FMath::FloorToInt(Seconds * 100.f + 0.5f);
		const int32 Minutes = TotalHundredths / 6000;
		const int32 Secs = (TotalHundredths / 100) % 60;
		const int32 Hund = TotalHundredths % 100;
		return FString::Printf(TEXT("%d:%02d.%02d"), Minutes, Secs, Hund);
	}
}
