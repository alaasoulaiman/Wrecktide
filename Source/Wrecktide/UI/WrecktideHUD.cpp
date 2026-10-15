// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "UI/WrecktideHUD.h"
#include "Variant_Combat/CombatCharacter.h"
#include "Systems/WrecktideAudio.h"
#include "Variant_Combat/CombatPlayerController.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Gameplay/WrecktideBoat.h"
#include "Gameplay/WrecktideMissionBeacon.h"
#include "EngineUtils.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Kismet/GameplayStatics.h"
#include "RenderUtils.h"
#include "UI/WrecktideMapView.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktideOptions.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideQuestLogic.h"
#include "Variant_Combat/AI/CombatEnemy.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
/** Feel pass v0 (2026-09-04). All times are world seconds, so everything here freezes while paused. */
constexpr float RewardFeedFadeSeconds = 4.0f;
constexpr int32 MaxRewardFeedLines = 5;
constexpr float HitMarkerSeconds = 0.15f;
constexpr float TitleCardSeconds = 4.0f;
constexpr float TitleCardFadeSeconds = 1.0f;
constexpr float ControlsHintSeconds = 20.0f;
constexpr float ControlsHintFadeSeconds = 2.0f;

/** Map pass v0 (2026-09-06). All in screen pixels at the drawn resolution unless stated. */
constexpr float MinimapDiameter = 220.0f;      // the visible disc in the MEDIUM state
constexpr float MinimapSmallDiameter = 120.0f; // the SMALL compass ring (spec C27)
constexpr float MinimapRimScale = 1.30f;       // rim plate is this much wider than the disc
constexpr float MinimapMargin = 18.0f;         // gap to the screen corner
constexpr float MinimapMarkerInset = 0.90f;    // rim-clamped markers sit at this fraction of the radius
constexpr float FullMapHeightFraction = 0.78f; // plate height as a fraction of the screen
constexpr float ObjectiveArrowInset = 46.0f;   // how far in from the screen edge the chevron sits
constexpr float ObjectiveArrowSize = 20.0f;

/** Weapon bar (spec C17). Screen pixels at the drawn resolution; tuned at 1280x720. */
constexpr float WeaponSlotSize = 64.0f;        // one square slot
constexpr float WeaponSlotGap = 8.0f;          // gap between slots
constexpr float WeaponBarBottomMargin = 104.0f; // slot top edge, up from the bottom of the screen;
                                               // 64 slot + 4 gap + the label line, or the name under
                                               // slot 1 falls off the window (capture, 2026-09-06)
constexpr float WeaponSlotBorder = 2.0f;       // frame thickness
const FLinearColor WeaponSlotFill(0.09f, 0.06f, 0.03f, 0.72f);
const FLinearColor WeaponSlotEdge(0.42f, 0.31f, 0.16f, 0.95f);
const FLinearColor WeaponSlotEdgeLit(0.86f, 0.66f, 0.20f, 1.0f);   // the wielded slot
const FLinearColor WeaponSlotInk(0.96f, 0.92f, 0.80f, 1.0f);
const FLinearColor WeaponSlotInkDim(0.62f, 0.56f, 0.46f, 1.0f);    // an empty slot
// SEA lane (spec S1.4 and S1.6). One block, one place: everything the sailing HUD needs.
/** uu/s shown as one knot on the sail line. A readout scale chosen so a small boat reads 4 to 8
 *  knots, not a real-world conversion; change it here and nothing else moves. */
constexpr float SailUUPerKnot = 100.0f;
/** Length and half width of the wind arrow drawn on the minimap rim, in pixels. */
constexpr float WindArrowLength = 15.0f;
constexpr float WindArrowHalfWidth = 7.0f;
/** Fraction of the radar radius the wind arrow sits at. */
constexpr float WindArrowRadiusFraction = 0.72f;
/** Hull bar while aboard: pixels wide, pixels tall. */
constexpr float HullBarWidth = 168.0f;
constexpr float HullBarHeight = 10.0f;

/** Sand on sea, used only when the island plate texture is missing so the radar never goes blank. */
const FLinearColor FallbackSea(0.11f, 0.23f, 0.31f, 1.0f);
const FLinearColor FallbackSand(0.84f, 0.73f, 0.52f, 1.0f);
}

void AWrecktideHUD::PostRender()
{
	bool bShowEnemyStateText = false;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	bShowEnemyStateText = FParse::Param(FCommandLine::Get(), TEXT("DebugPlates"));
#endif
	if (!bShowEnemyStateText)
	{
		DebugTextList.RemoveAll([](const FDebugTextInfo& Entry)
		{
			return Entry.SrcActor && Entry.SrcActor->IsA<ACombatEnemy>();
		});
	}

	Super::PostRender();
}

