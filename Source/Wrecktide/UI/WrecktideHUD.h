// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Systems/SkillComponent.h"
#include "UI/WrecktideMenuTypes.h"
#include "WrecktideHUD.generated.h"

class AWrecktidePlayerState;
class ACombatEnemy;
class USkillComponent;
class UTexture2D;

// Per-island chart plate (C34.6). Forward declared rather than included: WrecktideMapView.h is a
// plain header and this one is pulled in all over the module.
namespace WrecktideMap { struct FIslandChart; }

UENUM(BlueprintType)
enum class EWrecktideDamageNumberKind : uint8
{
	Normal,
	Critical,
	Heal,
	PlayerDamage
};

/**
 * Immediate-mode Wrecktide HUD for the Phase 1 combat economy loop.
 */
UCLASS()
class WRECKTIDE_API AWrecktideHUD : public AHUD
{
	GENERATED_BODY()

public:
	/** Filters stock StateTree labels before AHUD draws its persistent debug strings. */
	virtual void PostRender() override;

	/** DrawHUD is the SINGLE authoritative draw path for gold, XP, toasts, and the shop menu. */
	virtual void DrawHUD() override;

	/** True when a StateTree debug string is queued for this enemy, used by the plate probe. */
	bool HasEnemyStateText(const ACombatEnemy* Enemy) const;

	/** ShowToast is the SINGLE authoritative path for transient HUD messages. */
	void ShowToast(const FString& Message, float Seconds = 2.5f);

	/** Persistent interaction prompt, refreshed by the nearby interactable until it stops applying. */
	void SetInteractionPrompt(const FString& Message);
	void ClearInteractionPrompt();
	const FString& GetInteractionPrompt() const { return InteractionPrompt; }

	/** Live toast text for interaction probes; empty after expiry. */
	FString GetToastText() const;

	/** Exact top-left notoriety text, exposed so the contract probe reads the same presentation path. */
	FString GetNotorietyLine() const;

	/** Live notoriety bar state and animated fill used by the contract probe. */
	bool IsNotorietyBarVisible() const;
	float GetNotorietyBarFillFraction() const;
	float GetNotorietyBarTargetFraction() const { return NotorietyBarTargetFraction; }
	float GetNotorietyBarVisibleSeconds() const { return NotorietyBarVisibleSeconds; }

	/** SetShopMenu is the SINGLE authoritative path for displaying and selecting shop rows. */
	void SetShopMenu(const TArray<FString>& Lines, int32 SelectedIndex);

	/** ClearShopMenu is the SINGLE authoritative path for hiding the modal shop menu. */
	void ClearShopMenu();

	/** PushRewardLine is the SINGLE authoritative path for kill-feed entries. Callers pass the
	 *  rewards they ALREADY awarded; the HUD never computes gold or XP itself. */
	void PushRewardLine(const FString& Line);

	/** FlashHitMarker is the SINGLE authoritative path for the melee hit marker. A critical strike
	 *  (spec C10) marks bigger and gold, so the roll is readable without reading the feed. */
	void FlashHitMarker(bool bCritical = false);

	/** Adds one already computed combat result at its world anchor. The HUD only presents it. */
	void PushDamageNumber(const FVector& WorldLocation, float Amount, EWrecktideDamageNumberKind Kind);

	/** Live entry reads used by the damage number contract probe. */
	int32 GetDamageNumberCount() const;
	bool GetLatestDamageNumber(float& OutAmount, EWrecktideDamageNumberKind& OutKind, bool& OutWasDrawn) const;
	float GetDamageNumberLifetime() const { return DamageNumberLifeSeconds; }

	/** SetDeathScreen is the SINGLE authoritative path for the YOU DIED overlay. MaxSeconds caps
	 *  how long it can stay up, so a respawn that never lands cannot strand it on screen. */
	void SetDeathScreen(bool bDead, float MaxSeconds = 0.0f);

	/** SetPauseMenu is the SINGLE authoritative path for the pause panel. The player controller
	 *  owns the pause state AND the option labels, so a label can never drift from its action;
	 *  the HUD only mirrors what it is told. */
	void SetPauseMenu(bool bOpen, const TArray<FString>& Lines, int32 SelectedIndex);

