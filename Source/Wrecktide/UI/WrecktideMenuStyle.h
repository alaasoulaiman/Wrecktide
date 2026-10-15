// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/Object.h"
#include "WrecktideMenuStyle.generated.h"

class UTexture2D;

/**
 * Brushes and fonts for the Slate menu, in one rooted UObject (2026-09-06).
 *
 * WHY a UObject and not a plain struct: an FSlateBrush holds a RAW UObject pointer to its texture.
 * Nothing else in the game references /Game/UI/Textures, so without a UPROPERTY holding them the
 * first garbage collection would free the textures out from under the live brushes. This object is
 * created once, AddToRoot'ed, and never destroyed; four UI textures is a few MB of the budget.
 *
 * Fonts come from TTF files by path (Content/UI/Fonts, staged as UFS) rather than UFont assets, so
 * no editor asset had to be authored for them. A missing file falls back to the engine's own font
 * and says so in the log: the menu stays readable, it just loses the flavour.
 */
UCLASS()
class WRECKTIDE_API UWrecktideMenuStyle : public UObject
{
	GENERATED_BODY()

public:
	/** The one instance. Builds and roots it on first use; never returns null. */
	static UWrecktideMenuStyle* Get();

	/** Parchment sheet, stretched to the panel. */
	const FSlateBrush* GetParchmentBrush() const { return &ParchmentBrush; }

	/** Rope-and-wood border, nine-sliced so corners never stretch. */
	const FSlateBrush* GetFrameBrush() const { return &FrameBrush; }

	/** Compass rose: the selected-row glyph. */
	const FSlateBrush* GetCompassBrush() const { return &CompassBrush; }

	/** Wax seal at the foot of the panel. */
	const FSlateBrush* GetSealBrush() const { return &SealBrush; }

	/** Flat dimming layer over the world behind the panel. */
	const FSlateBrush* GetDimBrush() const { return &DimBrush; }

	FSlateFontInfo GetTitleFont(int32 Size) const;
	FSlateFontInfo GetHeadingFont(int32 Size) const;
	FSlateFontInfo GetEntryFont(int32 Size) const;
	FSlateFontInfo GetBodyFont(int32 Size) const;
	FSlateFontInfo GetItalicFont(int32 Size) const;

	/** True when all four textures resolved; false means the panel is drawing on flat colour. */
	bool AreTexturesLoaded() const { return bTexturesLoaded; }

	/** True when all four TTFs were found on disk; false means the engine font is standing in. */
	bool AreFontsLoaded() const { return bFontsLoaded; }

	/** Ink brown for unselected entries. */
	static FLinearColor InkColour() { return FLinearColor(0.16f, 0.10f, 0.05f, 1.0f); }

	/** Warm gold for the title and the selected entry. */
	static FLinearColor GoldColour() { return FLinearColor(0.58f, 0.38f, 0.05f, 1.0f); }  // antique gold: the pale 0.86/0.66/0.26 vanished on the parchment (2026-09-06 eyes)

	/** Faded ink for hints, captions and values. */
	static FLinearColor FadedInkColour() { return FLinearColor(0.30f, 0.21f, 0.12f, 1.0f); }

private:
	void Initialise();

	/** Loads a texture into Brush; returns false and leaves the brush as flat colour when missing. */
	bool SetBrushTexture(FSlateBrush& Brush, const TCHAR* AssetPath, const FVector2D& Size, bool bNineSlice);

	/** Font path under Content/UI/Fonts, or an empty string when the file is not there. */
	FString ResolveFontPath(const TCHAR* FileName) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> HeldTextures;

	FSlateBrush ParchmentBrush;
	FSlateBrush FrameBrush;
	FSlateBrush CompassBrush;
	FSlateBrush SealBrush;
	FSlateBrush DimBrush;

	FString TitleFontPath;
	FString HeadingFontPath;
	FString EntryFontPath;
	FString BodyFontPath;
	FString ItalicFontPath;

	bool bTexturesLoaded = false;
	bool bFontsLoaded = false;
};