bool AWrecktideHUD::HasEnemyStateText(const ACombatEnemy* Enemy) const
{
	for (const FDebugTextInfo& Entry : DebugTextList)
	{
		if (Entry.SrcActor == Enemy && !Entry.DebugText.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

void AWrecktideHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PlayerController = GetOwningPlayerController();
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	BoundSkill = PlayerState ? PlayerState->GetSkill() : nullptr;
	if (BoundSkill)
	{
		BoundSkill->OnSkillLevelUp.AddUniqueDynamic(this, &AWrecktideHUD::HandleSkillLevelUp);
		BoundSkill->OnSkillXPChanged.AddUniqueDynamic(this, &AWrecktideHUD::HandleSkillXPChanged);
		BoundSkill->OnNotorietyLevelUp.AddUniqueDynamic(this, &AWrecktideHUD::HandleNotorietyLevelUp);
	}

	if (const UWorld* World = GetWorld())
	{
		HudStartTime = World->GetTimeSeconds();
		NotorietyBarAnimationStartTime = HudStartTime;
	}
	NotorietyBarStartFraction = CalculateNotorietyFraction();
	NotorietyBarTargetFraction = NotorietyBarStartFraction;

	// The saved Options value owns the radar orientation from the first frame; the Options screen
	// pushes later changes through SetMinimapNorthUp.
	bMinimapNorthUp = FWrecktideOptions::Get().bMinimapNorthUp;

	// Loaded by string, like the Slate menu art: nothing else references these, so /Game/UI is cooked
	// as a folder (Config\DefaultGame.ini:21) and /Game/UI/Map rides along with it.
	IslandMapTexture = LoadObject<UTexture2D>(nullptr, WrecktideMap::IslandTexturePath);
	MapRimTexture = LoadObject<UTexture2D>(nullptr, WrecktideMap::RimTexturePath);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP hud art island=%d rim=%d"), IslandMapTexture != nullptr, MapRimTexture != nullptr);

	// One plate per charted island (C34.6). Index 0 is Wreck Cove and is the same object
	// IslandMapTexture holds, so -MapProbe's "the plate is loaded" read path is unchanged.
	const TArray<WrecktideMap::FIslandChart>& Charts = WrecktideMap::GetIslandCharts();
	ChartTextures.Reset();
	ChartTextures.Reserve(Charts.Num());
	for (const WrecktideMap::FIslandChart& Chart : Charts)
	{
		UTexture2D* Plate = LoadObject<UTexture2D>(nullptr, Chart.TexturePath);
		ChartTextures.Add(Plate);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP chart %s plate=%d (%s)"), Chart.Id, Plate != nullptr, Chart.TexturePath);
	}
}

int32 AWrecktideHUD::GetLoadedChartPlateCount() const
{
	int32 Loaded = 0;
	for (const TObjectPtr<UTexture2D>& Plate : ChartTextures)
	{
		Loaded += (Plate != nullptr) ? 1 : 0;
	}
	return Loaded;
}

UTexture2D* AWrecktideHUD::ChartTextureFor(const WrecktideMap::FIslandChart& Chart) const
{
	const TArray<WrecktideMap::FIslandChart>& Charts = WrecktideMap::GetIslandCharts();
	for (int32 Index = 0; Index < Charts.Num(); ++Index)
	{
		// Address identity: ChartFor() always hands back a reference INTO this same static table.
		if (&Charts[Index] == &Chart && ChartTextures.IsValidIndex(Index))
		{
			// May legitimately be null: an island whose plate has not been baked draws the
			// sand-on-sea fallback, which is chart-relative, rather than another island's picture.
			return ChartTextures[Index];
		}
	}
	return nullptr;
}

void AWrecktideHUD::DrawHUD()
{
	Super::DrawHUD();
	bDrewFullMapPlate = false;
	bPistolCrosshairDrawn = false;
	bPistolReloadArcDrawn = false;
	bPistolCrosshairGold = false;
	// No stale toolbar targets under the title, pause tree or a missing canvas.
	bWeaponBarDrawn = false;

	if (!Canvas || !GEngine)
	{
		return;
	}

	// Pull, do not only trust the push: if the controller has a menu open and this HUD shows none
	// (HUD created or replaced after the push), take the lines now rather than after the next key.
	ACombatPlayerController* MenuController = Cast<ACombatPlayerController>(GetOwningPlayerController());
	// Deferred by one engine tick: this runs inside the paint pass, and the controller's refresh
	// rebuilds the Slate menu tree, which must never happen while Slate is drawing it.
	if (!bPauseMenuOpen && MenuController && MenuController->IsAnyMenuOpen())
	{
		MenuController->RequestMenuRefresh();
	}

	// Pull, do not only trust the push, exactly as the pause panel above does: the controller owns
	// the map state and a HUD created or replaced after SetMapScreen would otherwise draw the play
	// HUD over a paused world with no chart on it.
	if (MenuController && MenuController->IsMapScreenOpen() != bMapScreenOpen)
	{
		bMapScreenOpen = MenuController->IsMapScreenOpen();
	}

	// The title menu owns the whole screen: gold, XP, the objective line and the kill feed are
	// gameplay furniture and have no business over a main menu.
	if (MenuController && MenuController->IsTitleMenuOpen())
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayerController();
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (!PlayerState)
	{
		return;
	}

	UFont* MediumFont = GEngine->GetMediumFont();
	UFont* LargeFont = GEngine->GetLargeFont();
	if (!MediumFont || !LargeFont)
	{
		return;
	}

	const float X = Canvas->SizeX * 0.025f;
	float Y = Canvas->SizeY * 0.045f;

	const UEconomyComponent* Economy = PlayerState->GetEconomy();
	const USkillComponent* Skill = PlayerState->GetSkill();
	const int64 Gold = Economy ? Economy->GetGoldTotal() : 0;
	const int32 MeleeLevel = Skill ? Skill->GetSkillLevel(EWrecktideSkill::Melee) : 1;
	const int64 MeleeXP = Skill ? Skill->GetSkillXP(EWrecktideSkill::Melee) : 0;
	const int64 MeleeXPToNext = Skill ? Skill->GetXPToNext(MeleeLevel) : 0;

	DrawShadowedText(FString::Printf(TEXT("Gold: %lld"), Gold), MediumFont, X, Y, FLinearColor::White);
	Y += 28.0f;
	DrawShadowedText(GetNotorietyLine(), MediumFont, X, Y, FLinearColor::White);
	Y += 28.0f;
	DrawShadowedText(FString::Printf(TEXT("Melee Lv %d  XP %lld/%lld"), MeleeLevel, MeleeXP, MeleeXPToNext), MediumFont, X, Y, FLinearColor::White);

	// Camp NPC nameplates and their quest glyphs (C21/C22). Drawn before the panels so an open menu
	// covers them rather than the other way round.
	DrawNpcPlates();

	// SEA lane, ONE self-contained block (spec S1.4, S1.6 and S2.1): sail state, speed, wind and the
	// hull bar, drawn only while the player is at the tiller. Nothing above or below depends on it.
	if (const AWrecktideBoat* SailedBoat = Cast<AWrecktideBoat>(PlayerController->GetPawn()))
	{
		Y += 30.0f;
		const float Knots = FMath::Abs(SailedBoat->GetForwardSpeed()) / SailUUPerKnot;
		const FString SailLabel = SailedBoat->IsBackingDown()
			? TEXT("Backing down") : AWrecktideBoat::SailStateName(SailedBoat->GetSailState());
		DrawShadowedText(FString::Printf(TEXT("%s  %.1f kn"), *SailLabel, Knots), MediumFont, X, Y,
			FLinearColor(0.86f, 0.94f, 1.0f));
		Y += 26.0f;
		const float WindToYaw = WrecktideWind::ToYaw(this);
		DrawShadowedText(FString::Printf(TEXT("Wind %s"), *WrecktideWind::Compass(WindToYaw)), MediumFont, X, Y,
			FLinearColor(0.80f, 0.88f, 0.95f));
		Y += 26.0f;

		const int32 Hull = SailedBoat->GetHullHealth();
		const int32 HullMax = FMath::Max(SailedBoat->GetHullMax(), 1);
		const float HullFraction = FMath::Clamp(static_cast<float>(Hull) / static_cast<float>(HullMax), 0.0f, 1.0f);
		FCanvasTileItem HullBack(FVector2D(X, Y + 4.0f), GWhiteTexture, FVector2D(HullBarWidth, HullBarHeight),
			FLinearColor(0.06f, 0.05f, 0.04f, 0.75f));
		HullBack.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(HullBack);
		FCanvasTileItem HullFill(FVector2D(X, Y + 4.0f), GWhiteTexture,
			FVector2D(HullBarWidth * HullFraction, HullBarHeight),
			HullFraction > 0.35f ? FLinearColor(0.55f, 0.78f, 0.45f) : FLinearColor(0.85f, 0.35f, 0.25f));
		HullFill.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(HullFill);
		DrawShadowedText(FString::Printf(TEXT("Hull %d/%d   Hold %d/%d"), Hull, HullMax,
			SailedBoat->GetHoldSlotsUsed(), SailedBoat->GetHoldSlots()),
			MediumFont, X + HullBarWidth + 12.0f, Y, FLinearColor(0.92f, 0.90f, 0.85f));
	}

	// Active bounty, top right. Read through the PlayerState every frame on purpose: a cached
	// ProgressComponent would go stale the moment the player respawns with a new pawn.
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FString Objective = WrecktideQuestLogic::ObjectiveLine(PlayerState->GetProgress(), DataSubsystem);
	if (!Objective.IsEmpty())
	{
		DrawRightAlignedShadowedText(Objective, MediumFont, Canvas->SizeX * 0.975f, Canvas->SizeY * 0.045f,
			FLinearColor(1.0f, 0.85f, 0.4f));
	}

	// The story mission line sits directly under the bounty line, in the same gold, and pushes the
	// kill feed down so the two right-hand columns can never draw on top of each other.
	const APawn* OwnPawn = GetOwningPawn();
	const FVector PawnLocation = OwnPawn ? OwnPawn->GetActorLocation() : FVector::ZeroVector;
	const FString MissionLine = WrecktideMissionLogic::LogLine(PlayerState->GetProgress(), DataSubsystem, this, OwnPawn ? &PawnLocation : nullptr);
	float RightColumnY = Canvas->SizeY * 0.045f + 30.0f;
	if (!MissionLine.IsEmpty())
	{
		DrawRightAlignedShadowedText(MissionLine, MediumFont, Canvas->SizeX * 0.975f, RightColumnY,
			FLinearColor(1.0f, 0.85f, 0.4f));
		RightColumnY += 28.0f;
	}

	// Radar size is the controller's to own (spec C27), the same rule the map screen follows: pull
	// it every frame so a HUD created or replaced after the push still draws the right one.
	if (MenuController)
	{
		RadarState = MenuController->GetRadarState();
	}

	// Map layer (2026-09-06). The minimap and the edge arrow are gameplay furniture like the gold
	// line above: they go away under any menu, and under the full map, which owns the screen.
	const bool bMenuOverlay = MenuController && MenuController->IsAnyMenuOpen();
	FVector MissionTarget = FVector::ZeroVector;
	FString MissionTargetLabel;
	const bool bHasMissionTarget = WrecktideMap::ResolveMissionTarget(this, PlayerState, MissionTarget, MissionTargetLabel);
	float ViewYaw = 0.0f;
	if (PlayerController)
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		ViewYaw = ViewRotation.Yaw;
	}
	if (!bMenuOverlay && !bMapScreenOpen)
	{
		DrawMinimap(PawnLocation, ViewYaw, bHasMissionTarget ? &MissionTarget : nullptr);
		if (bHasMissionTarget)
		{
			DrawObjectiveArrow(MissionTarget);
		}
	}

	// Weapon bar (spec C17). ALWAYS visible in play, and it stays up under the inventory screen so
	// that screen can point at the slot it is assigning to. It goes away only under a screen that
	// owns the whole display: the full map, the pause tree and the title menu (which returned above).
	bWeaponBarDrawn = false;
	if (!bMapScreenOpen && (!bMenuOverlay || (MenuController && MenuController->IsInventoryOpen())))
	{
		DrawWeaponBar(*PlayerState);
		bWeaponBarDrawn = true;
	}

	const UWorld* World = GetWorld();
	if (World && !ToastText.IsEmpty() && World->GetTimeSeconds() < ToastExpireTime)
	{
		DrawCenteredShadowedText(ToastText, LargeFont, Canvas->SizeX * 0.5f, Canvas->SizeY * 0.30f, FLinearColor::White);
	}
	if (!InteractionPrompt.IsEmpty())
	{
		DrawCenteredShadowedText(InteractionPrompt, MediumFont, Canvas->SizeX * 0.5f, Canvas->SizeY * 0.70f,
			FLinearColor(1.0f, 0.85f, 0.4f));
	}

	ShopRowRects.Reset();
	if (ShopMenuLines.Num() > 0)
	{
		// 0.70, not 0.76: the weapon bar now owns the bottom of the screen and a four row list ran
		// into it at 720p (capture, 2026-09-06). The row rectangles below follow this number, so the
		// mouse hit test moves with the layout.
		float OfferY = Canvas->SizeY * 0.70f;
		for (int32 Index = 0; Index < ShopMenuLines.Num(); ++Index)
		{
			const bool bSelected = Index == ShopMenuSelectedIndex;
			const FString DisplayLine = FString::Printf(TEXT("%s %s"), bSelected ? TEXT(">") : TEXT(" "), *ShopMenuLines[Index]);
			// A hovered row lights the same gold a selected one does, so the mouse and the arrow keys
			// speak the same language (spec C20). The selection IS the hover: the controller moves it.
			DrawCenteredShadowedText(DisplayLine, MediumFont, Canvas->SizeX * 0.5f, OfferY,
				bSelected ? FLinearColor(1.0f, 0.8f, 0.1f) : FLinearColor::White);

			// Recorded WHERE IT IS DRAWN: a band wider than the text so a mouse near a short line
			// still lands on it, and exactly one row tall so two rows can never both claim a point.
			const float HalfWidth = FMath::Min(Canvas->SizeX * 0.42f, 420.0f);
			ShopRowRects.Emplace(FVector2D(Canvas->SizeX * 0.5f - HalfWidth, OfferY - 3.0f),
				FVector2D(Canvas->SizeX * 0.5f + HalfWidth, OfferY + 23.0f));
			OfferY += 26.0f;
		}
	}

	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	DrawNotorietyBar(Now, MediumFont);

	// Kill reward feed, newest on top, under the objective line. Right aligned so it can never
	// collide with the gold and XP block on the left.
	PruneRewardFeed();
	float FeedY = RightColumnY;
	for (const TPair<FString, float>& Entry : RewardFeed)
	{
		const float Remaining = Entry.Value - Now;
		if (Remaining <= 0.0f)
		{
			continue;
		}
		const float FeedAlpha = FMath::Clamp(Remaining / RewardFeedFadeSeconds, 0.0f, 1.0f);
		DrawRightAlignedShadowedText(Entry.Key, MediumFont, Canvas->SizeX * 0.975f, FeedY,
			FLinearColor(0.55f, 1.0f, 0.6f, FeedAlpha));
		FeedY += 24.0f;
	}

	if (!bMenuOverlay && !bMapScreenOpen)
	{
		DrawDamageNumbers(Now, LargeFont);
		if (const ACombatCharacter* Character = Cast<ACombatCharacter>(PlayerController->GetPawn()))
		{
			if (Character->IsPistolWielded()) { DrawPistolReticle(*Character); }
		}
	}

	if (Now < HitMarkerExpireTime)
	{
		// A critical marks twice the size and gold; a normal hit keeps the shipped white x exactly
		// as it was (spec C10).
		const float MarkerScale = bHitMarkerCritical ? 2.0f : 1.0f;
		const FLinearColor MarkerColor = bHitMarkerCritical ? FLinearColor(1.0f, 0.82f, 0.25f, 1.0f) : FLinearColor::White;
		DrawCenteredShadowedText(TEXT("x"), LargeFont, Canvas->SizeX * 0.5f,
			Canvas->SizeY * 0.5f - (12.0f * MarkerScale), MarkerColor, MarkerScale);
	}

	// Title card and controls hint: a first-run flourish. Probe runs skip both so an automated
	// screenshot or verdict never reads a title-card frame as gameplay.
	if (!AWrecktidePlayerState::IsProbeRun())
	{
		const float SinceStart = Now - HudStartTime;
		if (SinceStart < TitleCardSeconds && !bPauseMenuOpen)
		{
			const float TitleAlpha = FMath::Clamp((TitleCardSeconds - SinceStart) / TitleCardFadeSeconds, 0.0f, 1.0f);
			DrawCenteredShadowedText(TEXT("WRECKTIDE"), LargeFont, Canvas->SizeX * 0.5f, Canvas->SizeY * 0.34f,
				FLinearColor(1.0f, 0.85f, 0.4f, TitleAlpha));
			DrawCenteredShadowedText(TEXT("A pirate's fortune awaits"), MediumFont, Canvas->SizeX * 0.5f, Canvas->SizeY * 0.41f,
				FLinearColor(1.0f, 1.0f, 1.0f, TitleAlpha));
		}
		if (SinceStart < ControlsHintSeconds && !bPauseMenuOpen)
		{
			const float HintAlpha = FMath::Clamp((ControlsHintSeconds - SinceStart) / ControlsHintFadeSeconds, 0.0f, 1.0f);
			DrawShadowedText(TEXT("WASD move   Mouse aim   LMB attack   RMB charged   F interact   1 wield blade   Tab chest   Esc pause"),
				MediumFont, X, Canvas->SizeY * 0.93f, FLinearColor(1.0f, 1.0f, 1.0f, HintAlpha));
		}
	}

	if (IsDeathScreenActive())
	{
		DrawCenteredShadowedText(TEXT("YOU DIED"), LargeFont, Canvas->SizeX * 0.5f, Canvas->SizeY * 0.42f,
			FLinearColor(0.9f, 0.1f, 0.1f));
	}

	// The full map is modal: it draws last so nothing from the play HUD sits on top of it.
	if (bMapScreenOpen && !bMenuOverlay)
	{
		DrawFullMap(PawnLocation, ViewYaw, bHasMissionTarget ? &MissionTarget : nullptr, MissionTargetLabel);
	}

	// The pause panel is no longer drawn here (2026-09-06): UI\SWrecktideMenu draws a real parchment
	// panel in Slate, above the canvas, with a mouse path the canvas never had. The SetPauseMenu /
	// SetPauseMenuHeader API stays exactly as it was, and the lines it stores are still the machine
	// evidence -MenuProbe and -FeelProbe read, so every existing caller and probe is untouched.
}

void AWrecktideHUD::ShowToast(const FString& Message, float Seconds)
{
	ToastText = Message;

	if (UWorld* World = GetWorld())
	{
		ToastExpireTime = World->GetTimeSeconds() + Seconds;
	}
}

void AWrecktideHUD::SetInteractionPrompt(const FString& Message)
{
	InteractionPrompt = Message;
}

void AWrecktideHUD::ClearInteractionPrompt()
{
	InteractionPrompt.Reset();
}

FString AWrecktideHUD::GetToastText() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < ToastExpireTime ? ToastText : FString();
}

