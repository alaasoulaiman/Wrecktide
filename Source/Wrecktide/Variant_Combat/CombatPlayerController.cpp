// Copyright Epic Games, Inc. All Rights Reserved.
// Modifications Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.


#include "Variant_Combat/CombatPlayerController.h"
#include "Systems/EconomyComponent.h"
#include "Systems/WrecktideAudio.h"
#include "Containers/Ticker.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerStart.h"
#include "CombatCharacter.h"
#include "Camera/CameraActor.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameUserSettings.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "InputCoreTypes.h"
#include "UI/SWrecktideChest.h"
#include "UI/SWrecktideMenu.h"
#include "UI/WrecktideHUD.h"
#include "Wrecktide.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Systems/WrecktideOptions.h"
#include "Systems/ProgressComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideQuestLogic.h"
#include "Systems/WrecktideWardrobeSubsystem.h"
#include "TimerManager.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
/**
 * Action ids. Every row carries one, filled in the SAME loop that writes the label, so a relabel can
 * never quit when the player asked to resume (the lesson from the 2026-09-04 index-constant pause menu,
 * kept now that the menu has eight screens).
 */
constexpr int32 ActContinue = 100;
constexpr int32 ActNewVoyage = 101;
constexpr int32 ActLoadScreen = 102;
constexpr int32 ActWardrobeScreen = 103;
constexpr int32 ActOptionsScreen = 104;
constexpr int32 ActCreditsScreen = 105;
constexpr int32 ActQuit = 106;
constexpr int32 ActResume = 110;
constexpr int32 ActSaveScreen = 111;
constexpr int32 ActControlsScreen = 112;
constexpr int32 ActMainMenu = 113;
constexpr int32 ActBack = 120;
constexpr int32 ActApplyOptions = 121;
constexpr int32 ActColourway = 130;
/** Save/load rows: base + slot index (0 = autosave, 1..3 = manual). */
constexpr int32 ActSlotBase = 200;
/** Options rows: base + option index. */
constexpr int32 ActOptionBase = 300;
/** Wardrobe rows: base + outfit index. */
constexpr int32 ActOutfitBase = 400;
/** Chest screen, Weapons page: base + index into the owned melee list (spec C18). */
constexpr int32 ActChestWieldBase = 500;
/** Chest screen, Journal page: base + index into the journal entries (spec C19). */
constexpr int32 ActChestTrackBase = 600;
/** Chest screen: empty the melee slot, and the row that leaves for the change room. */
constexpr int32 ActChestUnassign = 140;
constexpr int32 ActChestWardrobe = 141;
constexpr int32 PistolChestUnassign = 142;
constexpr int32 PistolChestWieldBase = 700;
/** Captions and other rows nothing can act on. */
constexpr int32 ActNone = -1;

/** Options row order; the labels and the Left/Right handling are indexed by these. */
constexpr int32 OptMaster = 0;
constexpr int32 OptEffects = 1;
constexpr int32 OptAmbience = 2;
constexpr int32 OptSensitivity = 3;
constexpr int32 OptInvert = 4;
constexpr int32 OptWindowMode = 5;
constexpr int32 OptResolution = 6;
constexpr int32 OptVSync = 7;
constexpr int32 OptQuality = 8;
/** Added 2026-09-06 with the minimap: north-up instead of heading-up. Appended, not inserted, so no
 *  existing option index moves. */
constexpr int32 OptMinimapNorthUp = 9;
constexpr int32 OptCount = 10;

constexpr int32 VolumeStep = 10;
constexpr float SensitivityStep = 0.1f;

/** First-voyage chest hint: world seconds; retry after a menu or late HUD has become ready. */
constexpr float FirstVoyageChestHintDelaySeconds = 6.0f;
constexpr float FirstVoyageChestHintDurationSeconds = 6.0f;
constexpr float FirstVoyageChestHintRetrySeconds = 1.0f;
const FName FirstVoyageChestHintFlag(TEXT("Hint_Chest"));

/** Scenic title camera: over the sea south-west of the pier, looking back at the moored boat and camp. */
const FVector ScenicCameraLocation(-2300.0f, -9200.0f, 260.0f);
const FVector ScenicCameraLookAt(0.0f, -6400.0f, -60.0f);

const TCHAR* WindowModeLabel(int32 Index)
{
	switch (Index)
	{
	case 1: return TEXT("Borderless");
	case 2: return TEXT("Fullscreen");
	default: return TEXT("Windowed");
	}
}

EWindowMode::Type WindowModeFromIndex(int32 Index)
{
	switch (Index)
	{
	case 1: return EWindowMode::WindowedFullscreen;
	case 2: return EWindowMode::Fullscreen;
	default: return EWindowMode::Windowed;
	}
}

int32 WindowModeToIndex(EWindowMode::Type Mode)
{
	switch (Mode)
	{
	case EWindowMode::WindowedFullscreen: return 1;
	case EWindowMode::Fullscreen: return 2;
	default: return 0;
	}
}

const TCHAR* QualityLabel(int32 Level)
{
	switch (Level)
	{
	case 0: return TEXT("Low");
	case 1: return TEXT("Medium");
	case 2: return TEXT("High");
	default: return TEXT("Epic");
	}
}

FString OnOff(bool bValue)
{
	return bValue ? TEXT("On") : TEXT("Off");
}
}

void ACombatPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Options are read once here so the first sound of the run already carries the player's mix.
	FWrecktideOptions::Load();

	MapArmFrame = GFrameCounter;
	ChestArmFrame = GFrameCounter;

	// The size he left it on (spec C27). Full is never persisted, so this can only be small or
	// medium, and a fresh install is small.
	RadarState = FWrecktideOptions::Get().RadarSize == 1
		? EWrecktideRadarState::Medium : EWrecktideRadarState::Small;

	// Title menu (2026-09-05): armed a moment after BeginPlay so the pawn is possessed and the HUD exists.
	if (IsLocalPlayerController() && ShouldShowTitleMenu())
	{
		TitleArmFrame = GFrameCounter;
		// Menu keys stay dead until the title is actually up: Esc inside the warm-up window used to
		// open the PAUSE root instead, which cancelled the title for the whole session.
		bTitlePending = true;
		GetWorldTimerManager().SetTimer(TitleMenuTimer, this, &ACombatPlayerController::OpenTitleMenu, 0.5f, false);
	}

	if (IsLocalPlayerController() && !AWrecktidePlayerState::IsProbeRun())
	{
		GetWorldTimerManager().SetTimer(ChestHintTimer, this, &ACombatPlayerController::CheckFirstVoyageChestHint,
			FirstVoyageChestHintDelaySeconds, false);
	}

	// only spawn touch controls on local player controllers
	if (SVirtualJoystick::ShouldDisplayTouchInterface() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogWrecktide, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void ACombatPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The viewport outlives the world on a level restart: a menu widget left in it would draw over
	// the fresh world forever (and hold a stale controller pointer).
	RemoveMenuWidget();
	ClearScenicCamera();

	Super::EndPlay(EndPlayReason);
}

void ACombatPlayerController::SetupInputComponent()
{
	// Super creates the controller's own InputComponent (this override used to skip it, so the
	// controller had none). Its bindings sit BELOW every actor that called EnableInput in the
	// input stack, which is exactly the priority the pause key needs: an open shop or bounty menu
	// consumes Esc first and only closes itself.
	Super::SetupInputComponent();

	if (InputComponent)
	{
		// bExecuteWhenPaused: without it the pause key could pause but never unpause.
		FInputKeyBinding& PauseBinding = InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACombatPlayerController::HandleMenuBackKey);
		PauseBinding.bExecuteWhenPaused = true;
		PauseBinding.bConsumeInput = true;

		// Navigation keys do NOT consume: while unpaused they must still reach the pawn and the
		// modal menus below this component in the stack.
		FInputKeyBinding& UpBinding = InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &ACombatPlayerController::SelectPreviousPauseOption);
		UpBinding.bExecuteWhenPaused = true;
		UpBinding.bConsumeInput = false;

		FInputKeyBinding& DownBinding = InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &ACombatPlayerController::SelectNextPauseOption);
		DownBinding.bExecuteWhenPaused = true;
		DownBinding.bConsumeInput = false;

		FInputKeyBinding& EnterBinding = InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACombatPlayerController::ConfirmPauseSelection);
		EnterBinding.bExecuteWhenPaused = true;
		EnterBinding.bConsumeInput = false;

		// Left/Right change an option's VALUE (volumes, sensitivity, resolution, colourways).
		FInputKeyBinding& LeftBinding = InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &ACombatPlayerController::AdjustPreviousOptionValue);
		LeftBinding.bExecuteWhenPaused = true;
		LeftBinding.bConsumeInput = false;

		FInputKeyBinding& RightBinding = InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &ACombatPlayerController::AdjustNextOptionValue);
		RightBinding.bExecuteWhenPaused = true;
		RightBinding.bConsumeInput = false;

		// Mouse (spec C20). The four canvas modals draw their list through the HUD and pause
		// nothing, so their hover and click are hit tested against the rows the HUD recorded.
		// bConsumeInput stays false: this component sits BELOW the pawn and the modal actors, so
		// consuming here would change nothing anyway, and the real click is normally swallowed by
		// Slate before it ever gets this far when a Slate screen is up.
		FInputKeyBinding& ClickBinding = InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACombatPlayerController::HandleModalClick);
		ClickBinding.bExecuteWhenPaused = true;
		ClickBinding.bConsumeInput = false;

		FInputKeyBinding& RightBackBinding = InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ACombatPlayerController::HandleModalRightClick);
		RightBackBinding.bExecuteWhenPaused = true;
		RightBackBinding.bConsumeInput = false;

		FInputKeyBinding& WheelUpBinding = InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ACombatPlayerController::HandleModalScrollUp);
		WheelUpBinding.bExecuteWhenPaused = true;
		WheelUpBinding.bConsumeInput = false;

		FInputKeyBinding& WheelDownBinding = InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ACombatPlayerController::HandleModalScrollDown);
		WheelDownBinding.bExecuteWhenPaused = true;
		WheelDownBinding.bConsumeInput = false;

		// Tab opens and closes the chest (spec C18). bExecuteWhenPaused for the reason Esc needs it:
		// the chest pauses, so without it Tab could open the chest and never shut it.
		FInputKeyBinding& ChestBinding = InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ACombatPlayerController::ToggleInventory);
		ChestBinding.bExecuteWhenPaused = true;
		ChestBinding.bConsumeInput = false;

		// I is an exact second binding for the chest, including while it has paused the world.
		FInputKeyBinding& ChestAlternateBinding = InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ACombatPlayerController::ToggleInventory);
		ChestAlternateBinding.bExecuteWhenPaused = true;
		ChestAlternateBinding.bConsumeInput = false;

		// J jumps straight to the journal page (research A1: the classic chest gave every page its
		// own letter). It opens the chest first when it is shut.
		FInputKeyBinding& JournalBinding = InputComponent->BindKey(EKeys::J, IE_Pressed, this, &ACombatPlayerController::OpenJournalPage);
		JournalBinding.bExecuteWhenPaused = true;
		JournalBinding.bConsumeInput = false;

		// M opens and closes the full map. bExecuteWhenPaused for the same reason Esc needs it: the
		// map pauses, so without it M could open the chart and never shut it. It does NOT consume,
		// so a modal shop or bounty menu below still sees the key.
		FInputKeyBinding& MapBinding = InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ACombatPlayerController::ToggleMapScreen);
		MapBinding.bExecuteWhenPaused = true;
		MapBinding.bConsumeInput = false;

		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-PAUSE keys bound=%d (Esc/Up/Down/Enter/Left/Right/M/Tab/I/J/mouse)"), InputComponent->KeyBindings.Num());
	}
	else
	{
		// Fail loud rather than shipping a dead Esc key: the probe drives TogglePauseMenu directly
		// and would still pass with no binding at all.
		UE_LOG(LogWrecktide, Error, TEXT("WRECKTIDE-PAUSE FAIL no controller input component, Esc is not bound"));
	}

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// add the input mapping context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

void ACombatPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// subscribe to the pawn's OnDestroyed delegate, but ONLY for the pirate: a possessed boat
	// (AWrecktideBoat) must never arm the respawn path, or destroying it later would spawn and
	// possess a fresh pirate while the real one stands on the pier (review 2026-09-04). AddUnique:
	// the same pirate is possessed again every time the player steps off a boat. The binding is
	// deliberately NOT removed on unpossess: the engine unpossesses a dying pawn before its
	// OnDestroyed fires, so removing it there kills every respawn (it did, for one build).
	if (Cast<ACombatCharacter>(InPawn))
	{
		InPawn->OnDestroyed.AddUniqueDynamic(this, &ACombatPlayerController::OnPawnDestroyed);
	}
}

void ACombatPlayerController::AddYawInput(float Val)
{
	Super::AddYawInput(Val * FWrecktideOptions::Get().MouseSensitivity);
}

void ACombatPlayerController::AddPitchInput(float Val)
{
	const FWrecktideOptionValues& Options = FWrecktideOptions::Get();
	Super::AddPitchInput(Val * Options.MouseSensitivity * (Options.bInvertLookY ? -1.0f : 1.0f));
}

void ACombatPlayerController::SetRespawnTransform(const FTransform& NewRespawn)
{
	// save the new respawn transform
	RespawnTransform = NewRespawn;
}

void ACombatPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// Respawn the SAME pawn class the player was just using, so a reparented pirate pawn (the GAS
	// locomotion CBP) survives death instead of reverting to the fixed CharacterClass. Fall back to
	// the configured default only if the destroyed class is somehow unavailable.
	UClass* RespawnClass = DestroyedActor ? DestroyedActor->GetClass() : nullptr;
	if (!RespawnClass || !RespawnClass->IsChildOf(ACombatCharacter::StaticClass()))
	{
		RespawnClass = CharacterClass;
	}

	// spawn a new character at the respawn transform
	if (ACombatCharacter* RespawnedCharacter = GetWorld()->SpawnActor<ACombatCharacter>(RespawnClass, RespawnTransform))
	{
		// possess the character
		Possess(RespawnedCharacter);

		// The death overlay is cleared HERE, on the respawn that actually landed, so it can never
		// be dismissed by a respawn that failed to spawn. Gold and XP live on the PlayerState and
		// are untouched by death: the toast is the only thing that changes.
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(GetHUD()))
		{
			HUD->SetDeathScreen(false);
			HUD->ShowToast(TEXT("Back on your feet."));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Menu tree: open, close, publish
// ---------------------------------------------------------------------------------------------

EWrecktideMenuScreen ACombatPlayerController::GetMenuScreen() const
{
	return MenuStack.Num() > 0 ? MenuStack.Last() : EWrecktideMenuScreen::None;
}

bool ACombatPlayerController::IsTitleMenuOpen() const
{
	return MenuStack.Num() > 0 && MenuStack[0] == EWrecktideMenuScreen::Main;
}

bool ACombatPlayerController::IsPauseMenuOpen() const
{
	return MenuStack.Num() > 0 && MenuStack[0] == EWrecktideMenuScreen::Pause;
}

void ACombatPlayerController::TogglePauseMenu()
{
	if (IsTitleMenuOpen())
	{
		return;
	}
	if (IsAnyMenuOpen())
	{
		CloseMenus();
		return;
	}
	// A modal shop or bounty menu owns Esc while it is open. Those bind Esc on their own actor
	// input components, which outrank this one, so this is a belt-and-braces guard for any path
	// that reaches the toggle some other way.
	if (IsModalMenuOpen())
	{
		return;
	}

	OpenMenuRoot(EWrecktideMenuScreen::Pause);
}

bool ACombatPlayerController::IsInventoryOpen() const
{
	return MenuStack.Num() > 0 && MenuStack[0] == EWrecktideMenuScreen::Inventory;
}

void ACombatPlayerController::SetChestTab(EWrecktideChestTab Tab)
{
	if (Tab == EWrecktideChestTab::Count || ChestTab == Tab)
	{
		return;
	}

	ChestTab = Tab;
	MenuSelectedIndex = 0;
	UWrecktideAudio::PlaySfx(this, TEXT("ui_move"));
	RebuildMenuRows();
}

void ACombatPlayerController::StepChestTab(int32 Delta)
{
	if (!IsInventoryOpen() || Delta == 0)
	{
		return;
	}

	const int32 TabCount = static_cast<int32>(EWrecktideChestTab::Count);
	const int32 Next = ((static_cast<int32>(ChestTab) + (Delta > 0 ? 1 : -1)) % TabCount + TabCount) % TabCount;
	SetChestTab(static_cast<EWrecktideChestTab>(Next));
}

void ACombatPlayerController::ToggleInventory()
{
	if (IsInventoryOpen())
	{
		CloseMenus();
		return;
	}
	// Tab does nothing while the title menu, the pause tree, the map or a stall menu owns the
	// screen: one modal at a time is the rule the whole game already follows.
	if (bTitlePending || IsAnyMenuOpen() || IsModalMenuOpen() || bMapScreenOpen)
	{
		return;
	}

	// Same exposure trap as the map and the title (RECON F): auto exposure freezes with the world,
	// so a screen opened on the first frames freezes it at its start value.
	const UWorld* World = GetWorld();
	if (World && (World->GetTimeSeconds() < 1.5f || GFrameCounter < ChestArmFrame + 60))
	{
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-CHEST refused, still warming up (t=%.2f frames=%llu)"),
			World->GetTimeSeconds(), static_cast<uint64>(GFrameCounter - ChestArmFrame));
		return;
	}

	OpenMenuRoot(EWrecktideMenuScreen::Inventory);
}

void ACombatPlayerController::CheckFirstVoyageChestHint()
{
	if (AWrecktidePlayerState::IsProbeRun())
	{
		return;
	}
	// World timers pause with the title. A long warm-up or another menu defers the hint as well.
	if (bTitlePending || IsAnyMenuOpen() || IsModalMenuOpen() || bMapScreenOpen
		|| !GetPlayerState<AWrecktidePlayerState>() || !Cast<AWrecktideHUD>(GetHUD()))
	{
		GetWorldTimerManager().SetTimer(ChestHintTimer, this, &ACombatPlayerController::CheckFirstVoyageChestHint,
			FirstVoyageChestHintRetrySeconds, false);
		return;
	}
	ShowChestHintIfUnseen();
}

bool ACombatPlayerController::ShowChestHintIfUnseen()
{
	AWrecktidePlayerState* State = GetPlayerState<AWrecktidePlayerState>();
	UProgressComponent* Progress = State ? State->GetProgress() : nullptr;
	AWrecktideHUD* HUD = Cast<AWrecktideHUD>(GetHUD());
	if (!IsLocalPlayerController() || !State || !State->HasAuthority() || !Progress || !HUD
		|| bTitlePending || IsAnyMenuOpen() || IsModalMenuOpen() || bMapScreenOpen
		|| Progress->QuestFlags.Contains(FirstVoyageChestHintFlag))
	{
		return false;
	}
	HUD->ShowToast(TEXT("Tab opens your Captain's Chest. J for the journal, M for the map."),
		FirstVoyageChestHintDurationSeconds);
	// The existing progress event schedules autosave; the flag map already belongs to the save.
	Progress->SetQuestFlag(FirstVoyageChestHintFlag, 1);
	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-CHEST first-voyage hint shown"));
	return true;
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
bool ACombatPlayerController::ShowChestHintForProbe()
{
	return AWrecktidePlayerState::IsProbeRun() && ShowChestHintIfUnseen();
}
#endif

void ACombatPlayerController::OpenJournalPage()
{
	// J is the journal's own key (research A1). It opens the chest when it is shut and jumps to the
	// page either way, so one key always lands on the journal.
	if (!IsInventoryOpen())
	{
		ToggleInventory();
		if (!IsInventoryOpen())
		{
			return;
		}
	}
	SetChestTab(EWrecktideChestTab::Journal);
}

void ACombatPlayerController::ToggleMapScreen()
{
	// M advances the cycle (spec C27): small compass ring, then the plate radar, then the chart,
	// then back to the ring. Leaving the chart is always allowed; entering anything is not, while
	// another screen owns the display.
	if (RadarState == EWrecktideRadarState::Full)
	{
		SetRadarState(EWrecktideRadarState::Small);
		return;
	}
	if (bTitlePending || IsAnyMenuOpen() || IsModalMenuOpen())
	{
		return;
	}

	if (RadarState == EWrecktideRadarState::Small)
	{
		// Small to medium changes nothing but a size: no pause, so no warm-up gate to clear.
		SetRadarState(EWrecktideRadarState::Medium);
		return;
	}

	// Auto exposure is engine default and freezes with the world: a screen opened on the very first
	// frames froze it at its start value and gave a black title card (2026-09-05). Same trap, same
	// cure as OpenTitleMenu: 1.5 s of world time AND 60 rendered frames before anything pauses.
	const UWorld* World = GetWorld();
	if (World && (World->GetTimeSeconds() < 1.5f || GFrameCounter < MapArmFrame + 60))
	{
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-MAP refused, still warming up (t=%.2f frames=%llu)"),
			World->GetTimeSeconds(), static_cast<uint64>(GFrameCounter - MapArmFrame));
		return;
	}

	SetRadarState(EWrecktideRadarState::Full);
}

void ACombatPlayerController::SetRadarState(EWrecktideRadarState NewState)
{
	if (RadarState == NewState)
	{
		return;
	}

	RadarState = NewState;
	if (AWrecktideHUD* WrecktideHUD = Cast<AWrecktideHUD>(GetHUD()))
	{
		WrecktideHUD->SetRadarState(RadarState);
	}

	// The chart and the pause it brings follow the state, so the two can never disagree.
	SetMapScreenOpen(RadarState == EWrecktideRadarState::Full);

	// Small and medium are sizes worth remembering; full is a moment, not a size, so a session that
	// ends on the chart comes back on the ring.
	if (RadarState != EWrecktideRadarState::Full)
	{
		FWrecktideOptionValues Options = FWrecktideOptions::Get();
		const int32 Persisted = RadarState == EWrecktideRadarState::Medium ? 1 : 0;
		if (Options.RadarSize != Persisted)
		{
			Options.RadarSize = Persisted;
			FWrecktideOptions::Set(Options);
			FWrecktideOptions::Save();
		}
	}

	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-MAP radar state=%d paused=%d"),
		static_cast<int32>(RadarState), UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void ACombatPlayerController::SetMapScreenOpen(bool bOpen)
{
	if (bMapScreenOpen == bOpen)
	{
		return;
	}

	bMapScreenOpen = bOpen;
	if (AWrecktideHUD* WrecktideHUD = Cast<AWrecktideHUD>(GetHUD()))
	{
		WrecktideHUD->SetMapScreen(bOpen);
	}

	// The map is its own little pause root. It can only open when no menu is up (ToggleMapScreen
	// refuses otherwise), so unpausing on close can never steal a pause the menu tree owns.
	UGameplayStatics::SetGamePaused(this, bOpen);
	UWrecktideAudio::PlaySfx(this, TEXT("ui_confirm"));

	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-MAP screen=%d paused=%d"),
		bOpen ? 1 : 0, UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void ACombatPlayerController::HandleMenuBackKey()
{
	// The title is armed but not up yet (scenic warm-up): every menu key is ignored. An Esc here
	// used to pause instead, and the title timer then bailed on the open menu and never re-armed,
	// leaving the session on the scenic camera with the pirate hidden. The other menu keys need no
	// guard: they all no-op while no menu is open.
	if (bTitlePending && !IsAnyMenuOpen())
	{
		return;
	}

	// Esc shuts the chart first: it is the topmost thing on screen when it is up. It lands on the
	// small ring, the same place a third press of M lands (spec C27).
	if (bMapScreenOpen)
	{
		SetRadarState(EWrecktideRadarState::Small);
		return;
	}

	// Inside a sub-screen Esc means "back one screen"; at the root it is the pause toggle.
	if (MenuStack.Num() > 1)
	{
		MenuBack();
		return;
	}
	TogglePauseMenu();
}

void ACombatPlayerController::OpenMenuRoot(EWrecktideMenuScreen Root)
{
	if (IsAnyMenuOpen())
	{
		return;
	}

	MenuStack.Reset();
	MenuStack.Add(Root);
	MenuSelectedIndex = 0;
	PendingOverwriteSlot = 0;

	bCursorBeforePause = bShowMouseCursor;
	UGameplayStatics::SetGamePaused(this, true);
	UWrecktideAudio::PlaySfx(this, TEXT("ui_confirm"));

	// The widget goes in FIRST: SetWidgetToFocus needs a widget that is already in the viewport,
	// and without focus Slate eats the first click (research B2).
	AddMenuWidget();
	ApplyMenuInputMode();
	RebuildMenuRows();

	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-PAUSE open root=%d paused=%d rows=%d"),
		static_cast<int32>(Root), UGameplayStatics::IsGamePaused(this) ? 1 : 0, MenuRows.Num());
}

void ACombatPlayerController::CloseMenus()
{
	if (!IsAnyMenuOpen())
	{
		return;
	}

	// Safety net, the twin of the ClearScenicCamera call below: the wardrobe screen normally closes
	// through MenuBack, but any path that closes the whole tree from inside it must still bring the
	// pirate out of the change room and off the preview camera. No-op when no preview is up.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UWrecktideWardrobeSubsystem* Wardrobe = GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>())
		{
			Wardrobe->EndPreview(this);
		}
	}

	MenuStack.Reset();
	MenuRows.Reset();
	MenuActions.Reset();
	MenuSelectedIndex = 0;
	PendingOverwriteSlot = 0;

	RemoveMenuWidget();
	RestoreGameInputMode();
	bShowMouseCursor = bCursorBeforePause;
	UGameplayStatics::SetGamePaused(this, false);

	// The title menu watched the bay from a spawned camera; hand the view back to the pirate.
	// UNCONDITIONAL, whatever root was open: the scenic camera and the hidden pawn are armed before
	// the title menu exists, so a pause opened in that window (or a scenic camera that failed to
	// spawn after the pawn was hidden) must still be undone here. It is a no-op when neither is set.
	ClearScenicCamera();

	PublishMenu();
	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-PAUSE close paused=%d"), UGameplayStatics::IsGamePaused(this) ? 1 : 0);
}

