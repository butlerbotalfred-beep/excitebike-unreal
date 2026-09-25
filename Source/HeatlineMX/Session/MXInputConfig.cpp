#include "Session/MXInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	UMXInputConfig* GInputConfig = nullptr;
}

FMXControlProfile FMXControlProfile::Defaults(EMXControlScheme Scheme)
{
	FMXControlProfile P;
	P.Scheme = Scheme;
	// Face buttons mirror the NES pad: turbo on the left button, accelerate on the bottom one.
	P.Gamepad.Accelerate = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_RightTriggerAxis};
	P.Gamepad.Turbo = {EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_RightShoulder};
	P.Gamepad.Pause = {EKeys::Gamepad_Special_Right};
	P.Keyboard.Accelerate = {EKeys::X, EKeys::J, EKeys::SpaceBar};
	P.Keyboard.Turbo = {EKeys::Z, EKeys::K, EKeys::LeftShift};
	P.Keyboard.Pause = {EKeys::Escape, EKeys::P};
	if (Scheme == EMXControlScheme::Classic)
	{
		// Screen-relative: up/down = lanes, left/right = nose up/down (bike faces screen-right).
		P.Gamepad.SteerFar = {EKeys::Gamepad_DPad_Up};
		P.Gamepad.SteerNear = {EKeys::Gamepad_DPad_Down};
		P.Gamepad.NoseUp = {EKeys::Gamepad_DPad_Left};
		P.Gamepad.NoseDown = {EKeys::Gamepad_DPad_Right};
		P.Keyboard.SteerFar = {EKeys::Up, EKeys::W};
		P.Keyboard.SteerNear = {EKeys::Down, EKeys::S};
		P.Keyboard.NoseUp = {EKeys::Left, EKeys::A};
		P.Keyboard.NoseDown = {EKeys::Right, EKeys::D};
	}
	else
	{
		// Rider-relative: left/right steer across the track, forward/back = nose down/up.
		P.Gamepad.SteerFar = {EKeys::Gamepad_DPad_Left};
		P.Gamepad.SteerNear = {EKeys::Gamepad_DPad_Right};
		P.Gamepad.NoseUp = {EKeys::Gamepad_DPad_Down};
		P.Gamepad.NoseDown = {EKeys::Gamepad_DPad_Up};
		P.Keyboard.SteerFar = {EKeys::Left, EKeys::A};
		P.Keyboard.SteerNear = {EKeys::Right, EKeys::D};
		P.Keyboard.NoseUp = {EKeys::Down, EKeys::S};
		P.Keyboard.NoseDown = {EKeys::Up, EKeys::W};
	}
	return P;
}

UMXInputConfig* UMXInputConfig::Get()
{
	if (!GInputConfig)
	{
		GInputConfig = NewObject<UMXInputConfig>(GetTransientPackage(), TEXT("MXInputConfig"));
		GInputConfig->AddToRoot();
		GInputConfig->CreateActions();
	}
	return GInputConfig;
}

void UMXInputConfig::CreateActions()
{
	auto Make = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		return A;
	};
	Accelerate = Make(TEXT("IA_Accelerate"), EInputActionValueType::Axis1D);
	Turbo = Make(TEXT("IA_Turbo"), EInputActionValueType::Boolean);
	Steer = Make(TEXT("IA_Steer"), EInputActionValueType::Axis1D);
	Pitch = Make(TEXT("IA_Pitch"), EInputActionValueType::Axis1D);
	Pause = Make(TEXT("IA_Pause"), EInputActionValueType::Boolean);
}