FString AWrecktideHUD::GetNotorietyLine() const
{
	const USkillComponent* Skill = BoundSkill;
	if (!Skill)
	{
		const APlayerController* PlayerController = GetOwningPlayerController();
		const AWrecktidePlayerState* PlayerState = PlayerController
			? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
		Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	}
	return FString::Printf(TEXT("Notoriety Lv %d"), Skill ? Skill->GetNotorietyLevel() : 1);
}

void AWrecktideHUD::SetShopMenu(const TArray<FString>& Lines, int32 SelectedIndex)
{
	ShopMenuLines = Lines;
	ShopMenuSelectedIndex = ShopMenuLines.IsValidIndex(SelectedIndex) ? SelectedIndex : INDEX_NONE;
}

void AWrecktideHUD::ClearShopMenu()
{
	ShopMenuLines.Reset();
	ShopMenuSelectedIndex = INDEX_NONE;
}

void AWrecktideHUD::PushRewardLine(const FString& Line)
{
	if (Line.IsEmpty())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float ExpireTime = (World ? World->GetTimeSeconds() : 0.0f) + RewardFeedFadeSeconds;
	RewardFeed.Insert(TPair<FString, float>(Line, ExpireTime), 0);

	// Oldest lines fall off the bottom rather than the feed growing without bound on a kill streak.
	while (RewardFeed.Num() > MaxRewardFeedLines)
	{
		RewardFeed.RemoveAt(RewardFeed.Num() - 1);
	}
}

void AWrecktideHUD::FlashHitMarker(bool bCritical)
{
	if (const UWorld* World = GetWorld())
	{
		HitMarkerExpireTime = World->GetTimeSeconds() + HitMarkerSeconds;
		bHitMarkerCritical = bCritical;
	}
}

void AWrecktideHUD::PushDamageNumber(const FVector& WorldLocation, float Amount, EWrecktideDamageNumberKind Kind)
{
	if (!FMath::IsFinite(Amount) || Amount <= 0.0f)
	{
		return;
	}

	FDamageNumberEntry Entry;
	Entry.WorldLocation = WorldLocation;
	Entry.Amount = Amount;
	Entry.Kind = Kind;
	Entry.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Entry.HorizontalJitter = FMath::FRandRange(-DamageNumberJitterPixels, DamageNumberJitterPixels);
	DamageNumbers.Insert(Entry, 0);

	while (DamageNumbers.Num() > FMath::Max(1, MaxDamageNumbers))
	{
		DamageNumbers.RemoveAt(DamageNumbers.Num() - 1);
	}
}

int32 AWrecktideHUD::GetDamageNumberCount() const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	int32 Count = 0;
	for (const FDamageNumberEntry& Entry : DamageNumbers)
	{
		if (Now - Entry.StartTime < DamageNumberLifeSeconds)
		{
			++Count;
		}
	}
	return Count;
}

bool AWrecktideHUD::GetLatestDamageNumber(float& OutAmount, EWrecktideDamageNumberKind& OutKind, bool& OutWasDrawn) const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	for (const FDamageNumberEntry& Entry : DamageNumbers)
	{
		if (Now - Entry.StartTime < DamageNumberLifeSeconds)
		{
			OutAmount = Entry.Amount;
			OutKind = Entry.Kind;
			OutWasDrawn = Entry.bWasDrawn;
			return true;
		}
	}
	return false;
}

void AWrecktideHUD::DrawDamageNumbers(float Now, UFont* Font)
{
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!Canvas || !PlayerController || !Font)
	{
		return;
	}

	PruneDamageNumbers();
	for (FDamageNumberEntry& Entry : DamageNumbers)
	{
		const float AgeFraction = FMath::Clamp((Now - Entry.StartTime) / DamageNumberLifeSeconds, 0.0f, 1.0f);
		FVector2D ScreenPoint = FVector2D::ZeroVector;
		if (!UGameplayStatics::ProjectWorldToScreen(PlayerController, Entry.WorldLocation, ScreenPoint, false))
		{
			continue;
		}

		const float EaseOut = 1.0f - FMath::Square(1.0f - AgeFraction);
		ScreenPoint.X += Entry.HorizontalJitter;
		ScreenPoint.Y -= DamageNumberRisePixels * EaseOut;

		FLinearColor Color = NormalDamageNumberColor;
		float Scale = NormalDamageNumberScale;
		switch (Entry.Kind)
		{
		case EWrecktideDamageNumberKind::Critical:
			Color = CriticalDamageNumberColor;
			Scale = CriticalDamageNumberScale;
			break;
		case EWrecktideDamageNumberKind::Heal:
			Color = HealDamageNumberColor;
			break;
		case EWrecktideDamageNumberKind::PlayerDamage:
			Color = PlayerDamageNumberColor;
			Scale = PlayerDamageNumberScale;
			break;
		default:
			break;
		}
		Color.A *= 1.0f - AgeFraction;

		const FString Text = FString::FromInt(FMath::Max(0, FMath::RoundToInt(Entry.Amount)));
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		GetTextSize(Text, TextWidth, TextHeight, Font, Scale);
		DrawShadowedText(Text, Font, ScreenPoint.X - TextWidth * 0.5f,
			ScreenPoint.Y - TextHeight * 0.5f, Color, Scale);
		Entry.bWasDrawn = true;
	}
}

void AWrecktideHUD::PruneDamageNumbers()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	DamageNumbers.RemoveAll([this, Now](const FDamageNumberEntry& Entry)
	{
		return Now - Entry.StartTime >= DamageNumberLifeSeconds;
	});
}

void AWrecktideHUD::SetDeathScreen(bool bDead, float MaxSeconds)
{
	bDeathScreen = bDead;
	DeathScreenExpireTime = 0.0f;

	if (bDead && MaxSeconds > 0.0f)
	{
		if (const UWorld* World = GetWorld())
		{
			DeathScreenExpireTime = World->GetTimeSeconds() + MaxSeconds;
		}
	}
}

void AWrecktideHUD::SetPauseMenu(bool bOpen, const TArray<FString>& Lines, int32 SelectedIndex)
{
	bPauseMenuOpen = bOpen;
	PauseMenuLines = bOpen ? Lines : TArray<FString>();
	PauseMenuSelectedIndex = PauseMenuLines.IsValidIndex(SelectedIndex) ? SelectedIndex : INDEX_NONE;
}

