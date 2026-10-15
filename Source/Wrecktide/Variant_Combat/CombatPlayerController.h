// Copyright Epic Games, Inc. All Rights Reserved.
// Modifications Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Systems/WrecktideOptions.h"
#include "UI/WrecktideMenuTypes.h"
#include "CombatPlayerController.generated.h"

class ACameraActor;
class SWrecktideChest;
class SWrecktideMenu;
class UInputMappingContext;
class ACombatCharacter;
struct FWrecktideOutfitInfo;

/**
 *  Simple Player Controller for a third person combat game
 *  Manages input mappings
 *  Respawns the player character at the checkpoint when it's destroyed
 *
 *  Menus (2026-09-06): this controller owns the whole menu MODEL - which screen is open, the rows, the
 *  selected index and what a confirm does. UI\SWrecktideMenu is only the view; the mouse calls back
 *  into the same functions the keys do, so a hover and an arrow key can never disagree.
 */
UCLASS(abstract)
class ACombatPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:

	/** Input mapping context for this player */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Character class to respawn when the possessed pawn is destroyed */
	UPROPERTY(EditAnywhere, Category="Respawn")
	TSubclassOf<ACombatCharacter> CharacterClass;

	/** Transform to respawn the character at. Can be set to create checkpoints */
	FTransform RespawnTransform;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Cursor and hover for the canvas modals; the Slate screens do their own through the widget. */
	virtual void PlayerTick(float DeltaTime) override;

	/** Look input scaled by the Options mouse sensitivity. */
	virtual void AddYawInput(float Val) override;

	/** Look input scaled by the Options mouse sensitivity and flipped by Invert look Y. */
	virtual void AddPitchInput(float Val) override;

public:

	/** Updates the character respawn transform */
	void SetRespawnTransform(const FTransform& NewRespawn);

	/** TogglePauseMenu is the SINGLE authoritative path in and out of the PAUSE root: Esc and
	 *  -FeelProbe both come through here. It refuses to pause while a shop or bounty menu is open,
	 *  so Esc keeps closing THAT menu first (those modals bind Esc on higher priority actor input
	 *  components). Esc inside a sub-screen goes back one screen instead; see HandleMenuBackKey. */
	UFUNCTION()
	void TogglePauseMenu();

	/** ToggleMapScreen is the SINGLE authoritative path behind M (spec C27). It ADVANCES the radar
	 *  cycle, small to medium to full and back to small; the full step pauses the world exactly as
	 *  the pause root does, and every step refuses while a menu owns the screen. */
	void ToggleMapScreen();

	/** The radar size right now. The HUD mirrors this; this owns it. */
	EWrecktideRadarState GetRadarState() const { return RadarState; }

	/** SetRadarState is the SINGLE authoritative write path: it moves the full map with the state,
	 *  pushes the HUD and persists small or medium so the choice survives a relaunch. */
	void SetRadarState(EWrecktideRadarState NewState);

	/** True while the full map is up. */
	bool IsMapScreenOpen() const { return bMapScreenOpen; }

	/** Open or close without the toggle's refusals; the capture switch and the probe use it. */
	void SetMapScreenOpen(bool bOpen);

	/** True while the pause menu tree owns the game (root screen = Pause). */
	bool IsPauseMenuOpen() const;

	/** Title menu (2026-09-05, rebuilt in Slate 2026-09-06): the first thing the player sees.
	 *  Continue (only when a save exists), New Voyage, Load Voyage, Wardrobe, Options, Credits, Quit.
	 *  The game is paused while it is open and the view sits on a scenic camera over the bay.
	 *  Probe runs and -NoTitle skip it; -MenuProbe forces it. */
	void OpenTitleMenu();

	/** True while the title menu tree owns the game (root screen = Main). */
	bool IsTitleMenuOpen() const;

	/** True while the title screen owns the pirate's visibility (hidden behind the scenic camera).
	 *  The wardrobe preview shows him for its own screen and asks this before hiding him again. */
	bool IsPawnHiddenForTitle() const { return bPawnHiddenForTitle; }

	/** Picks the title option with this label and confirms it (probe path, same code as Enter). */
	bool TitleChoose(const FString& Label);

	static bool ShouldShowTitleMenu();

	/** Push the open menu to the HUD (probe evidence) and the Slate view. Public so the HUD can pull
	 *  the state the frame it appears: the first title capture showed the controller's menu open with
	 *  the HUD still blank. */
	void RefreshPauseMenu();
	void RefreshTitleMenu();

	/** Same job, but consumed on the next engine tick: the HUD asks from inside its paint pass and a
	 *  rebuild there would rebuild the Slate menu tree while Slate is drawing it. */
	void RequestMenuRefresh();

	/** Set by New Voyage before the level restarts so the fresh world goes straight into play. */
	static bool bSkipTitleOnce;

	// --- menu model, read by UI\SWrecktideMenu -------------------------------------------------

	/** Fires whenever the rows, the selection or the header changed. */
	FOnWrecktideMenuChanged OnMenuChanged;

	/** The screen on top of the stack; None when no menu is open. */
	EWrecktideMenuScreen GetMenuScreen() const;

	const TArray<FWrecktideMenuRow>& GetMenuRows() const { return MenuRows; }
	int32 GetMenuSelectedIndex() const { return MenuSelectedIndex; }
	FString GetMenuTitle() const { return MenuTitleText; }
	FString GetMenuSubtitle() const { return MenuSubtitleText; }
	FString GetMenuHint() const { return MenuHintText; }

	/** True while any menu screen is open. */
	bool IsAnyMenuOpen() const { return MenuStack.Num() > 0; }

	// --- the chest screen on Tab (spec C18) ----------------------------------------------------

	/** ToggleInventory is the SINGLE authoritative path for Tab, I and the chest button. The button
	 *  closes a canvas modal before coming here, keeping one screen in charge of input. */
	void ToggleInventory();

	/** Shared viewport-point handler for the mouse binding, the chest view and the toolbar probe. */
	bool HandleModalClickAt(const FVector2D& ScreenPosition);

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	/** Exercises the same once-only hint path without enabling automatic hints in probe runs. */
	bool ShowChestHintForProbe();