	/** Header and hint for the pause panel; the title menu (2026-09-05) reuses the panel with its own
	 *  words (WRECKTIDE / Continue, New Game, Quit). Empty title restores PAUSED. */
	void SetPauseMenuHeader(const FString& Title, const FString& Hint);

	/** The lines the panel is showing right now (probe evidence). */
	const TArray<FString>& GetPauseMenuLines() const { return PauseMenuLines; }

	/** Live (unexpired) reward-feed line count; read path for -FeelProbe. */
	int32 GetRewardFeedCount() const;

	/** Live reward-feed line by index, newest first; empty when the index is not live. */
	FString GetRewardFeedLine(int32 Index) const;

	/** True while the pause panel is drawn; read path for -FeelProbe. */
	bool IsPauseMenuOpen() const { return bPauseMenuOpen; }

	/** True while YOU DIED is drawn; read path for -FeelProbe. */
	bool IsDeathScreenActive() const;

	/** SetMapScreen is the SINGLE authoritative path for the full map on M. The player controller
	 *  owns the pause state exactly as it does for the pause panel; the HUD only mirrors it. */
	void SetMapScreen(bool bOpen);

	/** SetRadarState is the SINGLE authoritative path for how big the radar draws (spec C27). The
	 *  controller owns the cycle and the persistence; the HUD only mirrors what it is told. */
	void SetRadarState(EWrecktideRadarState State) { RadarState = State; }
	EWrecktideRadarState GetRadarState() const { return RadarState; }

	/** True when the last drawn radar painted the island plate. False in the small state, which is
	 *  the read path -MapProbe uses to prove the small radar is plateless. */
	bool DidDrawMapPlate() const { return bDrewMapPlate; }

	/** True only when the last full-map frame submitted its texture, rather than fallback art. */
	bool DidDrawFullMapPlate() const { return bDrewFullMapPlate; }

	/** Diameter the radar disc was last drawn at, in pixels. 0 before the first draw. */
	float GetDrawnRadarDiameter() const { return DrawnRadarDiameter; }

	/** True while the full map is drawn; read path for -MapProbe and for the capture harness. */
	bool IsMapScreenOpen() const { return bMapScreenOpen; }

	/** Heading-up (GTA style, the default) versus north-up. The classic MMO shipped heading-up with
	 *  an option to turn it off because heading-up disorients some players; this is the hook that
	 *  option will drive. No Options row yet, see the lane report. */
	void SetMinimapNorthUp(bool bNorthUp) { bMinimapNorthUp = bNorthUp; }
	bool IsMinimapNorthUp() const { return bMinimapNorthUp; }

	/** True once the island plate texture is loaded; read path for -MapProbe. */
	bool IsMapTextureLoaded() const { return IslandMapTexture != nullptr; }

	/** Which island's chart the last radar or full-map draw used (C34.6). Empty until one has been
	 *  drawn. Read path for -Island2Probe: it proves the CHART followed the player across the sea,
	 *  not just that a second texture exists. */
	const FString& GetDrawnChartId() const { return DrawnChartId; }

	/** The title the last full-map draw wrote over the plate, e.g. WRECK COVE / GALLOWS REACH. */
	const FString& GetDrawnChartTitle() const { return DrawnChartTitle; }

	/** How many charted islands actually have their plate texture in memory. -Island2Probe requires
	 *  this to equal WrecktideMap::GetIslandCharts().Num(), which is what proves the second plate
	 *  was baked and loaded rather than silently falling back to sand on sea. */
	int32 GetLoadedChartPlateCount() const;

	// --- weapon bar (spec C17) ------------------------------------------------------------------

	/** Slot the last drawn frame lit as wielded, 1..5, or 0 when the pirate is unarmed. Read path
	 *  for -HotbarProbe: it proves the BAR agrees with the PlayerState, not just that the state is
	 *  right. INDEX_NONE while the bar has not been drawn yet (headless runs). */
	int32 GetDrawnWieldedSlot() const { return DrawnWieldedSlot; }
	bool IsPistolCrosshairDrawn() const { return bPistolCrosshairDrawn; }
	bool IsPistolReloadArcDrawn() const { return bPistolReloadArcDrawn; }
	bool IsPistolCrosshairGold() const { return bPistolCrosshairGold; }