int32 AWrecktideHUD::GetRewardFeedCount() const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	int32 Count = 0;
	for (const TPair<FString, float>& Entry : RewardFeed)
	{
		if (Entry.Value > Now)
		{
			++Count;
		}
	}
	return Count;
}

FString AWrecktideHUD::GetRewardFeedLine(int32 Index) const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	int32 Seen = 0;
	for (const TPair<FString, float>& Entry : RewardFeed)
	{
		if (Entry.Value <= Now)
		{
			continue;
		}
		if (Seen == Index)
		{
			return Entry.Key;
		}
		++Seen;
	}
	return FString();
}

bool AWrecktideHUD::IsDeathScreenActive() const
{
	if (!bDeathScreen)
	{
		return false;
	}

	// Fail-closed cap: a respawn that never lands must not strand YOU DIED on screen forever.
	const UWorld* World = GetWorld();
	return !World || DeathScreenExpireTime <= 0.0f || World->GetTimeSeconds() < DeathScreenExpireTime;
}

void AWrecktideHUD::PruneRewardFeed()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	RewardFeed.RemoveAll([Now](const TPair<FString, float>& Entry)
	{
		return Entry.Value <= Now;
	});
}

void AWrecktideHUD::HandleSkillLevelUp(EWrecktideSkill Skill, int32 NewLevel)
{
	if (Skill == EWrecktideSkill::Melee)
	{
		ShowToast(FString::Printf(TEXT("MELEE LEVEL %d!"), NewLevel));
		UWrecktideAudio::PlaySfx(this, TEXT("levelup"));
	}
}

void AWrecktideHUD::HandleSkillXPChanged(EWrecktideSkill Skill, int64 NewXP)
{
	static_cast<void>(Skill);
	static_cast<void>(NewXP);
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	NotorietyBarStartFraction = GetNotorietyBarFillFraction();
	NotorietyBarTargetFraction = CalculateNotorietyFraction();
	NotorietyBarAnimationStartTime = Now;
	NotorietyBarLastGainTime = Now;
	bNotorietyBarHasGain = true;
}

void AWrecktideHUD::HandleNotorietyLevelUp(int32 NewLevel)
{
	const UWorld* World = GetWorld();
	NotorietyBarFlashExpireTime = (World ? World->GetTimeSeconds() : 0.0f) + NotorietyBarLevelFlashSeconds;
	NotorietyBarFlashLevel = NewLevel;
	const FString Line = FString::Printf(TEXT("Notoriety level %d"), NewLevel);
	PushRewardLine(Line);
	ShowToast(Line);
	UWrecktideAudio::PlaySfx(this, TEXT("levelup"));
}

float AWrecktideHUD::CalculateNotorietyFraction() const
{
	const USkillComponent* Skill = BoundSkill;
	if (!Skill)
	{
		const APlayerController* PlayerController = GetOwningPlayerController();
		const AWrecktidePlayerState* PlayerState = PlayerController
			? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
		Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	}
	if (!Skill)
	{
		return 0.0f;
	}

	const int32 Level = Skill->GetNotorietyLevel();
	const int64 FloorXP = Skill->GetNotorietyXPForLevel(Level);
	const int64 CeilingXP = Skill->GetNotorietyXPForLevel(Level + 1);
	if (CeilingXP <= FloorXP)
	{
		return 1.0f;
	}
	return FMath::Clamp(static_cast<float>(Skill->GetNotorietyXP() - FloorXP)
		/ static_cast<float>(CeilingXP - FloorXP), 0.0f, 1.0f);
}

float AWrecktideHUD::GetNotorietyBarFillFraction() const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const float LinearAlpha = FMath::Clamp((Now - NotorietyBarAnimationStartTime)
		/ FMath::Max(NotorietyBarFillSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float SmoothAlpha = LinearAlpha * LinearAlpha * (3.0f - 2.0f * LinearAlpha);
	return FMath::Lerp(NotorietyBarStartFraction, NotorietyBarTargetFraction, SmoothAlpha);
}

bool AWrecktideHUD::IsNotorietyBarVisible() const
{
	const ACombatPlayerController* MenuController = Cast<ACombatPlayerController>(GetOwningPlayerController());
	if (bPauseMenuOpen || (MenuController && MenuController->IsAnyMenuOpen()))
	{
		return true;
	}
	const UWorld* World = GetWorld();
	return bNotorietyBarHasGain && World
		&& World->GetTimeSeconds() < NotorietyBarLastGainTime + NotorietyBarVisibleSeconds;
}

void AWrecktideHUD::DrawNotorietyBar(float Now, UFont* Font)
{
	if (!Canvas || !Font || !IsNotorietyBarVisible())
	{
		return;
	}

	const ACombatPlayerController* MenuController = Cast<ACombatPlayerController>(GetOwningPlayerController());
	const bool bMenuStatic = bPauseMenuOpen || (MenuController && MenuController->IsAnyMenuOpen());
	float Alpha = 1.0f;
	if (!bMenuStatic)
	{
		const float Age = FMath::Max(0.0f, Now - NotorietyBarLastGainTime);
		const float FadeIn = FMath::Clamp(Age / FMath::Max(NotorietyBarFadeInSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		const float Remaining = NotorietyBarVisibleSeconds - Age;
		const float FadeOut = FMath::Clamp(Remaining / FMath::Max(NotorietyBarFadeOutSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		Alpha = FMath::Min(FadeIn, FadeOut);
	}

	const USkillComponent* Skill = BoundSkill;
	if (!Skill)
	{
		return;
	}
	const int32 Level = Skill->GetNotorietyLevel();
	const float Width = Canvas->SizeX * NotorietyBarWidthFraction;
	const float X = (Canvas->SizeX - Width) * 0.5f;
	const float Y = NotorietyBarTop;
	const float FrameThickness = 3.0f;
	const bool bFlashing = Now < NotorietyBarFlashExpireTime;
	auto WithAlpha = [Alpha](FLinearColor Color)
	{
		Color.A *= Alpha;
		return Color;
	};

	FCanvasTileItem Frame(FVector2D(X - FrameThickness, Y - FrameThickness), GWhiteTexture,
		FVector2D(Width + FrameThickness * 2.0f, NotorietyBarHeight + FrameThickness * 2.0f),
		WithAlpha(bFlashing ? NotorietyBarFlashColor : NotorietyBarFrameColor));
	Frame.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Frame);
	FCanvasTileItem Parchment(FVector2D(X, Y), GWhiteTexture, FVector2D(Width, NotorietyBarHeight),
		WithAlpha(NotorietyBarParchmentColor));
	Parchment.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Parchment);

	const float FillFraction = bMenuStatic ? CalculateNotorietyFraction() : GetNotorietyBarFillFraction();
	if (FillFraction > 0.0f)
	{
		FCanvasTileItem Fill(FVector2D(X, Y), GWhiteTexture,
			FVector2D(Width * FillFraction, NotorietyBarHeight),
			WithAlpha(bFlashing ? NotorietyBarFlashColor : NotorietyBarFillColor));
		Fill.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Fill);
	}

	const FLinearColor LabelColor(1.0f, 0.94f, 0.78f, Alpha);
	DrawShadowedText(FString::Printf(TEXT("Notoriety %d"), Level), Font, X, Y - 22.0f, LabelColor);
	DrawRightAlignedShadowedText(FString::FromInt(Level + 1), Font, X + Width, Y - 22.0f, LabelColor);
	if (bFlashing)
	{
		DrawCenteredShadowedText(FString::Printf(TEXT("Notoriety level %d"), NotorietyBarFlashLevel),
			Font, Canvas->SizeX * 0.5f, Y + NotorietyBarHeight + 7.0f,
			FLinearColor(NotorietyBarFlashColor.R, NotorietyBarFlashColor.G, NotorietyBarFlashColor.B, Alpha));
	}
}

// ---------------------------------------------------------------------------------------------
// Weapon bar (spec C17). A VIEW of AWrecktidePlayerState::GetAssignedSlots and EquippedWeaponRow:
// it stores nothing but what it drew, which is the probe's evidence that the two agree.
// ---------------------------------------------------------------------------------------------
void AWrecktideHUD::DrawWeaponBar(const AWrecktidePlayerState& PlayerState)
{
	if (!Canvas)
	{
		return;
	}

	const int32 SlotCount = AWrecktidePlayerState::EquipSlotCount;
	// One utility square at each end: centre the entire seven-square group.
	const int32 ToolbarCount = SlotCount + 2;
	const float BarWidth = (ToolbarCount * WeaponSlotSize) + ((ToolbarCount - 1) * WeaponSlotGap);
	const float ToolbarX = (Canvas->SizeX - BarWidth) * 0.5f;
	const float StartX = ToolbarX + WeaponSlotSize + WeaponSlotGap;
	const float SlotY = Canvas->SizeY - WeaponBarBottomMargin;

	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	DrawnSlotLabels.SetNum(SlotCount);
	DrawnWeaponSlotRects.SetNum(SlotCount);
	DrawnWieldedSlot = 0;

	for (int32 Slot = 1; Slot <= SlotCount; ++Slot)
	{
		const FName Row = PlayerState.GetAssignedSlot(Slot);
		const bool bFilled = Row != NAME_None;
		FString Label;
		if (bFilled)
		{
			const FWeaponTableRow* WeaponRow = DataSubsystem ? DataSubsystem->GetWeaponRow(Row) : nullptr;
			Label = (WeaponRow && !WeaponRow->DisplayName.IsEmpty())
				? WeaponRow->DisplayName.ToString() : Row.ToString();
		}
		else
		{
			Label = AWrecktidePlayerState::GetSlotClassName(Slot);
		}

		// Lit means "this is what is in his hand right now", so an unarmed pirate lights nothing.
		const bool bWielded = bFilled && Row == PlayerState.EquippedWeaponRow;
		if (bWielded)
		{
			DrawnWieldedSlot = Slot;
		}

		DrawnSlotLabels[Slot - 1] = Label;
		const float SlotX = StartX + (Slot - 1) * (WeaponSlotSize + WeaponSlotGap);
		DrawnWeaponSlotRects[Slot - 1] = FBox2D(FVector2D(SlotX, SlotY),
			FVector2D(SlotX + WeaponSlotSize, SlotY + WeaponSlotSize));
		DrawWeaponSlot(Slot, SlotX, SlotY,
			WeaponSlotSize, Label, bFilled, bWielded);
	}

	const ACombatPlayerController* PC = Cast<ACombatPlayerController>(GetOwningPlayerController());
	bChestButtonLit = PC && PC->IsInventoryOpen();
	const float ChestX = StartX + SlotCount * (WeaponSlotSize + WeaponSlotGap);
	MapButtonRect = FBox2D(FVector2D(ToolbarX, SlotY), FVector2D(ToolbarX + WeaponSlotSize, SlotY + WeaponSlotSize));
	ChestButtonRect = FBox2D(FVector2D(ChestX, SlotY), FVector2D(ChestX + WeaponSlotSize, SlotY + WeaponSlotSize));
	DrawToolbarButton(ToolbarX, SlotY, WeaponSlotSize, false, false);
	DrawToolbarButton(ChestX, SlotY, WeaponSlotSize, true, bChestButtonLit);
}

void AWrecktideHUD::DrawWeaponSlotFrame(float X, float Y, float Size, bool bLit)
{
	const FLinearColor Edge = bLit ? WeaponSlotEdgeLit : WeaponSlotEdge;
	DrawRect(WeaponSlotFill, X, Y, Size, Size);
	// Four thin rectangles rather than lines: a rect edge stays crisp at any resolution scale.
	DrawRect(Edge, X, Y, Size, WeaponSlotBorder);
	DrawRect(Edge, X, Y + Size - WeaponSlotBorder, Size, WeaponSlotBorder);
	DrawRect(Edge, X, Y, WeaponSlotBorder, Size);
	DrawRect(Edge, X + Size - WeaponSlotBorder, Y, WeaponSlotBorder, Size);
}

void AWrecktideHUD::DrawWeaponSlot(int32 Slot, float X, float Y, float Size, const FString& Label,
	bool bFilled, bool bWielded)
{
	UFont* SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!SmallFont)
	{
		return;
	}
	DrawWeaponSlotFrame(X, Y, Size, bWielded);

	// Key badge: the whole point of the bar is that the number is readable at a glance.
	DrawShadowedText(FString::FromInt(Slot), SmallFont, X + 5.0f, Y + 3.0f,
		bWielded ? WeaponSlotEdgeLit : WeaponSlotInk);

	if (bFilled && Slot == 2)
	{
		DrawPistolGlyph(X + Size * 0.5f, Y + Size * 0.56f, Size * 0.62f,
			bWielded ? WeaponSlotEdgeLit : WeaponSlotInk);
	}
	else if (bFilled)
	{
		DrawBladeGlyph(X + Size * 0.5f, Y + Size * 0.56f, Size * 0.62f,
			bWielded ? WeaponSlotEdgeLit : WeaponSlotInk);
	}
	else
	{
		// An empty slot reads as a locked frame, not as a bug: one dim bar across the middle.
		DrawRect(WeaponSlotInkDim, X + Size * 0.28f, Y + Size * 0.52f, Size * 0.44f, 2.0f);
	}

	const FLinearColor LabelInk = bFilled ? (bWielded ? WeaponSlotEdgeLit : WeaponSlotInk) : WeaponSlotInkDim;
	FString FirstWord, Rest;
	if (Slot == 2 && bFilled && Label.Split(TEXT(" "), &FirstWord, &Rest))
	{
		DrawCenteredShadowedText(FirstWord, SmallFont, X + Size * 0.5f, Y + Size + 4.0f, LabelInk);
		DrawCenteredShadowedText(Rest, SmallFont, X + Size * 0.5f, Y + Size + 18.0f, LabelInk);
	}
	else { DrawCenteredShadowedText(Label, SmallFont, X + Size * 0.5f, Y + Size + 4.0f, LabelInk); }
}

void AWrecktideHUD::DrawToolbarButton(float X, float Y, float Size, bool bChest, bool bLit)
{
	UFont* SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!SmallFont)
	{
		return;
	}
	DrawWeaponSlotFrame(X, Y, Size, bLit);
	DrawShadowedText(bChest ? TEXT("Tab") : TEXT("M"), SmallFont, X + 5.0f, Y + 3.0f,
		bLit ? WeaponSlotEdgeLit : WeaponSlotInk);

	// Glyph coordinates are original pixel art on a 16-unit square, scaled with the slot.
	const float GlyphUnit = Size / 16.0f;
	auto InkRect = [this, X, Y, GlyphUnit](float Left, float Top, float Width, float Height)
	{
		DrawRect(WeaponSlotInk, X + Left * GlyphUnit, Y + Top * GlyphUnit,
			Width * GlyphUnit, Height * GlyphUnit);
	};
	if (bChest)
	{
		// Lid, body outline and clasp; the gap between lid and body stays visible.
		InkRect(4, 6, 8, 1);
		InkRect(3, 7, 10, 1);
		InkRect(3, 9, 1, 4);
		InkRect(12, 9, 1, 4);
		InkRect(3, 12, 10, 1);
		InkRect(7, 8, 2, 3);
	}
	else
	{
		// Stepped compass ring and a north needle, all rectangles like the blade glyph.
		InkRect(6, 5, 4, 1);
		InkRect(4, 6, 2, 1);
		InkRect(10, 6, 2, 1);
		InkRect(3, 7, 1, 4);
		InkRect(12, 7, 1, 4);
		InkRect(4, 11, 2, 1);
		InkRect(10, 11, 2, 1);
		InkRect(6, 12, 4, 1);
		InkRect(7.5f, 6, 1, 6);
		InkRect(6.5f, 8, 3, 2);
	}
	DrawCenteredShadowedText(bChest ? TEXT("Chest") : TEXT("Map"), SmallFont,
		X + Size * 0.5f, Y + Size + 4.0f, bLit ? WeaponSlotEdgeLit : WeaponSlotInk);
}