#endif

	/** True while the chest screen tree owns the game (root screen = Inventory). */
	bool IsInventoryOpen() const;

	/** J: opens the chest if it is shut and lands on the Journal page either way. */
	UFUNCTION()
	void OpenJournalPage();

	/** The tab the chest screen is showing. */
	EWrecktideChestTab GetChestTab() const { return ChestTab; }

	/** Switches tab (the tab strip's click path and Left/Right). */
	void SetChestTab(EWrecktideChestTab Tab);

	/** Left/Right or the arrow keys move one tab along, wrapping. */
	void StepChestTab(int32 Delta);

	// --- menu model, driven by the keys, the mouse and the probes -----------------------------

	/** Moves the selection by Delta over SELECTABLE rows only. */
	void MenuNavigate(int32 Delta);

	/** Left/Right on a value row (volumes, sensitivity, toggles, colourways). */
	void MenuAdjust(int32 Delta);

	/** Acts on the selected row. */
	void MenuConfirm();

	/** One screen back; at the root it closes the pause tree and does nothing on the title. */
	void MenuBack();

	/** Mouse hover: selects the row without acting on it. */
	void MenuSetSelected(int32 Index);

	/** Mouse hover from Slate: selects the row ONLY when the mouse really moved (see the body). */
	void MenuHover(int32 Index);

	/** Mouse click: selects the row and confirms it. */
	void MenuClick(int32 Index);

	/** Selects the row with this exact label and confirms it. False when no such row is selectable. */
	bool MenuChoose(const FString& Label);

	// --- the mouse in the CANVAS modals: shop, bounty board, quest giver, smithy (spec C20) ------
	// Those four draw their list through the HUD, not through Slate, so hover and click are hit
	// tested here against the rectangles the HUD recorded at draw time.

	/** Moves the modal's selection onto Index and optionally confirms it. False when no canvas
	 *  modal is open or the index is not a drawn row. The probe and the mouse share this path. */
	bool DriveCanvasModal(int32 Index, bool bConfirm);

	/** Row under a viewport point while a canvas modal is open, else INDEX_NONE. */
	int32 HitTestCanvasModal(const FVector2D& ScreenPosition) const;

	/** True while the cursor is shown FOR a canvas modal (as opposed to a Slate screen). */
	bool IsCanvasModalCursorOn() const { return bCanvasModalCursor; }

	/** Opens a screen inside the current tree (probe path for Options / Load without label matching). */
	void MenuOpenScreen(EWrecktideMenuScreen Screen);