	/** Label the last drawn frame put under a slot (1..5). Empty before the first draw. */
	FString GetDrawnSlotLabel(int32 Slot) const;

	/** True while the weapon bar is part of the drawn HUD (spec C17: always, except under a screen
	 *  that owns the display). */
	bool IsWeaponBarDrawn() const { return bWeaponBarDrawn; }

	/** Toolbar hit targets, recorded by the draw path in viewport pixels. Invalid when hidden. */
	FBox2D GetChestButtonRect() const { return bWeaponBarDrawn ? ChestButtonRect : FBox2D(ForceInit); }
	FBox2D GetMapButtonRect() const { return bWeaponBarDrawn ? MapButtonRect : FBox2D(ForceInit); }
	FBox2D GetWeaponSlotRect(int32 Slot) const;
	bool IsChestButtonLit() const { return bWeaponBarDrawn && bChestButtonLit; }

	// --- mouse over the canvas modal list (spec C20) --------------------------------------------
	// The shop, the bounty board, the quest giver and the smithy all draw through SetShopMenu, so
	// ONE set of row rectangles, recorded where they are actually drawn, serves every one of them.

	/** Screen rect of a drawn modal row. An empty box when that row is not on screen. */
	FBox2D GetShopRowRect(int32 Index) const;

	/** Row under this viewport point, or INDEX_NONE. */
	int32 HitTestShopRow(const FVector2D& ScreenPosition) const;

	/** The row the modal list is highlighting, mirrored from whichever actor pushed it. */
	int32 GetShopMenuSelectedIndex() const { return ShopMenuSelectedIndex; }

	/** How many rows the modal list is showing right now. */
	int32 GetShopMenuLineCount() const { return ShopMenuLines.Num(); }

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleSkillLevelUp(EWrecktideSkill Skill, int32 NewLevel);

	/** Five numbered slots along the bottom centre (spec C17). Reads the PlayerState, never stores
	 *  a copy: the bar is a view of the assignment, not a second source of truth. */
	void DrawWeaponBar(const AWrecktidePlayerState& PlayerState);

	/** One slot's frame, key badge, glyph and label. */
	void DrawWeaponSlot(int32 Slot, float X, float Y, float Size, const FString& Label, bool bFilled, bool bWielded);
	void DrawWeaponSlotFrame(float X, float Y, float Size, bool bLit);
	/** Matching end buttons, with original rectangle glyphs and the same frame as a weapon slot. */
	void DrawToolbarButton(float X, float Y, float Size, bool bChest, bool bLit);

	/** A blade silhouette drawn from rectangles: ours, no texture, no asset to stage. */
	void DrawBladeGlyph(float CenterX, float CenterY, float Size, const FLinearColor& Color);
	void DrawPistolGlyph(float CenterX, float CenterY, float Size, const FLinearColor& Color);
	void DrawPistolReticle(const class ACombatCharacter& Character);

	UPROPERTY(EditAnywhere, Category="Pistol HUD", meta=(ClampMin="1"))
	float PistolCrosshairGap = 5.0f;
	UPROPERTY(EditAnywhere, Category="Pistol HUD", meta=(ClampMin="1"))
	float PistolCrosshairTickLength = 6.0f;
	UPROPERTY(EditAnywhere, Category="Pistol HUD", meta=(ClampMin="1"))
	float PistolCrosshairThickness = 2.0f;
	UPROPERTY(EditAnywhere, Category="Pistol HUD", meta=(ClampMin="1"))
	float PistolReloadArcRadius = 9.0f;
	UPROPERTY(EditAnywhere, Category="Pistol HUD")
	float PistolReloadArcOffset = 29.0f;
	UPROPERTY(EditAnywhere, Category="Pistol HUD", meta=(ClampMin="12", ClampMax="128"))
	int32 PistolReloadArcSegments = 48;
	bool bPistolCrosshairDrawn = false;
	bool bPistolReloadArcDrawn = false;
	bool bPistolCrosshairGold = false;
	UFUNCTION()
	void HandleSkillXPChanged(EWrecktideSkill Skill, int64 NewXP);

	/** Bottom-left radar. Returns without drawing when the plate and the fallback both fail. */
	void DrawMinimap(const FVector& PlayerLocation, float ViewYaw, const FVector* MissionTarget);