FBox2D AWrecktideHUD::GetWeaponSlotRect(int32 Slot) const
{
	return bWeaponBarDrawn && DrawnWeaponSlotRects.IsValidIndex(Slot - 1)
		? DrawnWeaponSlotRects[Slot - 1] : FBox2D(ForceInit);
}

void AWrecktideHUD::DrawPistolGlyph(float CenterX, float CenterY, float Size, const FLinearColor& Color)
{
	DrawRect(Color, CenterX - Size * 0.45f, CenterY - Size * 0.22f, Size * 0.85f, Size * 0.15f);
	DrawRect(Color, CenterX - Size * 0.40f, CenterY - Size * 0.07f, Size * 0.20f, Size * 0.46f);
	DrawRect(Color, CenterX - Size * 0.20f, CenterY + Size * 0.08f, Size * 0.25f, Size * 0.07f);
	DrawRect(Color, CenterX, CenterY - Size * 0.07f, Size * 0.06f, Size * 0.20f);
}

void AWrecktideHUD::DrawPistolReticle(const ACombatCharacter& Character)
{
	const float X = Canvas->SizeX * 0.5f;
	const float Y = Canvas->SizeY * 0.5f;
	const float Gap = PistolCrosshairGap;
	const float Length = PistolCrosshairTickLength;
	const float Width = PistolCrosshairThickness;
	bPistolCrosshairDrawn = true;
	bPistolCrosshairGold = Character.IsPistolTargetLivingEnemy();
	const FLinearColor Ink = bPistolCrosshairGold ? WeaponSlotEdgeLit : FLinearColor::White;
	auto Tick = [this, Ink](float Left, float Top, float W, float H)
	{
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f), Left - 1.0f, Top - 1.0f, W + 2.0f, H + 2.0f);
		DrawRect(Ink, Left, Top, W, H);
	};
	Tick(X - Gap - Length, Y - Width * 0.5f, Length, Width);
	Tick(X + Gap, Y - Width * 0.5f, Length, Width);
	Tick(X - Width * 0.5f, Y - Gap - Length, Width, Length);
	Tick(X - Width * 0.5f, Y + Gap, Width, Length);
	Tick(X - Width * 0.5f, Y - Width * 0.5f, Width, Width);
	if (!Character.IsPistolReloading()) { return; }
	bPistolReloadArcDrawn = true;
	const int32 Segments = FMath::Clamp(PistolReloadArcSegments, 12, 128);
	const float Fraction = Character.GetPistolReloadFraction();
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float A = -HALF_PI + 2.0f * PI * Segment / Segments;
		const float B = -HALF_PI + 2.0f * PI * (Segment + 1) / Segments;
		const FLinearColor ArcInk = static_cast<float>(Segment + 1) / Segments <= Fraction
			? WeaponSlotEdgeLit : FLinearColor(0.15f, 0.12f, 0.07f, 0.65f);
		DrawLine(X + FMath::Cos(A) * PistolReloadArcRadius, Y + PistolReloadArcOffset + FMath::Sin(A) * PistolReloadArcRadius,
			X + FMath::Cos(B) * PistolReloadArcRadius, Y + PistolReloadArcOffset + FMath::Sin(B) * PistolReloadArcRadius, ArcInk, 1.5f);
	}
}

void AWrecktideHUD::DrawBladeGlyph(float CenterX, float CenterY, float Size, const FLinearColor& Color)
{
	// Ours, drawn from four rectangles: blade, crossguard, grip, pommel. No texture to author, no
	// asset to stage, and it scales with the slot.
	const float BladeWidth = FMath::Max(3.0f, Size * 0.14f);
	const float BladeHeight = Size * 0.54f;
	const float GuardWidth = Size * 0.56f;

	DrawRect(Color, CenterX - BladeWidth * 0.5f, CenterY - Size * 0.5f, BladeWidth, BladeHeight);
	DrawRect(Color, CenterX - GuardWidth * 0.5f, CenterY - Size * 0.5f + BladeHeight, GuardWidth, FMath::Max(2.0f, Size * 0.09f));
	DrawRect(Color, CenterX - BladeWidth * 0.35f, CenterY - Size * 0.5f + BladeHeight + Size * 0.09f,
		FMath::Max(2.0f, BladeWidth * 0.7f), Size * 0.24f);
	DrawRect(Color, CenterX - BladeWidth * 0.6f, CenterY - Size * 0.5f + BladeHeight + Size * 0.33f,
		FMath::Max(3.0f, BladeWidth * 1.2f), FMath::Max(2.0f, Size * 0.08f));
}

