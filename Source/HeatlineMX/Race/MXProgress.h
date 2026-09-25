#pragma once

#include "CoreMinimal.h"
#include "Race/MXRaceTypes.h"

struct FMXGate;

/** What happened to one rider's progress during one simulation step (the race manager turns it into HUD/audio). */
struct FMXProgressStep
{
	/** Lap lines crossed this step (0 or 1 in practice). */
	int32 LapsCompleted = 0;
	bool bFinished = false;
	/** Crossed the last validated gate backwards; it has to be crossed again. */
	bool bBackwardCrossing = false;
	/** Moved forward without it being movement (a reset or teleport); ignored. */
	bool bIgnoredJump = false;
};

/**
 * Checkpoint integrity, free of engine types so it can be tested headless.
 * Only real movement crosses gates, strictly in order. Resets and teleports never count, and crossing
 * the last gate backwards un-crosses it. The finish counts only as the final lap line crossed forward
 * after every earlier gate.
 */
namespace MXProgress
{
	/** Largest forward step (m) a fixed step can produce; anything bigger is not movement. */
	constexpr float MaxStepDistance = 1.0f;

	/** True when the move from SPrev to S in one fixed step is real riding (not a recovery or teleport). */
	HEATLINEMX_API bool IsValidMove(float SPrev, float S, bool bRecoveredThisStep);

	/** Advances one rider's validated progress for one fixed step. FinishCounter hands out finishing order. */
	HEATLINEMX_API FMXProgressStep Advance(FMXRacerProgress& P, const TArray<FMXGate>& Gates, int32 Laps, float RaceFinishS,
		float SPrev, float S, bool bValidMove, float RaceTime, int32& FinishCounter);
}