void ACombatPlayerController::ApplyMenuInputMode()
{
	// GameAndUI with no lock: the mouse can reach the rows AND the game keeps receiving the key
	// bindings that drive the model. UIOnly would hand the keyboard to Slate and kill Esc.
	// SetHideCursorDuringCapture(false) is not cosmetic: with it true the cursor vanishes on a right
	// button press and every row fires OnUnhovered (research B2).
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	// Focus goes on the widget only when there IS one and it is already in the viewport: TakeWidget
	// on an unparented widget is the documented crash. Without focus the first click is eaten.
	if (MenuWidget.IsValid())
	{
		InputMode.SetWidgetToFocus(MenuWidget.ToSharedRef());
	}
	else if (ChestWidget.IsValid())
	{
		InputMode.SetWidgetToFocus(ChestWidget.ToSharedRef());
	}
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void ACombatPlayerController::RestoreGameInputMode()
{
	// The reported trap is game input dying after a menu closes, and it dies when the mode is left
	// on GameAndUI with a focused widget. Every close comes through here: mode, cursor and focus.
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	bCanvasModalCursor = false;
	bHasCanvasHoverMouse = false;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
	}
}

void ACombatPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdateCanvasModalCursor();
}

void ACombatPlayerController::UpdateCanvasModalCursor()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	// A Slate screen owns the cursor while it is up; this only handles the canvas modals, which
	// have no widget of their own. The two can never overlap: each refuses to open over the other.
	const bool bWantCursor = !IsAnyMenuOpen() && IsModalMenuOpen();
	if (bWantCursor != bCanvasModalCursor)
	{
		bCanvasModalCursor = bWantCursor;
		if (bWantCursor)
		{
			ApplyMenuInputMode();
			bHasCanvasHoverMouse = false;
			UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-CURSOR canvas modal open, cursor on"));
		}
		else
		{
			RestoreGameInputMode();
			UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-CURSOR canvas modal closed, cursor off and game input back"));
		}
	}

	if (!bCanvasModalCursor)
	{
		return;
	}

	// Hover moves the selection, exactly as it does on the Slate rows, and only once the mouse has
	// really moved: a rebuilt list under a still cursor must not steal the arrow keys' choice.
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	const FVector2D MousePosition(MouseX, MouseY);
	if (bHasCanvasHoverMouse && FVector2D::Distance(MousePosition, LastCanvasHoverMouse) < MenuHoverMoveTolerance)
	{
		return;
	}
	LastCanvasHoverMouse = MousePosition;
	bHasCanvasHoverMouse = true;

	const int32 HoveredRow = HitTestCanvasModal(MousePosition);
	if (HoveredRow != INDEX_NONE)
	{
		DriveCanvasModal(HoveredRow, false);
	}
}

int32 ACombatPlayerController::HitTestCanvasModal(const FVector2D& ScreenPosition) const
{
	const AWrecktideHUD* WrecktideHUD = Cast<AWrecktideHUD>(GetHUD());
	return WrecktideHUD ? WrecktideHUD->HitTestShopRow(ScreenPosition) : INDEX_NONE;
}

bool ACombatPlayerController::DriveCanvasModal(int32 Index, bool bConfirm)
{
	UWorld* World = GetWorld();
	AWrecktideHUD* WrecktideHUD = Cast<AWrecktideHUD>(GetHUD());
	if (!World || !WrecktideHUD || !WrecktideHUD->GetShopMenuLineCount())
	{
		return false;
	}

	const int32 RowCount = WrecktideHUD->GetShopMenuLineCount();
	if (Index < 0 || Index >= RowCount)
	{
		return false;
	}

	// The four modals share no base class, only a shape: MoveMenuSelection(+-1) wrapping over their
	// own rows, ConfirmMenuSelection, and the HUD line list they all push. Stepping that shared API
	// to the target row means the mouse cannot reach any state the keyboard could not, and no modal
	// actor needed a new entry point (which keeps this lane out of the quest lane's files).
	auto StepAndConfirm = [this, WrecktideHUD, Index, bConfirm, RowCount](auto* Modal) -> bool
	{
		const int32 Current = WrecktideHUD->GetShopMenuSelectedIndex();
		int32 Steps = ((Index - Current) % RowCount + RowCount) % RowCount;
		// Never more than one lap, whatever the HUD mirror says.
		for (int32 Step = 0; Step < Steps && Step < RowCount; ++Step)
		{
			Modal->MoveMenuSelection(1);
		}
		if (bConfirm)
		{
			Modal->ConfirmMenuSelection();
		}
		return true;
	};

	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		if (It->IsShopMenuOpen())
		{
			return StepAndConfirm(*It);
		}
	}
	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return StepAndConfirm(*It);
		}
	}
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return StepAndConfirm(*It);
		}
	}
	for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return StepAndConfirm(*It);
		}
	}
	return false;
}

void ACombatPlayerController::HandleModalClick()
{
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (GetMousePosition(MouseX, MouseY))
	{
		HandleModalClickAt(FVector2D(MouseX, MouseY));
	}
}

bool ACombatPlayerController::HandleModalClickAt(const FVector2D& ScreenPosition)
{
	// Hidden-cursor play uses the printed key badges. Only a visible toolbar can receive a click.
	if (!bShowMouseCursor || bTitlePending || bMapScreenOpen
		|| (IsAnyMenuOpen() && !IsInventoryOpen()))
	{
		return false;
	}
	if (LastModalClickFrame == GFrameCounter)
	{
		return true;
	}

	const AWrecktideHUD* HUD = Cast<AWrecktideHUD>(GetHUD());
	const FBox2D ChestRect = HUD ? HUD->GetChestButtonRect() : FBox2D(ForceInit);
	const FBox2D MapRect = HUD ? HUD->GetMapButtonRect() : FBox2D(ForceInit);
	const bool bChest = ChestRect.bIsValid && ChestRect.IsInside(ScreenPosition);
	const bool bMap = MapRect.bIsValid && MapRect.IsInside(ScreenPosition);
	if (bChest || bMap)
	{
		LastModalClickFrame = GFrameCounter;
		if (bMap && IsInventoryOpen())
		{
			CloseMenus();
		}
		CloseCanvasModalsForToolbar();
		if (bChest)
		{
			ToggleInventory();
		}
		else
		{
			ToggleMapScreen();
		}
		return true;
	}

	const int32 Row = bCanvasModalCursor ? HitTestCanvasModal(ScreenPosition) : INDEX_NONE;
	if (Row == INDEX_NONE)
	{
		return false;
	}
	LastModalClickFrame = GFrameCounter;
	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-CURSOR click row=%d at (%.0f,%.0f)"), Row, ScreenPosition.X, ScreenPosition.Y);
	return DriveCanvasModal(Row, true);
}

void ACombatPlayerController::CloseCanvasModalsForToolbar()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		if (It->IsShopMenuOpen()) { It->ToggleShopMenu(Cast<ACombatCharacter>(GetPawn())); }
	}
	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		if (It->IsMenuOpen()) { It->ToggleBoardMenu(Cast<ACombatCharacter>(GetPawn())); }
	}
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->IsMenuOpen()) { It->CloseGiverMenu(); }
	}
	for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It)
	{
		if (It->IsMenuOpen()) { It->ToggleSmithMenu(Cast<ACombatCharacter>(GetPawn())); }
	}
	// Restore game input now, before the chest records the cursor state to restore on close.
	UpdateCanvasModalCursor();
}

void ACombatPlayerController::HandleModalRightClick()
{
	// Slate screens only. A canvas modal leaves the pawn live, so a right button there is a charged
	// attack and must stay one; Esc and F close those.
	if (!IsAnyMenuOpen())
	{
		return;
	}
	HandleMenuBackKey();
}

void ACombatPlayerController::HandleModalScrollUp()
{
	if (IsAnyMenuOpen())
	{
		MenuNavigate(-1);
	}
	else if (bCanvasModalCursor)
	{
		const AWrecktideHUD* WrecktideHUD = Cast<AWrecktideHUD>(GetHUD());
		const int32 Current = WrecktideHUD ? WrecktideHUD->GetShopMenuSelectedIndex() : INDEX_NONE;
		const int32 Rows = WrecktideHUD ? WrecktideHUD->GetShopMenuLineCount() : 0;
		if (Rows > 0 && Current != INDEX_NONE)
		{
			DriveCanvasModal((Current - 1 + Rows) % Rows, false);
		}
	}
}

void ACombatPlayerController::HandleModalScrollDown()
{
	if (IsAnyMenuOpen())
	{
		MenuNavigate(1);
	}
	else if (bCanvasModalCursor)
	{
		const AWrecktideHUD* WrecktideHUD = Cast<AWrecktideHUD>(GetHUD());
		const int32 Current = WrecktideHUD ? WrecktideHUD->GetShopMenuSelectedIndex() : INDEX_NONE;
		const int32 Rows = WrecktideHUD ? WrecktideHUD->GetShopMenuLineCount() : 0;
		if (Rows > 0 && Current != INDEX_NONE)
		{
			DriveCanvasModal((Current + 1) % Rows, false);
		}
	}
}

void ACombatPlayerController::AddMenuWidget()
{
	if (MenuWidget.IsValid() || !IsLocalPlayerController())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport && GEngine)
	{
		Viewport = GEngine->GameViewport;
	}
	if (!Viewport)
	{
		// No viewport means a headless run: the model still works and the HUD lines still publish,
		// so probes pass; only the drawn panel is missing.
		UE_LOG(LogWrecktide, Warning, TEXT("WRECKTIDE-MENU no game viewport, menu draws through the HUD lines only"));
		return;
	}

	// One widget per ROOT screen: the chest is a different shape (right edge, page rail) and would
	// only make the menu view a switch statement. The root cannot change while a tree is open, so
	// exactly one of the two is ever alive.
	if (MenuStack.Num() > 0 && MenuStack[0] == EWrecktideMenuScreen::Inventory)
	{
		ChestWidget = SNew(SWrecktideChest).Controller(this);
		Viewport->AddViewportWidgetContent(ChestWidget.ToSharedRef(), 100);
		return;
	}

	MenuWidget = SNew(SWrecktideMenu).Controller(this);
	Viewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 100);
}