FBox2D AWrecktideHUD::GetShopRowRect(int32 Index) const
{
	return ShopRowRects.IsValidIndex(Index) ? ShopRowRects[Index] : FBox2D(ForceInit);
}

int32 AWrecktideHUD::HitTestShopRow(const FVector2D& ScreenPosition) const
{
	for (int32 Index = 0; Index < ShopRowRects.Num(); ++Index)
	{
		if (ShopRowRects[Index].bIsValid && ShopRowRects[Index].IsInside(ScreenPosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FString AWrecktideHUD::GetDrawnSlotLabel(int32 Slot) const
{
	return DrawnSlotLabels.IsValidIndex(Slot - 1) ? DrawnSlotLabels[Slot - 1] : FString();
}

void AWrecktideHUD::DrawShadowedText(const FString& Text, UFont* Font, float X, float Y, const FLinearColor& Color, float Scale)
{
	// The shadow inherits the caller's alpha, otherwise a fading line (kill feed, title card)
	// leaves a solid black ghost behind. Opaque callers are unaffected.
	DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, Color.A), X + 1.0f, Y + 1.0f, Font, Scale, false);
	DrawText(Text, Color, X, Y, Font, Scale, false);
}

void AWrecktideHUD::DrawCenteredShadowedText(const FString& Text, UFont* Font, float CenterX, float Y, const FLinearColor& Color, float Scale)
{
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Text, TextWidth, TextHeight, Font, Scale);
	DrawShadowedText(Text, Font, CenterX - TextWidth * 0.5f, Y, Color, Scale);
}

void AWrecktideHUD::DrawRightAlignedShadowedText(const FString& Text, UFont* Font, float RightX, float Y, const FLinearColor& Color)
{
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Text, TextWidth, TextHeight, Font, 1.0f);
	DrawShadowedText(Text, Font, RightX - TextWidth, Y, Color);
}

void AWrecktideHUD::SetPauseMenuHeader(const FString& Title, const FString& Hint)
{
	PauseMenuTitle = Title.IsEmpty() ? FString(TEXT("PAUSED")) : Title;
	PauseMenuHint = Hint;
}

void AWrecktideHUD::SetMapScreen(bool bOpen)
{
	bMapScreenOpen = bOpen;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP hud mirror=%d"), bOpen ? 1 : 0);
}

void AWrecktideHUD::DrawSolidTriangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
	if (!Canvas)
	{
		return;
	}

	FCanvasUVTri Tri;
	Tri.V0_Pos = A;
	Tri.V1_Pos = B;
	Tri.V2_Pos = C;
	Tri.V0_UV = FVector2D::ZeroVector;
	Tri.V1_UV = FVector2D::ZeroVector;
	Tri.V2_UV = FVector2D::ZeroVector;
	Tri.V0_Color = Color;
	Tri.V1_Color = Color;
	Tri.V2_Color = Color;

	TArray<FCanvasUVTri> Triangles;
	Triangles.Add(Tri);
	FCanvasTriangleItem Item(Triangles, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AWrecktideHUD::DrawMapGlyph(const FString& Glyph, const FLinearColor& Color, float CenterX, float CenterY, const FString& Label)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	float GlyphWidth = 0.0f;
	float GlyphHeight = 0.0f;
	GetTextSize(Glyph, GlyphWidth, GlyphHeight, Font, 1.0f);
	DrawShadowedText(Glyph, Font, CenterX - GlyphWidth * 0.5f, CenterY - GlyphHeight * 0.5f, Color);

	if (!Label.IsEmpty())
	{
		float LabelWidth = 0.0f;
		float LabelHeight = 0.0f;
		GetTextSize(Label, LabelWidth, LabelHeight, Font, 1.0f);
		DrawShadowedText(Label, Font, CenterX - LabelWidth * 0.5f, CenterY + GlyphHeight * 0.5f,
			FLinearColor(0.96f, 0.93f, 0.83f, 0.95f));
	}
}

