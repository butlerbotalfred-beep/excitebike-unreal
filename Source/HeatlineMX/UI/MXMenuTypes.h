#pragma once

#include "CoreMinimal.h"

/** Device-independent menu actions produced by the viewport client from keys/buttons/sticks. */
enum class EMXMenuAction : uint8
{
	Up,
	Down,
	Left,
	Right,
	Confirm,
	Back,
	Start,
	Aux,      // Y / Tab
	Aux2,     // X / R
	PageLeft, // LB / Q
	PageRight // RB / E
};

/** Device keys: -1 = keyboard, >= 0 = gamepad input device id. */
namespace MXDevice
{
	constexpr int32 Keyboard = -1;
	constexpr int32 None = -2;
	inline FString Label(int32 DeviceKey)
	{
		return DeviceKey == Keyboard ? TEXT("Keyboard") : (DeviceKey >= 0 ? FString::Printf(TEXT("Gamepad %d"), DeviceKey) : TEXT("-"));
	}
}

enum class EMXAppState : uint8
{
	Frontend,     // menus with the attract race behind them
	Race,
	Results,
	Designer,
	DesignerTest
};