void ACombatPlayerController::RemoveMenuWidget()
{
	if (!MenuWidget.IsValid() && !ChestWidget.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport && GEngine)
	{
		Viewport = GEngine->GameViewport;
	}
	if (Viewport)
	{
		if (MenuWidget.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
		}
		if (ChestWidget.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(ChestWidget.ToSharedRef());
		}
	}
	MenuWidget.Reset();
	ChestWidget.Reset();
}

void ACombatPlayerController::PublishMenu()
{
	// The HUD keeps the label list as probe evidence even though it no longer draws the panel.
	if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(GetHUD()))
	{
		if (IsAnyMenuOpen())
		{
			TArray<FString> Labels;
			Labels.Reserve(MenuRows.Num());
			for (const FWrecktideMenuRow& Row : MenuRows)
			{
				Labels.Add(Row.Label);
			}
			HUD->SetPauseMenuHeader(MenuTitleText, MenuHintText);
			HUD->SetPauseMenu(true, Labels, MenuSelectedIndex);
		}
		else
		{
			HUD->SetPauseMenuHeader(FString(), FString());
			HUD->SetPauseMenu(false, TArray<FString>(), INDEX_NONE);
		}
	}

	if (MenuWidget.IsValid())
	{
		MenuWidget->Rebuild();
	}
	if (ChestWidget.IsValid())
	{
		ChestWidget->Rebuild();
	}
	OnMenuChanged.Broadcast();
}

void ACombatPlayerController::RefreshPauseMenu()
{
	RebuildMenuRows();
}

void ACombatPlayerController::RefreshTitleMenu()
{
	RebuildMenuRows();
}

void ACombatPlayerController::RequestMenuRefresh()
{
	// The HUD asks for this from inside DrawHUD. Rebuilding there would rebuild the Slate widget
	// tree during the paint pass, so the work is deferred by one engine tick. Core ticker, not a
	// world timer: the menus run with the world paused (trap 1).
	if (bMenuRefreshPending || !IsAnyMenuOpen())
	{
		return;
	}

	bMenuRefreshPending = true;
	TWeakObjectPtr<ACombatPlayerController> WeakThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float) -> bool
	{
		if (ACombatPlayerController* Controller = WeakThis.Get())
		{
			Controller->bMenuRefreshPending = false;
			if (Controller->IsAnyMenuOpen())
			{
				Controller->RebuildMenuRows();
			}
		}
		return false;
	}), 0.0f);
}

// ---------------------------------------------------------------------------------------------
// Menu tree: the rows
// ---------------------------------------------------------------------------------------------