void AWrecktideHUD::DrawMinimap(const FVector& PlayerLocation, float ViewYaw, const FVector* MissionTarget)
{
	if (!Canvas)
	{
		return;
	}

	// Spec C27: the small state is the same radar at compass size with no island plate under it,
	// so one draw path serves both and they can never drift apart.
	const bool bSmall = RadarState == EWrecktideRadarState::Small;
	const float Diameter = bSmall ? MinimapSmallDiameter : MinimapDiameter;
	const float Radius = Diameter * 0.5f;
	const float RimSize = Diameter * MinimapRimScale;
	bDrewMapPlate = false;
	DrawnRadarDiameter = Diameter;
	const float CenterX = MinimapMargin + RimSize * 0.5f;
	const float CenterY = Canvas->SizeY - MinimapMargin - RimSize * 0.5f;
	const FVector2D Center(CenterX, CenterY);
	const float PixelsPerUU = Radius / WrecktideMap::MinimapRangeUU;

	// Heading-up by default (GTA style). North-up is the same code with the rotation taken out, which
	// is why the toggle costs one line rather than a second draw path.
	// C34.6: the plate of the island the pirate is on, or the nearest one at sea.
	const WrecktideMap::FIslandChart& Chart = WrecktideMap::ChartFor(PlayerLocation);
	UTexture2D* ChartPlate = ChartTextureFor(Chart);
	DrawnChartId = Chart.Id;

	const float RadarYaw = bMinimapNorthUp ? 0.0f : ViewYaw;
	const float CosYaw = FMath::Cos(FMath::DegreesToRadians(RadarYaw));
	const float SinYaw = FMath::Sin(FMath::DegreesToRadians(RadarYaw));

	// The plate. Each screen corner of the square carries the UV of the world point it shows, so the
	// rotation lives entirely in the UVs: exact, one draw, and no oversized tile to hide the corners.
	// The rim texture then masks the square down to a disc.
	const FVector2D Corners[4] = {
		FVector2D(-Radius, -Radius),
		FVector2D(Radius, -Radius),
		FVector2D(Radius, Radius),
		FVector2D(-Radius, Radius)
	};
	FVector2D CornerPos[4];
	FVector2D CornerUV[4];
	for (int32 Index = 0; Index < 4; ++Index)
	{
		CornerPos[Index] = Center + Corners[Index];
		const float ForwardUU = -Corners[Index].Y / PixelsPerUU;
		const float RightUU = Corners[Index].X / PixelsPerUU;
		const FVector WorldPoint(
			PlayerLocation.X + ForwardUU * CosYaw - RightUU * SinYaw,
			PlayerLocation.Y + ForwardUU * SinYaw + RightUU * CosYaw,
			0.0f);
		CornerUV[Index] = WrecktideMap::WorldToUV(WorldPoint, Chart);
	}

	if (bSmall)
	{
		// No plate at all: a plain dark-sea disc, so the blips are the only thing to read. The rim
		// texture below masks this square down to the disc, exactly as it does for the plate.
		FCanvasTileItem Sea(Center - FVector2D(Radius, Radius), GWhiteTexture,
			FVector2D(Diameter, Diameter), FLinearColor(0.05f, 0.09f, 0.13f, 0.92f));
		Sea.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Sea);
	}
	else if (ChartPlate && ChartPlate->GetResource())
	{
		bDrewMapPlate = true;
		FCanvasUVTri First;
		First.V0_Pos = CornerPos[0]; First.V0_UV = CornerUV[0]; First.V0_Color = FLinearColor::White;
		First.V1_Pos = CornerPos[1]; First.V1_UV = CornerUV[1]; First.V1_Color = FLinearColor::White;
		First.V2_Pos = CornerPos[2]; First.V2_UV = CornerUV[2]; First.V2_Color = FLinearColor::White;
		FCanvasUVTri Second;
		Second.V0_Pos = CornerPos[0]; Second.V0_UV = CornerUV[0]; Second.V0_Color = FLinearColor::White;
		Second.V1_Pos = CornerPos[2]; Second.V1_UV = CornerUV[2]; Second.V1_Color = FLinearColor::White;
		Second.V2_Pos = CornerPos[3]; Second.V2_UV = CornerUV[3]; Second.V2_Color = FLinearColor::White;

		TArray<FCanvasUVTri> Triangles;
		Triangles.Add(First);
		Triangles.Add(Second);
		FCanvasTriangleItem Plate(Triangles, ChartPlate->GetResource());
		Plate.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Plate);
	}
	else
	{
		// Fallback so a missing texture never blanks the radar: sea, then a coarse grid of sand cells
		// wherever the world point under that cell is inside the beach ring. Clipped by construction,
		// which a rotated island quad would not be.
		FCanvasTileItem Sea(Center - FVector2D(Radius, Radius), GWhiteTexture,
			FVector2D(Diameter, Diameter), FallbackSea);
		Sea.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Sea);

		constexpr int32 Cells = 22;
		const float CellSize = Diameter / Cells;
		for (int32 CellY = 0; CellY < Cells; ++CellY)
		{
			for (int32 CellX = 0; CellX < Cells; ++CellX)
			{
				const FVector2D Local(-Radius + (CellX + 0.5f) * CellSize, -Radius + (CellY + 0.5f) * CellSize);
				const float ForwardUU = -Local.Y / PixelsPerUU;
				const float RightUU = Local.X / PixelsPerUU;
				const float WorldX = PlayerLocation.X + ForwardUU * CosYaw - RightUU * SinYaw;
				const float WorldY = PlayerLocation.Y + ForwardUU * SinYaw + RightUU * CosYaw;
				// Sand wherever the world point is inside THIS chart's own beach ring. Not a
				// fraction of the plate: Wreck Cove is 7,000 of 10,000 but Gallows Reach is 4,500
				// of 6,000, so one shared 0.7 drew the reach's shore 300 uu short.
				if (FMath::Abs(WorldX - Chart.CentreX) > Chart.BeachHalfExtent
					|| FMath::Abs(WorldY - Chart.CentreY) > Chart.BeachHalfExtent)
				{
					continue;
				}
				FCanvasTileItem Sand(Center + Local - FVector2D(CellSize * 0.5f, CellSize * 0.5f),
					GWhiteTexture, FVector2D(CellSize, CellSize), FallbackSand);
				Sand.BlendMode = SE_BLEND_Opaque;
				Canvas->DrawItem(Sand);
			}
		}
	}

	if (MapRimTexture && MapRimTexture->GetResource())
	{
		FCanvasTileItem Rim(Center - FVector2D(RimSize * 0.5f, RimSize * 0.5f), MapRimTexture->GetResource(),
			FVector2D(RimSize, RimSize), FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), FLinearColor::White);
		Rim.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Rim);
	}

	// Icons. Anything past the inset is off the radar and simply is not drawn; only the mission
	// target survives as a rim marker, so the disc never turns into a ring of clamped clutter.
	const float MarkerLimit = Radius * MinimapMarkerInset;
	TArray<WrecktideMap::FMapMarker> Markers;
	WrecktideMap::CollectMarkers(this, Markers);
	for (const WrecktideMap::FMapMarker& Marker : Markers)
	{
		const FVector2D Offset = WrecktideMap::WorldToRadarOffset(PlayerLocation, RadarYaw, Marker.Location, PixelsPerUU);
		if (Offset.SizeSquared() > MarkerLimit * MarkerLimit)
		{
			continue;
		}
		DrawMapGlyph(WrecktideMap::IconGlyph(Marker.Icon), WrecktideMap::IconColour(Marker.Icon),
			CenterX + Offset.X, CenterY + Offset.Y, FString());
	}

	if (MissionTarget)
	{
		const FLinearColor Gold = WrecktideMap::IconColour(WrecktideMap::EMapIcon::Mission);
		FVector2D Offset = WrecktideMap::WorldToRadarOffset(PlayerLocation, RadarYaw, *MissionTarget, PixelsPerUU);
		if (Offset.Size() > MarkerLimit)
		{
			Offset = WrecktideMap::ClampToCircle(Offset, MarkerLimit);
			const FVector2D Direction = Offset.GetSafeNormal();
			const FVector2D Perpendicular(-Direction.Y, Direction.X);
			const FVector2D Tip = Center + Offset + Direction * 8.0f;
			DrawSolidTriangle(Tip, Center + Offset - Direction * 6.0f + Perpendicular * 8.0f,
				Center + Offset - Direction * 6.0f - Perpendicular * 8.0f, Gold);
		}
		else
		{
			DrawMapGlyph(WrecktideMap::IconGlyph(WrecktideMap::EMapIcon::Mission), Gold,
				CenterX + Offset.X, CenterY + Offset.Y, FString());
		}
	}

	// The pirate, always dead centre, pointing where he faces.
	const float ViewYawRadians = FMath::DegreesToRadians(ViewYaw);
	const FVector Ahead = PlayerLocation + FVector(FMath::Cos(ViewYawRadians), FMath::Sin(ViewYawRadians), 0.0f) * 1000.0f;
	FVector2D Facing = WrecktideMap::WorldToRadarOffset(PlayerLocation, RadarYaw, Ahead, 1.0f).GetSafeNormal();
	if (Facing.IsNearlyZero())
	{
		Facing = FVector2D(0.0f, -1.0f);
	}
	const FVector2D FacingPerpendicular(-Facing.Y, Facing.X);
	DrawSolidTriangle(Center + Facing * 13.0f,
		Center - Facing * 9.0f + FacingPerpendicular * 8.0f,
		Center - Facing * 9.0f - FacingPerpendicular * 8.0f, FLinearColor::White);

	// Compass ticks. World +X is north by the map's own convention (see WrecktideMapView.h). The
	// medium plate only needs N, because the island under it says which way you are looking; the
	// small ring has no plate, so it carries all four (spec C27).
	const FVector2D NorthWorld[4] = {
		FVector2D(1000.0f, 0.0f),
		FVector2D(0.0f, 1000.0f),
		FVector2D(-1000.0f, 0.0f),
		FVector2D(0.0f, -1000.0f)
	};
	const TCHAR* TickLabels[4] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
	const int32 TickCount = bSmall ? 4 : 1;
	for (int32 Tick = 0; Tick < TickCount; ++Tick)
	{
		const FVector TickTarget = PlayerLocation + FVector(NorthWorld[Tick].X, NorthWorld[Tick].Y, 0.0f);
		const FVector2D Direction = WrecktideMap::WorldToRadarOffset(PlayerLocation, RadarYaw, TickTarget, 1.0f).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			continue;
		}
		DrawMapGlyph(TickLabels[Tick], FLinearColor(0.98f, 0.94f, 0.80f),
			CenterX + Direction.X * (Radius * 0.86f), CenterY + Direction.Y * (Radius * 0.86f), FString());
	}

	// SEA lane, ONE self-contained block (spec S1.4): the wind arrow on the rim, pointing the way the
	// wind blows in the same radar space every icon above uses, so it rotates with a heading-up radar.
	const float WindToYaw = WrecktideWind::ToYaw(this);
	const float WindRadians = FMath::DegreesToRadians(WindToYaw);
	const FVector WindAhead = PlayerLocation + FVector(FMath::Cos(WindRadians), FMath::Sin(WindRadians), 0.0f) * 1000.0f;
	FVector2D WindDir = WrecktideMap::WorldToRadarOffset(PlayerLocation, RadarYaw, WindAhead, 1.0f).GetSafeNormal();
	if (!WindDir.IsNearlyZero())
	{
		const FVector2D WindAt = Center - WindDir * (Radius * WindArrowRadiusFraction);
		const FVector2D WindPerpendicular(-WindDir.Y, WindDir.X);
		const FLinearColor WindColour(0.72f, 0.90f, 1.00f);
		DrawSolidTriangle(WindAt + WindDir * WindArrowLength,
			WindAt - WindDir * (WindArrowLength * 0.4f) + WindPerpendicular * WindArrowHalfWidth,
			WindAt - WindDir * (WindArrowLength * 0.4f) - WindPerpendicular * WindArrowHalfWidth, WindColour);
	}
}

void AWrecktideHUD::DrawNpcPlates()
{
	APlayerController* PlayerController = GetOwningPlayerController();
	const APawn* OwnPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Canvas || !PlayerController || !OwnPawn || !Font)
	{
		return;
	}

	const FVector PawnLocation = OwnPawn->GetActorLocation();
	for (TActorIterator<AWrecktideMissionGiver> It(GetWorld()); It; ++It)
	{
		const AWrecktideMissionGiver* Giver = *It;
		if (!Giver)
		{
			continue;
		}

		const FVector PlateWorld = Giver->GetPlateWorldLocation();
		const float Distance = FVector::Dist(PawnLocation, PlateWorld);
		if (Distance > NpcPlateRangeUU)
		{
			continue;
		}

		// ProjectWorldToScreen treats the view as an infinite LINE, so a point behind the camera
		// still projects, onto the wrong side. Reject it by the view direction, the objective
		// arrow's rule, or plates would appear mirrored behind the player.
		const FVector CameraForward = PlayerController->PlayerCameraManager
			? PlayerController->PlayerCameraManager->GetCameraRotation().Vector()
			: OwnPawn->GetActorForwardVector();
		if (FVector::DotProduct(CameraForward, (PlateWorld - PawnLocation).GetSafeNormal()) <= 0.0f)
		{
			continue;
		}

		FVector2D Screen = FVector2D::ZeroVector;
		if (!UGameplayStatics::ProjectWorldToScreen(PlayerController, PlateWorld, Screen, false))
		{
			continue;
		}

		// Fade with distance so a far plate is a hint and a near one is a label.
		const float Alpha = FMath::Clamp(1.0f - (Distance / FMath::Max(NpcPlateRangeUU, 1.0f)), 0.25f, 1.0f);
		DrawCenteredShadowedText(Giver->GetPlateName(), Font, Screen.X, Screen.Y,
			FLinearColor(0.94f, 0.90f, 0.78f, Alpha));

		// "!" has work to give, "?" has work to hand back. One rule in the mission logic feeds both
		// this glyph and the map marker (C22.4).
		const FString Signal = Giver->GetQuestSignal();
		if (!Signal.IsEmpty())
		{
			FLinearColor SignalColour = NpcSignalColour;
			SignalColour.A = Alpha;
			DrawCenteredShadowedText(Signal, Font, Screen.X, Screen.Y - 34.0f, SignalColour, 1.6f);
		}
	}
}

