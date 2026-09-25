#include "UI/MXDraw.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

namespace MXDraw
{
	static FSlateFontInfo Font(float Size, bool bBold)
	{
		const int32 Px = FMath::Max(6, FMath::RoundToInt(Size));
		// Canvas text only renders fonts backed by a UFont asset (a bare Slate composite font draws
		// nothing in UE 5.8 on Metal; see the "fonts" autotest). The engine's Roboto has both typefaces.
		if (GEngine && GEngine->GetLargeFont())
		{
			return FSlateFontInfo(GEngine->GetLargeFont(), Px, bBold ? FName("Bold") : FName("Regular"));
		}
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Px);
	}

	FVector2D Measure(const FString& Text, float Size, bool bBold)
	{
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
		{
			const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return M->Measure(Text, Font(Size, bBold));
		}
		return FVector2D(Text.Len() * Size * 0.55f, Size * 1.2f);
	}

	FVector2D Text(UCanvas* C, const FString& Str, float X, float Y, float Size, const FLinearColor& Color, float AlignX, float AlignY, bool bBold, bool bShadow)
	{
		if (!C || Str.IsEmpty())
		{
			return FVector2D::ZeroVector;
		}
		const FVector2D Sz = Measure(Str, Size, bBold);
		FCanvasTextItem Item(FVector2D(X - Sz.X * AlignX, Y - Sz.Y * AlignY), FText::FromString(Str), Font(Size, bBold), Color);
		if (bShadow)
		{
			Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f), FVector2D(FMath::Max(1.f, Size / 18.f), FMath::Max(1.f, Size / 18.f)));
		}
		C->DrawItem(Item);
		return Sz;
	}

	FVector2D TextFit(UCanvas* C, const FString& Str, float X, float Y, float Size, float MaxWidth, const FLinearColor& Color,
		float AlignX, float AlignY, bool bBold, bool bShadow, float MinScale)
	{
		if (!C || Str.IsEmpty() || MaxWidth <= 0.f)
		{
			return FVector2D::ZeroVector;
		}
		float S = Size;
		const float Wd = Measure(Str, S, bBold).X;
		if (Wd > MaxWidth)
		{
			S = FMath::Max(Size * MinScale, Size * MaxWidth / Wd);
		}
		FString Out = Str;
		while (Out.Len() > 1 && Measure(Out, S, bBold).X > MaxWidth)
		{
			Out = Out.LeftChop(Out.EndsWith(TEXT("...")) ? 4 : 1).TrimEnd() + TEXT("...");
		}
		return Text(C, Out, X, Y, S, Color, AlignX, AlignY, bBold, bShadow);
	}

	float TextWrap(UCanvas* C, const FString& Str, float X, float Y, float Size, float MaxWidth, const FLinearColor& Color, bool bBold, bool bShadow)
	{
		TArray<FString> Words;
		Str.ParseIntoArray(Words, TEXT(" "));
		const float LH = LineHeight(Size, bBold) * 0.95f;
		float LY = Y;
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Try = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			if (!Line.IsEmpty() && Measure(Try, Size, bBold).X > MaxWidth)
			{
				Text(C, Line, X, LY, Size, Color, 0.f, 0.f, bBold, bShadow);
				LY += LH;
				Line = Word;
			}
			else
			{
				Line = Try;
			}
		}
		if (!Line.IsEmpty())
		{
			Text(C, Line, X, LY, Size, Color, 0.f, 0.f, bBold, bShadow);
			LY += LH;
		}
		return LY - Y;
	}

	float LineHeight(float Size, bool bBold)
	{
		return Measure(TEXT("Ag"), Size, bBold).Y;
	}

	void Rect(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		if (!C || W <= 0.f || H <= 0.f)
		{
			return;
		}
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(Tile);
	}

	void Frame(UCanvas* C, float X, float Y, float W, float H, float T, const FLinearColor& Color)
	{
		Rect(C, X, Y, W, T, Color);
		Rect(C, X, Y + H - T, W, T, Color);
		Rect(C, X, Y, T, H, Color);
		Rect(C, X + W - T, Y, T, H, Color);
	}

	void Line(UCanvas* C, const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color)
	{
		if (!C)
		{
			return;
		}
		FCanvasLineItem L(A, B);
		L.SetColor(Color);
		L.LineThickness = Thickness;
		C->DrawItem(L);
	}

	FString Ordinal(int32 N)
	{
		const int32 Mod100 = N % 100;
		const TCHAR* Suffix = TEXT("th");
		if (Mod100 < 11 || Mod100 > 13)
		{
			switch (N % 10)
			{
			case 1: Suffix = TEXT("st"); break;
			case 2: Suffix = TEXT("nd"); break;
			case 3: Suffix = TEXT("rd"); break;
			default: break;
			}
		}
		return FString::Printf(TEXT("%d%s"), N, Suffix);
	}

	FLinearColor HeatColor(float H)
	{
		if (H < 0.5f)
		{
			return FMath::Lerp(FLinearColor(0.1f, 0.85f, 0.3f), FLinearColor(0.95f, 0.85f, 0.1f), H / 0.5f);
		}
		if (H < 0.75f)
		{
			return FMath::Lerp(FLinearColor(0.95f, 0.85f, 0.1f), FLinearColor(1.f, 0.45f, 0.05f), (H - 0.5f) / 0.25f);
		}
		return FMath::Lerp(FLinearColor(1.f, 0.45f, 0.05f), FLinearColor(1.f, 0.08f, 0.05f), FMath::Clamp((H - 0.75f) / 0.25f, 0.f, 1.f));
	}
}