void ACombatPlayerController::RebuildMenuRows()
{
	MenuRows.Reset();
	MenuActions.Reset();
	MenuSubtitleText.Reset();
	MenuHintText = TEXT("Up / Down choose   Enter confirm   Esc back");

	const EWrecktideMenuScreen Screen = GetMenuScreen();
	auto AddRow = [this](const FString& Label, int32 Action, const FString& Value = FString(),
		const FString& SubLine = FString(), bool bSelectable = true)
	{
		MenuRows.Add(FWrecktideMenuRow(Label, Value, SubLine, bSelectable));
		MenuActions.Add(bSelectable ? Action : ActNone);
	};

	switch (Screen)
	{
	case EWrecktideMenuScreen::Main:
	{
		MenuTitleText = TEXT("WRECKTIDE");
		MenuSubtitleText = TEXT("a pirate's fortune");
		const bool bHasSave = !AWrecktidePlayerState::IsPersistenceSuppressed()
			&& UGameplayStatics::DoesSaveGameExist(AWrecktidePlayerState::GetSaveSlot(), 0);
		if (bHasSave)
		{
			AddRow(TEXT("Continue"), ActContinue);
		}
		AddRow(TEXT("New Voyage"), ActNewVoyage);
		AddRow(TEXT("Load Voyage"), ActLoadScreen);
		AddRow(TEXT("Wardrobe"), ActWardrobeScreen);
		AddRow(TEXT("Options"), ActOptionsScreen);
		AddRow(TEXT("Credits"), ActCreditsScreen);
		AddRow(TEXT("Quit"), ActQuit);
		break;
	}
	case EWrecktideMenuScreen::Pause:
		MenuTitleText = TEXT("Paused");
		AddRow(TEXT("Resume"), ActResume);
		AddRow(TEXT("Save Voyage"), ActSaveScreen);
		AddRow(TEXT("Load Voyage"), ActLoadScreen);
		AddRow(TEXT("Wardrobe"), ActWardrobeScreen);
		AddRow(TEXT("Options"), ActOptionsScreen);
		AddRow(TEXT("Controls"), ActControlsScreen);
		AddRow(TEXT("Main Menu"), ActMainMenu);
		AddRow(TEXT("Quit to Desktop"), ActQuit);
		break;

	case EWrecktideMenuScreen::SaveVoyage:
	{
		MenuTitleText = TEXT("Save Voyage");
		for (int32 Slot = 1; Slot <= AWrecktidePlayerState::GetManualSlotCount(); ++Slot)
		{
			const FString Summary = SlotSummaries.IsValidIndex(Slot) ? SlotSummaries[Slot] : FString(TEXT("Empty"));
			// The summary goes UNDER the label: a whole "Notoriety Lv 3, 750 gold, ..." line will not fit in
			// the right-hand value column at any readable size.
			AddRow(FString::Printf(TEXT("Slot %d"), Slot), ActSlotBase + Slot, FString(), Summary);
		}
		AddRow(TEXT("Back"), ActBack);
		if (PendingOverwriteSlot > 0)
		{
			MenuHintText = FString::Printf(TEXT("Overwrite Slot %d?   Enter confirm   Esc cancel"), PendingOverwriteSlot);
		}
		break;
	}
	case EWrecktideMenuScreen::LoadVoyage:
	{
		MenuTitleText = TEXT("Load Voyage");
		for (int32 Slot = 0; Slot <= AWrecktidePlayerState::GetManualSlotCount(); ++Slot)
		{
			const FString Summary = SlotSummaries.IsValidIndex(Slot) ? SlotSummaries[Slot] : FString(TEXT("Empty"));
			const bool bOccupied = !Summary.Equals(TEXT("Empty"));
			const FString Label = Slot == 0 ? FString(TEXT("Autosave")) : FString::Printf(TEXT("Slot %d"), Slot);
			// An empty slot is a caption, not a dead button: navigation skips it entirely.
			AddRow(Label, ActSlotBase + Slot, FString(), Summary, bOccupied);
		}
		AddRow(TEXT("Back"), ActBack);
		break;
	}
	case EWrecktideMenuScreen::Options:
	{
		MenuTitleText = TEXT("Options");
		MenuHintText = TEXT("Left / Right change   Enter confirm   Esc back");
		AddRow(TEXT("Master volume"), ActOptionBase + OptMaster, FString::Printf(TEXT("%d"), PendingOptions.MasterVolume));
		AddRow(TEXT("Effects volume"), ActOptionBase + OptEffects, FString::Printf(TEXT("%d"), PendingOptions.EffectsVolume));
		AddRow(TEXT("Ambience volume"), ActOptionBase + OptAmbience, FString::Printf(TEXT("%d"), PendingOptions.AmbienceVolume));
		AddRow(TEXT("Mouse sensitivity"), ActOptionBase + OptSensitivity, FString::Printf(TEXT("%.1f"), PendingOptions.MouseSensitivity));
		AddRow(TEXT("Invert look Y"), ActOptionBase + OptInvert, OnOff(PendingOptions.bInvertLookY));
		AddRow(TEXT("Minimap north up"), ActOptionBase + OptMinimapNorthUp, OnOff(PendingOptions.bMinimapNorthUp));
		AddRow(TEXT("Window mode"), ActOptionBase + OptWindowMode, WindowModeLabel(PendingWindowMode));
		{
			const FIntPoint Resolution = ResolutionChoices.IsValidIndex(PendingResolutionIndex)
				? ResolutionChoices[PendingResolutionIndex] : FIntPoint(1280, 720);
			AddRow(TEXT("Resolution"), ActOptionBase + OptResolution,
				FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y));
		}
		AddRow(TEXT("VSync"), ActOptionBase + OptVSync, OnOff(bPendingVSync));
		AddRow(TEXT("Quality"), ActOptionBase + OptQuality, QualityLabel(PendingQuality));
		AddRow(TEXT("Apply"), ActApplyOptions);
		AddRow(TEXT("Back"), ActBack);
		break;
	}
	case EWrecktideMenuScreen::Wardrobe:
	{
		MenuTitleText = TEXT("Wardrobe");
		const UGameInstance* GameInstance = GetGameInstance();
		UWrecktideWardrobeSubsystem* Wardrobe = GameInstance
			? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
		const TArray<FWrecktideOutfitInfo> Outfits = Wardrobe ? Wardrobe->GetOutfits() : TArray<FWrecktideOutfitInfo>();
		if (Outfits.Num() == 0)
		{
			// The catalogue lands with the wardrobe feature; until then the screen still opens,
			// still navigates and still closes, so nothing here has to change when it does.
			AddRow(TEXT("No outfits found"), ActNone, FString(), FString(), false);
		}
		else
		{
			// EFFECTIVE id, not the raw persisted one: a fresh save has chosen nothing (None) but the
			// pirate is still wearing the default outfit, and that row is the one marked "worn".
			const FName CurrentId = Wardrobe->GetEffectiveOutfitId(this);
			const int32 GatingLevel = Wardrobe->GetPlayerGatingLevel(this);
			for (int32 Index = 0; Index < Outfits.Num(); ++Index)
			{
				const FWrecktideOutfitInfo& Outfit = Outfits[Index];
				const bool bWorn = Outfit.Id == CurrentId;
				// A locked row carries its price tag instead of a tick: "Lv 18  700 gold". It stays
				// selectable, because Enter on it is how the player buys it.
				FString Value;
				if (bWorn)
				{
					Value = TEXT("worn");
				}
				else if (!Outfit.bOwned)
				{
					Value = FString::Printf(TEXT("Lv %d  %lld gold"), Outfit.RequiredLevel, Outfit.Price);
				}
				AddRow(Outfit.DisplayName.ToString(), ActOutfitBase + Index, Value, Outfit.Description.ToString());
			}
			// The colourway row shows the variant of whatever outfit the cursor is on; when the cursor
			// has moved off the list it falls back to the one actually worn.
			const FName SelectedId = GetColourwayOutfitId(Outfits, CurrentId);
			// The palette is earned too: Left/Right only walks the colourways the level has opened.
			const int32 UnlockedVariants = FMath::Max(1, Wardrobe->GetUnlockedVariantCount(GatingLevel));
			WardrobeVariant = FMath::Clamp(WardrobeVariant, 0, UnlockedVariants - 1);
			AddRow(TEXT("Colourway"), ActColourway, Wardrobe->GetVariantRowLabel(SelectedId, WardrobeVariant).ToString());
			MenuHintText = TEXT("Left / Right colourway   Enter wear   Esc back");
			const int32 NextVariantLevel = Wardrobe->GetNextVariantLevel(UnlockedVariants);
			if (NextVariantLevel > 0)
			{
				MenuHintText = FString::Printf(TEXT("%d of %d colourways   next at level %d   Esc back"),
					UnlockedVariants, Wardrobe->GetVariantCount(), NextVariantLevel);
			}
			if (PendingOutfitBuyIndex != INDEX_NONE && Outfits.IsValidIndex(PendingOutfitBuyIndex))
			{
				MenuHintText = FString::Printf(TEXT("Buy the %s for %lld gold? Enter"),
					*Outfits[PendingOutfitBuyIndex].DisplayName.ToString(), Outfits[PendingOutfitBuyIndex].Price);
			}
			// Live preview: the pirate wears whatever the rows are SHOWING while the screen is up.
			// Nothing is persisted until Enter, and EndPreview puts the worn outfit back, so a
			// player who only browses leaves in the clothes he arrived in.
			Wardrobe->PreviewOutfit(this, SelectedId, WardrobeVariant);
		}
		AddRow(TEXT("Back"), ActBack);
		break;
	}
	case EWrecktideMenuScreen::Controls:
		MenuTitleText = TEXT("Controls");
		MenuHintText = TEXT("Scroll for more controls   Esc back");
		// Checked against IMC_Sandbox, the pawn's input graph, and the native bindings.
		AddRow(TEXT("Move"), ActNone, TEXT("W A S D / Arrow keys"), FString(), false);
		AddRow(TEXT("Sprint"), ActNone, TEXT("Left Shift"), FString(), false);
		AddRow(TEXT("Jump"), ActNone, TEXT("Space"), FString(), false);
		AddRow(TEXT("Attack combo"), ActNone, TEXT("Left mouse"), FString(), false);
		AddRow(TEXT("Talk / Board / Use"), ActNone, TEXT("F"), FString(), false);
		AddRow(TEXT("Weapon bar"), ActNone, TEXT("1 to 5"), FString(), false);
		AddRow(TEXT("Captain's Chest"), ActNone, TEXT("Tab or I"), FString(), false);
		AddRow(TEXT("Journal"), ActNone, TEXT("J"), FString(), false);
		AddRow(TEXT("Map cycle"), ActNone, TEXT("M"), FString(), false);
		AddRow(TEXT("Pause"), ActNone, TEXT("Esc"), FString(), false);
		AddRow(TEXT("Aim"), ActNone, TEXT("Mouse"), FString(), false);
		AddRow(TEXT("Charged attack"), ActNone, TEXT("Right mouse"), FString(), false);
		AddRow(TEXT("Walk"), ActNone, TEXT("Left Ctrl"), FString(), false);
		AddRow(TEXT("Crouch"), ActNone, TEXT("C"), FString(), false);
		AddRow(TEXT("Strafe"), ActNone, TEXT("Middle mouse"), FString(), false);
		AddRow(TEXT("Camera zoom"), ActNone, TEXT("Mouse wheel"), FString(), false);
		AddRow(TEXT("Aboard the boat"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Sail"), ActNone, TEXT("W / S"), FString(), false);
		AddRow(TEXT("Steer"), ActNone, TEXT("A / D"), FString(), false);
		AddRow(TEXT("Go ashore"), ActNone, TEXT("F"), FString(), false);
		AddRow(TEXT("Swimming"), ActNone, TEXT("W A S D"), FString(), false);
		AddRow(TEXT("Menu selection / Confirm"), ActNone, TEXT("Arrows / Enter"), FString(), false);
		AddRow(TEXT("Chest page"), ActNone, TEXT("Left / Right"), FString(), false);
		AddRow(TEXT("Back"), ActBack);
		break;

	case EWrecktideMenuScreen::Credits:
		MenuTitleText = TEXT("Credits");
		AddRow(TEXT("WRECKTIDE"), ActNone, FString(), FString(), false);
		AddRow(TEXT("a KovaTech Industries game"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Design and code: Alaa Soulaiman"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Characters, props and island: POLYGON Pirates by Synty Studios (licensed)"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Locomotion animation: Game Animation Sample by Epic Games"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Fonts: Pirata One, Cinzel Decorative, IM Fell English (SIL Open Font License)"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Sounds: synthesised in house, plus foley and ambient sounds by Epic Games"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Made with Unreal Engine"), ActNone, FString(), FString(), false);
		AddRow(TEXT("Wrecktide uses Unreal\u00AE Engine. Unreal\u00AE is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere."), ActNone, FString(), FString(), false);
		AddRow(TEXT("Unreal\u00AE Engine, Copyright 1998 \u2013 2026, Epic Games, Inc. All rights reserved."), ActNone, FString(), FString(), false);
		AddRow(TEXT("Back"), ActBack);
		break;

	case EWrecktideMenuScreen::Inventory:
	{
		// The chest (spec C18). One page at a time; the page rail is drawn by the Slate view, which
		// reads GetChestTab. Rows are built per page, and the action id is written in the SAME loop
		// as the label so a relabel can never fire another page's action.
		MenuTitleText = TEXT("Captain's Chest");
		AWrecktidePlayerState* ChestState = GetPlayerState<AWrecktidePlayerState>();
		UProgressComponent* ChestProgress = ChestState ? ChestState->GetProgress() : nullptr;
		const UGameInstance* ChestGameInstance = GetGameInstance();
		const UWrecktideDataSubsystem* ChestData = ChestGameInstance
			? ChestGameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

		switch (ChestTab)
		{
		case EWrecktideChestTab::Weapons:
		{
			MenuSubtitleText = TEXT("Belt");
			MenuHintText = TEXT("Up / Down choose   Enter wield   Left / Right page   Tab close");
			// One row per class, the classic belt shape (research A2): the player picks WHICH owned
			// weapon of a class is wielded, he does not drag items into free slots.
			AddRow(TEXT("Slot 1  Blade"), ActNone, FString(), FString(), false);
			int32 MeleeIndex = 0;
			ChestMeleeRows.Reset();
			if (ChestData && ChestProgress && ChestState)
			{
				for (const TPair<FName, const FWeaponTableRow*>& Weapon : ChestData->GetWeaponRows())
				{
					if (!Weapon.Value || Weapon.Value->CombatClass != EWrecktideSkill::Melee
						|| !ChestProgress->IsItemUnlocked(Weapon.Key))
					{
						continue;
					}
					ChestMeleeRows.Add(Weapon.Key);
					const bool bWielded = ChestState->EquippedWeaponRow == Weapon.Key;
					const bool bAssigned = ChestState->GetAssignedSlot(1) == Weapon.Key;
					FString Detail = FString::Printf(TEXT("Blade   %.0f damage"), Weapon.Value->BaseDamage);
					if (!ChestState->CanEquipWeapon(Weapon.Key))
					{
						Detail += FString::Printf(TEXT("   Notoriety Lv %d needed"), Weapon.Value->RequiredLevel);
					}
					AddRow(Weapon.Value->DisplayName.IsEmpty() ? Weapon.Key.ToString() : Weapon.Value->DisplayName.ToString(),
						ActChestWieldBase + MeleeIndex,
						bWielded ? TEXT("1") : (bAssigned ? TEXT("belt") : FString()), Detail);
					++MeleeIndex;
				}
			}
			if (MeleeIndex == 0)
			{
				AddRow(TEXT("No blade in the chest yet"), ActNone,
					FString(), TEXT("The market stall sells one for 25 gold."), false);
			}
			else if (ChestState && ChestState->GetAssignedSlot(1) != NAME_None)
			{
				AddRow(TEXT("Empty the blade slot"), ActChestUnassign);
			}
			AddRow(TEXT("Slot 2  Gun"), ActNone, FString(), FString(), false);
			ChestGunRows.Reset();
			if (ChestData && ChestProgress && ChestState)
			{
				for (const TPair<FName, const FWeaponTableRow*>& Weapon : ChestData->GetWeaponRows())
				{
					if (!Weapon.Value || Weapon.Value->CombatClass != EWrecktideSkill::Shooting
						|| !ChestProgress->IsItemUnlocked(Weapon.Key)) { continue; }
					const int32 GunIndex = ChestGunRows.Add(Weapon.Key);
					FString Detail = FString::Printf(TEXT("Gun   %.0f damage"), Weapon.Value->BaseDamage);
					if (!ChestState->CanEquipWeapon(Weapon.Key))
					{
						Detail += FString::Printf(TEXT("   Notoriety Lv %d needed"), Weapon.Value->RequiredLevel);
					}
					AddRow(Weapon.Value->DisplayName.IsEmpty() ? Weapon.Key.ToString() : Weapon.Value->DisplayName.ToString(),
						PistolChestWieldBase + GunIndex,
						ChestState->EquippedWeaponRow == Weapon.Key ? TEXT("2")
							: (ChestState->GetAssignedSlot(2) == Weapon.Key ? TEXT("belt") : FString()), Detail);
				}
			}
			if (ChestGunRows.IsEmpty())
			{
				AddRow(TEXT("No gun in the chest yet"), ActNone, FString(), AWrecktidePlayerState::GetSlotHint(2), false);
			}
			else if (ChestState && !ChestState->GetAssignedSlot(2).IsNone())
			{
				AddRow(TEXT("Empty the gun slot"), PistolChestUnassign);
			}
			AddRow(TEXT("Slot 3  Hex"), ActNone, FString(), AWrecktidePlayerState::GetSlotHint(3), false);
			AddRow(TEXT("Slots 4 and 5  Tonics"), ActNone, FString(), AWrecktidePlayerState::GetSlotHint(4), false);
			break;
		}
		case EWrecktideChestTab::Outfits:
		{
			MenuSubtitleText = TEXT("Slops");
			MenuHintText = TEXT("Enter opens the change room   Left / Right page   Tab close");
			if (ChestProgress && ChestData)
			{
				const FName WornId = ChestState ? ChestState->GetOutfitId() : NAME_None;
				for (const TPair<FName, const FOutfitTableRow*>& Outfit : ChestData->GetOutfitRows())
				{
					if (!Outfit.Value || !ChestProgress->OwnsOutfit(Outfit.Key))
					{
						continue;
					}
					AddRow(Outfit.Value->DisplayName.IsEmpty() ? Outfit.Key.ToString() : Outfit.Value->DisplayName.ToString(),
						ActNone, Outfit.Key == WornId ? TEXT("worn") : FString(), FString(), false);
				}
			}
			AddRow(TEXT("Open the change room"), ActChestWardrobe);
			break;
		}
		case EWrecktideChestTab::Journal:
		{
			MenuSubtitleText = TEXT("Journal");
			MenuHintText = TEXT("Enter follows a line   Left / Right page   Tab close");
			ChestJournalKeys.Reset();
			// One read API for the journal, owned by the mission logic (C19 + C23.3): the quest
			// lane's model has SEVERAL active missions with one tracked, so every active line is
			// listed and the tracked one carries the beacon glyph.
			TArray<WrecktideMissionLogic::FMissionJournalEntry> Entries;
			WrecktideMissionLogic::GetJournalEntries(ChestState, Entries);
			bool bWroteActive = false;
			bool bWroteAvailable = false;
			bool bWroteDone = false;
			for (const WrecktideMissionLogic::FMissionJournalEntry& Entry : Entries)
			{
				if (Entry.State == WrecktideMissionLogic::EMissionEntryState::Active && !bWroteActive)
				{
					AddRow(TEXT("At hand"), ActNone, FString(), FString(), false);
					bWroteActive = true;
				}
				else if (Entry.State == WrecktideMissionLogic::EMissionEntryState::Available && !bWroteAvailable)
				{
					AddRow(TEXT("On offer"), ActNone, FString(), FString(), false);
					bWroteAvailable = true;
				}
				else if (Entry.State == WrecktideMissionLogic::EMissionEntryState::Completed && !bWroteDone)
				{
					AddRow(TEXT("Behind you"), ActNone, FString(), FString(), false);
					bWroteDone = true;
				}

				// The beacon glyph marks the tracked line; only an ACTIVE line can be followed.
				const bool bFollowable = Entry.State == WrecktideMissionLogic::EMissionEntryState::Active;
				FString SubLine = Entry.Objective.IsEmpty()
					? FString::Printf(TEXT("%s"), *Entry.Giver)
					: FString::Printf(TEXT("%s   %s"), *Entry.Giver, *Entry.Objective);
				if (Entry.bTracked)
				{
					// In the SUB line, not the value column: a value beside a mission title squeezed
					// the title off the row at panel width (capture, 2026-09-06). The beacon glyph in
					// front of the title is what marks the followed line.
					SubLine += TEXT("   (following)");
				}
				AddRow(FString::Printf(TEXT("%s%s"), Entry.bTracked ? TEXT("* ") : TEXT(""), *Entry.Title),
					ActChestTrackBase + ChestJournalKeys.Num(), FString(), SubLine, bFollowable);
				ChestJournalKeys.Add(Entry.Row);
			}
			if (Entries.Num() == 0)
			{
				AddRow(TEXT("Nothing written yet"), ActNone, FString(),
					TEXT("Find the quartermaster in the camp."), false);
			}
			break;
		}
		case EWrecktideChestTab::Gold:
		default:
		{
			MenuSubtitleText = TEXT("Ledger");
			MenuHintText = TEXT("Left / Right page   Tab close");
			const UEconomyComponent* ChestEconomy = ChestState ? ChestState->GetEconomy() : nullptr;
			const USkillComponent* ChestSkill = ChestState ? ChestState->GetSkill() : nullptr;
			const int32 NotorietyLevel = ChestSkill ? ChestSkill->GetNotorietyLevel() : 1;
			AddRow(TEXT("Gold"), ActNone,
				FString::Printf(TEXT("%lld"), ChestEconomy ? ChestEconomy->GetGoldTotal() : 0),
				FString(), false);
			AddRow(TEXT("Notoriety"), ActNone, FString::Printf(TEXT("Lv %d"), NotorietyLevel), FString(), false);
			if (ChestSkill)
			{
				AddRow(TEXT("To the next level"), ActNone,
					FString::Printf(TEXT("%lld XP"), FMath::Max<int64>(0,
						ChestSkill->GetNotorietyXPForLevel(NotorietyLevel + 1) - ChestSkill->GetNotorietyXP())),
					FString(), false);
			}
			int32 MissionsDone = 0;
			int32 BountiesDone = 0;
			if (ChestProgress && ChestData)
			{
				for (const TPair<FName, const FMissionTableRow*>& Mission : ChestData->GetMissionRows())
				{
					MissionsDone += ChestProgress->GetQuestFlag(WrecktideMissionLogic::DoneKey(Mission.Key)) > 0 ? 1 : 0;
				}
				for (const TPair<FName, const FQuestTableRow*>& Bounty : ChestData->GetQuestRows())
				{
					BountiesDone += ChestProgress->GetQuestFlag(WrecktideQuestLogic::StateKey(Bounty.Key))
						== WrecktideQuestLogic::QuestState_Completed ? 1 : 0;
				}
			}
			AddRow(TEXT("Missions done"), ActNone, FString::FromInt(MissionsDone), FString(), false);
			AddRow(TEXT("Bounties turned in"), ActNone, FString::FromInt(BountiesDone), FString(), false);
			break;
		}
		}
		break;
	}

	default:
		MenuTitleText.Reset();
		MenuHintText.Reset();
		break;
	}

	// The selection must always sit on a row something can happen to.
	if (!MenuRows.IsValidIndex(MenuSelectedIndex) || !MenuRows[MenuSelectedIndex].bSelectable)
	{
		MenuSelectedIndex = INDEX_NONE;
		for (int32 Index = 0; Index < MenuRows.Num(); ++Index)
		{
			if (MenuRows[Index].bSelectable)
			{
				MenuSelectedIndex = Index;
				break;
			}
		}
		if (MenuSelectedIndex == INDEX_NONE)
		{
			MenuSelectedIndex = 0;
		}
	}

	PublishMenu();
}

// ---------------------------------------------------------------------------------------------
// Menu tree: navigation and actions
// ---------------------------------------------------------------------------------------------

void ACombatPlayerController::MenuNavigate(int32 Delta)
{
	if (!IsAnyMenuOpen() || MenuRows.Num() == 0 || Delta == 0)
	{
		return;
	}

	const int32 Step = Delta > 0 ? 1 : -1;
	int32 Index = MenuSelectedIndex;
	// Walk over captions; stop at the ends rather than wrapping, so holding a key parks on a row.
	for (int32 Guard = 0; Guard < MenuRows.Num(); ++Guard)
	{
		Index += Step;
		if (!MenuRows.IsValidIndex(Index))
		{
			return;
		}
		if (MenuRows[Index].bSelectable)
		{
			MenuSelectedIndex = Index;
			UWrecktideAudio::PlaySfx(this, TEXT("ui_move"));
			// A cancelled overwrite question (or buy question) must not follow the cursor.
			PendingOverwriteSlot = 0;
			PendingOutfitBuyIndex = INDEX_NONE;
			RebuildMenuRows();
			return;
		}
	}
}

void ACombatPlayerController::SelectPreviousPauseOption()
{
	MenuNavigate(-1);
}

void ACombatPlayerController::SelectNextPauseOption()
{
	MenuNavigate(1);
}

void ACombatPlayerController::AdjustPreviousOptionValue()
{
	MenuAdjust(-1);
}

void ACombatPlayerController::AdjustNextOptionValue()
{
	MenuAdjust(1);
}

void ACombatPlayerController::ConfirmPauseSelection()
{
	MenuConfirm();
}

void ACombatPlayerController::MenuSetSelected(int32 Index)
{
	if (!IsAnyMenuOpen() || !MenuRows.IsValidIndex(Index) || !MenuRows[Index].bSelectable
		|| Index == MenuSelectedIndex)
	{
		return;
	}

	MenuSelectedIndex = Index;
	PendingOverwriteSlot = 0;
	PendingOutfitBuyIndex = INDEX_NONE;
	UWrecktideAudio::PlaySfx(this, TEXT("ui_move"));
	RebuildMenuRows();
}

void ACombatPlayerController::MenuHover(int32 Index)
{
	// Slate rebuilds the row buttons on every model change, and a freshly created button under a
	// STATIONARY mouse fires OnHovered again: Left/Right on the Colourway row rebuilt the rows and
	// the hover immediately dragged the selection back to whatever row the mouse was resting on.
	// Only a mouse that actually moved may move the selection.
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	const bool bHaveMouse = GetMousePosition(MouseX, MouseY);
	if (bHaveMouse)
	{
		const FVector2D MousePosition(MouseX, MouseY);
		if (bHasMenuHoverMouse && FVector2D::Distance(MousePosition, LastMenuHoverMouse) < MenuHoverMoveTolerance)
		{
			return;
		}
		LastMenuHoverMouse = MousePosition;
		bHasMenuHoverMouse = true;
	}

	MenuSetSelected(Index);
}

void ACombatPlayerController::MenuClick(int32 Index)
{
	if (!IsAnyMenuOpen() || !MenuRows.IsValidIndex(Index) || !MenuRows[Index].bSelectable)
	{
		return;
	}

	// Select first: a click and a keyboard confirm then run the exact same code.
	MenuSelectedIndex = Index;
	MenuConfirm();
}

bool ACombatPlayerController::MenuChoose(const FString& Label)
{
	if (!IsAnyMenuOpen())
	{
		return false;
	}

	for (int32 Index = 0; Index < MenuRows.Num(); ++Index)
	{
		if (MenuRows[Index].bSelectable && MenuRows[Index].Label.Equals(Label))
		{
			MenuSelectedIndex = Index;
			MenuConfirm();
			return true;
		}
	}
	return false;
}

bool ACombatPlayerController::TitleChoose(const FString& Label)
{
	return IsTitleMenuOpen() && MenuChoose(Label);
}

void ACombatPlayerController::MenuOpenScreen(EWrecktideMenuScreen Screen)
{
	if (!IsAnyMenuOpen())
	{
		return;
	}

	if (Screen == EWrecktideMenuScreen::SaveVoyage || Screen == EWrecktideMenuScreen::LoadVoyage)
	{
		RefreshSlotSummaries();
	}
	else if (Screen == EWrecktideMenuScreen::Options)
	{
		EnterOptionsScreen();
	}
	else if (Screen == EWrecktideMenuScreen::Wardrobe)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UWrecktideWardrobeSubsystem* Wardrobe = GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>())
			{
				WardrobeVariant = Wardrobe->GetCurrentVariant(this);
				Wardrobe->BeginPreview(this);
			}
		}
	}

	MenuStack.Add(Screen);
	MenuSelectedIndex = 0;
	PendingOverwriteSlot = 0;
	RebuildMenuRows();
}