void AWrecktideHUD::DrawObjectiveArrow(const FVector& MissionTarget)
{
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!Canvas || !PlayerController)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// ProjectWorldToScreen treats the view as an infinite LINE, so a target behind the camera still
	// yields a screen point, on the wrong side. Detect it with the camera-forward dot and negate.
	const bool bBehind = FVector::DotProduct(ViewRotation.Vector(), MissionTarget - ViewLocation) < 0.0f;

	FVector2D ScreenPoint = FVector2D::ZeroVector;
	const bool bProjected = UGameplayStatics::ProjectWorldToScreen(PlayerController, MissionTarget, ScreenPoint, false);

	const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	FVector2D Offset = bProjected ? (ScreenPoint - Center) : FVector2D::ZeroVector;
	if (bBehind)
	{
		Offset = -Offset;
	}

	const bool bOnScreen = !bBehind && bProjected
		&& ScreenPoint.X > ObjectiveArrowInset && ScreenPoint.X < Canvas->SizeX - ObjectiveArrowInset
		&& ScreenPoint.Y > ObjectiveArrowInset && ScreenPoint.Y < Canvas->SizeY - ObjectiveArrowInset;
	if (bOnScreen)
	{
		return;
	}

	if (Offset.IsNearlyZero())
	{
		Offset = FVector2D(0.0f, -1.0f);
	}

	// Ray versus the screen RECTANGLE, never a per-axis clamp: a long ray along the direction always
	// exits on an edge, and the exit point stays on the centre-to-target line even near a corner.
	const FVector2D HalfSize(Canvas->SizeX * 0.5f - ObjectiveArrowInset, Canvas->SizeY * 0.5f - ObjectiveArrowInset);
	const FVector2D Edge = Center + WrecktideMap::ClampToRect(Offset.GetSafeNormal() * 100000.0f, HalfSize);

	const FVector2D Direction = Offset.GetSafeNormal();
	const FVector2D Perpendicular(-Direction.Y, Direction.X);
	const FLinearColor Gold = WrecktideMap::IconColour(WrecktideMap::EMapIcon::Mission);
	DrawSolidTriangle(Edge + Direction * ObjectiveArrowSize,
		Edge - Direction * (ObjectiveArrowSize * 0.5f) + Perpendicular * (ObjectiveArrowSize * 0.7f),
		Edge - Direction * (ObjectiveArrowSize * 0.5f) - Perpendicular * (ObjectiveArrowSize * 0.7f), Gold);

	const APawn* OwnPawn = GetOwningPawn();
	if (OwnPawn && GEngine)
	{
		const int32 Metres = FMath::RoundToInt(FVector::Dist2D(OwnPawn->GetActorLocation(), MissionTarget) / 100.0f);
		const FString Distance = FString::Printf(TEXT("%d m"), Metres);
		const FVector2D Text = Edge - Direction * (ObjectiveArrowSize * 1.9f);
		DrawMapGlyph(Distance, Gold, Text.X, Text.Y, FString());
	}
}

void AWrecktideHUD::DrawFullMap(const FVector& PlayerLocation, float ViewYaw, const FVector* MissionTarget, const FString& MissionLabel)
{
	if (!Canvas || !GEngine)
	{
		return;
	}

	UFont* MediumFont = GEngine->GetMediumFont();
	UFont* LargeFont = GEngine->GetLargeFont();
	if (!MediumFont || !LargeFont)
	{
		return;
	}

	FCanvasTileItem Dim(FVector2D::ZeroVector, GWhiteTexture, FVector2D(Canvas->SizeX, Canvas->SizeY),
		FLinearColor(0.02f, 0.02f, 0.03f, 0.78f));
	Dim.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Dim);

	const float PlateSize = Canvas->SizeY * FullMapHeightFraction;
	const float PlateX = Canvas->SizeX * 0.38f - PlateSize * 0.5f;
	const float PlateY = (Canvas->SizeY - PlateSize) * 0.5f + Canvas->SizeY * 0.02f;

	// C34.6: the chart of the island under the pirate, or the nearest one at sea.
	const WrecktideMap::FIslandChart& Chart = WrecktideMap::ChartFor(PlayerLocation);
	UTexture2D* ChartPlate = ChartTextureFor(Chart);
	DrawnChartId = Chart.Id;
	DrawnChartTitle = Chart.Title;

	// North-up, no rotation: a chart you read, not a radar you steer by.
	auto ToScreen = [PlateX, PlateY, PlateSize, &Chart](const FVector& World)
	{
		const FVector2D UV = WrecktideMap::WorldToUV(World, Chart);
		return FVector2D(PlateX + UV.X * PlateSize, PlateY + UV.Y * PlateSize);
	};

	if (ChartPlate && ChartPlate->GetResource())
	{
		FCanvasTileItem Plate(FVector2D(PlateX, PlateY), ChartPlate->GetResource(),
			FVector2D(PlateSize, PlateSize), FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), FLinearColor::White);
		Plate.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Plate);
		bDrewFullMapPlate = true;
	}
	else
	{
		FCanvasTileItem Sea(FVector2D(PlateX, PlateY), GWhiteTexture, FVector2D(PlateSize, PlateSize), FallbackSea);
		Sea.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Sea);

		// Each island's own beach ring; see the radar fallback for the same read.
		const float RingHalf = Chart.BeachHalfExtent;
		const float IslandPixels = (2.0f * RingHalf / (2.0f * Chart.HalfExtent)) * PlateSize;
		const FVector2D IslandTopLeft = ToScreen(FVector(Chart.CentreX + RingHalf, Chart.CentreY - RingHalf, 0.0f));
		FCanvasTileItem Sand(IslandTopLeft, GWhiteTexture, FVector2D(IslandPixels, IslandPixels), FallbackSand);
		Sand.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Sand);
	}

	// Rope-coloured border, four thin tiles: the parchment rim without a nine-slice.
	const FLinearColor Frame(0.47f, 0.36f, 0.21f, 1.0f);
	const float Thickness = 4.0f;
	const FVector2D Borders[4][2] = {
		{ FVector2D(PlateX - Thickness, PlateY - Thickness), FVector2D(PlateSize + Thickness * 2.0f, Thickness) },
		{ FVector2D(PlateX - Thickness, PlateY + PlateSize), FVector2D(PlateSize + Thickness * 2.0f, Thickness) },
		{ FVector2D(PlateX - Thickness, PlateY), FVector2D(Thickness, PlateSize) },
		{ FVector2D(PlateX + PlateSize, PlateY), FVector2D(Thickness, PlateSize) }
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FCanvasTileItem Edge(Borders[Index][0], GWhiteTexture, Borders[Index][1], Frame);
		Edge.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Edge);
	}

	DrawCenteredShadowedText(Chart.Title, LargeFont, PlateX + PlateSize * 0.5f, PlateY - 44.0f,
		FLinearColor(1.0f, 0.85f, 0.4f));
	DrawCenteredShadowedText(TEXT("N"), MediumFont, PlateX + PlateSize * 0.5f, PlateY + 6.0f,
		FLinearColor(0.98f, 0.94f, 0.80f));

	TArray<WrecktideMap::FMapMarker> Markers;
	WrecktideMap::CollectMarkers(this, Markers);
	for (const WrecktideMap::FMapMarker& Marker : Markers)
	{
		const FVector2D Point = ToScreen(Marker.Location);
		DrawMapGlyph(WrecktideMap::IconGlyph(Marker.Icon), WrecktideMap::IconColour(Marker.Icon),
			Point.X, Point.Y, Marker.Label);
	}

	const FVector2D PlayerPoint = ToScreen(PlayerLocation);

	if (MissionTarget)
	{
		const FLinearColor Gold = WrecktideMap::IconColour(WrecktideMap::EMapIcon::Mission);
		const FVector2D TargetPoint = ToScreen(*MissionTarget);

		// Dotted lead line: short tiles with gaps, so the eye follows player to objective.
		const FVector2D Delta = TargetPoint - PlayerPoint;
		const float Length = Delta.Size();
		const FVector2D Step = (Length > KINDA_SMALL_NUMBER) ? Delta / Length : FVector2D::ZeroVector;
		for (float Along = 8.0f; Along < Length - 8.0f; Along += 14.0f)
		{
			FCanvasTileItem Dot(PlayerPoint + Step * Along - FVector2D(1.5f, 1.5f), GWhiteTexture,
				FVector2D(3.0f, 3.0f), Gold);
			Dot.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Dot);
		}

		const int32 Metres = FMath::RoundToInt(FVector::Dist2D(PlayerLocation, *MissionTarget) / 100.0f);
		const FString Label = MissionLabel.IsEmpty()
			? FString::Printf(TEXT("Objective  %d m"), Metres)
			: FString::Printf(TEXT("%s  %d m"), *MissionLabel, Metres);
		DrawMapGlyph(WrecktideMap::IconGlyph(WrecktideMap::EMapIcon::Mission), Gold, TargetPoint.X, TargetPoint.Y, Label);
	}

	const float ViewYawRadians = FMath::DegreesToRadians(ViewYaw);
	const FVector Ahead = PlayerLocation + FVector(FMath::Cos(ViewYawRadians), FMath::Sin(ViewYawRadians), 0.0f) * 1000.0f;
	FVector2D Facing = (ToScreen(Ahead) - PlayerPoint).GetSafeNormal();
	if (Facing.IsNearlyZero())
	{
		Facing = FVector2D(0.0f, -1.0f);
	}
	const FVector2D FacingPerpendicular(-Facing.Y, Facing.X);
	DrawSolidTriangle(PlayerPoint + Facing * 15.0f,
		PlayerPoint - Facing * 10.0f + FacingPerpendicular * 9.0f,
		PlayerPoint - Facing * 10.0f - FacingPerpendicular * 9.0f, FLinearColor::White);

	// Legend column, right of the plate.
	const float LegendX = PlateX + PlateSize + 46.0f;
	float LegendY = PlateY + 10.0f;
	DrawShadowedText(TEXT("LEGEND"), MediumFont, LegendX, LegendY, FLinearColor(1.0f, 0.85f, 0.4f));
	LegendY += 34.0f;
	const WrecktideMap::EMapIcon LegendIcons[] = {
		WrecktideMap::EMapIcon::Mission,
		WrecktideMap::EMapIcon::Stall,
		WrecktideMap::EMapIcon::Board,
		WrecktideMap::EMapIcon::Giver,
		WrecktideMap::EMapIcon::Boat,
		WrecktideMap::EMapIcon::Chest,
		WrecktideMap::EMapIcon::Boss,
		WrecktideMap::EMapIcon::Enemy
	};
	for (const WrecktideMap::EMapIcon Icon : LegendIcons)
	{
		DrawShadowedText(WrecktideMap::IconLegend(Icon), MediumFont, LegendX, LegendY, WrecktideMap::IconColour(Icon));
		LegendY += 26.0f;
	}

	LegendY += 18.0f;
	DrawShadowedText(FString::Printf(TEXT("You: X %.0f  Y %.0f"), PlayerLocation.X, PlayerLocation.Y), MediumFont,
		LegendX, LegendY, FLinearColor(0.9f, 0.9f, 0.9f));

	DrawCenteredShadowedText(TEXT("M or Esc to close"), MediumFont, Canvas->SizeX * 0.5f, Canvas->SizeY * 0.955f,
		FLinearColor(1.0f, 1.0f, 1.0f, 0.85f));
}
