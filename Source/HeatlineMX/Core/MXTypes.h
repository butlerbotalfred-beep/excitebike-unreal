#pragma once

#include "CoreMinimal.h"
#include "MXTypes.generated.h"

/** Reusable obstacle / track-piece library. NES Design-mode letters in comments. */
UENUM(BlueprintType)
enum class EMXObstacleType : uint8
{
	None,
	RampSmall,      // A  ($08)
	RampMedium,     // B  ($07)
	RampLarge,      // C  ($05)
	TableLow,       // D  ($01)
	RampSteep,      // E  ($0B)
	RampSteepBack,  // F  ($06) long face, steep landing side
	RampSteepFace,  // G  ($0A) steep face, long landing side
	Kicker,         // H  ($0E) short ramp with sheer drop
	Barrier,        // I/J ($02/$03/$04) small obstacle, lane pair
	Mud,            // K/L ($0C/$0D)
	CoolStrip,      // M/N ($0F/$10)
	Grass,          // O/P/Q ($12/$13/$11) "track missing" rough ground
	Mountain,       // R  ($15) two-step mesa
	PlatformDeck,   // S  ($14) ramp to a raised deck over lanes 1-2
	FinishDeck,     // finish structure ($09), lap line on top
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EMXSurface : uint8
{
	Dirt,
	Mud,
	Grass,
	Cool,
	Verge,
	Deck
};

UENUM(BlueprintType)
enum class EMXLandingGrade : uint8
{
	None,
	Perfect,
	Clean,
	Wobble,
	Crash
};

UENUM(BlueprintType)
enum class EMXControlScheme : uint8
{
	Classic,  // screen-relative: up/down = lanes, left/right = pitch
	Modern    // rider-relative: stick X = steer across track, stick Y = pitch
};

UENUM(BlueprintType)
enum class EMXRaceMode : uint8
{
	TimeTrial,
	RaceAI,
	Versus,
	Championship,
	DesignerTest,
	Attract
};

UENUM(BlueprintType)
enum class EMXLayoutVariant : uint8
{
	Challenge,  // NES qualifier layout (main-only pieces omitted)
	Main        // NES main-race layout (all pieces)
};

UENUM(BlueprintType)
enum class EMXAIDifficulty : uint8
{
	Easy,
	Medium,
	Hard
};

UENUM(BlueprintType)
enum class EMXBikePhase : uint8
{
	Grounded,
	Airborne,
	Crashed,     // tumbling
	Recovering,  // running back / remounting
	Stalled,     // overheated
	Finished
};

UENUM(BlueprintType)
enum class EMXCrashCause : uint8
{
	None,
	Landing,
	Barrier,
	Wheelie,
	Contact,
	Wall
};

UENUM(BlueprintType)
enum class EMXSplitOrientation : uint8
{
	Horizontal, // top / bottom (default: track runs horizontally)
	Vertical    // side by side
};

/** Camera tuning slot, one per viewport shape. */
UENUM(BlueprintType)
enum class EMXViewShape : uint8
{
	Full,
	Wide,     // 2P top/bottom (very wide)
	Tall,     // 2P side by side
	Quarter   // 3P/4P quadrant
};

/** Input for one bike for one frame. Identical for humans and AI. */
USTRUCT(BlueprintType)
struct FMXBikeInput
{
	GENERATED_BODY()

	/** 0..1 normal throttle. */
	UPROPERTY(BlueprintReadWrite) float Throttle = 0.f;
	/** Turbo button held. */
	UPROPERTY(BlueprintReadWrite) bool bTurbo = false;
	/** -1 = toward lane 1 (far side / screen up), +1 = toward lane 4 (near side / screen down). */
	UPROPERTY(BlueprintReadWrite) float Steer = 0.f;
	/** +1 = nose up (pull back / lean back), -1 = nose down (push forward). */
	UPROPERTY(BlueprintReadWrite) float Pitch = 0.f;
	/** Classic scheme: steer is interpreted as discrete lane taps. */
	UPROPERTY(BlueprintReadWrite) bool bLaneTaps = true;
	/** Counts accelerate presses this frame (recovery mashing). */
	UPROPERTY(BlueprintReadWrite) int32 MashPresses = 0;
};

namespace MX
{
	constexpr int32 NumLanes = 4;
	constexpr int32 MaxLocalPlayers = 4;
	constexpr int32 MaxBikes = 8;

	/** Lane mask helpers: bit 0 = lane 1 (far) ... bit 3 = lane 4 (near). */
	inline bool LaneInMask(int32 Mask, int32 LaneIndex0) { return (Mask >> LaneIndex0) & 1; }
	inline int32 MaskFromLanes(std::initializer_list<int32> Lanes1Based)
	{
		int32 M = 0;
		for (int32 L : Lanes1Based) { M |= 1 << (L - 1); }
		return M;
	}
	constexpr int32 AllLanes = 0xF;

	inline FString ObstacleLetter(EMXObstacleType T)
	{
		switch (T)
		{
		case EMXObstacleType::RampSmall: return TEXT("A");
		case EMXObstacleType::RampMedium: return TEXT("B");
		case EMXObstacleType::RampLarge: return TEXT("C");
		case EMXObstacleType::TableLow: return TEXT("D");
		case EMXObstacleType::RampSteep: return TEXT("E");
		case EMXObstacleType::RampSteepBack: return TEXT("F");
		case EMXObstacleType::RampSteepFace: return TEXT("G");
		case EMXObstacleType::Kicker: return TEXT("H");
		case EMXObstacleType::Barrier: return TEXT("I/J");
		case EMXObstacleType::Mud: return TEXT("K/L");
		case EMXObstacleType::CoolStrip: return TEXT("M/N");
		case EMXObstacleType::Grass: return TEXT("O/P/Q");
		case EMXObstacleType::Mountain: return TEXT("R");
		case EMXObstacleType::PlatformDeck: return TEXT("S");
		case EMXObstacleType::FinishDeck: return TEXT("FIN");
		default: return TEXT("-");
		}
	}

	HEATLINEMX_API FString ObstacleName(EMXObstacleType T);
	HEATLINEMX_API EMXObstacleType ObstacleFromString(const FString& S);
	HEATLINEMX_API FString ObstacleToString(EMXObstacleType T);

	/** Rider colour palette (distinct silhouettes + strong contrast). */
	HEATLINEMX_API FLinearColor RiderColor(int32 ColorIndex);
	HEATLINEMX_API FString RiderColorName(int32 ColorIndex);
	constexpr int32 NumRiderColors = 8;

	HEATLINEMX_API FString FormatRaceTime(float Seconds);
}
