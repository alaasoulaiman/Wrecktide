// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "UI/WrecktideMenuStyle.h"

#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
/** Where the generated art and the OFL fonts live. Cooked by DefaultGame.ini (/Game/UI) and staged
 *  as UFS (UI/Fonts) respectively; both rules must exist or a packaged build loses the menu look. */
const TCHAR* ParchmentAsset = TEXT("/Game/UI/Textures/parchment.parchment");
const TCHAR* FrameAsset = TEXT("/Game/UI/Textures/frame_rope.frame_rope");
const TCHAR* CompassAsset = TEXT("/Game/UI/Textures/compass_rose.compass_rose");
const TCHAR* SealAsset = TEXT("/Game/UI/Textures/wax_seal.wax_seal");

/** frame_rope.png is drawn with a 48 px border on a 256 px sheet: 48/256 = 0.1875. */
constexpr float FrameNineSliceMargin = 0.1875f;

UWrecktideMenuStyle* GMenuStyle = nullptr;
}

UWrecktideMenuStyle* UWrecktideMenuStyle::Get()
{
	if (!GMenuStyle)
	{
		GMenuStyle = NewObject<UWrecktideMenuStyle>(GetTransientPackage(), UWrecktideMenuStyle::StaticClass(),
			TEXT("WrecktideMenuStyle"), RF_Transient);
		// Rooted for the life of the process: the brushes hold raw pointers into HeldTextures, so a
		// collected style object would leave the menu drawing from freed textures.
		GMenuStyle->AddToRoot();
		GMenuStyle->Initialise();
	}
	return GMenuStyle;
}

bool UWrecktideMenuStyle::SetBrushTexture(FSlateBrush& Brush, const TCHAR* AssetPath, const FVector2D& Size, bool bNineSlice)
{
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, AssetPath);
	if (!Texture)
	{
		// Fail soft: a flat warm panel is ugly but still readable, and the log names the asset.
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MENUSTYLE missing texture %s, using flat colour"), AssetPath);
		Brush = FSlateBrush();
		Brush.TintColor = FSlateColor(FLinearColor(0.78f, 0.68f, 0.50f, 1.0f));
		return false;
	}

	HeldTextures.Add(Texture);
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = Size;
	Brush.DrawAs = bNineSlice ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
	if (bNineSlice)
	{
		Brush.Margin = FMargin(FrameNineSliceMargin);
	}
	return true;
}

FString UWrecktideMenuStyle::ResolveFontPath(const TCHAR* FileName) const
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("UI/Fonts") / FileName;
	return FPaths::FileExists(Path) ? Path : FString();
}

void UWrecktideMenuStyle::Initialise()
{
	const bool bParchment = SetBrushTexture(ParchmentBrush, ParchmentAsset, FVector2D(1024.0f, 1536.0f), false);
	// 160, not the source 256: a Box brush draws its corners at Margin * ImageSize, so this is what
	// sets the border to 30 px on screen. Any bigger and the frame eats the panel's text column.
	const bool bFrame = SetBrushTexture(FrameBrush, FrameAsset, FVector2D(160.0f, 160.0f), true);
	const bool bCompass = SetBrushTexture(CompassBrush, CompassAsset, FVector2D(28.0f, 28.0f), false);
	const bool bSeal = SetBrushTexture(SealBrush, SealAsset, FVector2D(64.0f, 64.0f), false);
	bTexturesLoaded = bParchment && bFrame && bCompass && bSeal;

	DimBrush = FSlateBrush();
	DimBrush.DrawAs = ESlateBrushDrawType::Image;
	DimBrush.TintColor = FSlateColor(FLinearColor::White);

	TitleFontPath = ResolveFontPath(TEXT("PirataOne-Regular.ttf"));
	HeadingFontPath = ResolveFontPath(TEXT("CinzelDecorative-Bold.ttf"));
	EntryFontPath = ResolveFontPath(TEXT("CinzelDecorative-Regular.ttf"));
	BodyFontPath = ResolveFontPath(TEXT("IMFellEnglish-Regular.ttf"));
	ItalicFontPath = ResolveFontPath(TEXT("IMFellEnglish-Italic.ttf"));
	bFontsLoaded = !TitleFontPath.IsEmpty() && !HeadingFontPath.IsEmpty() && !EntryFontPath.IsEmpty()
		&& !BodyFontPath.IsEmpty() && !ItalicFontPath.IsEmpty();

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENUSTYLE textures=%d fonts=%d (parchment=%d frame=%d compass=%d seal=%d title=%d entry=%d body=%d)"),
		bTexturesLoaded ? 1 : 0, bFontsLoaded ? 1 : 0, bParchment ? 1 : 0, bFrame ? 1 : 0, bCompass ? 1 : 0,
		bSeal ? 1 : 0, TitleFontPath.IsEmpty() ? 0 : 1, EntryFontPath.IsEmpty() ? 0 : 1, BodyFontPath.IsEmpty() ? 0 : 1);
}

FSlateFontInfo UWrecktideMenuStyle::GetTitleFont(int32 Size) const
{
	return TitleFontPath.IsEmpty()
		? FCoreStyle::GetDefaultFontStyle("Bold", static_cast<float>(Size))
		: FSlateFontInfo(TitleFontPath, static_cast<float>(Size));
}

FSlateFontInfo UWrecktideMenuStyle::GetHeadingFont(int32 Size) const
{
	return HeadingFontPath.IsEmpty()
		? FCoreStyle::GetDefaultFontStyle("Bold", static_cast<float>(Size))
		: FSlateFontInfo(HeadingFontPath, static_cast<float>(Size));
}

FSlateFontInfo UWrecktideMenuStyle::GetEntryFont(int32 Size) const
{
	return EntryFontPath.IsEmpty()
		? FCoreStyle::GetDefaultFontStyle("Regular", static_cast<float>(Size))
		: FSlateFontInfo(EntryFontPath, static_cast<float>(Size));
}

FSlateFontInfo UWrecktideMenuStyle::GetBodyFont(int32 Size) const
{
	return BodyFontPath.IsEmpty()
		? FCoreStyle::GetDefaultFontStyle("Regular", static_cast<float>(Size))
		: FSlateFontInfo(BodyFontPath, static_cast<float>(Size));
}

FSlateFontInfo UWrecktideMenuStyle::GetItalicFont(int32 Size) const
{
	return ItalicFontPath.IsEmpty()
		? FCoreStyle::GetDefaultFontStyle("Italic", static_cast<float>(Size))
		: FSlateFontInfo(ItalicFontPath, static_cast<float>(Size));
}