UInputMappingContext* UMXInputConfig::BuildContext(const FMXControlProfile& Profile, UObject* Outer)
{
	UInputMappingContext* IMC = NewObject<UInputMappingContext>(Outer ? Outer : this);
	auto MapPlain = [IMC](UInputAction* A, const TArray<FKey>& Keys)
	{
		for (const FKey& K : Keys)
		{
			IMC->MapKey(A, K);
		}
	};
	auto MapNeg = [IMC, this](UInputAction* A, const TArray<FKey>& Keys)
	{
		for (const FKey& K : Keys)
		{
			FEnhancedActionKeyMapping& M = IMC->MapKey(A, K);
			M.Modifiers.Add(NewObject<UInputModifierNegate>(IMC));
		}
	};
	auto MapStick = [IMC](UInputAction* A, const FKey& Key, bool bNegate)
	{
		FEnhancedActionKeyMapping& M = IMC->MapKey(A, Key);
		UInputModifierDeadZone* DZ = NewObject<UInputModifierDeadZone>(IMC);
		DZ->LowerThreshold = 0.28f;
		DZ->UpperThreshold = 0.95f;
		M.Modifiers.Add(DZ);
		if (bNegate)
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(IMC));
		}
	};

	for (const FMXBindingSet* Set : {&Profile.Keyboard, &Profile.Gamepad})
	{
		MapPlain(Accelerate, Set->Accelerate);
		MapPlain(Turbo, Set->Turbo);
		MapNeg(Steer, Set->SteerFar);
		MapPlain(Steer, Set->SteerNear);
		MapPlain(Pitch, Set->NoseUp);
		MapNeg(Pitch, Set->NoseDown);
		MapPlain(Pause, Set->Pause);
	}
	if (Profile.Scheme == EMXControlScheme::Classic)
	{
		// Stick up = toward the far lanes (screen up); stick left = nose up.
		MapStick(Steer, EKeys::Gamepad_LeftY, true);
		MapStick(Pitch, EKeys::Gamepad_LeftX, true);
	}
	else
	{
		// Stick right = rider's right = near side; stick back = lean back (nose up).
		MapStick(Steer, EKeys::Gamepad_LeftX, false);
		MapStick(Pitch, EKeys::Gamepad_LeftY, true);
	}
	return IMC;
}

TArray<FKey>* UMXInputConfig::GetKeys(FMXBindingSet& Set, EMXBindAction A)
{
	switch (A)
	{
	case EMXBindAction::Accelerate: return &Set.Accelerate;
	case EMXBindAction::Turbo: return &Set.Turbo;
	case EMXBindAction::SteerFar: return &Set.SteerFar;
	case EMXBindAction::SteerNear: return &Set.SteerNear;
	case EMXBindAction::NoseUp: return &Set.NoseUp;
	case EMXBindAction::NoseDown: return &Set.NoseDown;
	default: return nullptr;
	}
}

FString UMXInputConfig::ActionLabel(EMXBindAction A, EMXControlScheme Scheme)
{
	const bool bClassic = Scheme == EMXControlScheme::Classic;
	switch (A)
	{
	case EMXBindAction::Accelerate: return TEXT("Accelerate");
	case EMXBindAction::Turbo: return TEXT("Turbo (heats engine)");
	case EMXBindAction::SteerFar: return bClassic ? TEXT("Lane up (far)") : TEXT("Steer left (far side)");
	case EMXBindAction::SteerNear: return bClassic ? TEXT("Lane down (near)") : TEXT("Steer right (near side)");
	case EMXBindAction::NoseUp: return bClassic ? TEXT("Nose up / wheelie") : TEXT("Lean back / nose up");
	case EMXBindAction::NoseDown: return bClassic ? TEXT("Nose down") : TEXT("Lean forward / nose down");
	default: return TEXT("?");
	}
}