protected:

	/** Called if the possessed pawn is destroyed */
	UFUNCTION()
	void OnPawnDestroyed(AActor* DestroyedActor);

	/** Pause menu navigation; no-ops unless a menu is open. */
	UFUNCTION()
	void SelectPreviousPauseOption();

	UFUNCTION()
	void SelectNextPauseOption();

	UFUNCTION()
	void ConfirmPauseSelection();

	UFUNCTION()
	void AdjustPreviousOptionValue();

	UFUNCTION()
	void AdjustNextOptionValue();

	/** Esc: one screen back inside a tree, otherwise the pause toggle. */
	UFUNCTION()
	void HandleMenuBackKey();

	/** Left mouse button: confirms the row under the cursor in a canvas modal (spec C20). */
	UFUNCTION()
	void HandleModalClick();

	/** Right mouse button: one screen back, Slate screens only (the world is paused there, so it
	 *  cannot also swing the sword). */
	UFUNCTION()
	void HandleModalRightClick();

	/** Mouse wheel: moves the selection in whichever list is open. */
	UFUNCTION()
	void HandleModalScrollUp();

	UFUNCTION()
	void HandleModalScrollDown();

private:

	/** Opens a menu tree at this root: pauses, shows the cursor and adds the Slate view. */
	void OpenMenuRoot(EWrecktideMenuScreen Root);

	/** Closes the whole tree: unpauses, hides the cursor, removes the Slate view. */
	void CloseMenus();

	/** Rebuilds MenuRows / MenuActions / the header for the top screen and tells both views. */
	void RebuildMenuRows();

	/** Pushes the current labels to the HUD (probe evidence) and broadcasts to the Slate view. */
	void PublishMenu();

	void AddMenuWidget();
	void RemoveMenuWidget();

	/** GameAndUI, cursor on, focus on the added widget (research B2). The ONE place a menu enters
	 *  UI input, so no screen hand-rolls it. */
	void ApplyMenuInputMode();

	/** GameOnly, cursor off, focus cleared. The ONE place a menu leaves UI input: the reported trap
	 *  is game input dying after a menu closes, and it dies when this is skipped. */
	void RestoreGameInputMode();

	/** Cursor and input mode follow the canvas modals, which pause nothing and own no Slate widget.
	 *  Called every tick; acts only on a transition. */
	void UpdateCanvasModalCursor();
	/** Release canvas modal input before handing a toolbar click to the chest or map toggle. */
	void CloseCanvasModalsForToolbar();
	void CheckFirstVoyageChestHint();
	bool ShowChestHintIfUnseen();
	FTimerHandle ChestHintTimer;

	/** Scenic view behind the title menu; spawned on demand, destroyed when the game starts. */
	void EnsureScenicCamera();
	void ClearScenicCamera();

	/** Reads the live option values into the pending set the Options screen edits. */
	void EnterOptionsScreen();

	/** Applies and persists the pending option values. */
	void ApplyPendingOptions();

	/** Re-reads the save slot summaries (never cached across a save). */
	void RefreshSlotSummaries();

	/** Loads a slot (0 = autosave, 1..3 = manual) and closes the menus on success. */
	bool LoadSlotAndPlay(int32 SlotIndex);

	/** True when a shop stall or bounty board menu is open anywhere in the world. */
	bool IsModalMenuOpen() const;

	/** The outfit the Colourway row is showing (and that Enter on it wears). */
	FName GetColourwayOutfitId(const TArray<FWrecktideOutfitInfo>& Outfits, FName EffectiveWornId) const;

	/** Pushes the minimap north-up option to the HUD (the only place that owns the radar). */
	void ApplyMinimapOption();

	/** Enter on a locked wardrobe row: refuse with a reason, ask to confirm, then buy and wear. */
	void BuyOutfitRow(int32 OutfitIndex, const FWrecktideOutfitInfo& Outfit);

	/** The open screens, innermost last. Empty means no menu. */
	TArray<EWrecktideMenuScreen> MenuStack;

	/** Rows and the action id for each row, filled together so a relabel cannot fire the wrong action. */
	TArray<FWrecktideMenuRow> MenuRows;
	TArray<int32> MenuActions;
	int32 MenuSelectedIndex = 0;

	FString MenuTitleText;
	FString MenuSubtitleText;
	FString MenuHintText;

	/** Cursor visibility to restore when every menu closes. */
	bool bCursorBeforePause = false;

	/** The Slate view; alive only while a menu is open. */
	TSharedPtr<SWrecktideMenu> MenuWidget;

	/** The chest view; alive only while the chest screen is open. Exactly one of the two exists. */
	TSharedPtr<SWrecktideChest> ChestWidget;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ScenicCamera;

	/** Save-slot summaries, index 0 = autosave, 1..3 = manual. Refreshed on entering a slot screen. */
	TArray<FString> SlotSummaries;

	/** Set while the Save screen is asking to confirm an overwrite; 0 = not asking. */
	int32 PendingOverwriteSlot = 0;

	/** Wardrobe row the buy question is about; INDEX_NONE = not asking. */
	int32 PendingOutfitBuyIndex = INDEX_NONE;

	/** Option values the Options screen is editing; written to disk by Apply. */
	FWrecktideOptionValues PendingOptions;
	int32 PendingWindowMode = 0;
	int32 PendingResolutionIndex = 0;
	int32 PendingQuality = 3;
	bool bPendingVSync = true;
	TArray<FIntPoint> ResolutionChoices;

	/** Wardrobe screen: the colourway the Colourway row is cycling. */
	int32 WardrobeVariant = 0;

	/** Viewport pixels the mouse must travel before a hover may move the selection again. */
	static constexpr float MenuHoverMoveTolerance = 2.0f;

	/** Where the mouse was when a hover last moved the selection (see MenuHover). */
	FVector2D LastMenuHoverMouse = FVector2D::ZeroVector;
	bool bHasMenuHoverMouse = false;

	/** Scenic camera warm-up: the cut needs rendered frames of its own before the world freezes, or
	 *  the temporal history under the menu is a smear (first capture, 2026-09-06). */
	bool bScenicWarmedUp = false;
	uint64 ScenicArmFrame = 0;

	/** The pirate is hidden behind the title menu (his floating life bar sat in the middle of the
	 *  scenic shot); restored when the game starts. */
	bool bPawnHiddenForTitle = false;

	/** A deferred menu refresh is already queued on the core ticker. */
	bool bMenuRefreshPending = false;

	/** Armed from BeginPlay until the title menu is actually open: menu keys are ignored in that
	 *  window (an Esc there used to pause instead and cancel the title for the whole session). */
	bool bTitlePending = false;

	FTimerHandle TitleMenuTimer;
	/** GFrameCounter at BeginPlay: the title waits for real rendered frames, not just world seconds,
	 *  so eye adaptation has settled before the world freezes under the menu. */
	uint64 TitleArmFrame = 0;

	/** Frame the HUD came up, for the map screen's exposure warm-up (see ToggleMapScreen). */
	uint64 MapArmFrame = 0;

	/** True while the full map is up; the HUD mirrors it, this owns it. Always the same thing as
	 *  RadarState == Full (spec C27), which SetRadarState is the only writer of. */
	bool bMapScreenOpen = false;

	/** Radar size (spec C27). Loaded from the options on BeginPlay, small on a fresh install. */
	EWrecktideRadarState RadarState = EWrecktideRadarState::Small;

	/** The chest screen's current tab. Kept across a close so re-opening lands where he left off. */
	EWrecktideChestTab ChestTab = EWrecktideChestTab::Weapons;

	/** Rows the chest's Weapons page listed, in drawn order, so an action id maps back to a weapon
	 *  row without the view ever holding a table pointer. Rebuilt with the rows. */
	TArray<FName> ChestMeleeRows;
	TArray<FName> ChestGunRows;

	/** Same, for the Journal page's entry keys. */
	TArray<FName> ChestJournalKeys;

	/** True while THIS controller turned the cursor on for a canvas modal. */
	bool bCanvasModalCursor = false;

	/** Cursor position the canvas hover last acted on, so a still mouse over a rebuilt list does not
	 *  fight the arrow keys (the same rule MenuHover follows for Slate). */
	FVector2D LastCanvasHoverMouse = FVector2D::ZeroVector;
	bool bHasCanvasHoverMouse = false;

	/** Frame a modal click was handled on. A real click reaches Slate OR this binding, never both,
	 *  but the guard costs nothing and makes a double route impossible. */
	uint64 LastModalClickFrame = 0;

	/** Frame the HUD came up, for the chest screen's exposure warm-up (same trap as the map). */
	uint64 ChestArmFrame = 0;
};