void ACombatPlayerController::MenuBack()
{
	if (!IsAnyMenuOpen())
	{
		return;
	}

	// An overwrite question is the innermost thing on screen: Esc answers it, not the screen.
	if (PendingOverwriteSlot > 0)
	{
		PendingOverwriteSlot = 0;
		RebuildMenuRows();
		return;
	}

	// Same for a pending buy question on the wardrobe screen.
	if (PendingOutfitBuyIndex != INDEX_NONE)
	{
		PendingOutfitBuyIndex = INDEX_NONE;
		RebuildMenuRows();
		return;
	}

	if (MenuStack.Num() > 1)
	{
		const EWrecktideMenuScreen Leaving = MenuStack.Last();
		MenuStack.Pop();
		if (Leaving == EWrecktideMenuScreen::Wardrobe)
		{
			if (UGameInstance* GameInstance = GetGameInstance())
			{
				if (UWrecktideWardrobeSubsystem* Wardrobe = GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>())
				{
					Wardrobe->EndPreview(this);
				}
			}
			// The wardrobe camera replaced the scenic one; put the bay back behind the title.
			if (IsTitleMenuOpen())
			{
				EnsureScenicCamera();
			}
		}
		MenuSelectedIndex = 0;
		UWrecktideAudio::PlaySfx(this, TEXT("ui_confirm"));
		RebuildMenuRows();
		return;
	}

	// At the root: the pause tree closes, the title menu stays (there is nothing behind it).
	if (IsPauseMenuOpen())
	{
		CloseMenus();
	}
}