FString UMXInputConfig::KeyName(const FKey& K)
{
	// Short pad names (Xbox layout) so labels fit a player card or a quarter-screen HUD.
	struct FShort { const FKey* Key; const TCHAR* Name; };
	static const FShort Shorts[] = {
		{&EKeys::Gamepad_FaceButton_Bottom, TEXT("A")}, {&EKeys::Gamepad_FaceButton_Right, TEXT("B")},
		{&EKeys::Gamepad_FaceButton_Left, TEXT("X")}, {&EKeys::Gamepad_FaceButton_Top, TEXT("Y")},
		{&EKeys::Gamepad_LeftShoulder, TEXT("LB")}, {&EKeys::Gamepad_RightShoulder, TEXT("RB")},
		{&EKeys::Gamepad_LeftTrigger, TEXT("LT")}, {&EKeys::Gamepad_RightTrigger, TEXT("RT")},
		{&EKeys::Gamepad_LeftTriggerAxis, TEXT("LT")}, {&EKeys::Gamepad_RightTriggerAxis, TEXT("RT")},
		{&EKeys::Gamepad_DPad_Up, TEXT("D-pad up")}, {&EKeys::Gamepad_DPad_Down, TEXT("D-pad down")},
		{&EKeys::Gamepad_DPad_Left, TEXT("D-pad left")}, {&EKeys::Gamepad_DPad_Right, TEXT("D-pad right")},
		{&EKeys::Gamepad_LeftStick_Up, TEXT("stick up")}, {&EKeys::Gamepad_LeftStick_Down, TEXT("stick down")},
		{&EKeys::Gamepad_LeftStick_Left, TEXT("stick left")}, {&EKeys::Gamepad_LeftStick_Right, TEXT("stick right")},
		{&EKeys::Gamepad_LeftX, TEXT("stick")}, {&EKeys::Gamepad_LeftY, TEXT("stick")},
		{&EKeys::Gamepad_Special_Right, TEXT("Menu")}, {&EKeys::Gamepad_Special_Left, TEXT("View")},
		{&EKeys::Gamepad_LeftThumbstick, TEXT("L3")}, {&EKeys::Gamepad_RightThumbstick, TEXT("R3")},
	};
	for (const FShort& S : Shorts)
	{
		if (K == *S.Key)
		{
			return S.Name;
		}
	}
	return K.GetDisplayName(false).ToString();
}

FString UMXInputConfig::KeysLabel(const TArray<FKey>& Keys)
{
	FString Out;
	for (const FKey& K : Keys)
	{
		if (!Out.IsEmpty())
		{
			Out += TEXT(" / ");
		}
		Out += KeyName(K);
	}
	return Out.IsEmpty() ? TEXT("(unbound)") : Out;
}

/** "D-pad up" + "D-pad down" -> "D-pad up/down"; anything else -> "A / B". */
static FString PairLabel(const TArray<FKey>& A, const TArray<FKey>& B)
{
	const FString LA = UMXInputConfig::KeysLabel(A);
	const FString LB = UMXInputConfig::KeysLabel(B);
	int32 SpaceA, SpaceB;
	if (A.Num() == 1 && B.Num() == 1 && LA.FindLastChar(TEXT(' '), SpaceA) && LB.FindLastChar(TEXT(' '), SpaceB)
		&& LA.Left(SpaceA) == LB.Left(SpaceB))
	{
		return LA + TEXT("/") + LB.Mid(SpaceB + 1);
	}
	return LA + TEXT(" / ") + LB;
}

TArray<FString> UMXInputConfig::DescribeLayout(const FMXControlProfile& P, bool bGamepad)
{
	const FMXBindingSet& S = bGamepad ? P.Gamepad : P.Keyboard;
	TArray<FString> Lines;
	if (P.Scheme == EMXControlScheme::Classic)
	{
		Lines.Add(TEXT("CLASSIC controls (like the NES)"));
		Lines.Add(FString::Printf(TEXT("Lanes: %s%s"), *PairLabel(S.SteerFar, S.SteerNear), bGamepad ? TEXT(" (or stick)") : TEXT("")));
		Lines.Add(FString::Printf(TEXT("Nose up/down: %s%s"), *PairLabel(S.NoseUp, S.NoseDown), bGamepad ? TEXT(" (or stick)") : TEXT("")));
	}
	else
	{
		Lines.Add(TEXT("MODERN controls (rider-relative)"));
		Lines.Add(FString::Printf(TEXT("Steer: %s"), bGamepad ? TEXT("stick left/right") : *PairLabel(S.SteerFar, S.SteerNear)));
		Lines.Add(FString::Printf(TEXT("Lean back/forward: %s"), bGamepad ? TEXT("stick back/forward") : *PairLabel(S.NoseUp, S.NoseDown)));
	}
	Lines.Add(FString::Printf(TEXT("Accelerate: %s   Turbo: %s"), *KeysLabel(S.Accelerate), *KeysLabel(S.Turbo)));
	Lines.Add(TEXT("In the air: nose up floats, nose down dives"));
	return Lines;
}