	/** Full-screen north-up chart on M. */
	void DrawFullMap(const FVector& PlayerLocation, float ViewYaw, const FVector* MissionTarget, const FString& MissionLabel);

	/** Gold chevron on the screen edge while the mission target is off screen. */
	void DrawObjectiveArrow(const FVector& MissionTarget);

	/** Projects and presents every live damage number. */
	void DrawDamageNumbers(float Now, UFont* Font);

	/** Top centre notoriety progress presentation. */
	void DrawNotorietyBar(float Now, UFont* Font);
	float CalculateNotorietyFraction() const;
	/** Nameplates and quest glyphs over the camp NPCs (C21/C22, 2026-09-06). Screen space, projected
	 *  from the world every frame like the objective arrow, so no widget or texture is authored. */
	void DrawNpcPlates();

	/** How far a nameplate stays legible, in uu. Past this the NPC is a map marker, not a label. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|HUD", meta=(ClampMin="100.0", Units="cm"))
	float NpcPlateRangeUU = 2200.0f;

	/** Gold of the "!" and "?" glyphs, matched to the beacon sparkle so the cues read as one system. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|HUD")
	FLinearColor NpcSignalColour = FLinearColor(1.0f, 0.82f, 0.28f, 1.0f);

	/** One glyph plus an optional label, centred on a screen point. */
	void DrawMapGlyph(const FString& Glyph, const FLinearColor& Color, float CenterX, float CenterY, const FString& Label);