void ACombatPlayerController::MenuConfirm()
{
	// The chest pages act on their own ids and nothing else (spec C18, C19).
	if (IsInventoryOpen() && MenuActions.IsValidIndex(MenuSelectedIndex))
	{
		const int32 ChestAction = MenuActions[MenuSelectedIndex];
		AWrecktidePlayerState* ChestState = GetPlayerState<AWrecktidePlayerState>();
		if (ChestAction == PistolChestUnassign && ChestState)
		{
			ChestState->AssignSlot(2, NAME_None);
			UWrecktideAudio::PlaySfx(this, TEXT("ui_confirm"));
			RebuildMenuRows();
			return;
		}
		if (ChestAction >= PistolChestWieldBase && ChestAction < PistolChestWieldBase + 100 && ChestState)
		{
			const int32 GunIndex = ChestAction - PistolChestWieldBase;
			if (ChestGunRows.IsValidIndex(GunIndex))
			{
				const bool bAssigned = ChestState->AssignSlot(2, ChestGunRows[GunIndex]);
				UWrecktideAudio::PlaySfx(this, bAssigned ? TEXT("ui_confirm") : TEXT("ui_move"));
				if (!bAssigned)
				{
					if (AWrecktideHUD* ChestHUD = Cast<AWrecktideHUD>(GetHUD()))
					{
						ChestHUD->ShowToast(TEXT("Not yet: your name is not feared enough for that gun."));
					}
				}
				RebuildMenuRows();
			}
			return;
		}
		if (ChestAction == ActChestUnassign && ChestState)
		{
			ChestState->AssignSlot(1, NAME_None);
			UWrecktideAudio::PlaySfx(this, TEXT("ui_confirm"));
			RebuildMenuRows();
			return;
		}
		if (ChestAction == ActChestWardrobe)
		{
			// The change room is a pause-tree screen and owns the camera while it runs, so the chest
			// closes behind it rather than trying to hold two screens at once.
			CloseMenus();
			OpenMenuRoot(EWrecktideMenuScreen::Pause);
			MenuOpenScreen(EWrecktideMenuScreen::Wardrobe);
			return;
		}
		if (ChestAction >= ActChestWieldBase && ChestAction < ActChestWieldBase + 100 && ChestState)
		{
			const int32 MeleeIndex = ChestAction - ActChestWieldBase;
			if (ChestMeleeRows.IsValidIndex(MeleeIndex))
			{
				const FName Row = ChestMeleeRows[MeleeIndex];
				const bool bAssigned = ChestState->AssignSlot(1, Row);
				UWrecktideAudio::PlaySfx(this, bAssigned ? TEXT("ui_confirm") : TEXT("ui_move"));
				if (!bAssigned)
				{
					if (AWrecktideHUD* ChestHUD = Cast<AWrecktideHUD>(GetHUD()))
					{
						ChestHUD->ShowToast(TEXT("Not yet: your name is not feared enough for that blade."));
					}
				}
				RebuildMenuRows();
			}
			return;
		}
		if (ChestAction >= ActChestTrackBase && ChestAction < ActChestTrackBase + 100 && ChestState)
		{
			const int32 JournalIndex = ChestAction - ActChestTrackBase;
			if (ChestJournalKeys.IsValidIndex(JournalIndex))
			{
				// The mission logic owns what is tracked (it moves the beacon and the HUD line with
				// it) and refuses fail-closed on a line that is not active.
				const FName Row = ChestJournalKeys[JournalIndex];
				const bool bFollowed = WrecktideMissionLogic::SetTrackedMission(ChestState, Row);
				UWrecktideAudio::PlaySfx(this, bFollowed ? TEXT("ui_confirm") : TEXT("ui_move"));
				UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-CHEST following %s ok=%d"), *Row.ToString(), bFollowed ? 1 : 0);
				RebuildMenuRows();
			}
			return;
		}
		if (ChestAction == ActNone)
		{
			return;
		}
	}

	if (!IsAnyMenuOpen() || !MenuActions.IsValidIndex(MenuSelectedIndex))
	{
		return;
	}

	const int32 Action = MenuActions[MenuSelectedIndex];
	if (Action == ActNone)
	{
		return;
	}
	UWrecktideAudio::PlaySfx(this, TEXT("ui_confirm"));

	switch (Action)
	{
	case ActContinue:
	{
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-TITLE continue"));
		// The startup load parks the saved position and normally applies it while the title is up;
		// if the pawn did not exist in time the pending position is still waiting, and Continue is
		// the moment to put the pirate back where he stood. A consumed position makes this a no-op.
		if (AWrecktidePlayerState* WrecktideState = GetPlayerState<AWrecktidePlayerState>())
		{
			WrecktideState->PlacePawnFromSave();
		}
		CloseMenus();
		return;
	}

	case ActResume:
		CloseMenus();
		return;

	case ActBack:
		MenuBack();
		return;

	case ActLoadScreen:
		MenuOpenScreen(EWrecktideMenuScreen::LoadVoyage);
		return;

	case ActSaveScreen:
		MenuOpenScreen(EWrecktideMenuScreen::SaveVoyage);
		return;

	case ActOptionsScreen:
		MenuOpenScreen(EWrecktideMenuScreen::Options);
		return;

	case ActWardrobeScreen:
		MenuOpenScreen(EWrecktideMenuScreen::Wardrobe);
		return;

	case ActControlsScreen:
		MenuOpenScreen(EWrecktideMenuScreen::Controls);
		return;

	case ActCreditsScreen:
		MenuOpenScreen(EWrecktideMenuScreen::Credits);
		return;

	case ActApplyOptions:
		ApplyPendingOptions();
		RebuildMenuRows();
		return;

	case ActNewVoyage:
	{
		AWrecktidePlayerState* WrecktideState = GetPlayerState<AWrecktidePlayerState>();
		if (!WrecktideState || !WrecktideState->AbandonSaveForNewGame())
		{
			// The slot is still there (locked file, missing state): restarting now would silently
			// continue the old game. Stay on the title and say so.
			UE_LOG(LogWrecktide, Warning, TEXT("WRECKTIDE-TITLE newgame refused: save slot not cleared (state=%d)"),
				WrecktideState != nullptr ? 1 : 0);
			MenuHintText = TEXT("Could not clear the old save. Quit and delete it by hand.");
			PublishMenu();
			return;
		}
		UWorld* World = GetWorld();
		const FString MapName = World ? World->GetOutermost()->GetName() : FString();
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-TITLE newgame restart map=%s"), *MapName);
		bSkipTitleOnce = true;
		CloseMenus();
		if (World && !MapName.IsEmpty())
		{
			UGameplayStatics::OpenLevel(World, FName(*MapName));
		}
		return;
	}

	case ActMainMenu:
	{
		// Back to the title state: the same level restart New Voyage uses, but the save survives and
		// the title is allowed this time. The PlayerState's EndPlay flush writes anything pending.
		UWorld* World = GetWorld();
		const FString MapName = World ? World->GetOutermost()->GetName() : FString();
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-PAUSE main-menu restart map=%s"), *MapName);
		bSkipTitleOnce = false;
		CloseMenus();
		if (World && !MapName.IsEmpty())
		{
			UGameplayStatics::OpenLevel(World, FName(*MapName));
		}
		return;
	}

	case ActQuit:
		// The PlayerState's EndPlay flush is what saves; quitting through the engine path lets it run.
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-PAUSE quit-to-desktop"));
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		return;

	case ActColourway:
	{
		// Enter on the colourway row wears the outfit the row is SHOWING in the shown colourway.
		// On a fresh save nothing was ever selected, so the persisted id is None; the effective id
		// (the default pirate) is used instead, or this row would silently do nothing.
		UGameInstance* GameInstance = GetGameInstance();
		UWrecktideWardrobeSubsystem* Wardrobe = GameInstance
			? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
		if (Wardrobe)
		{
			const TArray<FWrecktideOutfitInfo> Outfits = Wardrobe->GetOutfits();
			const FName TargetId = GetColourwayOutfitId(Outfits, Wardrobe->GetEffectiveOutfitId(this));
			const bool bWorn = !TargetId.IsNone() && Wardrobe->SetOutfit(this, TargetId, WardrobeVariant);
			UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-WARDROBE colourway outfit=%s variant=%d ok=%d"),
				*TargetId.ToString(), WardrobeVariant, bWorn ? 1 : 0);
		}
		RebuildMenuRows();
		return;
	}

	default:
		break;
	}

	if (Action >= ActOutfitBase)
	{
		const int32 OutfitIndex = Action - ActOutfitBase;
		UGameInstance* GameInstance = GetGameInstance();
		UWrecktideWardrobeSubsystem* Wardrobe = GameInstance
			? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
		const TArray<FWrecktideOutfitInfo> Outfits = Wardrobe ? Wardrobe->GetOutfits() : TArray<FWrecktideOutfitInfo>();
		if (Wardrobe && Outfits.IsValidIndex(OutfitIndex))
		{
			const FWrecktideOutfitInfo& Outfit = Outfits[OutfitIndex];
			if (!Outfit.bOwned)
			{
				BuyOutfitRow(OutfitIndex, Outfit);
				return;
			}
			const bool bWorn = Wardrobe->SetOutfit(this, Outfit.Id, WardrobeVariant);
			UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-WARDROBE set outfit=%s variant=%d ok=%d"),
				*Outfit.Id.ToString(), WardrobeVariant, bWorn ? 1 : 0);
		}
		PendingOutfitBuyIndex = INDEX_NONE;
		RebuildMenuRows();
		return;
	}

	if (Action >= ActOptionBase)
	{
		// Enter on a toggle flips it; on a slider it is a no-op (Left/Right is the way).
		const int32 Option = Action - ActOptionBase;
		if (Option == OptInvert)
		{
			PendingOptions.bInvertLookY = !PendingOptions.bInvertLookY;
			FWrecktideOptions::Set(PendingOptions);
			RebuildMenuRows();
		}
		else if (Option == OptVSync)
		{
			bPendingVSync = !bPendingVSync;
			RebuildMenuRows();
		}
		else if (Option == OptMinimapNorthUp)
		{
			PendingOptions.bMinimapNorthUp = !PendingOptions.bMinimapNorthUp;
			FWrecktideOptions::Set(PendingOptions);
			ApplyMinimapOption();
			RebuildMenuRows();
		}
		return;
	}

	if (Action >= ActSlotBase)
	{
		const int32 SlotIndex = Action - ActSlotBase;
		if (GetMenuScreen() == EWrecktideMenuScreen::SaveVoyage)
		{
			AWrecktidePlayerState* WrecktideState = GetPlayerState<AWrecktidePlayerState>();
			const bool bOccupied = SlotSummaries.IsValidIndex(SlotIndex) && !SlotSummaries[SlotIndex].Equals(TEXT("Empty"));
			if (bOccupied && PendingOverwriteSlot != SlotIndex)
			{
				// Ask once. The second Enter on the same slot goes through.
				PendingOverwriteSlot = SlotIndex;
				RebuildMenuRows();
				return;
			}
			const bool bSaved = WrecktideState && WrecktideState->SaveToManualSlot(SlotIndex);
			PendingOverwriteSlot = 0;
			// Never cached across a save: re-read every summary from disk right here.
			RefreshSlotSummaries();
			// Rebuild first (it resets the hint), then say what just happened.
			RebuildMenuRows();
			MenuHintText = bSaved
				? FString::Printf(TEXT("Saved to Slot %d"), SlotIndex)
				: FString::Printf(TEXT("Could not write Slot %d"), SlotIndex);
			PublishMenu();
			return;
		}
		if (GetMenuScreen() == EWrecktideMenuScreen::LoadVoyage)
		{
			LoadSlotAndPlay(SlotIndex);
			return;
		}
	}
}

void ACombatPlayerController::MenuAdjust(int32 Delta)
{
	// On the chest, Left and Right turn the PAGE: no row there carries a value to nudge.
	if (IsInventoryOpen())
	{
		StepChestTab(Delta);
		return;
	}

	if (!IsAnyMenuOpen() || Delta == 0 || !MenuActions.IsValidIndex(MenuSelectedIndex))
	{
		return;
	}

	const int32 Action = MenuActions[MenuSelectedIndex];
	const int32 Step = Delta > 0 ? 1 : -1;

	if (Action == ActColourway)
	{
		UGameInstance* GameInstance = GetGameInstance();
		UWrecktideWardrobeSubsystem* Wardrobe = GameInstance
			? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
		const TArray<FWrecktideOutfitInfo> Outfits = Wardrobe ? Wardrobe->GetOutfits() : TArray<FWrecktideOutfitInfo>();
		// Only the earned colourways are in the cycle: the locked ones are skipped, not shown greyed.
		const int32 VariantCount = Wardrobe
			? FMath::Max(1, Wardrobe->GetUnlockedVariantCount(Wardrobe->GetPlayerGatingLevel(this)))
			: 1;
		WardrobeVariant = (WardrobeVariant + Step + VariantCount) % VariantCount;
		UWrecktideAudio::PlaySfx(this, TEXT("ui_move"));
		RebuildMenuRows();
		return;
	}

	if (Action < ActOptionBase || Action >= ActOutfitBase)
	{
		return;
	}

	const int32 Option = Action - ActOptionBase;
	if (Option < 0 || Option >= OptCount)
	{
		return;
	}

	switch (Option)
	{
	case OptMaster:
		PendingOptions.MasterVolume = FMath::Clamp(PendingOptions.MasterVolume + Step * VolumeStep, 0, 100);
		break;
	case OptEffects:
		PendingOptions.EffectsVolume = FMath::Clamp(PendingOptions.EffectsVolume + Step * VolumeStep, 0, 100);
		break;
	case OptAmbience:
		PendingOptions.AmbienceVolume = FMath::Clamp(PendingOptions.AmbienceVolume + Step * VolumeStep, 0, 100);
		break;
	case OptSensitivity:
		PendingOptions.MouseSensitivity = FMath::Clamp(PendingOptions.MouseSensitivity + Step * SensitivityStep, 0.5f, 2.0f);
		break;
	case OptInvert:
		PendingOptions.bInvertLookY = !PendingOptions.bInvertLookY;
		break;
	case OptWindowMode:
		PendingWindowMode = FMath::Clamp(PendingWindowMode + Step, 0, 2);
		break;
	case OptResolution:
		if (ResolutionChoices.Num() > 0)
		{
			PendingResolutionIndex = FMath::Clamp(PendingResolutionIndex + Step, 0, ResolutionChoices.Num() - 1);
		}
		break;
	case OptVSync:
		bPendingVSync = !bPendingVSync;
		break;
	case OptQuality:
		PendingQuality = FMath::Clamp(PendingQuality + Step, 0, 3);
		break;
	case OptMinimapNorthUp:
		PendingOptions.bMinimapNorthUp = !PendingOptions.bMinimapNorthUp;
		break;
	default:
		break;
	}

	// Audio and look values take effect while the player is still on the row, so he can hear and feel
	// what he is choosing; Apply is what writes them to disk.
	FWrecktideOptions::Set(PendingOptions);
	ApplyMinimapOption();
	UWrecktideAudio::PlaySfx(this, TEXT("ui_move"));
	RebuildMenuRows();
}

// ---------------------------------------------------------------------------------------------
// Options, slots, wardrobe helpers
// ---------------------------------------------------------------------------------------------

