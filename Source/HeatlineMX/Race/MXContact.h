#pragma once

#include "CoreMinimal.h"
#include "Bike/MXBikeSim.h"

class FMXTrackModel;
class UMXBikeTuning;

/** What happened to one rider in a contact (for HUD messages). */
enum class EMXContactOutcome : uint8
{
	None,
	SideBump,
	HeldBack,
	CutOff,       // wobble: the rider ahead cut in
	CooldownBump, // wobble: already crashed by contact recently
	LandingBump,  // wobble: one of them had only just landed, so neither could react
	Crashed       // rear-ended a rear wheel (NES rule)
};

struct FMXContactResult
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	int32 Rear = INDEX_NONE;
	EMXContactOutcome Outcome = EMXContactOutcome::None;
};

/**
 * Rider-to-rider contact rules (pure, deterministic, unit-tested):
 *  - same lane band, front wheel into rear wheel: the rear rider is at fault; crashes if closing > 3 m/s
 *  - but it is only a wobble (then held behind) if the front rider cut in during the last 0.5 s, if either
 *    rider landed less than 0.5 s ago (nobody could react), or if the rear rider was contact-crashed in the
 *    last 6 s (no unavoidable crash chains)
 *  - gentle closing: the rear rider is held behind the leader
 *  - side by side: pushed apart, small speed loss, never a crash
 *  - ghosted (just remounted), crashed, stalled and finished riders never collide
 */
namespace MXContact
{
	HEATLINEMX_API void Resolve(TArrayView<FMXBikeState*> Bikes, const FMXTrackModel& Track, const UMXBikeTuning& T, TArray<FMXContactResult>& OutResults);
}
