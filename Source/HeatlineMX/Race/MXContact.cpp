#include "Race/MXContact.h"
#include "Track/MXTrackModel.h"
#include "Core/MXTuning.h"

namespace MXContact
{
	namespace
	{
		/** Seconds on the ground since the last landing (riders that haven't jumped yet count as settled). */
		float GroundedFor(const FMXBikeState& S)
		{
			return S.Phase == EMXBikePhase::Grounded ? S.SimTime - S.LastLandingTime : 0.f;
		}

		/** The rear rider ends up behind the one ahead, no faster than them. */
		void HoldBehind(FMXBikeState& Rear, const FMXBikeState& Front, const UMXBikeTuning& T)
		{
			const float FrontV = Front.ForwardSpeed();
			if (Rear.Phase == EMXBikePhase::Grounded)
			{
				Rear.Speed = FMath::Min(Rear.Speed, FMath::Max(0.f, FrontV - 0.5f));
			}
			else
			{
				Rear.VX = FMath::Min(Rear.VX, FMath::Max(1.5f, FrontV - 0.5f));
			}
			Rear.S = FMath::Min(Rear.S, Front.S - T.BikeContactLength);
			Rear.Events |= EMXBikeEvent::Bumped;
		}
	}

	void Resolve(TArrayView<FMXBikeState*> Bikes, const FMXTrackModel& Track, const UMXBikeTuning& T, TArray<FMXContactResult>& Out)
	{
		const int32 N = Bikes.Num();
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				FMXBikeState& A = *Bikes[i];
				FMXBikeState& B = *Bikes[j];
				if (!A.CanCollide() || !B.CanCollide() || A.bFinished || B.bFinished)
				{
					continue;
				}
				if (FMath::Abs(A.Z - B.Z) > T.BikeContactHeight)
				{
					continue;
				}
				const float Dy = B.Y - A.Y;
				if (FMath::Abs(Dy) > T.BikeContactWidth)
				{
					continue;
				}
				const float Ds = B.S - A.S;
				if (FMath::Abs(Ds) > T.BikeContactLength)
				{
					continue;
				}
				FMXContactResult R;
				R.A = i;
				R.B = j;
				if (FMath::Abs(Ds) < T.BikeContactLength * 0.55f)
				{
					// Side by side: push apart, never a crash.
					const float Sign = Dy >= 0.f ? 1.f : -1.f;
					const float Overlap = T.BikeContactWidth - FMath::Abs(Dy);
					A.Y -= Sign * Overlap * 0.5f;
					B.Y += Sign * Overlap * 0.5f;
					A.VY = -Sign * T.SideBumpImpulse;
					B.VY = Sign * T.SideBumpImpulse;
					A.TargetLane = Track.NearestLane(A.Y);
					B.TargetLane = Track.NearestLane(B.Y);
					for (FMXBikeState* S : {&A, &B})
					{
						if (S->Phase == EMXBikePhase::Grounded)
						{
							S->Speed *= 1.f - T.SideBumpSpeedLoss;
						}
						S->Events |= EMXBikeEvent::Bumped;
					}
					R.Outcome = EMXContactOutcome::SideBump;
					Out.Add(R);
					continue;
				}
				FMXBikeState& Rear = Ds > 0.f ? A : B;
				FMXBikeState& Front = Ds > 0.f ? B : A;
				R.Rear = Ds > 0.f ? i : j;
				const float Closing = Rear.ForwardSpeed() - Front.ForwardSpeed();
				if (Closing > T.RearEndCrashSpeed)
				{
					const bool bCutIn = Front.LastLateralTime < T.CutInWindow;
					// Only a rider who had time to see it coming crashes: both settled on the ground.
					const bool bJustLanded = GroundedFor(Rear) < T.ContactReactTime || GroundedFor(Front) < T.ContactReactTime;
					if (bCutIn || bJustLanded || Rear.ContactCooldown > 0.f)
					{
						FMXBikeSim::Wobble(Rear, T.CutInSpeedLoss, T);
						HoldBehind(Rear, Front, T);
						Rear.ContactCooldown = FMath::Max(Rear.ContactCooldown, 1.f);
						R.Outcome = bCutIn ? EMXContactOutcome::CutOff
							: bJustLanded ? EMXContactOutcome::LandingBump : EMXContactOutcome::CooldownBump;
					}
					else
					{
						FMXBikeSim::Crash(Rear, EMXCrashCause::Contact, T);
						Rear.ContactCooldown = T.ContactCrashCooldown;
						R.Outcome = EMXContactOutcome::Crashed;
					}
					Front.Events |= EMXBikeEvent::Bumped;
				}
				else
				{
					HoldBehind(Rear, Front, T);
					R.Outcome = EMXContactOutcome::HeldBack;
				}
				Out.Add(R);
			}
		}
	}
}