	/** Solid triangle helper (player arrow, rim chevrons): no texture, no asset. */
	void DrawSolidTriangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);

	UFUNCTION()
	void HandleNotorietyLevelUp(int32 NewLevel);

	void DrawShadowedText(const FString& Text, UFont* Font, float X, float Y, const FLinearColor& Color, float Scale = 1.0f);
	void DrawCenteredShadowedText(const FString& Text, UFont* Font, float CenterX, float Y, const FLinearColor& Color, float Scale = 1.0f);
	void DrawRightAlignedShadowedText(const FString& Text, UFont* Font, float RightX, float Y, const FLinearColor& Color);

	/** Drops feed lines whose fade has finished; called from every draw and every read. */
	void PruneRewardFeed();
	void PruneDamageNumbers();

	struct FDamageNumberEntry
	{
		FVector WorldLocation = FVector::ZeroVector;
		float Amount = 0.0f;
		EWrecktideDamageNumberKind Kind = EWrecktideDamageNumberKind::Normal;
		float StartTime = 0.0f;
		float HorizontalJitter = 0.0f;
		bool bWasDrawn = false;
	};

	UPROPERTY(Transient)
	TObjectPtr<USkillComponent> BoundSkill;

	FString ToastText;
	float ToastExpireTime = 0.0f;
	FString InteractionPrompt;
	TArray<FString> ShopMenuLines;
	int32 ShopMenuSelectedIndex = INDEX_NONE;

	/** Where each modal row was DRAWN last frame, in viewport pixels. Recorded at draw time so the
	 *  hit test can never drift from the layout (spec C20). */
	TArray<FBox2D> ShopRowRects;

	/** Kill feed, newest first: <line text, world time it disappears at>. Plain pairs rather than
	 *  a new struct because nothing here is ever a UObject. */
	TArray<TPair<FString, float>> RewardFeed;

	float HitMarkerExpireTime = 0.0f;
	/** True while the marker on screen belongs to a critical strike. */
	bool bHitMarkerCritical = false;

	/** Floating combat text data and presentation tunables. */
	TArray<FDamageNumberEntry> DamageNumbers;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers", meta=(ClampMin="0.1", Units="s"))
	float DamageNumberLifeSeconds = 0.9f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers", meta=(ClampMin="0.0"))
	float DamageNumberRisePixels = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers", meta=(ClampMin="0.0"))
	float DamageNumberJitterPixels = 18.0f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers", meta=(ClampMin="1", ClampMax="32"))
	int32 MaxDamageNumbers = 12;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers", meta=(ClampMin="0.1"))
	float NormalDamageNumberScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers", meta=(ClampMin="0.1"))
	float CriticalDamageNumberScale = 1.4f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers", meta=(ClampMin="0.1"))
	float PlayerDamageNumberScale = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers")
	FLinearColor NormalDamageNumberColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers")
	FLinearColor CriticalDamageNumberColor = FLinearColor(1.0f, 0.78f, 0.12f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers")
	FLinearColor HealDamageNumberColor = FLinearColor(0.25f, 1.0f, 0.35f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="HUD|Damage Numbers")
	FLinearColor PlayerDamageNumberColor = FLinearColor(1.0f, 0.18f, 0.12f, 1.0f);

	/** Notoriety bar animation state. */
	float NotorietyBarStartFraction = 0.0f;
	float NotorietyBarTargetFraction = 0.0f;
	float NotorietyBarAnimationStartTime = 0.0f;
	float NotorietyBarLastGainTime = 0.0f;
	float NotorietyBarFlashExpireTime = 0.0f;
	int32 NotorietyBarFlashLevel = 1;
	bool bNotorietyBarHasGain = false;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="0.1", ClampMax="1.0"))
	float NotorietyBarWidthFraction = 0.38f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="2.0"))
	float NotorietyBarHeight = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="0.0"))
	float NotorietyBarTop = 28.0f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="0.01", Units="s"))
	float NotorietyBarFillSeconds = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="0.0", Units="s"))
	float NotorietyBarVisibleSeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="0.01", Units="s"))
	float NotorietyBarFadeInSeconds = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="0.01", Units="s"))
	float NotorietyBarFadeOutSeconds = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety", meta=(ClampMin="0.0", Units="s"))
	float NotorietyBarLevelFlashSeconds = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety")
	FLinearColor NotorietyBarFrameColor = FLinearColor(0.38f, 0.25f, 0.12f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety")
	FLinearColor NotorietyBarParchmentColor = FLinearColor(0.78f, 0.66f, 0.44f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety")
	FLinearColor NotorietyBarFillColor = FLinearColor(0.83f, 0.19f, 0.08f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="HUD|Notoriety")
	FLinearColor NotorietyBarFlashColor = FLinearColor(1.0f, 0.78f, 0.12f, 1.0f);
	bool bDeathScreen = false;
	float DeathScreenExpireTime = 0.0f;
	bool bPauseMenuOpen = false;
	TArray<FString> PauseMenuLines;
	int32 PauseMenuSelectedIndex = INDEX_NONE;
	FString PauseMenuTitle = TEXT("PAUSED");
	FString PauseMenuHint;

	/** World time the HUD came up, for the one-shot title card and controls hint. */
	float HudStartTime = 0.0f;

	/** Map art, loaded by string in BeginPlay. Null is survivable: the minimap falls back to a plain
	 *  sand-on-sea plate so a missing texture never blanks the radar. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> IslandMapTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MapRimTexture;

	/** One plate per WrecktideMap::GetIslandCharts() row, same order, loaded once in BeginPlay. A
	 *  null entry is survivable exactly as IslandMapTexture's null is: that island draws the
	 *  sand-on-sea fallback instead of blanking the radar. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> ChartTextures;

	/** Plate for Chart, or null when that island has no baked texture. */
	UTexture2D* ChartTextureFor(const WrecktideMap::FIslandChart& Chart) const;

	FString DrawnChartId;
	FString DrawnChartTitle;

	bool bMapScreenOpen = false;
	bool bMinimapNorthUp = false;

	/** Radar size, mirrored from the controller (spec C27). Small is the shipped default. */
	EWrecktideRadarState RadarState = EWrecktideRadarState::Small;

	/** What the last radar draw actually did; the probe reads these rather than pixels. */
	bool bDrewMapPlate = false;
	bool bDrewFullMapPlate = false;
	float DrawnRadarDiameter = 0.0f;

	/** What the weapon bar actually drew last frame; the probe's evidence that the view agrees with
	 *  the model. INDEX_NONE until the first draw, 0 for "nothing wielded". */
	int32 DrawnWieldedSlot = INDEX_NONE;
	TArray<FString> DrawnSlotLabels;
	bool bWeaponBarDrawn = false;
	TArray<FBox2D> DrawnWeaponSlotRects;
	FBox2D ChestButtonRect = FBox2D(ForceInit);
	FBox2D MapButtonRect = FBox2D(ForceInit);
	bool bChestButtonLit = false;
};
