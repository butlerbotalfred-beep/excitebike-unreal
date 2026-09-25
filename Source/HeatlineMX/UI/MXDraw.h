#pragma once

#include "CoreMinimal.h"

class UCanvas;

/** Canvas drawing helpers with crisp Slate fonts at any size (HUD + full-screen menus). */
namespace MXDraw
{
	HEATLINEMX_API FVector2D Measure(const FString& Text, float Size, bool bBold = true);
	/** AlignX/AlignY: 0 = left/top, 0.5 = centre, 1 = right/bottom. Returns the drawn size. */
	HEATLINEMX_API FVector2D Text(UCanvas* C, const FString& Text, float X, float Y, float Size, const FLinearColor& Color,
		float AlignX = 0.f, float AlignY = 0.f, bool bBold = true, bool bShadow = true);
	/** Like Text, but shrinks the size (down to MinScale) so the string fits MaxWidth, then clips with "..." if it still doesn't. */
	HEATLINEMX_API FVector2D TextFit(UCanvas* C, const FString& Text, float X, float Y, float Size, float MaxWidth, const FLinearColor& Color,
		float AlignX = 0.f, float AlignY = 0.f, bool bBold = true, bool bShadow = true, float MinScale = 0.72f);
	/** Word-wraps Text into lines no wider than MaxWidth, top-left at (X, Y). Returns the height used. */
	HEATLINEMX_API float TextWrap(UCanvas* C, const FString& Text, float X, float Y, float Size, float MaxWidth, const FLinearColor& Color,
		bool bBold = false, bool bShadow = true);
	/** Height of one line of text at this size (the font's line box, which includes line spacing). */
	HEATLINEMX_API float LineHeight(float Size, bool bBold = true);
	HEATLINEMX_API void Rect(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Color);
	HEATLINEMX_API void Frame(UCanvas* C, float X, float Y, float W, float H, float Thickness, const FLinearColor& Color);
	HEATLINEMX_API void Line(UCanvas* C, const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color);
	HEATLINEMX_API FString Ordinal(int32 N);
	HEATLINEMX_API FLinearColor HeatColor(float Heat01);
}