void ACombatPlayerController::EnterOptionsScreen()
{
	PendingOptions = FWrecktideOptions::Get();

	ResolutionChoices.Reset();
	TArray<FIntPoint> Supported;
	// The list lives on UKismetSystemLibrary in 5.6, not on UGameUserSettings.
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported);
	ResolutionChoices = Supported;
	// The windowed modes need small sizes too, which the fullscreen list does not always carry.
	for (const FIntPoint& Extra : { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080) })
	{
		ResolutionChoices.AddUnique(Extra);
	}
	ResolutionChoices.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X == B.X ? A.Y < B.Y : A.X < B.X;
	});

	PendingResolutionIndex = 0;
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		PendingWindowMode = WindowModeToIndex(Settings->GetFullscreenMode());
		bPendingVSync = Settings->IsVSyncEnabled();
		const int32 Level = Settings->GetOverallScalabilityLevel();
		PendingQuality = Level < 0 ? 3 : FMath::Clamp(Level, 0, 3);
		const FIntPoint Current = Settings->GetScreenResolution();
		const int32 Found = ResolutionChoices.IndexOfByKey(Current);
		PendingResolutionIndex = Found == INDEX_NONE ? FMath::Max(0, ResolutionChoices.Num() - 1) : Found;
	}
}

void ACombatPlayerController::ApplyMinimapOption()
{
	// The HUD owns the radar; this is the one place the option reaches it.
	if (AWrecktideHUD* WrecktideHUD = Cast<AWrecktideHUD>(GetHUD()))
	{
		WrecktideHUD->SetMinimapNorthUp(PendingOptions.bMinimapNorthUp);
	}
}

void ACombatPlayerController::ApplyPendingOptions()
{
	FWrecktideOptions::Set(PendingOptions);
	FWrecktideOptions::Save();
	ApplyMinimapOption();

	// Probe runs keep their launch window: resizing a -windowed -ResX=1280 harness run mid probe
	// would break every capture that follows it. The audio and look values above still persist.
	if (AWrecktidePlayerState::IsProbeRun())
	{
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-OPTIONS apply audio+look only (probe run keeps its window)"));
		return;
	}

	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		UE_LOG(LogWrecktide, Warning, TEXT("WRECKTIDE-OPTIONS apply: no GameUserSettings, video left alone"));
		return;
	}

	Settings->SetFullscreenMode(WindowModeFromIndex(PendingWindowMode));
	if (ResolutionChoices.IsValidIndex(PendingResolutionIndex))
	{
		Settings->SetScreenResolution(ResolutionChoices[PendingResolutionIndex]);
	}
	Settings->SetVSyncEnabled(bPendingVSync);
	Settings->SetOverallScalabilityLevel(PendingQuality);
	Settings->ApplySettings(false);
	Settings->SaveSettings();

	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-OPTIONS apply window=%d res=%dx%d vsync=%d quality=%d"),
		PendingWindowMode,
		ResolutionChoices.IsValidIndex(PendingResolutionIndex) ? ResolutionChoices[PendingResolutionIndex].X : 0,
		ResolutionChoices.IsValidIndex(PendingResolutionIndex) ? ResolutionChoices[PendingResolutionIndex].Y : 0,
		bPendingVSync ? 1 : 0, PendingQuality);
}

void ACombatPlayerController::BuyOutfitRow(int32 OutfitIndex, const FWrecktideOutfitInfo& Outfit)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWrecktideWardrobeSubsystem* Wardrobe = GameInstance
		? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
	const AWrecktidePlayerState* WrecktideState = GetPlayerState<AWrecktidePlayerState>();
	const UEconomyComponent* Economy = WrecktideState ? WrecktideState->GetEconomy() : nullptr;
	if (!Wardrobe)
	{
		return;
	}

	// Say what is missing BEFORE asking to confirm: a question the player cannot answer is noise.
	const int32 Level = Wardrobe->GetPlayerGatingLevel(this);
	const int64 Gold = Economy ? Economy->GetGoldTotal() : 0;
	FString Message;
	if (Level < Outfit.RequiredLevel)
	{
		Message = FString::Printf(TEXT("%s needs level %d; you are level %d"),
			*Outfit.DisplayName.ToString(), Outfit.RequiredLevel, Level);
	}
	else if (Gold < Outfit.Price)
	{
		Message = FString::Printf(TEXT("%s costs %lld gold; you have %lld"),
			*Outfit.DisplayName.ToString(), Outfit.Price, Gold);
	}
	else if (PendingOutfitBuyIndex != OutfitIndex)
	{
		// Ask once. The second Enter on the same row goes through, exactly like an overwrite.
		PendingOutfitBuyIndex = OutfitIndex;
		RebuildMenuRows();
		return;
	}
	else
	{
		const EWrecktideOutfitBuyResult Result = Wardrobe->BuyOutfit(this, Outfit.Id);
		PendingOutfitBuyIndex = INDEX_NONE;
		if (Result == EWrecktideOutfitBuyResult::Bought || Result == EWrecktideOutfitBuyResult::AlreadyOwned)
		{
			Wardrobe->SetOutfit(this, Outfit.Id, WardrobeVariant);
			Message = FString::Printf(TEXT("Bought the %s"), *Outfit.DisplayName.ToString());
		}
		else
		{
			Message = FString::Printf(TEXT("The tailor kept the %s"), *Outfit.DisplayName.ToString());
		}
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-WARDROBE buy outfit=%s result=%d level=%d gold=%lld"),
			*Outfit.Id.ToString(), static_cast<int32>(Result), Level, Gold);
	}

	PendingOutfitBuyIndex = INDEX_NONE;
	// Rebuild first (it resets the hint), then say what just happened.
	RebuildMenuRows();
	MenuHintText = Message;
	PublishMenu();
}

FName ACombatPlayerController::GetColourwayOutfitId(const TArray<FWrecktideOutfitInfo>& Outfits, FName EffectiveWornId) const
{
	// One definition for the row's VALUE and for what Enter on it wears, so the two can never
	// disagree: the outfit under the cursor, else the one actually worn, else the first row.
	if (Outfits.IsValidIndex(MenuSelectedIndex))
	{
		return Outfits[MenuSelectedIndex].Id;
	}
	if (!EffectiveWornId.IsNone())
	{
		return EffectiveWornId;
	}
	return Outfits.Num() > 0 ? Outfits[0].Id : NAME_None;
}

void ACombatPlayerController::RefreshSlotSummaries()
{
	SlotSummaries.Reset();
	SlotSummaries.SetNum(AWrecktidePlayerState::GetManualSlotCount() + 1);
	SlotSummaries[0] = AWrecktidePlayerState::GetSlotSummary(this, AWrecktidePlayerState::GetSaveSlot());
	for (int32 Slot = 1; Slot <= AWrecktidePlayerState::GetManualSlotCount(); ++Slot)
	{
		SlotSummaries[Slot] = AWrecktidePlayerState::GetSlotSummary(this, AWrecktidePlayerState::GetManualSlot(Slot));
	}
}

bool ACombatPlayerController::LoadSlotAndPlay(int32 SlotIndex)
{
	AWrecktidePlayerState* WrecktideState = GetPlayerState<AWrecktidePlayerState>();
	if (!WrecktideState)
	{
		return false;
	}

	const bool bLoaded = SlotIndex == 0 ? WrecktideState->LoadAutosaveSlot() : WrecktideState->LoadManualSlot(SlotIndex);
	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-SAVE menu load slot=%d ok=%d"), SlotIndex, bLoaded ? 1 : 0);
	if (!bLoaded)
	{
		MenuHintText = TEXT("That voyage could not be read.");
		PublishMenu();
		return false;
	}

	CloseMenus();
	return true;
}

// ---------------------------------------------------------------------------------------------
// Scenic title camera
// ---------------------------------------------------------------------------------------------

void ACombatPlayerController::EnsureScenicCamera()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!ScenicCamera)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FRotator LookRotation = (ScenicCameraLookAt - ScenicCameraLocation).Rotation();
		ScenicCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), ScenicCameraLocation, LookRotation, SpawnParams);
	}
	if (!ScenicCamera)
	{
		UE_LOG(LogWrecktide, Warning, TEXT("WRECKTIDE-TITLE scenic camera failed to spawn, keeping the pawn view"));
		return;
	}

	// Blend 0: the view must be in place BEFORE the world freezes, a blend would never finish.
	SetViewTargetWithBlend(ScenicCamera, 0.0f);
}

void ACombatPlayerController::ClearScenicCamera()
{
	// The unhide runs OUTSIDE the camera guard: the pawn is hidden even when the scenic camera
	// failed to spawn, and returning early here left an invisible pirate for the rest of the run.
	if (bPawnHiddenForTitle)
	{
		if (APawn* OwnPawn = GetPawn())
		{
			OwnPawn->SetActorHiddenInGame(false);
		}
		bPawnHiddenForTitle = false;
	}

	if (!ScenicCamera)
	{
		return;
	}

	if (APawn* OwnPawn = GetPawn())
	{
		SetViewTargetWithBlend(OwnPawn, 0.0f);
	}
	ScenicCamera->Destroy();
	ScenicCamera = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Title menu
// ---------------------------------------------------------------------------------------------
bool ACombatPlayerController::bSkipTitleOnce = false;

bool ACombatPlayerController::ShouldShowTitleMenu()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	if (FParse::Param(CommandLine, TEXT("NoTitle")))
	{
		return false;
	}
	if (bSkipTitleOnce)
	{
		// consumed exactly once: the world after New Voyage goes straight into play
		bSkipTitleOnce = false;
		UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-TITLE skipped once after New Voyage"));
		return false;
	}
	FString MenuProbeMode;
	if (FParse::Param(CommandLine, TEXT("MenuProbe")) || FParse::Value(CommandLine, TEXT("MenuProbe="), MenuProbeMode))
	{
		return true;
	}
	// every other probe switch means a machine is driving: no menu in the way. IsProbeRun, not
	// IsPersistenceSuppressed: the save probe keeps persistence ON for its own slot and still must
	// not be paused under a title menu (first save_write run after the menu landed: no write at all).
	return !AWrecktidePlayerState::IsProbeRun();
}

void ACombatPlayerController::OpenTitleMenu()
{
	if (IsAnyMenuOpen())
	{
		// Something else owns the screen right now. Keep asking rather than dropping the title for
		// the session: this timer is a world timer, so it simply waits out the paused menu and
		// fires again once the player closes it.
		GetWorldTimerManager().SetTimer(TitleMenuTimer, this, &ACombatPlayerController::OpenTitleMenu, 0.25f, false);
		return;
	}
	// Pausing on the very first frames froze auto exposure at its start value (a dark title screen in
	// the 2026-09-05 captures): wait for 1.5 s of world time AND 60 rendered frames, then freeze.
	UWorld* World = GetWorld();
	if (World && (World->GetTimeSeconds() < 1.5f || GFrameCounter < TitleArmFrame + 60))
	{
		GetWorldTimerManager().SetTimer(TitleMenuTimer, this, &ACombatPlayerController::OpenTitleMenu, 0.25f, false);
		return;
	}

	// The scenic view goes in BEFORE the pause, and then gets frames of its own: a camera cut that
	// freezes in the same breath leaves the temporal history smeared across the shot (first capture,
	// 2026-09-06). Warm up for 30 rendered frames, THEN pause.
	if (!bScenicWarmedUp)
	{
		EnsureScenicCamera();
		if (APawn* OwnPawn = GetPawn())
		{
			// The pirate's own floating life bar sat dead centre of the scenic shot.
			OwnPawn->SetActorHiddenInGame(true);
			bPawnHiddenForTitle = true;
		}
		bScenicWarmedUp = true;
		ScenicArmFrame = GFrameCounter;
		GetWorldTimerManager().SetTimer(TitleMenuTimer, this, &ACombatPlayerController::OpenTitleMenu, 0.35f, false);
		return;
	}
	if (GFrameCounter < ScenicArmFrame + 30)
	{
		GetWorldTimerManager().SetTimer(TitleMenuTimer, this, &ACombatPlayerController::OpenTitleMenu, 0.2f, false);
		return;
	}

	bTitlePending = false;
	OpenMenuRoot(EWrecktideMenuScreen::Main);

	UE_LOG(LogWrecktide, Log, TEXT("WRECKTIDE-TITLE open rows=%d paused=%d scenic=%d"),
		MenuRows.Num(), UGameplayStatics::IsGamePaused(this) ? 1 : 0, ScenicCamera != nullptr ? 1 : 0);
}

bool ACombatPlayerController::IsModalMenuOpen() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		if (It->IsShopMenuOpen())
		{
			return true;
		}
	}

	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return true;
		}
	}

	// The quest giver is the fourth canvas modal and was missing from this list: Esc and the mouse
	// both ask here, so without him the pause menu could open on top of his conversation.
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return true;
		}
	}

	for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return true;
		}
	}

	return false;
}
