// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktideRewardSubsystem.h"
#include "Systems/WrecktideAudio.h"

#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/InputComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Data/WrecktideDataRows.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerInput.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSeaSurface.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "Gameplay/WrecktideShipwright.h"
#include "Gameplay/WrecktideSupplyBarrel.h"
#include "Gameplay/WrecktideBoat.h"
#include "Gameplay/WrecktidePatrolBoat.h"
#include "Gameplay/WrecktideMissionBeacon.h"
#include "UI/WrecktideMapView.h"
#include "Gameplay/WrecktideTreasureChest.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Systems/EconomyComponent.h"
#include "Systems/WrecktideOptions.h"
#include "Systems/ProgressComponent.h"
#include "Systems/SkillComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideQuestLogic.h"
#include "Systems/WrecktideWardrobeSubsystem.h"
#include "TimerManager.h"
#include "UI/WrecktideHUD.h"
#include "Variant_Combat/AI/CombatEnemy.h"
#include "Variant_Combat/AI/CombatEnemySpawner.h"
#include "Variant_Combat/CombatCharacter.h"
#include "Variant_Combat/CombatGameMode.h"
#include "Variant_Combat/CombatPlayerController.h"
#include "Variant_Combat/UI/CombatLifeBar.h"
#include "WrecktideGameMode.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"

namespace
{
constexpr float EnemyRespawnDelay = 15.0f;
constexpr int32 MaxAliveEnemies = 6;

/** Every probe run quits by itself after this, verdict or not (below run_probe.ps1's 240s kill). */
constexpr float ProbeWatchdogSeconds = 180.0f;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
/** Chest toolbar probe: settle in world seconds, then sample rendered frames on the core ticker. */
constexpr float ChestButtonProbeSettleSeconds = 10.0f;
constexpr float ChestButtonProbeStepSeconds = 0.5f;
constexpr float ChestButtonProbeQuitDelaySeconds = 2.0f;
#endif

/**
 * Injects a REAL key event into the player input, the same route a keyboard press takes into
 * APlayerController::InputKey. Probes use it so the BINDING is under test, not just the handler
 * behind it (a dead binding is exactly the failure -InputProbe exists for). False when the run has
 * no viewport (a headless editor run), which the caller must fold into its verdict.
 */
bool SendProbeKey(APlayerController* PlayerController, const FKey& Key, EInputEvent Event = IE_Pressed)
{
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
	FViewport* Viewport = ViewportClient ? ViewportClient->Viewport : nullptr;
	if (!Viewport)
	{
		return false;
	}

	const FInputKeyEventArgs Args(Viewport, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
		Key, Event, /*AmountDepressed*/ 1.0f, /*bIsTouchEvent*/ false, /*EventTimestamp*/ 0u);
	// The return value says whether the key was CONSUMED, which most bindings deliberately do not do,
	// and the bound handler runs on the next input flush, not inside this call. So the answer here is
	// only "it went in", and every caller reads the effect a tick later.
	const bool bConsumed = PlayerController->InputKey(Args);
	UE_LOG(LogTemp, Verbose, TEXT("WRECKTIDE-PROBE-KEY %s consumed=%d"), *Key.ToString(), bConsumed ? 1 : 0);
	return true;
}
}

UWrecktideRewardSubsystem* UWrecktideRewardSubsystem::Get(UWorld* World)
{
	return World ? World->GetSubsystem<UWrecktideRewardSubsystem>() : nullptr;
}

void UWrecktideRewardSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!InWorld.IsGameWorld())
	{
		return;
	}

	// The sea is always there (2026-09-05): a looping 2D bed at low volume, one per world. Outside the
	// probe-only block below so a Shipping build hears it too (review 2026-09-05).
	UWrecktideAudio::StartAmbient(this, TEXT("ambient_sea"), 0.35f);

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	// Probe watchdog (review 2026-09-02): a wiring regression that returned before any probe timer
	// was registered left the process running with no verdict. Armed BEFORE the wiring checks so a
	// probe run always ends with an explicit line, whatever happened.
	if (AWrecktidePlayerState::IsProbeRun())
	{
		FTimerHandle WatchdogTimer;
		InWorld.GetTimerManager().SetTimer(WatchdogTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PROBE-WATCHDOG FAIL no verdict after %.0fs, quitting"), ProbeWatchdogSeconds);
			ScheduleProbeQuit(0.5f);
		}), ProbeWatchdogSeconds, false);
	}
#endif

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(&InWorld, 0);
	AWrecktidePlayerState* WrecktidePlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (!WrecktidePlayerState)
	{
		AGameModeBase* GameMode = InWorld.GetAuthGameMode();
		const bool bWrecktideGameMode = GameMode && (GameMode->IsA<ACombatGameMode>() || GameMode->IsA<AWrecktideGameMode>());
		if (bWrecktideGameMode)
		{
			const FString PlayerStateClassName = PlayerController && PlayerController->PlayerState ? PlayerController->PlayerState->GetClass()->GetName() : TEXT("None");
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING PlayerState is %s, GameMode BP override needed"), *PlayerStateClassName);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-SKIP %s stock PlayerState, rewards/stall inactive"), *InWorld.GetMapName());
		}

		return;
	}

	// The visible sea goes in before the gameplay props: it is scenery with no collision, and
	// spawning it here rather than placing it in the level is what keeps a stale serialised
	// BodyInstance from ever making the pirate stand on the water (three python attempts did).
	AWrecktideSeaSurface::SpawnFor(&InWorld);
	SpawnShopStall(&InWorld);
	SpawnQuestBoard(&InWorld);
	SpawnMissionGiver(&InWorld);
	SpawnSmithStall(&InWorld);
	SpawnSupplyBarrel(&InWorld);
	SpawnShipwright(&InWorld);
	SpawnPatrolBoat(&InWorld);
	// The gold sky beam over the active objective. Spawned here with the camp props because it is
	// runtime furniture like they are: nothing is written into Lvl_Island.umap.
	AWrecktideMissionBeacon::SpawnBeacons(&InWorld);

	// ReachSpot missions need to know where the player is standing. A 2 Hz timer rather than a
	// tick: arriving within a 4 m radius is not a frame-accurate question, and the callback drops
	// out immediately unless such a mission is actually active (missions v0, 2026-09-04).
	InWorld.GetTimerManager().SetTimer(MissionLocationTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::TickMissionLocation), 0.5f, true);

	// The shipped arena defers 3 of 4 spawners behind gauntlet triggers; the fun gate needs
	// enemies from second zero. ActivateInteraction is a no-op on immediate/already-active
	// spawners, so this is safe to apply to all of them.
	int32 ActivatedSpawners = 0;
	for (TActorIterator<ACombatEnemySpawner> It(&InWorld); It; ++It)
	{
		It->ActivateInteraction(nullptr);
		++ActivatedSpawners;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-SPAWNERS activated=%d"), ActivatedSpawners);

	// DEV PROBES ONLY. UE 5.6 does NOT filter the command line on a Win64 Shipping build:
	// UE_COMMAND_LINE_USES_ALLOW_LIST defaults to 0 (Misc/CommandLine.h) and nothing on Windows
	// overrides it, so a shipped Wrecktide.exe would parse these verbatim. Several probes call
	// UKismetSystemLibrary::QuitGame and ShopProbe grants gold AND force-levels the player by
	// looping AwardXP until CanEquipWeapon passes, so unguarded these are shipped cheats.
	// These registrations are the ONLY call sites for the Run*Probe bodies, so guarding here makes
	// every probe unreachable in Shipping and Test without touching the bodies or the header
	// declarations (guarding those without matching the header would break the build).
	// Test is excluded too in case a Steam Playtest build ever ships as a Test config.
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (FParse::Param(FCommandLine::Get(), TEXT("FungateProbe")))
	{
		FTimerHandle ProbeTimer;
		InWorld.GetTimerManager().SetTimer(ProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunFungateProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PlateProbe")))
	{
		FTimerHandle PlateProbeStartTimer;
		InWorld.GetTimerManager().SetTimer(PlateProbeStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunPlateProbe), 10.0f, false);
	}

	// -PlateForCapture : dev switch for the visual harness (spec C32). Walks the pirate out to a
	// placed enemy, stands him off its face and turns the camera onto it, so a capture actually has
	// an enemy in frame to photograph the name above its head; the wave-4 near shot framed empty
	// beach. It goes to a PLACED enemy rather than spawning one at the player start, because the
	// start is a pier and a spawn offset from it drops into the sea. It fires at t=9s like every
	// other ForCapture switch (launch_visual's first shot lands around world t=17s) and re-aims at
	// t=15s so the enemy that charged is centred. No verdict and no quit, the -PhotoAt contract.
	if (FParse::Param(FCommandLine::Get(), TEXT("PlateForCapture")))
	{
		FTimerHandle PlateCaptureTimer;
		InWorld.GetTimerManager().SetTimer(PlateCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			ACombatEnemy* CaptureEnemy = nullptr;
			for (TActorIterator<ACombatEnemy> It(CaptureWorld); It; ++It)
			{
				if (It->Stats && It->Stats->IsAlive())
				{
					CaptureEnemy = *It;
					break;
				}
			}
			if (!CapturePawn || !CaptureEnemy)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PLATE capture FAIL pawn=%d enemy=%d"),
					CapturePawn != nullptr, CaptureEnemy != nullptr);
				return;
			}

			// stand off the enemy's own face, on its own ground, and point pawn and camera at it
			const FVector EnemySpot = CaptureEnemy->GetActorLocation();
			const FVector StandOff = CaptureEnemy->GetActorForwardVector().GetSafeNormal2D();
			const FVector CaptureSpot = EnemySpot + StandOff * 600.0f + FVector(0.0f, 0.0f, 20.0f);
			CapturePawn->SetActorLocation(CaptureSpot, false, nullptr, ETeleportType::TeleportPhysics);
			const FRotator FacingEnemy = (EnemySpot - CaptureSpot).GetSafeNormal2D().Rotation();
			CapturePawn->SetActorRotation(FacingEnemy);
			// aim 20 degrees off the enemy so the pirate's own back does not sit on top of it: the
			// boom is always between the camera and whatever the camera aims at
			CapturePC->SetControlRotation(FRotator(-5.0f, FacingEnemy.Yaw - 20.0f, 0.0f));

			float CaptureClearance = 0.0f;
			const bool bAboveHead = CaptureEnemy->IsPlateAboveHead(10.0f, CaptureClearance);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PLATE capture enemy=%s row=%s aboveHead=%d gap=%.0f enemyAt X=%.0f Y=%.0f Z=%.0f pawnAt X=%.0f Y=%.0f Z=%.0f"),
				*CaptureEnemy->GetName(), *CaptureEnemy->EnemyRowName.ToString(), bAboveHead ? 1 : 0, CaptureClearance,
				EnemySpot.X, EnemySpot.Y, EnemySpot.Z, CaptureSpot.X, CaptureSpot.Y, CaptureSpot.Z);
		}), 9.0f, false);

		// re-aim on the enemy that actually closed, and log where everyone really ended up so a
		// framing miss is diagnosable from the log instead of from the picture
		FTimerHandle PlateReaimTimer;
		InWorld.GetTimerManager().SetTimer(PlateReaimTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			if (!CapturePawn)
			{
				return;
			}

			const FVector PawnSpot = CapturePawn->GetActorLocation();
			ACombatEnemy* Nearest = nullptr;
			float NearestDistance = 0.0f;
			for (TActorIterator<ACombatEnemy> It(CaptureWorld); It; ++It)
			{
				if (!It->Stats || !It->Stats->IsAlive())
				{
					continue;
				}
				const float Distance = FVector::Distance(It->GetActorLocation(), PawnSpot);
				if (!Nearest || Distance < NearestDistance)
				{
					Nearest = *It;
					NearestDistance = Distance;
				}
			}
			if (!Nearest)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PLATE reaim FAIL (no living enemy)"));
				return;
			}

			const FRotator FacingEnemy = (Nearest->GetActorLocation() - PawnSpot).GetSafeNormal2D().Rotation();
			CapturePawn->SetActorRotation(FacingEnemy);
			CapturePC->SetControlRotation(FRotator(-5.0f, FacingEnemy.Yaw, 0.0f));
			float ReaimClearance = 0.0f;
			const bool bAboveHead = Nearest->IsPlateAboveHead(10.0f, ReaimClearance);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PLATE reaim enemy=%s row=%s dist=%.0f plateVisible=%d aboveHead=%d gap=%.0f yaw=%.0f pawnAt X=%.0f Y=%.0f Z=%.0f"),
				*Nearest->GetName(), *Nearest->EnemyRowName.ToString(), NearestDistance,
				Nearest->IsEnemyPlateVisible() ? 1 : 0, bAboveHead ? 1 : 0, ReaimClearance,
				FacingEnemy.Yaw, PawnSpot.X, PawnSpot.Y, PawnSpot.Z);
		}), 15.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("XpSkillProbe")))
	{
		FTimerHandle XpSkillProbeTimer;
		InWorld.GetTimerManager().SetTimer(XpSkillProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunXpSkillProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ShootProbe")))
	{
		FTimerHandle ShootStartTimer;
		InWorld.GetTimerManager().SetTimer(ShootStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunShootProbe), 10.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("MusketProbe")))
	{
		FTimerHandle MusketStartTimer;
		InWorld.GetTimerManager().SetTimer(MusketStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunMusketProbe), 10.0f, false);
	}
	FString MusketCaptureMode;
	if (FParse::Param(FCommandLine::Get(), TEXT("MusketForCapture"))
		|| FParse::Value(FCommandLine::Get(), TEXT("MusketForCapture="), MusketCaptureMode))
	{
		FTimerHandle MusketCaptureStartTimer;
		InWorld.GetTimerManager().SetTimer(MusketCaptureStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::ArmMusketCapture), 8.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("GunsmithProbe")))
	{
		FTimerHandle GunsmithStartTimer;
		InWorld.GetTimerManager().SetTimer(GunsmithStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunGunsmithProbe), 10.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("PistolForCapture")))
	{
		FTimerHandle PistolCaptureStartTimer;
		InWorld.GetTimerManager().SetTimer(PistolCaptureStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::ArmPistolCapture), 8.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("GunsmithForCapture")))
	{
		FTimerHandle GunsmithCaptureStartTimer;
		InWorld.GetTimerManager().SetTimer(GunsmithCaptureStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::ArmGunsmithCapture), 8.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ShopProbe")))
	{
		FTimerHandle ShopProbeTimer;
		InWorld.GetTimerManager().SetTimer(ShopProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunShopProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("QuestProbe")))
	{
		FTimerHandle QuestProbeTimer;
		InWorld.GetTimerManager().SetTimer(QuestProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunQuestProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("QuestFlowProbe")))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW-PROBE armed"));
		FTimerHandle QuestFlowProbeTimer;
		InWorld.GetTimerManager().SetTimer(QuestFlowProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunQuestFlowProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("MissionProbe")))
	{
		FTimerHandle MissionProbeTimer;
		InWorld.GetTimerManager().SetTimer(MissionProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunMissionProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("AttackProbe")))
	{
		FTimerHandle AttackProbeTimer;
		InWorld.GetTimerManager().SetTimer(AttackProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunAttackProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("LocoProbe")))
	{
		FTimerHandle LocoProbeTimer;
		InWorld.GetTimerManager().SetTimer(LocoProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunLocoProbe), 10.0f, false);
	}

	// REAL-input gate (2026-07-27): LocoProbe drives DoMove, which stayed green while the whole
	// keyboard chain was dead (CBP compile failure). This probe never drives the pawn - an
	// external harness (tools\input_probe.ps1) holds W via OS SendInput and the verdict is
	// displacement that only real keys can produce.
	if (FParse::Param(FCommandLine::Get(), TEXT("InputProbe")))
	{
		FTimerHandle InputProbeTimer;
		InWorld.GetTimerManager().SetTimer(InputProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunInputProbe), 6.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("EnemySkinProbe")))
	{
		FTimerHandle EnemySkinProbeTimer;
		InWorld.GetTimerManager().SetTimer(EnemySkinProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunEnemySkinProbe), 12.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("HitProbe")))
	{
		FTimerHandle HitProbeTimer;
		InWorld.GetTimerManager().SetTimer(HitProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunHitProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ComboProbe")))
	{
		FTimerHandle ComboProbeTimer;
		InWorld.GetTimerManager().SetTimer(ComboProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunComboProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("DamageNumbersProbe")))
	{
		FTimerHandle DamageNumbersProbeTimer;
		InWorld.GetTimerManager().SetTimer(DamageNumbersProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunDamageNumbersProbe), 10.0f, false);
	}

	// Capture aid. It keeps clicking through the two swing string and never quits.
	if (FParse::Param(FCommandLine::Get(), TEXT("ComboForCapture")))
	{
		InWorld.GetTimerManager().SetTimer(ComboCaptureClickTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* InnerWorld = GetWorld();
			APlayerController* PC = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
			if (ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr)
			{
				Pawn->DoComboAttackStart();
			}
		}), 0.3f, true, 8.0f);
	}

	// Capture aid timed for the two standard launch_visual.ps1 frames. Each strike finishes a
	// prepared living enemy through the real trace, leaving both a gold number and the XP bar.
	if (FParse::Param(FCommandLine::Get(), TEXT("DamageNumbersForCapture")))
	{
		FTimerHandle DamageNumbersCaptureTimer;
		InWorld.GetTimerManager().SetTimer(DamageNumbersCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			ACombatEnemy* Target = nullptr;
			float BestDistance = TNumericLimits<float>::Max();
			if (CaptureWorld)
			{
				for (TActorIterator<ACombatEnemy> It(CaptureWorld); It; ++It)
				{
					if (It->Stats && It->Stats->IsAlive())
					{
						const float Distance = CapturePawn
							? FVector::DistSquared(It->GetActorLocation(), CapturePawn->GetActorLocation()) : 0.0f;
						if (Distance < BestDistance)
						{
							BestDistance = Distance;
							Target = *It;
						}
					}
				}
			}
			if (!CapturePawn || !CaptureState || !Target || !Target->Stats)
			{
				UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-DMGNUM capture waiting pawn=%d state=%d target=%d"),
					CapturePawn != nullptr, CaptureState != nullptr, Target != nullptr);
				return;
			}

			CaptureState->SetProbeCriticalChanceOverride(1.0f);
			const float DesiredHealth = FMath::Max(1.0f,
				FMath::Min(Target->Stats->GetHealth(), CapturePawn->GetAttackDamage() * CaptureState->GetCriticalMultiplier()));
			for (int32 Step = 0; Step < 10 && Target->Stats->GetHealth() > DesiredHealth + KINDA_SMALL_NUMBER; ++Step)
			{
				Target->Stats->ApplyDamage(Target->Stats->GetHealth() - DesiredHealth, nullptr, false);
			}

			FVector Direction = (Target->GetActorLocation() - CapturePawn->GetActorLocation()).GetSafeNormal2D();
			if (Direction.IsNearlyZero())
			{
				Direction = FVector::ForwardVector;
			}
			CapturePawn->SetActorLocation(Target->GetActorLocation() - Direction * 100.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			CapturePawn->SetActorRotation(Direction.Rotation());
			CapturePC->SetControlRotation(Direction.Rotation());
			CapturePawn->DoAttackTrace(FName(TEXT("hand_r")));
			CaptureState->SetProbeCriticalChanceOverride(-1.0f);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-DMGNUM capture strike target=%s"), *Target->GetName());
		}), 5.0f, true, 17.3f);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ChaseProbe")))
	{
		FTimerHandle ChaseProbeTimer;
		InWorld.GetTimerManager().SetTimer(ChaseProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunChaseProbe), 12.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("LeashProbe")))
	{
		FTimerHandle LeashProbeTimer;
		InWorld.GetTimerManager().SetTimer(LeashProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunLeashProbe), 12.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("SwimProbe")))
	{
		FTimerHandle SwimProbeTimer;
		InWorld.GetTimerManager().SetTimer(SwimProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunSwimProbe), 12.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("NavProbe")))
	{
		FTimerHandle NavProbeTimer;
		InWorld.GetTimerManager().SetTimer(NavProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunNavProbe), 12.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ChestProbe")))
	{
		FTimerHandle ChestProbeTimer;
		InWorld.GetTimerManager().SetTimer(ChestProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunChestProbe), 10.0f, false);
	}

	FString MenuProbeMode;
	if (FParse::Param(FCommandLine::Get(), TEXT("MenuProbe")) || FParse::Value(FCommandLine::Get(), TEXT("MenuProbe="), MenuProbeMode))
	{
		// The title menu PAUSES the world and world timers pause with it (the first -MenuProbe run sat
		// on a timer that never fired); a core ticker runs regardless of pause.
		TWeakObjectPtr<UWrecktideRewardSubsystem> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
		{
			if (WeakThis.IsValid())
			{
				WeakThis->RunMenuProbe();
			}
			return false;
		}), 10.0f);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("Island3Probe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("Island3ChartProbe")))
	{
		InWorld.GetTimerManager().SetTimer(Island3ProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunIsland3Probe), 13.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("Island3StoryProbe")))
	{
		FTimerHandle MangroveStoryStart;
		InWorld.GetTimerManager().SetTimer(MangroveStoryStart,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunIsland3StoryProbe), 13.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("Island3PopulationProbe")))
	{
		FTimerHandle MangrovePopulationStart;
		InWorld.GetTimerManager().SetTimer(MangrovePopulationStart,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunIsland3PopulationProbe), 13.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("Island3TerrainProbe")))
	{
		FTimerHandle MangroveTerrainStart;
		InWorld.GetTimerManager().SetTimer(MangroveTerrainStart,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunIsland3TerrainProbe), 13.0f, false);
	}

	// -Island2Probe (C34.8): 13 s so the navy posts have spawned their enemies and the HUD has been
	// drawing for a while before the chart stage teleports the pirate to Gallows Reach.
	if (FParse::Param(FCommandLine::Get(), TEXT("Island2Probe")))
	{
		FTimerHandle Island2StartTimer;
		InWorld.GetTimerManager().SetTimer(Island2StartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunIsland2Probe), 13.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("SfxProbe")))
	{
		FTimerHandle SfxProbeTimer;
		InWorld.GetTimerManager().SetTimer(SfxProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunSfxProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("BoatProbe")))
	{
		FTimerHandle BoatProbeTimer;
		InWorld.GetTimerManager().SetTimer(BoatProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunBoatProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ShipwrightProbe")))
	{
		FTimerHandle ShipwrightTimer;
		InWorld.GetTimerManager().SetTimer(ShipwrightTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunShipwrightProbe), 10.0f, false);
	}

	// -OpenShipwrightForCapture : dev switch for the visual harness (spec S4). Stands the pirate at the
	// bench with gold and notoriety in hand and opens the list at t=9s. No verdict, no quit.
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenShipwrightForCapture")))
	{
		FTimerHandle ShipwrightCaptureTimer;
		InWorld.GetTimerManager().SetTimer(ShipwrightCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			UEconomyComponent* CaptureEconomy = CaptureState ? CaptureState->GetEconomy() : nullptr;
			USkillComponent* CaptureSkill = CaptureState ? CaptureState->GetSkill() : nullptr;
			AWrecktideShipwright* CaptureBench = nullptr;
			for (TActorIterator<AWrecktideShipwright> It(CaptureWorld); It; ++It)
			{
				CaptureBench = *It;
				break;
			}
			if (!CapturePawn || !CaptureEconomy || !CaptureSkill || !CaptureBench)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT capture FAIL pawn=%d economy=%d skill=%d bench=%d"),
					CapturePawn != nullptr, CaptureEconomy != nullptr, CaptureSkill != nullptr, CaptureBench != nullptr);
				return;
			}
			CaptureEconomy->ApplyGoldDelta(3000, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("ShipwrightCapture")));
			for (int32 Attempt = 0; Attempt < 60 && CaptureState->GetGatingLevel() < 3; ++Attempt)
			{
				CaptureSkill->AwardXP(EWrecktideSkill::Sailing, 250, FName(TEXT("ShipwrightCapture")));
			}
			CapturePawn->SetActorLocation(CaptureBench->GetActorLocation() + FVector(0.0f, 0.0f, 130.0f) + CaptureBench->GetActorRightVector() * 160.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			CaptureBench->OpenShipwrightMenu(CapturePawn);
			const FVector BenchSpot = CaptureBench->GetActorLocation();
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT capture open=%d rows=%d notoriety=%d at X=%.0f Y=%.0f Z=%.0f"),
				CaptureBench->IsMenuOpen() ? 1 : 0, CaptureBench->GetRowCount(), CaptureState->GetGatingLevel(),
				BenchSpot.X, BenchSpot.Y, BenchSpot.Z);
		}), 9.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("HoldProbe")))
	{
		FTimerHandle HoldProbeTimer;
		InWorld.GetTimerManager().SetTimer(HoldProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunHoldProbe), 10.0f, false);
	}

	// -OpenBarrelForCapture : dev switch for the visual harness (spec S2). Stands the pirate at the
	// supply barrel with gold in hand and opens the list at t=9s. No verdict, no quit.
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenBarrelForCapture")))
	{
		FTimerHandle BarrelCaptureTimer;
		InWorld.GetTimerManager().SetTimer(BarrelCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			UEconomyComponent* CaptureEconomy = CaptureState ? CaptureState->GetEconomy() : nullptr;
			AWrecktideSupplyBarrel* CaptureBarrel = nullptr;
			for (TActorIterator<AWrecktideSupplyBarrel> It(CaptureWorld); It; ++It)
			{
				CaptureBarrel = *It;
				break;
			}
			if (!CapturePawn || !CaptureEconomy || !CaptureBarrel)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD capture FAIL pawn=%d economy=%d barrel=%d"),
					CapturePawn != nullptr, CaptureEconomy != nullptr, CaptureBarrel != nullptr);
				return;
			}
			CaptureEconomy->ApplyGoldDelta(500, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("BarrelCapture")));
			CapturePawn->SetActorLocation(CaptureBarrel->GetActorLocation() + FVector(0.0f, 0.0f, 150.0f) + CaptureBarrel->GetActorRightVector() * 160.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			CaptureBarrel->OpenBarrelMenu(CapturePawn);
			const FVector BarrelSpot = CaptureBarrel->GetActorLocation();
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD capture open=%d rows=%d at X=%.0f Y=%.0f Z=%.0f"),
				CaptureBarrel->IsMenuOpen() ? 1 : 0, CaptureBarrel->GetRowCount(), BarrelSpot.X, BarrelSpot.Y, BarrelSpot.Z);
		}), 9.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("SailFeelProbe")))
	{
		FTimerHandle SailFeelProbeTimer;
		InWorld.GetTimerManager().SetTimer(SailFeelProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunSailFeelProbe), 10.0f, false);
	}

	// -SailForCapture : dev switch for the visual harness (spec S1). Puts the pirate aboard, takes her
	// out into open water and sets full sail with a touch of rudder, so a capture can photograph the
	// wake without a synthetic key press. Prints no verdict and never quits, the same contract as
	// -PhotoAt and -OpenSmithForCapture.
	if (FParse::Param(FCommandLine::Get(), TEXT("SailForCapture")))
	{
		FTimerHandle SailCaptureTimer;
		InWorld.GetTimerManager().SetTimer(SailCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktideBoat* CaptureBoat = nullptr;
			for (TActorIterator<AWrecktideBoat> It(CaptureWorld); It; ++It)
			{
				CaptureBoat = *It;
				break;
			}
			if (!CapturePawn || !CaptureBoat)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAIL capture FAIL pawn=%d boat=%d"),
					CapturePawn != nullptr, CaptureBoat != nullptr);
				return;
			}
			CapturePawn->SetActorLocation(CaptureBoat->GetActorLocation() + CaptureBoat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
				false, nullptr, ETeleportType::TeleportPhysics);
			if (!CaptureBoat->Board(CapturePawn))
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAIL capture FAIL boarding refused"));
				return;
			}
			CaptureBoat->SetActorLocation(FVector(0.0f, -11000.0f, -120.0f), false, nullptr, ETeleportType::TeleportPhysics);
			CaptureBoat->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
			CaptureBoat->SetSailState(EWrecktideSailState::Full);
			// A steady sliver of rudder so the wake curves behind her and reads in a still frame.
			CaptureBoat->HoldSail(0.0f, 0.35f, 600.0f);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL capture aboard at X=%.0f Y=%.0f Z=%.0f state=%s wind=%s"),
				CaptureBoat->GetActorLocation().X, CaptureBoat->GetActorLocation().Y, CaptureBoat->GetActorLocation().Z,
				*AWrecktideBoat::SailStateName(CaptureBoat->GetSailState()),
				*WrecktideWind::Compass(WrecktideWind::ToYaw(CaptureBoat)));
		}), 9.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PatrolProbe")))
	{
		FTimerHandle PatrolProbeStartTimer;
		InWorld.GetTimerManager().SetTimer(PatrolProbeStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunPatrolProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("HelmProbe")))
	{
		FTimerHandle HelmProbeStartTimer;
		InWorld.GetTimerManager().SetTimer(HelmProbeStartTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunHelmProbe), 10.0f, false);
	}

	// -HelmForCapture : dev switch for the visual harness (spec C28.5). Puts the pirate aboard, takes
	// her into open water at HALF sail and holds A, so a capture from behind photographs the wheel
	// hard over with the pirate's hands on it. Prints no verdict and never quits, the -PhotoAt
	// contract that -SailForCapture and -PatrolForCapture already follow.
	if (FParse::Param(FCommandLine::Get(), TEXT("HelmForCapture")))
	{
		FTimerHandle HelmCaptureTimer;
		InWorld.GetTimerManager().SetTimer(HelmCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktideBoat* CaptureBoat = nullptr;
			for (TActorIterator<AWrecktideBoat> It(CaptureWorld); It; ++It)
			{
				CaptureBoat = *It;
				break;
			}
			if (!CapturePawn || !CaptureBoat)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HELM capture FAIL pawn=%d boat=%d"),
					CapturePawn != nullptr, CaptureBoat != nullptr);
				return;
			}
			CapturePawn->SetActorLocation(CaptureBoat->GetActorLocation() + CaptureBoat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
				false, nullptr, ETeleportType::TeleportPhysics);
			if (!CaptureBoat->Board(CapturePawn))
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HELM capture FAIL boarding refused"));
				return;
			}
			CaptureBoat->SetActorLocation(FVector(0.0f, -11000.0f, -120.0f), false, nullptr, ETeleportType::TeleportPhysics);
			CaptureBoat->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
			CaptureBoat->SetSailState(EWrecktideSailState::Half);
			// A held A: steer -1 is hard a port, which is the wheel at its full negative lock.
			CaptureBoat->HoldSail(0.0f, -1.0f, 600.0f);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM capture aboard at X=%.0f Y=%.0f state=%s wheelLocal=%s grip=%d"),
				CaptureBoat->GetActorLocation().X, CaptureBoat->GetActorLocation().Y,
				*AWrecktideBoat::SailStateName(CaptureBoat->GetSailState()),
				*CaptureBoat->GetHelmWheelLocalOffset().ToString(), CaptureBoat->IsHelmGripPlaying() ? 1 : 0);
		}), 9.0f, false);
	}

	// -PatrolForCapture : dev switch for the visual harness (spec S3). Puts the pirate aboard, sails
	// him across the navy patrol's bow at half sail and leaves them to it, so a capture photographs
	// her coming alongside. Prints no verdict and never quits, the same contract as -SailForCapture.
	if (FParse::Param(FCommandLine::Get(), TEXT("PatrolForCapture")))
	{
		FTimerHandle PatrolCaptureTimer;
		InWorld.GetTimerManager().SetTimer(PatrolCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktideBoat* CaptureBoat = nullptr;
			for (TActorIterator<AWrecktideBoat> It(CaptureWorld); It; ++It)
			{
				CaptureBoat = *It;
				break;
			}
			AWrecktidePatrolBoat* CapturePatrol = nullptr;
			for (TActorIterator<AWrecktidePatrolBoat> It(CaptureWorld); It; ++It)
			{
				CapturePatrol = *It;
				break;
			}
			if (!CapturePawn || !CaptureBoat || !CapturePatrol)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PATROL capture FAIL pawn=%d boat=%d patrol=%d"),
					CapturePawn != nullptr, CaptureBoat != nullptr, CapturePatrol != nullptr);
				return;
			}
			CapturePawn->SetActorLocation(CaptureBoat->GetActorLocation() + CaptureBoat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
				false, nullptr, ETeleportType::TeleportPhysics);
			if (!CaptureBoat->Board(CapturePawn))
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PATROL capture FAIL boarding refused"));
				return;
			}
			// Across her bow on her own course: she turns onto him inside a couple of seconds, and
			// half sail keeps way on him so the stern wake reads in the same frame.
			const FVector Ahead = CapturePatrol->GetActorLocation()
				+ CapturePatrol->GetActorForwardVector() * 1500.0f
				+ CapturePatrol->GetActorRightVector() * 700.0f;
			CaptureBoat->SetActorLocation(FVector(Ahead.X, Ahead.Y, -120.0f), false, nullptr, ETeleportType::TeleportPhysics);
			CaptureBoat->SetActorRotation(FRotator(0.0f, CapturePatrol->GetActorRotation().Yaw, 0.0f));
			CaptureBoat->SetSailState(EWrecktideSailState::Half);
			CaptureBoat->HoldSail(0.0f, 0.0f, 600.0f);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL capture aboard at X=%.0f Y=%.0f patrol X=%.0f Y=%.0f gap=%.0f"),
				CaptureBoat->GetActorLocation().X, CaptureBoat->GetActorLocation().Y,
				CapturePatrol->GetActorLocation().X, CapturePatrol->GetActorLocation().Y,
				FVector::Dist2D(CaptureBoat->GetActorLocation(), CapturePatrol->GetActorLocation()));
		}), 9.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("EconomyProbe")))
	{
		FTimerHandle EconomyProbeTimer;
		InWorld.GetTimerManager().SetTimer(EconomyProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunEconomyProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("NotorietyProbe")))
	{
		FTimerHandle NotorietyProbeTimer;
		InWorld.GetTimerManager().SetTimer(NotorietyProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunNotorietyProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("BoatWalkProbe")))
	{
		FTimerHandle BoatWalkProbeTimer;
		InWorld.GetTimerManager().SetTimer(BoatWalkProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunBoatWalkProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PoseProbe")))
	{
		FTimerHandle PoseProbeTimer;
		InWorld.GetTimerManager().SetTimer(PoseProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunPoseProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("FeelProbe")))
	{
		FTimerHandle FeelProbeTimer;
		InWorld.GetTimerManager().SetTimer(FeelProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunFeelProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("OwnershipProbe")))
	{
		FTimerHandle OwnershipProbeTimer;
		InWorld.GetTimerManager().SetTimer(OwnershipProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunOwnershipProbe), 10.0f, false);
	}

	// -SaveProbe=write / -SaveProbe=read (a value switch, so FParse::Value rather than Param). Any new
	// probe switch must ALSO be listed in AWrecktidePlayerState::IsPersistenceSuppressed, or the probe
	// loads and overwrites the player's real slot.
	FString SaveProbePhase;
	if (FParse::Value(FCommandLine::Get(), TEXT("SaveProbe="), SaveProbePhase))
	{
		FTimerHandle SaveProbeTimer;
		InWorld.GetTimerManager().SetTimer(SaveProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunSaveProbe), 10.0f, false);
	}

	FString WardrobeProbePhase;
	if (FParse::Value(FCommandLine::Get(), TEXT("WardrobeProbe="), WardrobeProbePhase))
	{
		FTimerHandle WardrobeProbeTimer;
		InWorld.GetTimerManager().SetTimer(WardrobeProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunWardrobeProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("MapProbe")))
	{
		FTimerHandle MapProbeTimer;
		InWorld.GetTimerManager().SetTimer(MapProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunMapProbe), 10.0f, false);
	}

	// BEACON lane, wave 5 (spec C33): the gold beam marks the POST, not the man, and goes out once
	// he has stood on it.
	if (FParse::Param(FCommandLine::Get(), TEXT("BeaconProbe")))
	{
		FTimerHandle BeaconProbeTimer;
		InWorld.GetTimerManager().SetTimer(BeaconProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunBeaconProbe), 10.0f, false);
	}

	// -BeaconPostCapture : dev switch for the visual harness (spec C33). Puts the story on the kill
	// mission, walks its nearest target well off his post and stands the player back looking at the
	// post, so one photograph shows the gold beam standing on an EMPTY post with the man it belongs
	// to somewhere else. No verdict and no quit, the -PhotoAt contract.
	if (FParse::Param(FCommandLine::Get(), TEXT("BeaconPostCapture")))
	{
		FTimerHandle BeaconPostTimer;
		InWorld.GetTimerManager().SetTimer(BeaconPostTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			APawn* CapturePawn = CapturePC ? CapturePC->GetPawn() : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			AWrecktideMissionBeacon* CaptureBeacon = CaptureWorld
				? AWrecktideMissionBeacon::FindBeacon(CaptureWorld, EWrecktideBeaconMode::Mission) : nullptr;
			if (!CapturePawn || !CaptureState || !CaptureBeacon)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BEACON post capture FAIL pawn=%d state=%d beacon=%d"),
					CapturePawn != nullptr, CaptureState != nullptr, CaptureBeacon != nullptr);
				return;
			}

			// M01 completes by arriving, which leaves the kill mission tracked.
			WrecktideMissionLogic::Accept(CaptureState);
			FVector StallLocation = FVector::ZeroVector;
			for (TActorIterator<AWrecktideShopStall> It(CaptureWorld); It; ++It)
			{
				StallLocation = It->GetActorLocation();
				break;
			}
			WrecktideMissionLogic::NotifyLocation(CaptureState, StallLocation);
			WrecktideMissionLogic::Accept(CaptureState);

			const FVector Camp = CapturePawn->GetActorLocation();
			ACombatEnemy* Nearest = nullptr;
			float BestDistance = TNumericLimits<float>::Max();
			for (TActorIterator<ACombatEnemy> It(CaptureWorld); It; ++It)
			{
				ACombatEnemy* Enemy = *It;
				if (!Enemy || Enemy->EnemyRowName != FName(TEXT("Cutthroat")) || (Enemy->Stats && !Enemy->Stats->IsAlive()))
				{
					continue;
				}
				const float Distance = FVector::Dist2D(Enemy->GetActorLocation(), Camp);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Nearest = Enemy;
				}
			}
			if (!Nearest)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BEACON post capture FAIL no living cutthroat to move"));
				return;
			}

			const FVector Post = Nearest->HomeLocation.IsNearlyZero() ? Nearest->GetActorLocation() : Nearest->HomeLocation;
			FVector Away = (Post - Camp).GetSafeNormal2D();
			if (Away.IsNearlyZero())
			{
				Away = FVector(1.0f, 0.0f, 0.0f);
			}
			// Sideways rather than further out, so the man and the beam fit in the same frame. He
			// stays inside his leash radius, so nothing drags him back before the shutter.
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
			FVector Walked = Post + Side * 1400.0f;
			FHitResult GroundHit;
			if (CaptureWorld->LineTraceSingleByChannel(GroundHit, Walked + FVector(0.0f, 0.0f, 600.0f),
				Walked - FVector(0.0f, 0.0f, 1500.0f), ECC_Visibility))
			{
				Walked.Z = GroundHit.ImpactPoint.Z + 90.0f;
			}
			Nearest->SetActorLocation(Walked, false, nullptr, ETeleportType::TeleportPhysics);

			const FVector Stand = Post - Away * 1800.0f + FVector(0.0f, 0.0f, 150.0f);
			CapturePawn->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			const FRotator Look = (Post - Stand).Rotation();
			CapturePC->SetControlRotation(FRotator(-6.0f, Look.Yaw, 0.0f));
			CaptureBeacon->RefreshTarget();

			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON post capture beam=(%.0f,%.0f) post=(%.0f,%.0f) enemy=(%.0f,%.0f) player=(%.0f,%.0f) lit=%d row=%s"),
				CaptureBeacon->GetActorLocation().X, CaptureBeacon->GetActorLocation().Y, Post.X, Post.Y,
				Nearest->GetActorLocation().X, Nearest->GetActorLocation().Y, Stand.X, Stand.Y,
				CaptureBeacon->IsLit() ? 1 : 0, *CaptureBeacon->GetTargetRow().ToString());
		}), 8.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("SmithProbe")))
	{
		FTimerHandle SmithProbeTimer;
		InWorld.GetTimerManager().SetTimer(SmithProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunSmithProbe), 10.0f, false);
	}

	// UI lane, wave 3 (spec C17, C18, C19, C20).
	// -OpenShopForCapture : dev switch for the visual harness (spec C20). Opens the stall list and
	// parks the mouse on the SECOND row, so the gold row in the photograph is one the keyboard never
	// selected. No verdict, no quit, the same contract as -PhotoAt.
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenShopForCapture")))
	{
		FTimerHandle ShopCaptureTimer;
		InWorld.GetTimerManager().SetTimer(ShopCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			UEconomyComponent* CaptureEconomy = CaptureState ? CaptureState->GetEconomy() : nullptr;
			AWrecktideHUD* CaptureHUD = CapturePC ? Cast<AWrecktideHUD>(CapturePC->GetHUD()) : nullptr;
			AWrecktideShopStall* CaptureStall = nullptr;
			for (TActorIterator<AWrecktideShopStall> It(CaptureWorld); It; ++It)
			{
				CaptureStall = *It;
				break;
			}
			if (!CapturePC || !CapturePawn || !CaptureEconomy || !CaptureHUD || !CaptureStall)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CURSOR capture FAIL wiring pawn=%d stall=%d"),
					CapturePawn != nullptr, CaptureStall != nullptr);
				return;
			}

			CaptureEconomy->ApplyGoldDelta(400, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("ShopCapture")));
			CapturePawn->SetActorLocation(CaptureStall->GetActorLocation() + CaptureStall->GetActorRightVector() * 160.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			CaptureStall->OpenShopMenu(CapturePawn);

			// One beat later, so the HUD has drawn the list once and its row rectangles exist.
			FTimerHandle HoverTimer;
			CaptureWorld->GetTimerManager().SetTimer(HoverTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UWorld* HoverWorld = GetWorld();
				APlayerController* HoverPC = HoverWorld ? UGameplayStatics::GetPlayerController(HoverWorld, 0) : nullptr;
				AWrecktideHUD* HoverHUD = HoverPC ? Cast<AWrecktideHUD>(HoverPC->GetHUD()) : nullptr;
				if (!HoverPC || !HoverHUD)
				{
					return;
				}
				const FBox2D Rect = HoverHUD->GetShopRowRect(1);
				if (!Rect.bIsValid)
				{
					UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CURSOR capture FAIL no row rect"));
					return;
				}
				const FVector2D Point = Rect.GetCenter();
				HoverPC->SetMouseLocation(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y));
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR capture mouse at (%.0f,%.0f) row=1"), Point.X, Point.Y);
			}), 0.6f, false);
		}), 9.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("CursorProbe")))
	{
		FTimerHandle CursorProbeTimer;
		InWorld.GetTimerManager().SetTimer(CursorProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunCursorProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("InventoryProbe")))
	{
		FTimerHandle InventoryProbeTimer;
		InWorld.GetTimerManager().SetTimer(InventoryProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunInventoryProbe), 10.0f, false);
	}

	// -OpenInventoryForCapture[=journal|outfits|gold] : dev switch for the visual harness. Fills the
	// chest with something worth photographing (a bought blade, an accepted mission) and opens the
	// named page at t=9s. No verdict, no quit, the same contract as -PhotoAt.
	FString ChestCapturePage;
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenInventoryForCapture"))
		|| FParse::Value(FCommandLine::Get(), TEXT("OpenInventoryForCapture="), ChestCapturePage))
	{
		FTimerHandle ChestCaptureTimer;
		InWorld.GetTimerManager().SetTimer(ChestCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatPlayerController* CaptureController = Cast<ACombatPlayerController>(CapturePC);
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			UProgressComponent* CaptureProgress = CaptureState ? CaptureState->GetProgress() : nullptr;
			UEconomyComponent* CaptureEconomy = CaptureState ? CaptureState->GetEconomy() : nullptr;
			if (!CaptureController || !CaptureState || !CaptureProgress || !CaptureEconomy)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CHEST capture FAIL wiring controller=%d state=%d"),
					CaptureController != nullptr, CaptureState != nullptr);
				return;
			}

			CaptureEconomy->ApplyGoldDelta(640, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("ChestCapture")));
			CaptureProgress->UnlockItem(FName(TEXT("WornCutlass")));
			CaptureState->AssignSlot(1, FName(TEXT("WornCutlass")));
			WrecktideMissionLogic::Accept(CaptureState);

			FString Page;
			FParse::Value(FCommandLine::Get(), TEXT("OpenInventoryForCapture="), Page);
			// "bar" fills the belt and leaves the chest SHUT, which is how the weapon bar gets
			// photographed in play rather than under a screen.
			if (Page.Equals(TEXT("bar"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST capture open=0 page=bar slot1=%s"),
					*CaptureState->GetAssignedSlot(1).ToString());
				return;
			}

			CaptureController->ToggleInventory();
			if (Page.Equals(TEXT("journal"), ESearchCase::IgnoreCase))
			{
				CaptureController->SetChestTab(EWrecktideChestTab::Journal);
			}
			else if (Page.Equals(TEXT("outfits"), ESearchCase::IgnoreCase))
			{
				CaptureController->SetChestTab(EWrecktideChestTab::Outfits);
			}
			else if (Page.Equals(TEXT("gold"), ESearchCase::IgnoreCase))
			{
				CaptureController->SetChestTab(EWrecktideChestTab::Gold);
			}
			else
			{
				CaptureController->SetChestTab(EWrecktideChestTab::Weapons);
			}
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST capture open=%d page=%s rows=%d"),
				CaptureController->IsInventoryOpen() ? 1 : 0, *Page, CaptureController->GetMenuRows().Num());
		}), 9.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("HotbarProbe")))
	{
		FTimerHandle HotbarProbeTimer;
		InWorld.GetTimerManager().SetTimer(HotbarProbeTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunHotbarProbe), 10.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ChestButtonProbe")))
	{
		FTimerHandle ChestButtonTimer;
		InWorld.GetTimerManager().SetTimer(ChestButtonTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::RunChestButtonProbe),
			ChestButtonProbeSettleSeconds, false);
	}



	// -OpenSmithForCapture : dev switch for the visual harness (spec C10). Puts the pirate at the
	// smithy, gives him a weapon and enough gold to read prices, and opens the track page at t=9s so
	// launch_visual.ps1 can photograph the menu without a synthetic key press. No verdict, no quit.
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenSmithForCapture")))
	{
		FTimerHandle SmithCaptureTimer;
		InWorld.GetTimerManager().SetTimer(SmithCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePC ? Cast<ACombatCharacter>(CapturePC->GetPawn()) : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			UProgressComponent* CaptureProgress = CaptureState ? CaptureState->GetProgress() : nullptr;
			UEconomyComponent* CaptureEconomy = CaptureState ? CaptureState->GetEconomy() : nullptr;
			AWrecktideSmithStall* CaptureStall = nullptr;
			for (TActorIterator<AWrecktideSmithStall> It(CaptureWorld); It; ++It)
			{
				CaptureStall = *It;
				break;
			}
			if (!CapturePawn || !CaptureState || !CaptureProgress || !CaptureEconomy || !CaptureStall)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH capture FAIL pawn=%d state=%d stall=%d"),
					CapturePawn != nullptr, CaptureState != nullptr, CaptureStall != nullptr);
				return;
			}

			const FName CaptureWeapon(TEXT("WornCutlass"));
			CaptureProgress->UnlockItem(CaptureWeapon);
			CaptureState->EquipWeapon(CaptureWeapon);
			CaptureEconomy->ApplyGoldDelta(2000, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("SmithCapture")));
			CapturePawn->SetActorLocation(CaptureStall->GetActorLocation() + CaptureStall->GetActorRightVector() * 160.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			CaptureStall->OpenSmithMenu(CapturePawn);
			CaptureStall->OpenWeaponTracks(CaptureWeapon);
			// The stall's world spot goes in the log so a second capture can aim -PhotoAt at it
			// without anyone having to guess where the camp's right flank ended up.
			const FVector StallSpot = CaptureStall->GetActorLocation();
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH capture open=%d rows=%d at X=%.0f Y=%.0f Z=%.0f yaw=%.0f"),
				CaptureStall->IsMenuOpen() ? 1 : 0, CaptureStall->GetRowCount(),
				StallSpot.X, StallSpot.Y, StallSpot.Z, CaptureStall->GetActorRotation().Yaw);
		}), 9.0f, false);
	}

	// -BeaconCapture : dev switch for the visual harness (2026-09-06). Drives the story chain toward
	// M03 (open the camp chest) through the REAL mission logic so a capture can photograph the gold
	// beam standing on an actual objective. TWO STAGES on purpose: FireMissionProbeKill spawns its
	// victim DEFERRED, so the kill lands a tick or two later; the first single-stage version called
	// Complete in the same frame, the kills had not registered, and the chain stalled on M02
	// (WRECKTIDE-BEACON capture accepted=0 row=M02_FirstSteel). Stage 2 also fails SOFT: whatever
	// mission ends up active, it re-points the beacon and logs where the beam actually is, so the
	// capture is never wasted. No verdict and no quit, like -PhotoAt.
	if (FParse::Param(FCommandLine::Get(), TEXT("BeaconCapture")))
	{
		FTimerHandle BeaconStageOneTimer;
		InWorld.GetTimerManager().SetTimer(BeaconStageOneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			if (!CaptureState)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BEACON capture FAIL no player state"));
				return;
			}

			// M01: reach the market stall. Arriving completes it, so no giver visit is needed.
			WrecktideMissionLogic::Accept(CaptureState);
			FVector StallLocation = FVector::ZeroVector;
			for (TActorIterator<AWrecktideShopStall> It(CaptureWorld); It; ++It)
			{
				StallLocation = It->GetActorLocation();
				break;
			}
			WrecktideMissionLogic::NotifyLocation(CaptureState, StallLocation);

			// FireMissionProbeKill clones a LIVE enemy's class and transform, and it reads them from
			// the cached pair -MissionProbe fills in its own first stage. Fill them here too, or it
			// returns false and the chain stalls on M02 (first run: kills=0,0).
			for (TActorIterator<ACombatEnemy> It(CaptureWorld); It; ++It)
			{
				MissionProbeEnemyClass = It->GetClass();
				MissionProbeSpawnTransform = It->GetActorTransform();
				break;
			}

			// M02: two cutthroats through the real kill path. The payout waits for stage 2.
			const bool bAcceptedSecond = WrecktideMissionLogic::Accept(CaptureState);
			const bool bKillOne = FireMissionProbeKill(FName(TEXT("Cutthroat")));
			const bool bKillTwo = FireMissionProbeKill(FName(TEXT("Cutthroat")));
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON capture stage1 m02accepted=%d kills=%d,%d"),
				bAcceptedSecond ? 1 : 0, bKillOne ? 1 : 0, bKillTwo ? 1 : 0);
		}), 5.0f, false);

		FTimerHandle BeaconStageTwoTimer;
		InWorld.GetTimerManager().SetTimer(BeaconStageTwoTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePC = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			AWrecktidePlayerState* CaptureState = CapturePC ? CapturePC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
			if (!CaptureState)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BEACON capture FAIL no player state at stage 2"));
				return;
			}

			// M02 payout, then M03: open the camp chest. Left ACTIVE, which is the point.
			const bool bCompleted = WrecktideMissionLogic::Complete(CaptureState);
			const bool bAcceptedThird = WrecktideMissionLogic::Accept(CaptureState);

			AWrecktideMissionBeacon* Beacon = AWrecktideMissionBeacon::FindBeacon(CaptureWorld, EWrecktideBeaconMode::Mission);
			if (!Beacon)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BEACON capture FAIL no beacon in the world"));
				return;
			}
			Beacon->RefreshTarget();
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON capture stage2 m02completed=%d m03accepted=%d row=%s lit=%d at X=%.0f Y=%.0f Z=%.0f"),
				bCompleted ? 1 : 0, bAcceptedThird ? 1 : 0, *Beacon->GetTargetRow().ToString(),
				Beacon->IsLit() ? 1 : 0, Beacon->GetActorLocation().X, Beacon->GetActorLocation().Y,
				Beacon->GetActorLocation().Z);
		}), 7.0f, false);
	}

	// -OpenMapForCapture : dev switch for the visual harness (2026-09-06). Opens the full map at
	// t=9s, after the exposure warm-up the map screen enforces for itself, so launch_visual.ps1 can
	// photograph the chart without a synthetic key press. No verdict and no quit, like -PhotoAt.
	if (FParse::Param(FCommandLine::Get(), TEXT("OpenMapForCapture")))
	{
		FTimerHandle MapCaptureTimer;
		InWorld.GetTimerManager().SetTimer(MapCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* CaptureWorld = GetWorld();
			ACombatPlayerController* CapturePC = CaptureWorld
				? Cast<ACombatPlayerController>(UGameplayStatics::GetPlayerController(CaptureWorld, 0)) : nullptr;
			if (!CapturePC)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MAP capture FAIL no player controller"));
				return;
			}
			CapturePC->SetRadarState(EWrecktideRadarState::Full);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP capture opened=%d"), CapturePC->IsMapScreenOpen() ? 1 : 0);
		}), 9.0f, false);
	}

	// -OpenGiverForCapture=<GiverId> : dev switch for the visual harness (C30, 2026-09-06). Stands the
	// pirate two paces in front of that NPC, faces him, and presses his F for him at t=9s. WHY it
	// exists: a brush-off line only exists inside a conversation, so -PhotoAt alone can photograph
	// where an NPC stands but never what he says. No verdict and no quit, the same contract as
	// -PhotoAt: launch_visual.ps1 captures and kills.
	FString GiverCaptureId;
	if (FParse::Value(FCommandLine::Get(), TEXT("OpenGiverForCapture="), GiverCaptureId))
	{
		FTimerHandle GiverCaptureTimer;
		InWorld.GetTimerManager().SetTimer(GiverCaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this, GiverCaptureId]()
		{
			UWorld* CaptureWorld = GetWorld();
			APlayerController* CapturePlayerController = CaptureWorld ? UGameplayStatics::GetPlayerController(CaptureWorld, 0) : nullptr;
			ACombatCharacter* CapturePawn = CapturePlayerController ? Cast<ACombatCharacter>(CapturePlayerController->GetPawn()) : nullptr;
			AWrecktideMissionGiver* CaptureGiver = nullptr;
			if (CaptureWorld)
			{
				for (TActorIterator<AWrecktideMissionGiver> It(CaptureWorld); It; ++It)
				{
					if (It->GetGiverId() == FName(*GiverCaptureId))
					{
						CaptureGiver = *It;
						break;
					}
				}
			}
			if (!CapturePawn || !CaptureGiver)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-GIVER-CAPTURE FAIL id=%s pawn=%d giver=%d"),
					*GiverCaptureId, CapturePawn != nullptr, CaptureGiver != nullptr);
				return;
			}

			// 220 uu in front of him and 100 up: inside his 300 uu trigger, which is what OpenGiverMenu
			// demands, and the same framing the player gets walking up to talk.
			const FVector Stand = CaptureGiver->GetActorLocation()
				+ CaptureGiver->GetActorForwardVector() * 220.0f + FVector(0.0f, 0.0f, 100.0f);
			const float LookYaw = (CaptureGiver->GetActorLocation() - Stand).Rotation().Yaw;
			CapturePawn->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			CapturePawn->SetActorRotation(FRotator(0.0f, LookYaw, 0.0f));
			CapturePlayerController->SetControlRotation(FRotator(-5.0f, LookYaw, 0.0f));
			CaptureGiver->OpenGiverMenu(CapturePawn);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-GIVER-CAPTURE id=%s at=%s open=%d line=%s"),
				*GiverCaptureId, *CaptureGiver->GetActorLocation().ToString(),
				CaptureGiver->IsMenuOpen() ? 1 : 0, *CaptureGiver->GetMenuLine(0));
		}), 9.0f, false);
	}

	// -PhotoAt=X,Y,Z,Yaw : dev photo mode for the visual harness (2026-09-02). Teleports the player
	// pawn there at t=8s and aims the camera along Yaw so a proof capture can frame a specific spot
	// (enemy skins are unreadable from the PlayerStart, 2800uu from the spawner ring). No verdict and
	// no quit: launch_visual.ps1 captures and kills. Value parse must NOT stop at the commas.
	FString PhotoAt;
	if (FParse::Value(FCommandLine::Get(), TEXT("PhotoAt="), PhotoAt, false))
	{
		FTimerHandle PhotoTimer;
		InWorld.GetTimerManager().SetTimer(PhotoTimer, FTimerDelegate::CreateWeakLambda(this, [this, PhotoAt]()
		{
			TArray<FString> Parts;
			PhotoAt.ParseIntoArray(Parts, TEXT(","), true);
			UWorld* World = GetWorld();
			APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
			APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
			if (!Pawn || Parts.Num() < 4)
			{
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PHOTO FAIL pawn=%d parts=%d (%s)"), Pawn != nullptr, Parts.Num(), *PhotoAt);
				return;
			}
			const FVector Location(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
			const float Yaw = FCString::Atof(*Parts[3]);
			Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
			PlayerController->SetControlRotation(FRotator(-8.0f, Yaw, 0.0f));
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PHOTO at=%s yaw=%.0f"), *Location.ToString(), Yaw);
		}), 8.0f, false);
	}
#endif // !UE_BUILD_SHIPPING && !UE_BUILD_TEST
}

void UWrecktideRewardSubsystem::RunComboProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-COMBO-PROBE FAIL reason=no-player-pawn"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	ComboProbeSections.Reset();
	ComboProbeSamples = 0;
	bComboProbeSecondClickSent = false;
	bComboProbeThirdClickSent = false;
	bComboProbeSawSecond = false;
	bComboProbeSawEnd = false;
	bComboProbeFreshFirst = false;
	bComboProbeUnexpectedSection = false;
	const TArray<FName>& Sections = PlayerPawn->GetComboSectionNames();
	bComboProbeConfigOK = Sections.Num() == 2
		&& Sections[0] == FName(TEXT("Melee01"))
		&& Sections[1] == FName(TEXT("Melee02"));

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-COMBO-PROBE armed config=%d sections=%d"),
		bComboProbeConfigOK ? 1 : 0, Sections.Num());

	PlayerPawn->DoComboAttackStart();
	World->GetTimerManager().SetTimer(ComboProbeSampleTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::SampleComboProbe), 0.02f, true);

	FTimerHandle FinishTimer;
	World->GetTimerManager().SetTimer(FinishTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishComboProbe), 6.0f, false);
}

void UWrecktideRewardSubsystem::SampleComboProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	UAnimInstance* Anim = PlayerPawn && PlayerPawn->GetMesh() ? PlayerPawn->GetMesh()->GetAnimInstance() : nullptr;
	if (!PlayerPawn || !Anim)
	{
		return;
	}

	++ComboProbeSamples;
	UAnimMontage* Active = Anim->GetCurrentActiveMontage();
	if (!Active)
	{
		if (bComboProbeSawSecond && bComboProbeThirdClickSent && !bComboProbeSawEnd)
		{
			bComboProbeSawEnd = true;
			ComboProbeSections.Add(FName(TEXT("MontageEnd")));
		}
		else if (bComboProbeSawEnd && !bComboProbeFreshFirst)
		{
			// Retry only while the montage is inactive. The end delegate may clear the attack
			// flag a few blend frames after the active montage disappears.
			PlayerPawn->DoComboAttackStart();
		}
		return;
	}

	const FName Section = Anim->Montage_GetCurrentSection(Active);
	if (Section != FName(TEXT("Melee01")) && Section != FName(TEXT("Melee02")))
	{
		bComboProbeUnexpectedSection = true;
	}
	if (ComboProbeSections.Num() == 0 || ComboProbeSections.Last() != Section)
	{
		ComboProbeSections.Add(Section);
	}

	if (Section == FName(TEXT("Melee01")) && !bComboProbeSawEnd)
	{
		if (!bComboProbeSecondClickSent)
		{
			bComboProbeSecondClickSent = true;
			PlayerPawn->DoChargedAttackStart();
		}
		// Keep the one logical second click fresh until the montage notify consumes it.
		PlayerPawn->DoComboAttackStart();
	}
	else if (Section == FName(TEXT("Melee02")))
	{
		bComboProbeSawSecond = true;
		if (!bComboProbeThirdClickSent)
		{
			bComboProbeThirdClickSent = true;
			PlayerPawn->DoChargedAttackStart();
		}
		// Keep the one logical third click fresh through the terminal notify. The gameplay path
		// must consume it without a third section and without an automatic restart.
		PlayerPawn->DoComboAttackStart();
	}
	else if (bComboProbeSawEnd && Section == FName(TEXT("Melee01")))
	{
		bComboProbeFreshFirst = true;
	}
}

void UWrecktideRewardSubsystem::FinishComboProbe()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(ComboProbeSampleTimer);
	}

	FString Sequence;
	for (const FName& Section : ComboProbeSections)
	{
		Sequence += Section.ToString() + TEXT(">");
	}
	const bool bPass = bComboProbeConfigOK && bComboProbeSecondClickSent
		&& bComboProbeThirdClickSent && bComboProbeSawSecond && bComboProbeSawEnd
		&& bComboProbeFreshFirst && !bComboProbeUnexpectedSection;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-COMBO-PROBE %s (config=%d seq=%s thirdRejected=%d ended=%d freshFirst=%d unexpected=%d samples=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bComboProbeConfigOK ? 1 : 0, *Sequence,
		bComboProbeThirdClickSent ? 1 : 0, bComboProbeSawEnd ? 1 : 0,
		bComboProbeFreshFirst ? 1 : 0, bComboProbeUnexpectedSection ? 1 : 0, ComboProbeSamples);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunDamageNumbersProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	ACombatEnemy* Target = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	if (World)
	{
		for (TActorIterator<ACombatEnemy> It(World); It; ++It)
		{
			if (It->Stats && It->Stats->IsAlive())
			{
				const float Distance = PlayerPawn
					? FVector::DistSquared(It->GetActorLocation(), PlayerPawn->GetActorLocation()) : 0.0f;
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Target = *It;
				}
			}
		}
	}

	if (!World || !PlayerController || !PlayerPawn || !PlayerState || !HUD || !Target || !Target->Stats)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-DMGNUM-PROBE FAIL (wiring world=%d pawn=%d state=%d hud=%d target=%d)"),
			World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, HUD != nullptr, Target != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	Target->SetActorTickEnabled(false);
	if (AController* TargetController = Target->GetController())
	{
		TargetController->StopMovement();
		TargetController->SetActorTickEnabled(false);
	}

	FVector Direction = (Target->GetActorLocation() - PlayerPawn->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::ForwardVector;
	}
	PlayerPawn->SetActorLocation(Target->GetActorLocation() - Direction * 100.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	PlayerPawn->SetActorRotation(Direction.Rotation());
	PlayerController->SetControlRotation(Direction.Rotation());

	DamageNumbersProbeEnemy = Target;
	bDamageNumbersProbeNormalOK = false;
	bDamageNumbersProbeCriticalOK = false;
	PlayerState->SetProbeCriticalChanceOverride(0.0f);
	DamageNumbersProbeHealthBefore = Target->Stats->GetHealth();
	PlayerPawn->DoAttackTrace(FName(TEXT("hand_r")));

	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckDamageNumbersProbeNormal), 0.3f, false);
}

void UWrecktideRewardSubsystem::CheckDamageNumbersProbeNormal()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	ACombatEnemy* Target = DamageNumbersProbeEnemy.Get();
	float Amount = 0.0f;
	EWrecktideDamageNumberKind Kind = EWrecktideDamageNumberKind::PlayerDamage;
	bool bDrawn = false;
	const float Dealt = Target && Target->Stats
		? DamageNumbersProbeHealthBefore - Target->Stats->GetHealth() : -1.0f;
	const bool bEntry = HUD && HUD->GetLatestDamageNumber(Amount, Kind, bDrawn);
	bDamageNumbersProbeNormalOK = bEntry && bDrawn
		&& Kind == EWrecktideDamageNumberKind::Normal && FMath::IsNearlyEqual(Amount, Dealt, 0.01f);

	if (!World || !PlayerPawn || !PlayerState || !HUD || !Target || !Target->Stats)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-DMGNUM-PROBE FAIL (state lost during normal check)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	PlayerState->SetProbeCriticalChanceOverride(1.0f);
	DamageNumbersProbeHealthBefore = Target->Stats->GetHealth();
	PlayerPawn->DoAttackTrace(FName(TEXT("hand_r")));

	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckDamageNumbersProbeCritical), 0.3f, false);
}

void UWrecktideRewardSubsystem::CheckDamageNumbersProbeCritical()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	ACombatEnemy* Target = DamageNumbersProbeEnemy.Get();
	float Amount = 0.0f;
	EWrecktideDamageNumberKind Kind = EWrecktideDamageNumberKind::Normal;
	bool bDrawn = false;
	const float Dealt = Target && Target->Stats
		? DamageNumbersProbeHealthBefore - Target->Stats->GetHealth() : -1.0f;
	const bool bEntry = HUD && HUD->GetLatestDamageNumber(Amount, Kind, bDrawn);
	bDamageNumbersProbeCriticalOK = bEntry && bDrawn
		&& Kind == EWrecktideDamageNumberKind::Critical && FMath::IsNearlyEqual(Amount, Dealt, 0.01f);
	if (PlayerState)
	{
		PlayerState->SetProbeCriticalChanceOverride(-1.0f);
	}
	if (Target)
	{
		Target->SetActorEnableCollision(false);
	}

	if (!World || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-DMGNUM-PROBE FAIL (state lost during critical check)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	FTimerHandle FinishTimer;
	World->GetTimerManager().SetTimer(FinishTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishDamageNumbersProbe),
		HUD->GetDamageNumberLifetime() + 0.25f, false);
}

void UWrecktideRewardSubsystem::FinishDamageNumbersProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	const int32 LiveEntries = HUD ? HUD->GetDamageNumberCount() : -1;
	const bool bExpired = LiveEntries == 0;
	const bool bPass = bDamageNumbersProbeNormalOK && bDamageNumbersProbeCriticalOK && bExpired;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-DMGNUM-PROBE %s (normal=%d criticalGold=%d expired=%d live=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bDamageNumbersProbeNormalOK ? 1 : 0,
		bDamageNumbersProbeCriticalOK ? 1 : 0, bExpired ? 1 : 0, LiveEntries);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunHitProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HIT-PROBE FAIL reason=no-player-pawn"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// phase 1: the exact field repro - an enemy-style hit with the CHARGED launcher impulse
	// (worst case). Before the fix this fed the retargeted skin garbage = player "despawned".
	const FVector Impulse = (PlayerPawn->GetActorForwardVector() * -150.0f) + (FVector::UpVector * 350.0f);
	PlayerPawn->ApplyDamage(1.0f, nullptr, PlayerPawn->GetActorLocation(), Impulse);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HIT-PROBE player hit applied (charged-style impulse)"));

	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckHitProbePlayer), 1.5f, false);
}

void UWrecktideRewardSubsystem::CheckHitProbePlayer()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HIT-PROBE FAIL reason=player-lost-after-hit"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	USkeletalMeshComponent* Cosmetic = PlayerPawn->GetCosmeticMesh();
	const bool bConfigured = Cosmetic && Cosmetic->GetSkeletalMeshAsset() != nullptr;
	bool bSane = false;
	float RootDist = -1.0f;
	if (bConfigured)
	{
		// the despawn bug's signature: skin bones garbage/exploded. Sane = the skin's root bone
		// sits within 500 uu of the capsule and contains no NaNs.
		const FVector RootPos = Cosmetic->GetBoneLocation(Cosmetic->GetBoneName(0));
		RootDist = static_cast<float>(FVector::Dist(RootPos, PlayerPawn->GetActorLocation()));
		bSane = !RootPos.ContainsNaN() && RootDist < 500.0f && Cosmetic->IsVisible()
			&& PlayerPawn->IsCosmeticRetargeterLive();
	}
	bHitProbePlayerOK = bConfigured && bSane;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HIT-PROBE player %s configured=%d rootDist=%.1f visible=%d live=%d"),
		bHitProbePlayerOK ? TEXT("OK") : TEXT("FAIL"), bConfigured ? 1 : 0, RootDist,
		(bConfigured && Cosmetic->IsVisible()) ? 1 : 0,
		(bConfigured && PlayerPawn->IsCosmeticRetargeterLive()) ? 1 : 0);

	// phase 2: teleport in front of the nearest living enemy and land a COMBO trace on it
	ACombatEnemy* Nearest = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			const float Dist = static_cast<float>(FVector::Dist(It->GetActorLocation(), PlayerPawn->GetActorLocation()));
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Nearest = *It;
			}
		}
	}
	if (!Nearest)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HIT-PROBE %s (no living enemy for phase 2)"), bHitProbePlayerOK ? TEXT("PASS") : TEXT("FAIL"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	HitProbeEnemy = Nearest;
	HitProbeEnemyHP = Nearest->Stats->GetHealth();
	HitProbeEnemyBaseZ = static_cast<float>(Nearest->GetActorLocation().Z);
	HitProbeEnemyPeakZRise = 0.0f;

	// stand 100 uu from the enemy, face it, swing a combo hit (no charged montage playing ->
	// DoAttackTrace takes the combo/stagger branch)
	const FVector ToEnemy = (Nearest->GetActorLocation() - PlayerPawn->GetActorLocation()).GetSafeNormal2D();
	PlayerPawn->SetActorLocation(Nearest->GetActorLocation() - ToEnemy * 100.0f, false, nullptr, ETeleportType::TeleportPhysics);
	PlayerPawn->SetActorRotation(ToEnemy.Rotation());
	PlayerPawn->DoAttackTrace(FName(TEXT("hand_r")));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HIT-PROBE combo trace on %s hp=%.1f"), *Nearest->GetName(), HitProbeEnemyHP);

	// launch detector: sample the enemy's Z rise (a 350 velchange launch peaks ~62 uu; a
	// stagger stays flat)
	World->GetTimerManager().SetTimer(HitProbeSampleTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (const ACombatEnemy* Enemy = HitProbeEnemy.Get())
		{
			HitProbeEnemyPeakZRise = FMath::Max(HitProbeEnemyPeakZRise,
				static_cast<float>(Enemy->GetActorLocation().Z) - HitProbeEnemyBaseZ);
		}
	}), 0.016f, true);

	FTimerHandle EnemyCheckTimer;
	World->GetTimerManager().SetTimer(EnemyCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckHitProbeEnemy), 1.2f, false);
}

void UWrecktideRewardSubsystem::CheckHitProbeEnemy()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(HitProbeSampleTimer);
	}

	const ACombatEnemy* Enemy = HitProbeEnemy.Get();
	const float NewHP = (Enemy && Enemy->Stats) ? Enemy->Stats->GetHealth() : -1.0f;
	const bool bHitLanded = Enemy != nullptr && NewHP >= 0.0f && NewHP < HitProbeEnemyHP;
	const bool bStagger = HitProbeEnemyPeakZRise < 50.0f;

	const bool bPass = bHitProbePlayerOK && bHitLanded && bStagger;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HIT-PROBE %s playerSkin=%d hitLanded=%d (hp %.1f->%.1f) stagger=%d peakZRise=%.1f"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bHitProbePlayerOK ? 1 : 0, bHitLanded ? 1 : 0, HitProbeEnemyHP, NewHP,
		bStagger ? 1 : 0, HitProbeEnemyPeakZRise);
	ScheduleProbeQuit(2.0f);
}

namespace
{
/** One enemy's skin-wiring read, shared by all three skin-probe phases. WasRecentlyRendered is
 *  LOGGED but never gated: an off-screen enemy is healthy, and headless RHIs never render. */
bool EnemySkinWiringOK(ACombatEnemy* Enemy, const TCHAR* PhaseTag)
{
	USkeletalMeshComponent* Cosmetic = Enemy ? Enemy->GetCosmeticMesh() : nullptr;
	if (!Cosmetic || !Cosmetic->GetSkeletalMeshAsset())
	{
		return false;
	}
	const bool bSkinVisible = Cosmetic->IsVisible();
	const bool bDriverHidden = Enemy->GetMesh() && !Enemy->GetMesh()->IsVisible();
	const UAnimInstance* Anim = Cosmetic->GetAnimInstance();
	const FString ABPName = Anim ? Anim->GetClass()->GetName() : TEXT("None");
	const bool bLive = Enemy->IsCosmeticRetargeterLive();
	const UWorld* EnemyWorld = Enemy->GetWorld();
	const bool bRendered = EnemyWorld && (EnemyWorld->GetTimeSeconds() - Cosmetic->GetLastRenderTime()) < 2.0f;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ESKIN-PROBE %s enemy=%s skin=%s visible=%d driverHidden=%d abp=%s retargeterLive=%d rendered=%d"),
		PhaseTag, *Enemy->GetName(), *Cosmetic->GetSkeletalMeshAsset()->GetName(),
		bSkinVisible ? 1 : 0, bDriverHidden ? 1 : 0, *ABPName, bLive ? 1 : 0, bRendered ? 1 : 0);
	return bSkinVisible && bDriverHidden && bLive && ABPName.Contains(TEXT("ABP_GenericRetarget"));
}
}

void UWrecktideRewardSubsystem::RunEnemySkinProbe()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		// every probe path must reach a verdict + quit; without a world we can only scream
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ESKIN-PROBE FAIL reason=no-world"));
		return;
	}

	// Phase 1: every LIVING configured enemy must be fully hooked. Dead-but-not-yet-destroyed
	// enemies are phase 2's job; counting them here would double-judge mid-ragdoll actors.
	UGameInstance* GameInstance = World->GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	int32 Checked = 0;
	int32 Configured = 0;
	int32 Good = 0;
	int32 SkinMismatches = 0;
	TSet<FName> Rows;
	TSet<FName> Skins;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		ACombatEnemy* Enemy = *It;
		++Checked;
		if (!Enemy->Stats || !Enemy->Stats->IsAlive())
		{
			continue;
		}
		USkeletalMeshComponent* Cosmetic = Enemy->GetCosmeticMesh();
		if (!Cosmetic || !Cosmetic->GetSkeletalMeshAsset())
		{
			continue;
		}
		++Configured;
		Rows.Add(Enemy->EnemyRowName);
		Skins.Add(Cosmetic->GetSkeletalMeshAsset()->GetFName());
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ESKIN-PROBE enemy %s row=%s skin=%s"),
			*Enemy->GetName(), *Enemy->EnemyRowName.ToString(), *Cosmetic->GetSkeletalMeshAsset()->GetName());

		// Row -> skin consistency (review 2026-09-02): Enemies.csv names each row's skin, so a
		// configured enemy must wear exactly that mesh; distinct-count arithmetic alone could pass a
		// mismatch. Rows without a SkinMesh keep the class default and are not judged here.
		if (const FEnemyTableRow* Row = DataSubsystem ? DataSubsystem->GetEnemyRow(Enemy->EnemyRowName) : nullptr)
		{
			if (!Row->SkinMesh.IsNull())
			{
				const FString Expected = Row->SkinMesh.ToString();
				const FString Actual = Cosmetic->GetSkeletalMeshAsset()->GetPathName();
				if (!Expected.Equals(Actual, ESearchCase::IgnoreCase))
				{
					++SkinMismatches;
					UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ESKIN-PROBE skin MISMATCH %s row=%s expected=%s actual=%s"),
						*Enemy->GetName(), *Enemy->EnemyRowName.ToString(), *Expected, *Actual);
				}
			}
		}

		if (EnemySkinWiringOK(Enemy, TEXT("steady")))
		{
			++Good;
			if (!EnemySkinProbeTarget.IsValid())
			{
				EnemySkinProbeTarget = Enemy;
			}
		}
	}

	// Variety: every configured enemy wears its row's skin (above). Row and skin counts are logged
	// as diagnostics only: two rows may legitimately share one mesh, and a map whose spawners all use
	// one row is still correct.
	const bool bVarietyOK = SkinMismatches == 0;
	bEnemySkinSteadyOK = Configured > 0 && Good == Configured && bVarietyOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ESKIN-PROBE steady %s enemies=%d configuredAlive=%d good=%d rows=%d skins=%d mismatches=%d variety=%s"),
		bEnemySkinSteadyOK ? TEXT("OK") : TEXT("FAIL"), Checked, Configured, Good, Rows.Num(), Skins.Num(), SkinMismatches,
		bVarietyOK ? TEXT("OK") : TEXT("FAIL"));

	if (!bEnemySkinSteadyOK || !EnemySkinProbeTarget.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ESKIN-PROBE FAIL (steady phase; no ragdoll target)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Phase 2a: one real hit = TakeDamage's partial-ragdoll blend on the hidden driver
	// (SetPhysicsBlendWeight 0.5). The wiring must survive it.
	EnemySkinProbeTarget->TakeDamage(1.0f, FDamageEvent(), nullptr, nullptr);
	FTimerHandle DamageCheckTimer;
	World->GetTimerManager().SetTimer(DamageCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckEnemySkinAfterDamage), 1.5f, false);
}

void UWrecktideRewardSubsystem::CheckEnemySkinAfterDamage()
{
	UWorld* World = GetWorld();
	ACombatEnemy* Target = EnemySkinProbeTarget.Get();
	if (!World || !Target)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ESKIN-PROBE FAIL reason=target-lost-after-damage"));
		if (World)
		{
			ScheduleProbeQuit(2.0f);
		}
		return;
	}

	bEnemySkinRagdollOK = EnemySkinWiringOK(Target, TEXT("ragdoll"));

	// Phase 2b: kill it (per-hit damage is capped, so hit-loop like the fungate probe) and
	// re-check during the death-ragdoll window (RemoveFromLevel fires at +5s, we check at +1.5s).
	for (int32 Hit = 0; Hit < 12 && Target->Stats && Target->Stats->IsAlive(); ++Hit)
	{
		Target->TakeDamage(1000.0f, FDamageEvent(), nullptr, nullptr);
	}
	FTimerHandle DeathCheckTimer;
	World->GetTimerManager().SetTimer(DeathCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckEnemySkinAfterDeath), 1.5f, false);
}

void UWrecktideRewardSubsystem::CheckEnemySkinAfterDeath()
{
	ACombatEnemy* Target = EnemySkinProbeTarget.Get();
	const bool bStillAlive = Target && Target->Stats && Target->Stats->IsAlive();
	// the wiring must survive full death ragdoll; the actor still exists (Destroy is at +5s)
	const bool bDeathOK = Target != nullptr && !bStillAlive && EnemySkinWiringOK(Target, TEXT("death"));

	const bool bPass = bEnemySkinSteadyOK && bEnemySkinRagdollOK && bDeathOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ESKIN-PROBE %s steady=%d ragdoll=%d death=%d (targetValid=%d stillAlive=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bEnemySkinSteadyOK ? 1 : 0, bEnemySkinRagdollOK ? 1 : 0, bDeathOK ? 1 : 0,
		Target != nullptr ? 1 : 0, bStillAlive ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::ScheduleProbeQuit(float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerHandle QuitTimer;
	World->GetTimerManager().SetTimer(QuitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
	}), DelaySeconds, false);
}

void UWrecktideRewardSubsystem::RunAttackProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ATTACK-PROBE FAIL reason=no-player-pawn"));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ATTACK-PROBE FAIL (no pawn to probe)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// the gate must test the GameMode default pawn (the reparented CBP after wiring), not
	// whatever pawn happens to be possessed - a swing on BP_CombatCharacter proves nothing
	const AGameModeBase* GameMode = World->GetAuthGameMode();
	const UClass* DefaultPawn = GameMode ? GameMode->DefaultPawnClass.Get() : nullptr;
	bAttackProbeWrongPawn = DefaultPawn != nullptr && PlayerPawn->GetClass() != DefaultPawn;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ATTACK-PROBE pawnClass=%s defaultPawn=%s %s"),
		*PlayerPawn->GetClass()->GetName(),
		DefaultPawn ? *DefaultPawn->GetName() : TEXT("None"),
		bAttackProbeWrongPawn ? TEXT("MISMATCH") : TEXT("match"));

	// aim into empty space: directly away from the nearest alive enemy, so this click
	// happens with NO enemy in front and NO enemy aimed at
	FRotator Aim = PlayerController->GetControlRotation();
	ACombatEnemy* Nearest = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			const float DistSq = FVector::DistSquared(It->GetActorLocation(), PlayerPawn->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Nearest = *It;
			}
		}
	}
	if (Nearest)
	{
		const FVector ToEnemy = Nearest->GetActorLocation() - PlayerPawn->GetActorLocation();
		Aim = FRotator(0.0f, ToEnemy.Rotation().Yaw + 180.0f, 0.0f);
	}
	PlayerController->SetControlRotation(Aim);
	AttackProbeAimYaw = Aim.Yaw;

	// the exact entry point a left click reaches
	PlayerPawn->DoComboAttackStart();

	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckAttackProbeSwing, 1), 0.3f, false);
}

void UWrecktideRewardSubsystem::CheckAttackProbeSwing(int32 SwingIndex)
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	UAnimInstance* AnimInstance = PlayerPawn ? PlayerPawn->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ATTACK-PROBE FAIL swing=%d reason=no-anim-instance"), SwingIndex);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// check the combo montage specifically, so an enemy hit-reaction can't fake a pass. Read it off
	// the pawn: since 2026-09-02 the player plays AM_ComboAttack_Player on the UpperBody slot while
	// enemies keep AM_ComboAttack, and a hardcoded path faked a FAIL against the real swing.
	UAnimMontage* ComboMontage = PlayerPawn->GetComboAttackMontage();
	if (!ComboMontage)
	{
		ComboMontage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Variant_Combat/Anims/AM_ComboAttack.AM_ComboAttack"));
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ATTACK-PROBE montage=%s"), ComboMontage ? *ComboMontage->GetName() : TEXT("none"));
	const bool bSwinging = ComboMontage ? AnimInstance->Montage_IsPlaying(ComboMontage) : AnimInstance->IsAnyMontagePlaying();
	const float YawDelta = FMath::Abs(FRotator::NormalizeAxis(PlayerPawn->GetActorRotation().Yaw - AttackProbeAimYaw));
	const bool bSwingOK = bSwinging && YawDelta <= 2.0f;

	// slot truth: a montage can "play" (notifies fire, damage lands) without any visible swing
	// if the ABP has no matching slot node. Fail-closed into the verdict - the invisible-swing
	// state is exactly the silent failure this gate exists to catch.
	if (ComboMontage)
	{
		bool bSlotOKThisSwing = ComboMontage->SlotAnimTracks.Num() > 0;
		for (const FSlotAnimationTrack& Track : ComboMontage->SlotAnimTracks)
		{
			const bool bActive = AnimInstance->IsSlotActive(Track.SlotName);
			bSlotOKThisSwing &= bActive;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ATTACK-PROBE slot=%s activeOnABP=%d abp=%s"),
				*Track.SlotName.ToString(), bActive ? 1 : 0, *AnimInstance->GetClass()->GetName());
		}
		bAttackProbeSlotOK &= bSlotOKThisSwing;
	}
	else
	{
		bAttackProbeSlotOK = false;
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ATTACK-PROBE swing=%d montagePlaying=%d yawDelta=%.1f %s"),
		SwingIndex, bSwinging ? 1 : 0, YawDelta, bSwingOK ? TEXT("OK") : TEXT("BAD"));

	if (SwingIndex == 1)
	{
		bAttackProbeSwing1OK = bSwingOK;

		// a second click from idle proves bIsAttacking cleared and no input is being eaten;
		// wait out the first montage before clicking again
		FTimerHandle SecondClickTimer;
		World->GetTimerManager().SetTimer(SecondClickTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* InnerWorld = GetWorld();
			APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
			if (ACombatCharacter* InnerPawn = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr)
			{
				InnerPawn->DoComboAttackStart();
				FTimerHandle CheckTimer;
				InnerWorld->GetTimerManager().SetTimer(CheckTimer,
					FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckAttackProbeSwing, 2), 0.3f, false);
			}
		}), 4.0f, false);
	}
	else
	{
		const bool bPass = bAttackProbeSwing1OK && bSwingOK && bAttackProbeSlotOK && !bAttackProbeWrongPawn;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ATTACK-PROBE %s (aimed swings x2, slotVisible=%d, defaultPawn=%d)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), bAttackProbeSlotOK ? 1 : 0, bAttackProbeWrongPawn ? 0 : 1);

		ScheduleProbeQuit(2.0f);
	}
}

void UWrecktideRewardSubsystem::RunInputProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-INPUT-PROBE FAIL reason=no-player-pawn"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	InputProbeStart = PlayerPawn->GetActorLocation();
	InputProbePeakSpeed = 0.0f;
	InputProbeMaxDisp = 0.0f;
	bInputProbeSawWalking = false;

	bool bFocused = false;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		bFocused = GEngine->GameViewport->Viewport->HasFocus();
	}
	// The harness polls the log for this exact marker before it focuses the window and sends keys.
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-INPUT-PROBE armed x=%.1f y=%.1f focused=%d"),
		InputProbeStart.X, InputProbeStart.Y, bFocused ? 1 : 0);

	// Passive sampler only: peak speed and max displacement. NEVER calls DoMove - if no real
	// key events arrive, these stay at zero and the verdict fails honestly.
	World->GetTimerManager().SetTimer(InputProbeSampleTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		if (const ACombatCharacter* InnerPawn = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr)
		{
			const float Speed = InnerPawn->GetVelocity().Size2D();
			InputProbePeakSpeed = FMath::Max(InputProbePeakSpeed, Speed);
			InputProbeMaxDisp = FMath::Max(InputProbeMaxDisp,
				static_cast<float>(FVector::Dist2D(InnerPawn->GetActorLocation(), InputProbeStart)));
			const UCharacterMovementComponent* Movement = InnerPawn->GetCharacterMovement();
			if (Movement && Movement->MovementMode == MOVE_Walking && Speed > 50.0f)
			{
				bInputProbeSawWalking = true;
			}
		}
	}), 0.016f, true);

	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckInputProbe), 7.0f, false);
}

void UWrecktideRewardSubsystem::CheckInputProbe()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(InputProbeSampleTimer);
	}

	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-INPUT-PROBE FAIL reason=pawn-lost-during-window (killed by enemies?)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	bool bFocused = false;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		bFocused = GEngine->GameViewport->Viewport->HasFocus();
	}

	// Thresholds: a 3s W-hold at MaxWalkSpeed 500 covers ~1500 uu on the open beach; 150 uu is
	// 10x above idle noise and unreachable without movement input. Peak >= 300 rules out a
	// slow shove registering as a pass.
	const bool bPass = InputProbeMaxDisp >= 150.0f && InputProbePeakSpeed >= 300.0f && bInputProbeSawWalking;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-INPUT-PROBE %s disp=%.1f peak=%.1f walking=%d focused=%d"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		InputProbeMaxDisp, InputProbePeakSpeed, bInputProbeSawWalking ? 1 : 0, bFocused ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunLocoProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-LOCO-PROBE FAIL reason=no-player-pawn"));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE FAIL (no pawn to probe)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	LocoStartLocation = PlayerPawn->GetActorLocation();
	LocoOldClassName = PlayerPawn->GetClass()->GetName();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE start pawnClass=%s"), *LocoOldClassName);

	// Drive via DoMove, the pawn's designed programmatic/touch input entry. Two dead ends are
	// documented here so nobody retries them: (1) InjectInputVectorForAction on the sandbox
	// IA_Move moves the pawn ZERO units - the GAS graph consumes move input through its
	// CharacterInputState pipeline, not a naive action binding; (2) sampling speed only at the
	// END of the drive lies - the arena wall stopped run 1 at 306 units with final speed 3.
	// Peak speed is tracked DURING the drive instead, and the mapping-context check separately
	// proves the Enhanced Input wiring the drive bypasses.
	LocoPeakSpeed = 0.0f;
	World->GetTimerManager().SetTimer(LocoDriveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		if (ACombatCharacter* InnerPawn = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr)
		{
			InnerPawn->DoMove(0.0f, 1.0f);
			LocoPeakSpeed = FMath::Max(LocoPeakSpeed, static_cast<float>(InnerPawn->GetVelocity().Size2D()));
		}
	}), 0.016f, true);

	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckLocoProbeMovement), 2.0f, false);
}

void UWrecktideRewardSubsystem::CheckLocoProbeMovement()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(LocoDriveTimer);
	}

	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-LOCO-PROBE FAIL reason=pawn-lost-during-drive"));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE FAIL (pawn lost during drive)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const float Speed2D = PlayerPawn->GetVelocity().Size2D();
	const UCharacterMovementComponent* Movement = PlayerPawn->GetCharacterMovement();
	const EMovementMode Mode = Movement ? Movement->MovementMode.GetValue() : MOVE_None;
	const float Displacement = FVector::Dist2D(PlayerPawn->GetActorLocation(), LocoStartLocation);
	const UAnimInstance* AnimInstance = PlayerPawn->GetMesh() ? PlayerPawn->GetMesh()->GetAnimInstance() : nullptr;
	const FString ABPName = AnimInstance ? AnimInstance->GetClass()->GetName() : TEXT("None");

	// DoMove bypasses Enhanced Input, so separately prove BOTH mapping contexts are live on the
	// player (spec #12): a reparent that broke the CBP graph's AddMappingContext(IMC_Sandbox)
	// would leave keyboard movement dead while this probe still drives the pawn.
	bool bSandboxCtxOn = false;
	bool bCombatCtxOn = false;
	if (const UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		const UInputMappingContext* SandboxCtx = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Sandbox.IMC_Sandbox"));
		const UInputMappingContext* CombatCtx = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Variant_Combat/Input/IMC_Combat.IMC_Combat"));
		bSandboxCtxOn = SandboxCtx != nullptr && Subsystem->HasMappingContext(SandboxCtx);
		bCombatCtxOn = CombatCtx != nullptr && Subsystem->HasMappingContext(CombatCtx);
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE contexts sandbox=%d combat=%d"), bSandboxCtxOn ? 1 : 0, bCombatCtxOn ? 1 : 0);

	// a stranded attack root-motion scale of 0 would zero all GAS traversal (spec #8 fold); the
	// full vault-displacement assert waits for Chunk D's traversable level geometry
	const float RootScale = PlayerPawn->GetAnimRootMotionTranslationScale();

	// camera evidence for the triple-rig decision: which rig owns the view while the pawn moves
	if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
	{
		const AActor* ViewTarget = PlayerController->GetViewTarget();
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE camera viewTarget=%s camDist=%.0f"),
			ViewTarget ? *ViewTarget->GetName() : TEXT("None"),
			FVector::Dist(CameraManager->GetCameraLocation(), PlayerPawn->GetActorLocation()));
	}

	// cosmetic skin chain (Chunk C): asserted only when the pawn configures a skin, so the
	// fallback pawn and pre-skin runs stay green. Reported as its OWN verdict - a skin
	// regression must never masquerade as a locomotion failure.
	bLocoCosmeticOK = true;
	if (USkeletalMeshComponent* Cosmetic = PlayerPawn->GetCosmeticMesh())
	{
		if (Cosmetic->GetSkeletalMeshAsset() != nullptr)
		{
			const UAnimInstance* CosmeticAnim = Cosmetic->GetAnimInstance();
			const FString CosmeticABP = CosmeticAnim ? CosmeticAnim->GetClass()->GetName() : TEXT("None");
			const bool bSkinVisible = Cosmetic->IsVisible();
			const bool bDrivingHidden = PlayerPawn->GetMesh() && !PlayerPawn->GetMesh()->IsVisible();
			const bool bRetargetABP = CosmeticABP.Contains(TEXT("GenericRetarget"));
			// the retargeter must be SET on the live anim instance - an ABP running with a None
			// retargeter T-poses while every other check here still looks healthy
			const bool bRetargeterLive = PlayerPawn->IsCosmeticRetargeterLive();
			// held weapon: equip the tier-1 melee row through the SAME authoritative path the shop
			// uses, then require the weapon visual to actually render in the hand
			bool bWeaponOK = true;
			if (AWrecktidePlayerState* ProbeState = PlayerController->GetPlayerState<AWrecktidePlayerState>())
			{
				if (ProbeState->EquipWeapon(FName(TEXT("WornCutlass"))))
				{
					const UStaticMeshComponent* Weapon = PlayerPawn->GetWeaponMesh();
					bWeaponOK = Weapon != nullptr && Weapon->IsVisible() && Weapon->GetStaticMesh() != nullptr;
					UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE weapon equip=1 visible=%d mesh=%s"),
						bWeaponOK ? 1 : 0,
						(Weapon && Weapon->GetStaticMesh()) ? *Weapon->GetStaticMesh()->GetName() : TEXT("None"));
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE weapon equip refused (WornCutlass row missing?)"));
					bWeaponOK = false;
				}
			}
			bLocoCosmeticOK = bSkinVisible && bDrivingHidden && bRetargetABP && bRetargeterLive && bWeaponOK;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE cosmetic %s skin=%s visible=%d drivingHidden=%d abp=%s retargeterLive=%d weapon=%d"),
				bLocoCosmeticOK ? TEXT("OK") : TEXT("BAD"), *Cosmetic->GetSkeletalMeshAsset()->GetName(),
				bSkinVisible ? 1 : 0, bDrivingHidden ? 1 : 0, *CosmeticABP, bRetargeterLive ? 1 : 0, bWeaponOK ? 1 : 0);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE cosmetic not configured (skipped)"));
		}
	}

	const bool bVelocityOK = LocoPeakSpeed > 150.0f;
	const bool bModeOK = Mode == MOVE_Walking;
	const bool bABPOK = ABPName.Contains(TEXT("ABP_SandboxCharacter"));
	const bool bDisplacementOK = Displacement > 100.0f;
	const bool bRootScaleOK = FMath::IsNearlyEqual(RootScale, 1.0f);
	bLocoMovementOK = bVelocityOK && bModeOK && bABPOK && bDisplacementOK && bSandboxCtxOn && bCombatCtxOn && bRootScaleOK;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE move %s peakSpeed=%.1f finalSpeed=%.1f mode=%d disp=%.1f abp=%s rootScale=%.2f"),
		bLocoMovementOK ? TEXT("OK") : TEXT("BAD"), LocoPeakSpeed, Speed2D, static_cast<int32>(Mode), Displacement, *ABPName, RootScale);

	// phase 2: die once. The respawned pawn must come back as the SAME class, or first death
	// silently reverts the player off the reparented pirate pawn. Remember the exact pawn and
	// class so a never-died original can't match itself into a vacuous pass.
	LocoOldPawn = PlayerPawn;
	LocoOldClass = PlayerPawn->GetClass();

	// StatsComponent caps any single hit at 40% of max health (anti-one-shot rule), so no lone
	// TakeDamage can kill from full HP - hit until dead, the same way the fun-gate probe does
	int32 KillHits = 0;
	FDamageEvent DamageEvent;
	while (PlayerPawn->Stats && PlayerPawn->Stats->IsAlive() && KillHits < 10)
	{
		PlayerPawn->TakeDamage(100000.0f, DamageEvent, nullptr, PlayerPawn);
		++KillHits;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE kill hits=%d aliveAfter=%d"),
		KillHits, (PlayerPawn->Stats && PlayerPawn->Stats->IsAlive()) ? 1 : 0);

	FTimerHandle RespawnCheckTimer;
	World->GetTimerManager().SetTimer(RespawnCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckLocoProbeRespawn), 6.0f, false);
}

void UWrecktideRewardSubsystem::CheckLocoProbeRespawn()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* NewPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	const FString NewClassName = NewPawn ? NewPawn->GetClass()->GetName() : TEXT("None");

	// the old pawn must actually be GONE and the new pawn must be a DIFFERENT actor of the SAME
	// exact class; a still-alive original matching its own name would pass without any death or
	// respawn cycle ever having run
	const bool bOldGone = !LocoOldPawn.IsValid() || LocoOldPawn->IsActorBeingDestroyed();
	const bool bNewIsOther = NewPawn != nullptr && NewPawn != LocoOldPawn.Get();
	const bool bClassMatch = NewPawn != nullptr && LocoOldClass.IsValid() && NewPawn->GetClass() == LocoOldClass.Get();
	const bool bRespawnOK = bOldGone && bNewIsOther && bClassMatch;
	const bool bPass = bLocoMovementOK && bLocoCosmeticOK && bRespawnOK;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE respawn %s oldGone=%d newIsOther=%d class %s -> %s"),
		bRespawnOK ? TEXT("OK") : TEXT("BAD"), bOldGone ? 1 : 0, bNewIsOther ? 1 : 0, *LocoOldClassName, *NewClassName);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LOCO-PROBE %s (movement=%d cosmetic=%d respawn=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bLocoMovementOK ? 1 : 0, bLocoCosmeticOK ? 1 : 0, bRespawnOK ? 1 : 0);

	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunFungateProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (!PlayerPawn || !PlayerState || !PlayerState->GetEconomy() || !PlayerState->GetSkill())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-PROBE FAIL reason=wiring pawn=%d playerstate=%d"),
			PlayerPawn != nullptr, PlayerState != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	ACombatEnemy* Target = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			const float DistSq = FVector::DistSquared(It->GetActorLocation(), PlayerPawn->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Target = *It;
			}
		}
	}

	if (!Target)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-PROBE FAIL reason=no-alive-enemy"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const int64 GoldBefore = PlayerState->GetEconomy()->GetGoldTotal();
	const int64 XPBefore = PlayerState->GetSkill()->GetSkillXP(EWrecktideSkill::Melee);
	int32 Hits = 0;
	while (Target->Stats->IsAlive() && Hits < 40)
	{
		FDamageEvent DamageEvent;
		Target->TakeDamage(PlayerPawn->GetAttackDamage(), DamageEvent, nullptr, PlayerPawn);
		++Hits;
	}

	const int64 GoldAfter = PlayerState->GetEconomy()->GetGoldTotal();
	const int64 XPAfter = PlayerState->GetSkill()->GetSkillXP(EWrecktideSkill::Melee);
	const bool bPlateBands = UCombatLifeBar::GetRelativeLevelNameColor(1, 6).Equals(
		FLinearColor(0.55f, 0.55f, 0.55f, 1.0f))
		&& UCombatLifeBar::GetRelativeLevelNameColor(3, 6).Equals(
			FLinearColor(0.20f, 0.90f, 0.28f, 1.0f))
		&& UCombatLifeBar::GetRelativeLevelNameColor(5, 6).Equals(
			FLinearColor(1.0f, 0.82f, 0.18f, 1.0f))
		&& UCombatLifeBar::GetRelativeLevelNameColor(8, 6).Equals(
			FLinearColor(0.95f, 0.12f, 0.10f, 1.0f));
	const bool bPass = !Target->Stats->IsAlive() && GoldAfter > GoldBefore && bPlateBands;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-PROBE %s hits=%d row=%s gold %lld->%lld meleeXP %lld->%lld plates=%d"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), Hits, *Target->EnemyRowName.ToString(),
		GoldBefore, GoldAfter, XPBefore, XPAfter, bPlateBands ? 1 : 0);

	FTimerHandle QuitTimer;
	World->GetTimerManager().SetTimer(QuitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
	}), 3.0f, false);
}

void UWrecktideRewardSubsystem::RunPlateProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FName ProbeRowName(TEXT("Cutthroat"));
	const FEnemyTableRow* ProbeRow = Data ? Data->GetEnemyRow(ProbeRowName) : nullptr;
	UClass* EnemyClass = nullptr;
	if (World)
	{
		for (TActorIterator<ACombatEnemy> It(World); It; ++It)
		{
			if (It->Stats && It->Stats->IsAlive())
			{
				EnemyClass = It->GetClass();
				break;
			}
		}
	}

	if (!World || !PlayerPawn || !HUD || !ProbeRow || !EnemyClass)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PLATE-PROBE FAIL (reason=wiring world=%d pawn=%d hud=%d row=%d class=%d)"),
			World != nullptr, PlayerPawn != nullptr, HUD != nullptr, ProbeRow != nullptr, EnemyClass != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const FVector PlayerStart = PlayerPawn->GetActorLocation();
	const FVector SpawnLocation = PlayerStart + FVector(2000.0f, 0.0f, 0.0f);
	const FVector TowardPlayer = (PlayerStart - SpawnLocation).GetSafeNormal2D();
	const FTransform SpawnTransform(TowardPlayer.Rotation(), SpawnLocation);
	ACombatEnemy* ProbeEnemy = World->SpawnActorDeferred<ACombatEnemy>(
		EnemyClass,
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!ProbeEnemy)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PLATE-PROBE FAIL (reason=spawn)"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	ProbeEnemy->EnemyRowName = ProbeRowName;
	UGameplayStatics::FinishSpawningActor(ProbeEnemy, SpawnTransform);

	// Hold the probe's own actors strongly for as long as the probe runs. Nothing else in the world
	// owns a probe-spawned enemy and every staged check reads it back through a weak pointer.
	PlateProbeEnemies.Reset();
	PlateProbeEnemies.Add(ProbeEnemy);

	// The boss row is the tallest thing that wears a plate, so the head anchor has to hold on it too.
	// Spawn one beside the probe enemy, measure it the instant its BeginPlay has anchored the plate,
	// and take it straight back out: it never ticks, so it cannot join the staged chase run.
	const float MinPlateClearance = 10.0f;
	bool bBossHeadAnchored = false;
	float BossPlateClearance = 0.0f;
	{
		const FTransform BossTransform(TowardPlayer.Rotation(), SpawnLocation + FVector(0.0f, 600.0f, 0.0f));
		ACombatEnemy* BossEnemy = World->SpawnActorDeferred<ACombatEnemy>(
			EnemyClass,
			BossTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (BossEnemy)
		{
			BossEnemy->EnemyRowName = FName(TEXT("SkeletonCaptain"));
			UGameplayStatics::FinishSpawningActor(BossEnemy, BossTransform);
			bBossHeadAnchored = BossEnemy->IsPlateAboveHead(MinPlateClearance, BossPlateClearance);
			BossEnemy->Destroy();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-PLATE-PROBE boss spawn failed, boss head check cannot run"));
		}
	}

	struct FPlateProbeRuntimeState
	{
		TWeakObjectPtr<ACombatEnemy> Enemy;
		TWeakObjectPtr<ACombatCharacter> Player;
		TWeakObjectPtr<AWrecktideHUD> HUD;
		FText ExpectedName;
		FVector PlayerStart = FVector::ZeroVector;
		FVector TowardPlayer = FVector::ForwardVector;
		FTimerHandle Timer;
		float StageStartTime = 0.0f;
		float SpawnDistance = 0.0f;
		float NearTeleportDistance = 0.0f;
		float HomeDistance = 1000000.0f;
		int32 Stage = 0;
		bool bFarHidden = false;
		bool bNearVisible = false;
		bool bNameMatches = false;
		bool bNoStateText = false;
		bool bDamageApplied = false;
		bool bChasing = false;
		bool bChaseVisible = false;
		bool bSawReturning = false;
		bool bHomeHidden = false;

		/** Head anchor (spec C32): the plate must clear the capsule crown and hang from its bottom
		 *  edge, on the probe enemy, on the boss row and on the pirate's own bar. */
		float MinClearance = 10.0f;
		bool bHeadAnchored = false;
		float HeadClearance = 0.0f;
		bool bBossHeadAnchored = false;
		float BossClearance = 0.0f;
		bool bPlayerHeadAnchored = false;
		float PlayerClearance = 0.0f;
	};

	const TSharedRef<FPlateProbeRuntimeState> ProbeState = MakeShared<FPlateProbeRuntimeState>();
	ProbeState->Enemy = ProbeEnemy;
	ProbeState->Player = PlayerPawn;
	ProbeState->HUD = HUD;
	ProbeState->ExpectedName = ProbeRow->DisplayName;
	ProbeState->PlayerStart = PlayerStart;
	ProbeState->TowardPlayer = TowardPlayer.IsNearlyZero() ? FVector::BackwardVector : TowardPlayer;
	ProbeState->StageStartTime = World->GetTimeSeconds();
	ProbeState->SpawnDistance = FVector::Distance(ProbeEnemy->GetActorLocation(), PlayerStart);
	ProbeState->MinClearance = MinPlateClearance;
	ProbeState->bBossHeadAnchored = bBossHeadAnchored;
	ProbeState->BossClearance = BossPlateClearance;

	auto FinishPlateProbe = [this, World, ProbeState](bool bTimedOut)
	{
		// The repeating timer's delegate OWNS this lambda. FTimerManager::InternalClearTimer removes
		// the timer data outright when a timer clears itself from inside its own callback, which
		// destroys the delegate and every capture with it - including the shared state this body then
		// went on to read. That freed read is the packaged FATAL in FWeakObjectPtr::Get; the editor
		// build only survived it because the freed bytes happened to still be intact. Copy what is
		// needed onto the stack FIRST, then never touch a capture again.
		UWrecktideRewardSubsystem* const Subsystem = this;
		UWorld* const ProbeWorld = World;
		const TSharedRef<FPlateProbeRuntimeState> State = ProbeState;
		ProbeWorld->GetTimerManager().ClearTimer(State->Timer);

		ACombatEnemy* Enemy = State->Enemy.Get();
		ACombatCharacter* Player = State->Player.Get();
		if (Enemy)
		{
			State->HomeDistance = FVector::Dist2D(Enemy->GetActorLocation(), Enemy->GetHomeLocation());
			State->bHomeHidden = Enemy->GetAggroState() == EEnemyAggroState::Idle
				&& !Enemy->IsEnemyPlateVisible()
				&& State->HomeDistance <= Enemy->ReturnArrivalRadius + 10.0f;
		}
		const bool bDistances = FMath::IsNearlyEqual(State->SpawnDistance, 2000.0f, 25.0f)
			&& FMath::IsNearlyEqual(State->NearTeleportDistance, 900.0f, 25.0f);
		const bool bPass = !bTimedOut && Enemy && Player && bDistances
			&& State->bFarHidden && State->bNearVisible && State->bNameMatches
			&& State->bNoStateText && State->bDamageApplied && State->bChasing
			&& State->bChaseVisible && State->bSawReturning && State->bHomeHidden
			&& State->bHeadAnchored && State->bBossHeadAnchored && State->bPlayerHeadAnchored;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PLATE-PROBE %s (farHidden=%d far=%.0f nearVisible=%d near=%.0f name=%d stateText=%d damage=%d chasing=%d chaseVisible=%d returning=%d homeHidden=%d homeDist=%.0f head=%d headGap=%.0f boss=%d bossGap=%.0f player=%d playerGap=%.0f timeout=%d)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), State->bFarHidden ? 1 : 0, State->SpawnDistance,
			State->bNearVisible ? 1 : 0, State->NearTeleportDistance,
			State->bNameMatches ? 1 : 0, State->bNoStateText ? 0 : 1,
			State->bDamageApplied ? 1 : 0, State->bChasing ? 1 : 0,
			State->bChaseVisible ? 1 : 0, State->bSawReturning ? 1 : 0,
			State->bHomeHidden ? 1 : 0, State->HomeDistance,
			State->bHeadAnchored ? 1 : 0, State->HeadClearance,
			State->bBossHeadAnchored ? 1 : 0, State->BossClearance,
			State->bPlayerHeadAnchored ? 1 : 0, State->PlayerClearance, bTimedOut ? 1 : 0);
		if (Player)
		{
			Player->SetActorLocation(State->PlayerStart, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (Enemy)
		{
			Enemy->Destroy();
		}
		Subsystem->PlateProbeEnemies.Reset();
		Subsystem->ScheduleProbeQuit(2.0f);
	};

	World->GetTimerManager().SetTimer(ProbeState->Timer,
		FTimerDelegate::CreateWeakLambda(this, [World, ProbeState, FinishPlateProbe]()
		{
			ACombatEnemy* Enemy = ProbeState->Enemy.Get();
			ACombatCharacter* Player = ProbeState->Player.Get();
			AWrecktideHUD* ProbeHUD = ProbeState->HUD.Get();
			if (!Enemy || !Player || !ProbeHUD || !Enemy->Stats || !Enemy->Stats->IsAlive())
			{
				FinishPlateProbe(false);
				return;
			}

			const float Elapsed = World->GetTimeSeconds() - ProbeState->StageStartTime;
			switch (ProbeState->Stage)
			{
			case 0:
				if (Elapsed < 0.75f)
				{
					return;
				}
				ProbeState->bFarHidden = Enemy->GetAggroState() == EEnemyAggroState::Idle
					&& !Enemy->IsEnemyPlateVisible();
				Player->SetActorLocation(Enemy->GetHomeLocation() + ProbeState->TowardPlayer * 900.0f,
					false, nullptr, ETeleportType::TeleportPhysics);
				ProbeState->NearTeleportDistance = FVector::Distance(Player->GetActorLocation(), Enemy->GetHomeLocation());
				ProbeState->Stage = 1;
				ProbeState->StageStartTime = World->GetTimeSeconds();
				break;

			case 1:
				if (Elapsed < 1.0f)
				{
					return;
				}
				ProbeState->bNearVisible = Enemy->IsEnemyPlateVisible();
				ProbeState->bNameMatches = Enemy->GetEnemyPlateName().EqualTo(ProbeState->ExpectedName);
				ProbeState->bNoStateText = !ProbeHUD->HasEnemyStateText(Enemy);
				// measured while the plate is actually up and near, not just after spawn
				ProbeState->bHeadAnchored = Enemy->IsPlateAboveHead(ProbeState->MinClearance, ProbeState->HeadClearance);
				ProbeState->bPlayerHeadAnchored = Player->IsLifeBarAboveHead(ProbeState->MinClearance, ProbeState->PlayerClearance);
				{
					const float HealthBefore = Enemy->Stats->GetHealth();
					FDamageEvent DamageEvent;
					const float Applied = Enemy->TakeDamage(1.0f, DamageEvent, nullptr, Player);
					ProbeState->bDamageApplied = Applied > 0.0f && Enemy->Stats->GetHealth() < HealthBefore;
				}
				Player->SetActorLocation(Enemy->GetActorLocation() + ProbeState->TowardPlayer * 2000.0f,
					false, nullptr, ETeleportType::TeleportPhysics);
				ProbeState->Stage = 2;
				ProbeState->StageStartTime = World->GetTimeSeconds();
				break;

			case 2:
				if (Elapsed < 1.0f)
				{
					return;
				}
				ProbeState->bChasing = Enemy->GetAggroState() == EEnemyAggroState::Chasing;
				ProbeState->bChaseVisible = Enemy->IsEnemyPlateVisible();
				Player->SetActorLocation(Enemy->GetHomeLocation() + ProbeState->TowardPlayer * (Enemy->GetLeashRadius() + 1600.0f),
					false, nullptr, ETeleportType::TeleportPhysics);
				ProbeState->Stage = 3;
				ProbeState->StageStartTime = World->GetTimeSeconds();
				break;

			case 3:
				ProbeState->bSawReturning |= Enemy->GetAggroState() == EEnemyAggroState::Returning;
				if (Enemy->GetAggroState() == EEnemyAggroState::Idle && ProbeState->bSawReturning)
				{
					FinishPlateProbe(false);
					return;
				}
				if (Elapsed >= 22.0f)
				{
					FinishPlateProbe(true);
				}
				break;
			}
		}), 0.25f, true);
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
namespace
{
int64 PistolProbeTotalXP(const USkillComponent* Skill, EWrecktideSkill Which)
{
	if (!Skill) { return 0; }
	int64 Total = Skill->GetSkillXP(Which);
	for (int32 Level = 1; Level < Skill->GetSkillLevel(Which); ++Level) { Total += Skill->GetXPToNext(Level); }
	return Total;
}
}

ACombatEnemy* UWrecktideRewardSubsystem::SpawnMusketProbeEnemy(FName Row, const FVector& Location)
{
	if (!MusketProbeEnemyClass) { return nullptr; }
	const FTransform Transform(FRotator(0.0f, 180.0f, 0.0f), Location);
	ACombatEnemy* Enemy = GetWorld()->SpawnActorDeferred<ACombatEnemy>(MusketProbeEnemyClass, Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Enemy) { return nullptr; }
	Enemy->EnemyRowName = Row;
	UGameplayStatics::FinishSpawningActor(Enemy, Transform);
	MusketProbe.Enemy = Enemy;
	return Enemy;
}

void UWrecktideRewardSubsystem::RunMusketProbe()
{
	MusketProbe = FMusketProbeState();
	UWorld* World = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		MusketProbeEnemyClass = It->GetClass();
		It->SetActorTickEnabled(false);
		It->SetActorEnableCollision(false);
		It->GetCharacterMovement()->DisableMovement();
		if (AController* Controller = It->GetController())
		{
			Controller->StopMovement();
			Controller->SetActorTickEnabled(false);
		}
	}
	if (!Pawn || !Pawn->Stats || !MusketProbeEnemyClass) { FinishMusketProbe(); return; }
	MusketProbe.Player = Pawn;
	// Both combat legs use the same grounded, connected corridor. Raw photo coordinates can
	// leave the stationary player inside scenery or above the navmesh after the sea leg.
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ACombatEnemy* EnemyDefaults = MusketProbeEnemyClass->GetDefaultObject<ACombatEnemy>();
	const FVector CorridorStarts[] = {
		FVector(-1200.0f, -1800.0f, 100.0f), FVector(-1200.0f, -3500.0f, 100.0f),
		FVector(1400.0f, 0.0f, 100.0f), FVector(0.0f, -6200.0f, 70.0f)
	};
	bool bCorridor = false;
	for (const FVector& Start : CorridorStarts)
	{
		FNavLocation PlayerFloor, EnemyFloor;
		if (!NavSys || !NavSys->ProjectPointToNavigation(Start, PlayerFloor, FVector(20.0f, 20.0f, 500.0f))
			|| !NavSys->ProjectPointToNavigation(Start + FVector(1400.0f, 0.0f, 0.0f), EnemyFloor, FVector(20.0f, 20.0f, 500.0f))) { continue; }
		UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(World, EnemyFloor.Location, PlayerFloor.Location);
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > 1800.0f) { continue; }
		const FVector PlayerSpot = PlayerFloor.Location + FVector(0.0f, 0.0f, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0f);
		const FVector EnemySpot = EnemyFloor.Location + FVector(0.0f, 0.0f, EnemyDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0f);
		FCollisionQueryParams SightQuery(SCENE_QUERY_STAT(WrecktideMusketProbeCorridor), false, Pawn);
		FHitResult Obstruction;
		if (World->LineTraceSingleByChannel(Obstruction, EnemySpot + FVector(0.0f, 0.0f, EnemyDefaults->BaseEyeHeight),
			PlayerSpot, ECC_Visibility, SightQuery)) { continue; }
		MusketProbe.LandPlayerLocation = PlayerSpot;
		MusketProbe.LandEnemyLocation = EnemySpot;
		bCorridor = true;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE corridor player=%s enemy=%s path=%.0f"),
			*PlayerSpot.ToString(), *EnemySpot.ToString(), Path->GetPathLength());
		break;
	}
	if (!bCorridor)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE setup no clear connected corridor"));
		FinishMusketProbe(); return;
	}
	Pawn->SetActorLocationAndRotation(MusketProbe.LandPlayerLocation, FRotator::ZeroRotator,
		false, nullptr, ETeleportType::TeleportPhysics);
	Pawn->GetCharacterMovement()->StopMovementImmediately();
	Pawn->GetCharacterMovement()->DisableMovement();
	PC->SetControlRotation(FRotator(-5.0f, 0.0f, 0.0f));
	Pawn->GetCameraBoom()->bEnableCameraLag = false;
	Pawn->GetCameraBoom()->bEnableCameraRotationLag = false;
	const FVector MusketCameraLocation = Pawn->GetActorLocation() + FVector(700.0f, -1300.0f, 400.0f);
	const FVector MusketLookAt = Pawn->GetActorLocation() + FVector(700.0f, 0.0f, 30.0f);
	if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(MusketCameraLocation, (MusketLookAt - MusketCameraLocation).Rotation()))
	{
		Camera->GetCameraComponent()->SetFieldOfView(65.0f);
		PC->SetViewTarget(Camera);
	}
	ACombatEnemy* Enemy = SpawnMusketProbeEnemy(TEXT("NavyMusketeer"), MusketProbe.LandEnemyLocation);
	if (!Enemy || !Enemy->Stats || !Enemy->HasRangedProbeSight(Pawn)) { FinishMusketProbe(); return; }
	MusketProbe.Health = Pawn->Stats->GetHealth();
	MusketProbe.Deadline = World->GetTimeSeconds() + Enemy->RangedWindupSeconds + Enemy->RangedCooldownSeconds + 1.0f;
	Enemy->ApplyDamage(1.0f, Pawn, Enemy->GetActorLocation(), FVector::ZeroVector);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE start range=%.0f damage=%.0f cooldown=%.1f windup=%.1f spread=%.1f sight=1"),
		Enemy->RangedRange, Enemy->RangedDamage, Enemy->RangedCooldownSeconds, Enemy->RangedWindupSeconds, Enemy->RangedSpreadDegrees);
	World->GetTimerManager().SetTimer(MusketProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::SampleMusketProbe), 0.05f, true);
}

void UWrecktideRewardSubsystem::SampleMusketProbe()
{
	UWorld* World = GetWorld();
	ACombatCharacter* Pawn = MusketProbe.Player.Get();
	ACombatEnemy* Enemy = MusketProbe.Enemy.Get();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
	if (!Pawn || !Pawn->Stats || !Pawn->Stats->IsAlive() || !Enemy || !Enemy->Stats) { FinishMusketProbe(); return; }
	const float Now = World->GetTimeSeconds();
	const float Distance = FVector::Distance(Pawn->GetActorLocation(), Enemy->GetActorLocation());
	const int32 Shots = Enemy->GetRangedShotsFired();
	const bool bExpired = Now >= MusketProbe.Deadline;
	if (MusketProbe.Stage == 0)
	{
		MusketProbe.MinDistance = FMath::Min(MusketProbe.MinDistance, Distance);
		if (Enemy->IsRangedHolding() && Shots == 0)
		{
			if (!MusketProbe.bSawHold) { MusketProbe.HoldLocation = Enemy->GetActorLocation(); }
			MusketProbe.bSawHold = true;
			MusketProbe.HoldDrift = FMath::Max(MusketProbe.HoldDrift,
				static_cast<float>(FVector::Dist2D(MusketProbe.HoldLocation, Enemy->GetActorLocation())));
		}
		MusketProbe.bTell |= Enemy->IsRangedWindingUp() && Enemy->IsRangedPosePlaying()
			&& Enemy->GetRangedWeaponMesh() && Enemy->GetRangedWeaponMesh()->IsVisible()
			&& Enemy->GetEnemyPlateName().ToString() == TEXT("Navy Musketeer");
		float Number = 0.0f; EWrecktideDamageNumberKind Kind = EWrecktideDamageNumberKind::Normal; bool bDrawn = false;
		MusketProbe.bNumbers |= HUD && HUD->GetLatestDamageNumber(Number, Kind, bDrawn)
			&& Kind == EWrecktideDamageNumberKind::PlayerDamage && bDrawn && FMath::IsNearlyEqual(Number, 12.0f);
		if ((Shots > 0 && MusketProbe.bNumbers) || bExpired)
		{
			MusketProbe.bFire = Shots == 1 && FMath::IsNearlyEqual(MusketProbe.Health - Pawn->Stats->GetHealth(), Enemy->RangedDamage)
				&& FMath::IsNearlyEqual(Enemy->RangedDamage, 12.0f) && MusketProbe.bTell && MusketProbe.bNumbers;
			MusketProbe.bHeld = MusketProbe.bSawHold && MusketProbe.HoldDrift <= 5.0f
				&& MusketProbe.MinDistance > Enemy->CloseAggroRadius && Enemy->GetMeleeTracesFired() == 0;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE first shots=%d healthDrop=%.0f minDistance=%.0f drift=%.1f traces=%d pose=%d redDrawn=%d"),
				Shots, MusketProbe.Health - Pawn->Stats->GetHealth(), MusketProbe.MinDistance, MusketProbe.HoldDrift,
				Enemy->GetMeleeTracesFired(), MusketProbe.bTell, MusketProbe.bNumbers);
			if (!MusketProbe.bFire || !MusketProbe.bHeld) { FinishMusketProbe(); return; }
			MusketProbe.Stage = 1;
			MusketProbe.Shots = Shots;
			MusketProbe.Deadline = Now + Enemy->RangedCooldownSeconds + 1.0f;
		}
		return;
	}
	if (MusketProbe.Stage == 1)
	{
		// Insert cover DURING a real second windup, so the cancellation check is non-vacuous.
		if (!Enemy->IsRangedWindingUp()) { if (bExpired) { MusketProbe.bLos = false; FinishMusketProbe(); } return; }
		AStaticMeshActor* Cover = World->SpawnActor<AStaticMeshActor>();
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cover || !Cube) { MusketProbe.bLos = false; FinishMusketProbe(); return; }
		Cover->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Cover->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Cover->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
		Cover->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Cover->SetActorLocation((Pawn->GetActorLocation() + Enemy->GetActorLocation()) * 0.5f + FVector(0.0f, 0.0f, 100.0f));
		Cover->SetActorScale3D(FVector(0.4f, 40.0f, 6.0f));
		MusketProbe.Cover = Cover;
		MusketProbe.Stage = 2;
		MusketProbe.Deadline = Now + Enemy->RangedCooldownSeconds + 0.1f;
		return;
	}
	if (MusketProbe.Stage == 2)
	{
		MusketProbe.bLos &= Shots == MusketProbe.Shots && !Enemy->HasRangedProbeSight(Pawn)
			&& Distance < Enemy->RangedRange && !Enemy->IsRangedWindingUp();
		if (!bExpired) { return; }
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE cover cancelled=%d shots=%d"), MusketProbe.bLos, Shots);
		if (MusketProbe.Cover.IsValid()) { MusketProbe.Cover->Destroy(); }
		MusketProbe.Stage = 3;
		MusketProbe.Deadline = Now + Enemy->RangedCooldownSeconds + 0.2f;
		Pawn->SetActorLocation(Enemy->GetActorLocation() + FVector(2600.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	if (MusketProbe.Stage == 3)
	{
		MusketProbe.bRange &= Shots == MusketProbe.Shots && Distance > Enemy->RangedRange && !Enemy->IsRangedWindingUp();
		// Keep the player outside the range while the stock chase is free to close.
		Pawn->SetActorLocation(Enemy->GetActorLocation() + FVector(2600.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (!bExpired) { return; }
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE range stopped=%d distance=%.0f"), MusketProbe.bRange, Distance);
		Enemy->Destroy();
		Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		Pawn->SetActorLocation(FVector(-7600.0f, -6200.0f, 60.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Enemy = SpawnMusketProbeEnemy(TEXT("NavyMusketeer"), FVector(-6600.0f, -6200.0f, 70.0f));
		if (!Enemy) { MusketProbe.bSwim = false; FinishMusketProbe(); return; }
		Enemy->ApplyDamage(1.0f, Pawn, Enemy->GetActorLocation(), FVector::ZeroVector);
		MusketProbe.Stage = 4;
		MusketProbe.Shots = 0;
		MusketProbe.Deadline = Now + Enemy->RangedCooldownSeconds + 1.0f;
		return;
	}
	if (MusketProbe.Stage == 4)
	{
		MusketProbe.bSwim &= Shots == 0;
		MusketProbe.bSawSwimInRange |= Pawn->IsSwimming() && Distance < Enemy->RangedRange
			&& Enemy->HasRangedProbeSight(Pawn) && Enemy->GetAggroState() == EEnemyAggroState::Chasing;
		if (!bExpired) { return; }
		MusketProbe.bSwim &= MusketProbe.bSawSwimInRange;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE sea stopped=%d swimming=%d inRangeSight=%d distance=%.0f"),
			MusketProbe.bSwim, Pawn->IsSwimming(), MusketProbe.bSawSwimInRange, Distance);
		// Move the damaged guard inland so returning and healing require a real walk home.
		Enemy->SetActorLocation(Enemy->HomeLocation + FVector(600.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->SetActorLocation(Enemy->GetActorLocation() + FVector(3000.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->GetCharacterMovement()->StopMovementImmediately();
		Pawn->GetCharacterMovement()->DisableMovement();
		Enemy->ApplyDamage(10.0f, Pawn, Enemy->GetActorLocation(), FVector::ZeroVector);
		MusketProbe.bLeash = Enemy->Stats->GetHealth() < Enemy->Stats->GetMaxHealth();
		MusketProbe.Stage = 5;
		MusketProbe.Deadline = Now + Enemy->GetDeaggroSeconds() + Enemy->ReturnTimeoutSeconds + 1.0f;
		return;
	}
	if (MusketProbe.Stage == 5)
	{
		// Sustain the escape gap for the existing de-aggro timer. A fast chase can otherwise
		// catch a stationary player before that timer expires and legitimately enter gun range.
		Pawn->SetActorLocation(Enemy->GetActorLocation() + FVector(3000.0f, 0.0f, 0.0f),
			false, nullptr, ETeleportType::TeleportPhysics);
		MusketProbe.bLeash &= Shots == 0 && !Enemy->IsRangedWindingUp();
		MusketProbe.bSawReturning |= Enemy->GetAggroState() == EEnemyAggroState::Returning;
		const bool bHome = Enemy->GetAggroState() == EEnemyAggroState::Idle
			&& FVector::Dist2D(Enemy->HomeLocation, Enemy->GetActorLocation()) < Enemy->ReturnArrivalRadius
			&& FMath::IsNearlyEqual(Enemy->Stats->GetHealth(), Enemy->Stats->GetMaxHealth());
		if (!bHome && !bExpired) { return; }
		MusketProbe.bLeash &= bHome && MusketProbe.bSawReturning && Enemy->GetCharacterMovement()->MaxWalkSpeed > 0.0f;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE return healed=%d returning=%d home=%d speedLimit=%.0f"),
			MusketProbe.bLeash, MusketProbe.bSawReturning, bHome, Enemy->GetCharacterMovement()->MaxWalkSpeed);
		Enemy->Destroy();
		Pawn->SetActorLocation(MusketProbe.LandPlayerLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->GetCharacterMovement()->StopMovementImmediately();
		Pawn->GetCharacterMovement()->DisableMovement();
		Enemy = SpawnMusketProbeEnemy(TEXT("NavyCadet"), MusketProbe.LandEnemyLocation);
		if (!Enemy) { FinishMusketProbe(); return; }
		Enemy->ApplyDamage(1.0f, Pawn, Enemy->GetActorLocation(), FVector::ZeroVector);
		MusketProbe.Stage = 6;
		// Allow the stock tree to finish its random flank before it selects a melee approach.
		MusketProbe.Deadline = Now + 20.0f;
		return;
	}
	if (MusketProbe.Stage == 6)
	{
		MusketProbe.CadetMinDistance = FMath::Min(MusketProbe.CadetMinDistance, Distance);
		MusketProbe.CadetPeakSpeed = FMath::Max(MusketProbe.CadetPeakSpeed, static_cast<float>(Enemy->GetVelocity().Size2D()));
		MusketProbe.bCadet |= Distance <= Enemy->CloseAggroRadius && Enemy->RangedRange == 0.0f
			&& Shots == 0 && Enemy->GetAggroState() == EEnemyAggroState::Chasing && MusketProbe.CadetPeakSpeed >= 80.0f;
		if (!MusketProbe.bCadet && !bExpired) { return; }
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE cadet closed=%d distance=%.0f min=%.0f peakSpeed=%.0f player=%s enemy=%s mode=%d"),
			MusketProbe.bCadet, Distance, MusketProbe.CadetMinDistance, MusketProbe.CadetPeakSpeed,
			*Pawn->GetActorLocation().ToString(), *Enemy->GetActorLocation().ToString(), Enemy->GetCharacterMovement()->MovementMode.GetValue());
		Enemy->Destroy();
		Enemy = SpawnMusketProbeEnemy(TEXT("NavyMusketeer"), Pawn->GetActorLocation() + FVector(100.0f, 0.0f, 0.0f));
		if (!Enemy) { FinishMusketProbe(); return; }
		Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Enemy->ApplyDamage(1.0f, Pawn, Enemy->GetActorLocation(), FVector::ZeroVector);
		MusketProbe.Stage = 7;
		MusketProbe.Deadline = Now + 8.0f;
		return;
	}
	if (MusketProbe.Stage == 7)
	{
		// The cadet leg proves autonomous closing. This extra check dispatches the same entry
		// point as the tree's combo task and requires a real montage notify, independently of
		// the tree's random choice to flank instead. No trace or damage is called by the probe.
		if (!MusketProbe.bMeleeRequested && Enemy->GetAggroState() == EEnemyAggroState::Chasing)
		{
			MusketProbe.bMeleeRequested = true;
			Enemy->DoAIComboAttack();
		}
		MusketProbe.bFallback |= Enemy->GetMeleeTracesFired() > 0 && Shots == 0 && !Enemy->IsRangedHolding();
		if (!MusketProbe.bFallback && !bExpired) { return; }
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE close melee=%d traces=%d shots=%d distance=%.0f grounded=%d"),
			MusketProbe.bFallback, Enemy->GetMeleeTracesFired(), Shots, Distance, Pawn->GetCharacterMovement()->IsMovingOnGround());
		if (!MusketProbe.bFallback)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE close montage=%s enemyGrounded=%d controller=%s"),
				*GetNameSafe(Enemy->GetCurrentMontage()), Enemy->GetCharacterMovement()->IsMovingOnGround(), *GetNameSafe(Enemy->GetController()));
#if WITH_GAMEPLAY_DEBUGGER
			if (UStateTreeComponent* Tree = Enemy->GetController() ? Enemy->GetController()->FindComponentByClass<UStateTreeComponent>() : nullptr)
			{
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE close tree=%s"), *Tree->GetDebugInfoString());
			}
#endif
		}
		Enemy->Destroy();
		Enemy = SpawnMusketProbeEnemy(TEXT("NavyMusketeer"), Pawn->GetActorLocation() + FVector(1400.0f, 0.0f, 0.0f));
		if (!Enemy) { FinishMusketProbe(); return; }
		// Isolate the same attachment used by boarding, with clear land sight for this range check.
		for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
		{
			Pawn->AttachToActor(*It, FAttachmentTransformRules::KeepWorldTransform);
			MusketProbe.bBoat = true;
			break;
		}
		Enemy->ApplyDamage(1.0f, Pawn, Enemy->GetActorLocation(), FVector::ZeroVector);
		MusketProbe.Stage = 8;
		MusketProbe.Deadline = Now + Enemy->RangedCooldownSeconds + 0.1f;
		return;
	}
	if (MusketProbe.Stage == 8)
	{
		MusketProbe.bBoat &= Shots == 0;
		// Keep the rider beyond the pier exception even while the guard closes normally.
		Pawn->SetActorLocation(Enemy->GetActorLocation() - FVector(1400.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (bExpired)
		{
			MusketProbe.bBoat &= Pawn->GetAttachParentActor() && Enemy->HasRangedProbeSight(Pawn) && !Pawn->IsSwimming();
			Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			FinishMusketProbe();
		}
	}
}

void UWrecktideRewardSubsystem::FinishMusketProbe()
{
	GetWorld()->GetTimerManager().ClearTimer(MusketProbeTimer);
	MusketProbe.bLos &= MusketProbe.Stage > 2;
	MusketProbe.bRange &= MusketProbe.Stage > 3;
	MusketProbe.bSwim &= MusketProbe.Stage > 4;
	MusketProbe.bLeash &= MusketProbe.Stage > 5;
	const bool bMelee = MusketProbe.bCadet && MusketProbe.bFallback;
	const bool bPass = MusketProbe.bFire && MusketProbe.bHeld && MusketProbe.bLos && MusketProbe.bRange
		&& MusketProbe.bSwim && MusketProbe.bLeash && bMelee && MusketProbe.bBoat;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE details stage=%d pose=%d redDrawn=%d cadet=%d closeMelee=%d boat=%d"),
		MusketProbe.Stage, MusketProbe.bTell, MusketProbe.bNumbers, MusketProbe.bCadet, MusketProbe.bFallback, MusketProbe.bBoat);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-PROBE %s (fire=%d held=%d los=%d range=%d swim=%d leash=%d melee=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), MusketProbe.bFire, MusketProbe.bHeld, MusketProbe.bLos,
		MusketProbe.bRange, MusketProbe.bSwim, MusketProbe.bLeash, bMelee);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::ArmMusketCapture()
{
	RunMusketProbe();
	GetWorld()->GetTimerManager().ClearTimer(MusketProbeTimer);
	ACombatEnemy* Enemy = MusketProbe.Enemy.Get();
	ACombatCharacter* Pawn = MusketProbe.Player.Get();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!Enemy || !Pawn || !PC) { return; }
	// Photograph proximity acquisition, without blending the probe's wake-up hit reaction.
	Enemy->Destroy();
	const float CaptureFloorZ = -40.0f;
	Pawn->SetActorLocation(FVector(0.0f, -6200.0f, CaptureFloorZ + Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	const ACombatEnemy* EnemyDefaults = MusketProbeEnemyClass->GetDefaultObject<ACombatEnemy>();
	Enemy = SpawnMusketProbeEnemy(TEXT("NavyMusketeer"), FVector(650.0f, -6200.0f,
		CaptureFloorZ + EnemyDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0f));
	if (!Enemy) { return; }
	FString CaptureMode;
	bMusketCaptureHit = FParse::Value(FCommandLine::Get(), TEXT("MusketForCapture="), CaptureMode) && CaptureMode == TEXT("hit");
	if (!bMusketCaptureHit) { Enemy->RangedWindupSeconds = 60.0f; }
	const FVector CameraLocation = Pawn->GetActorLocation() + FVector(300.0f, -750.0f, 190.0f);
	const FVector LookAt = Pawn->GetActorLocation() + FVector(325.0f, 0.0f, 20.0f);
	ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(CameraLocation, (LookAt - CameraLocation).Rotation());
	if (Camera) { Camera->GetCameraComponent()->SetFieldOfView(65.0f); PC->SetViewTarget(Camera); }
	// Let the rifle pose settle before freezing the windup.
	MusketProbe.CaptureReadyTime = GetWorld()->GetTimeSeconds() + 2.5f;
	GetWorld()->GetTimerManager().SetTimer(MusketProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::SampleMusketCapture), 0.05f, true);
}

void UWrecktideRewardSubsystem::SampleMusketCapture()
{
	ACombatEnemy* Enemy = MusketProbe.Enemy.Get();
	if (!Enemy || GetWorld()->GetTimeSeconds() < MusketProbe.CaptureReadyTime) { return; }
	if (bMusketCaptureHit)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
		AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
		float Amount = 0.0f; EWrecktideDamageNumberKind Kind; bool bDrawn = false;
		if (!Enemy->GetRangedShotsFired() || !HUD || !HUD->GetLatestDamageNumber(Amount, Kind, bDrawn)
			|| !bDrawn || Kind != EWrecktideDamageNumberKind::PlayerDamage) { return; }
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-CAPTURE red damage=%.0f drawn=1"), Amount);
	}
	else if (!Enemy->IsRangedWindingUp() || !Enemy->IsRangedPosePlaying()) { return; }
	const UStaticMeshComponent* Weapon = Enemy->GetRangedWeaponMesh();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-CAPTURE ready time=%.2f held=%d pose=%d weapon=%s location=%s barrel=%s"),
		GetWorld()->GetTimeSeconds(), Enemy->IsRangedHolding(), Enemy->IsRangedPosePlaying(),
		Weapon && Weapon->GetStaticMesh() ? *Weapon->GetStaticMesh()->GetName() : TEXT("None"),
		Weapon ? *Weapon->GetComponentLocation().ToString() : TEXT("None"), Weapon ? *Weapon->GetRightVector().ToString() : TEXT("None"));
	if (Weapon && Weapon->GetAttachParent())
	{
		const FTransform Hand = Weapon->GetAttachParent()->GetSocketTransform(Enemy->RangedAttachBone);
		const FQuat Desired = FRotationMatrix::MakeFromYZ(Enemy->GetActorForwardVector(), FVector::UpVector).ToQuat();
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET-CAPTURE grip upright=%s"), *Hand.InverseTransformRotation(Desired).Rotator().ToString());
	}
	GetWorld()->GetTimerManager().ClearTimer(MusketProbeTimer);
	UGameplayStatics::SetGamePaused(this, true);
}

void UWrecktideRewardSubsystem::RunShootProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	AWrecktidePlayerState* State = PC ? PC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	const UWrecktideDataSubsystem* Data = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FWeaponTableRow* Row = Data ? Data->GetWeaponRow(TEXT("RustyFlintlock")) : nullptr;
	UClass* EnemyClass = nullptr;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		EnemyClass = It->GetClass();
		It->SetActorTickEnabled(false);
		It->GetCharacterMovement()->DisableMovement();
		if (AController* EnemyController = It->GetController())
		{
			EnemyController->StopMovement();
			EnemyController->SetActorTickEnabled(false);
		}
	}
	if (!Pawn || !State || !State->GetProgress() || !State->GetSkill() || !Row || !EnemyClass)
	{
		FinishShootProbe(); return;
	}
	State->GetProgress()->UnlockItem(TEXT("WornCutlass"));
	State->AssignSlot(1, TEXT("WornCutlass"));
	State->GetProgress()->UnlockItem(TEXT("RustyFlintlock"));
	bShootProbeEquip = State->AssignSlot(2, TEXT("RustyFlintlock"));
	State->EquipWeapon(NAME_None);
	Pawn->UseEquipSlot2();
	bShootProbeEquip &= State->EquippedWeaponRow == FName(TEXT("RustyFlintlock"))
		&& State->GetEquippedCombatClass() == EWrecktideSkill::Shooting
		&& State->GetAssignedSlot(1) == FName(TEXT("WornCutlass"))
		&& Pawn->GetWeaponMesh() && Pawn->GetWeaponMesh()->GetStaticMesh()
		&& !State->AssignSlot(1, TEXT("RustyFlintlock")) && !State->AssignSlot(2, TEXT("WornCutlass"));
	Pawn->SetActorLocationAndRotation(FVector(0.0f, -6200.0f, 70.0f), FRotator::ZeroRotator,
		false, nullptr, ETeleportType::TeleportPhysics);
	Pawn->GetCharacterMovement()->StopMovementImmediately();
	Pawn->GetCharacterMovement()->DisableMovement();
	PC->SetControlRotation(FRotator::ZeroRotator);
	Pawn->GetCameraBoom()->bEnableCameraLag = false;
	Pawn->GetCameraBoom()->bEnableCameraRotationLag = false;
	const FTransform TargetTransform(FRotator(0.0f, 180.0f, 0.0f), Pawn->GetActorLocation() + FVector(1500.0f, 0.0f, 0.0f));
	ACombatEnemy* Enemy = World->SpawnActorDeferred<ACombatEnemy>(EnemyClass, TargetTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Enemy) { FinishShootProbe(); return; }
	Enemy->EnemyRowName = TEXT("Cutthroat");
	UGameplayStatics::FinishSpawningActor(Enemy, TargetTransform);
	Enemy->SetActorTickEnabled(false);
	Enemy->GetCharacterMovement()->DisableMovement();
	if (AController* EnemyController = Enemy->GetController())
	{
		EnemyController->StopMovement();
		EnemyController->SetActorTickEnabled(false);
	}
	ShootProbeEnemy = Enemy;
	ShootProbeHealth = Enemy->Stats ? Enemy->Stats->GetHealth() : 0.0f;
	ShootProbeDamage = Row->BaseDamage;
	ShootProbeMeleeXP = PistolProbeTotalXP(State->GetSkill(), EWrecktideSkill::Melee);
	ShootProbeShootingXP = PistolProbeTotalXP(State->GetSkill(), EWrecktideSkill::Shooting);
	const float PistolProbeNormalArm = Pawn->GetCameraBoom()->TargetArmLength;
	const float PistolProbeNormalFOV = Pawn->GetFollowCamera()->FieldOfView;
	const FVector PistolProbeNormalSocket = Pawn->GetCameraBoom()->SocketOffset;
	Pawn->DoChargedAttackStart();
	bShootProbeEquip &= Pawn->IsPistolAiming() && Pawn->GetCameraBoom()->TargetArmLength < PistolProbeNormalArm
		&& Pawn->GetFollowCamera()->FieldOfView < PistolProbeNormalFOV
		&& Pawn->GetCameraBoom()->SocketOffset.Equals(PistolProbeNormalSocket + Pawn->PistolAimSocketOffset);
	Pawn->DoChargedAttackEnd();
	bShootProbeEquip &= !Pawn->IsPistolAiming() && FMath::IsNearlyEqual(Pawn->GetCameraBoom()->TargetArmLength, PistolProbeNormalArm)
		&& FMath::IsNearlyEqual(Pawn->GetFollowCamera()->FieldOfView, PistolProbeNormalFOV)
		&& Pawn->GetCameraBoom()->SocketOffset.Equals(PistolProbeNormalSocket);
	Pawn->DoChargedAttackStart();
	World->GetTimerManager().SetTimer(ShootProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::SampleShootProbe), 0.25f, true);
}

void UWrecktideRewardSubsystem::SampleShootProbe()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	AWrecktidePlayerState* State = PC ? PC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
	ACombatEnemy* Enemy = ShootProbeEnemy.Get();
	if (!Pawn || !State || !HUD || !Enemy || !Enemy->Stats) { FinishShootProbe(); return; }
	const float Now = GetWorld()->GetTimeSeconds();
	if (ShootProbeStage == 2 && Pawn->IsPistolReloading() && HUD->IsPistolReloadArcDrawn()) { bShootProbeSawReloadArc = true; }
	if (ShootProbeStage == 0)
	{
		FVector CameraLocation; FRotator CameraRotation;
		PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
		PC->SetControlRotation((Enemy->GetActorLocation() - CameraLocation).Rotation());
		++ShootProbeStage;
		return;
	}
	if (Now < ShootProbeNextTime) { return; }
	if (ShootProbeStage == 1)
	{
		Pawn->DoComboAttackStart();
		const float After = Enemy->Stats->GetHealth();
		bShootProbeFire = FMath::IsNearlyEqual(ShootProbeHealth - After, ShootProbeDamage)
			&& HUD->GetDamageNumberCount() > 0 && Pawn->IsPistolAiming();
		Pawn->DoComboAttackStart();
		bShootProbeReload = Pawn->IsPistolReloading() && FMath::IsNearlyEqual(Enemy->Stats->GetHealth(), After);
		ShootProbeHealth = After;
		ShootProbeNextTime = Now + Pawn->PistolReloadSeconds + 0.1f;
		++ShootProbeStage;
		return;
	}
	if (ShootProbeStage == 2)
	{
		Pawn->DoComboAttackStart();
		bShootProbeReload &= FMath::IsNearlyEqual(ShootProbeHealth - Enemy->Stats->GetHealth(), ShootProbeDamage);
		bShootProbeBar = HUD->GetDrawnWieldedSlot() == 2 && HUD->GetDrawnSlotLabel(2) == TEXT("Rusty Flintlock")
			&& HUD->IsPistolCrosshairDrawn() && HUD->IsPistolCrosshairGold() && bShootProbeSawReloadArc;
		++ShootProbeStage;
	}
	else if (Enemy->Stats->IsAlive() && !Pawn->IsPistolReloading())
	{
		Pawn->DoComboAttackStart();
		if (++ShootProbeShots > 20) { FinishShootProbe(); return; }
	}
	if (!Enemy->Stats->IsAlive())
	{
		bShootProbeXP = PistolProbeTotalXP(State->GetSkill(), EWrecktideSkill::Shooting) > ShootProbeShootingXP
			&& PistolProbeTotalXP(State->GetSkill(), EWrecktideSkill::Melee) == ShootProbeMeleeXP;
		Pawn->UseEquipSlot1();
		Pawn->DoComboAttackStart();
		UAnimInstance* Anim = Pawn->GetMesh()->GetAnimInstance();
		bShootProbeBack = State->EquippedWeaponRow == FName(TEXT("WornCutlass"))
			&& !Pawn->IsPistolAiming() && Anim && Anim->Montage_IsPlaying(Pawn->GetComboAttackMontage());
		FinishShootProbe();
	}
}

void UWrecktideRewardSubsystem::FinishShootProbe()
{
	GetWorld()->GetTimerManager().ClearTimer(ShootProbeTimer);
	const bool bPass = bShootProbeEquip && bShootProbeFire && bShootProbeReload && bShootProbeXP && bShootProbeBar && bShootProbeBack;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOOT-PROBE %s (equip=%d fire=%d reload=%d xp=%d bar=%d back=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bShootProbeEquip, bShootProbeFire, bShootProbeReload, bShootProbeXP, bShootProbeBar, bShootProbeBack);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunGunsmithProbe()
{
	UWorld* World = GetWorld();
	ACombatPlayerController* PC = Cast<ACombatPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	AWrecktidePlayerState* State = PC ? PC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = State ? State->GetProgress() : nullptr;
	USkillComponent* Skill = State ? State->GetSkill() : nullptr;
	UEconomyComponent* Economy = State ? State->GetEconomy() : nullptr;
	const UWrecktideDataSubsystem* Data = World->GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>();
	AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
	AWrecktideShopStall* Camp = nullptr;
	AWrecktideShopStall* Gunsmith = nullptr;
	AWrecktideShopStall* First = nullptr;
	int32 StallCount = 0;
	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		if (!First) { First = *It; }
		if (It->StallClass == EWrecktideStallClass::Shooting) { Gunsmith = *It; }
		else if (It->StallClass == EWrecktideStallClass::Melee) { Camp = *It; }
		++StallCount;
	}
	AWrecktideMissionGiver* Ada = nullptr;
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->GetGiverId() == FName(TEXT("Ada"))) { Ada = *It; }
	}
	AWrecktideTreasureChest* Magazine = nullptr;
	for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
	{
		if (It->ChestId == FName(TEXT("MagazineChest"))) { Magazine = *It; }
	}
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		MissionProbeEnemyClass = It->GetClass();
		MissionProbeSpawnTransform = It->GetActorTransform();
		It->SetActorTickEnabled(false);
		It->GetCharacterMovement()->DisableMovement();
		if (AController* EnemyController = It->GetController())
		{
			EnemyController->StopMovement();
			EnemyController->SetActorTickEnabled(false);
		}
	}
	if (!Pawn || !State || !Progress || !Skill || !Economy || !Data || !HUD || !Camp || !Gunsmith || !Ada || !Magazine)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-GUNSMITH-PROBE FAIL (stall=0 order=0 filter=0 gate=0 buy=0 reward=0 save=0) reason=wiring"));
		ScheduleProbeQuit(2.0f); return;
	}
	const bool bStall = StallCount == 2 && FVector::Dist2D(Gunsmith->GetActorLocation(), GunsmithLocation) < 10.0f
		&& Gunsmith->SignText == TEXT("GUNSMITH") && Gunsmith->PromptName == TEXT("Tobias Reed, gunsmith")
		&& FVector::Dist2D(Gunsmith->GetActorLocation(), Ada->GetActorLocation()) > 600.0f;
	const bool bOrder = First == Camp && FVector::Dist2D(Camp->GetActorLocation(), FVector(0.0f, -5200.0f, 0.0f)) < 1000.0f;
	Pawn->SetActorLocation(Camp->GetActorLocation() + Camp->GetActorRightVector() * 150.0f, false, nullptr, ETeleportType::TeleportPhysics);
	Camp->OpenShopMenu(Pawn);
	bool bFilter = Camp->GetOfferCount() == 3;
	for (const FName Row : Camp->GetOfferRows())
	{
		const FWeaponTableRow* Weapon = Data->GetWeaponRow(Row);
		bFilter &= Weapon && Weapon->CombatClass == EWrecktideSkill::Melee;
	}
	Camp->ToggleShopMenu(Pawn);
	Pawn->SetActorLocation(Gunsmith->GetActorLocation() + FVector(0.0f, -180.0f, 110.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Gunsmith->OpenShopMenu(Pawn);
	bFilter &= Gunsmith->GetOfferCount() == 1 && Gunsmith->GetSelectedOfferRow() == FName(TEXT("NavyPistol"));
	for (const FName Row : Gunsmith->GetOfferRows())
	{
		const FWeaponTableRow* Weapon = Data->GetWeaponRow(Row);
		bFilter &= Weapon && Weapon->CombatClass == EWrecktideSkill::Shooting;
	}
	Economy->ApplyGoldDelta(500, EGoldSource::Earned, EGoldReason::Adjustment, TEXT("GunsmithProbe"));
	Skill->AwardXP(EWrecktideSkill::Shooting, Skill->GetNotorietyXPForLevel(5) - Skill->GetNotorietyXP(), TEXT("GunsmithProbe"));
	const int64 BeforeGate = Economy->GetGoldTotal();
	Gunsmith->ConfirmMenuSelection();
	const bool bGate = Skill->GetNotorietyLevel() == 5 && Economy->GetGoldTotal() == BeforeGate
		&& !Progress->IsItemUnlocked(TEXT("NavyPistol")) && !State->CanEquipWeapon(TEXT("NavyPistol"));
	Skill->AwardXP(EWrecktideSkill::Shooting, Skill->GetNotorietyXPForLevel(6) - Skill->GetNotorietyXP(), TEXT("GunsmithProbe"));
	Gunsmith->ConfirmMenuSelection();
	const bool bBuy = Skill->GetNotorietyLevel() == 6 && Progress->IsItemUnlocked(TEXT("NavyPistol"))
		&& Economy->GetGoldTotal() == BeforeGate - 220 && State->EquippedWeaponRow == FName(TEXT("NavyPistol"));
	Gunsmith->ToggleShopMenu(Pawn);

	// Ada's conversation and the real chest event drive R02, just like the quest-flow probe.
	State->AssignSlot(2, NAME_None);
	Progress->UnlockItem(TEXT("WornCutlass"));
	State->AssignSlot(1, TEXT("WornCutlass"));
	Progress->SetQuestFlag(TEXT("Story_Reach"), 1);
	FString CompletionToast;
	auto Converse = [this, Ada, HUD, &CompletionToast](EMissionGiverAction Action)
	{
		if (!OpenGiverForFlow(Ada)) { return false; }
		const bool bSelected = Ada->SelectActionLine(Action);
		if (bSelected) { Ada->ConfirmMenuSelection(); CompletionToast = HUD->GetToastText(); }
		Ada->CloseGiverMenu();
		return bSelected;
	};
	bool bReward = !Progress->IsItemUnlocked(TEXT("RustyFlintlock")) && Converse(EMissionGiverAction::Accept);
	for (int32 Index = 0; Index < 3; ++Index) { bReward &= FireMissionProbeKill(TEXT("NavyMarine")); }
	bReward &= Converse(EMissionGiverAction::Complete) && Converse(EMissionGiverAction::Accept);
	Pawn->SetActorLocation(Magazine->GetActorLocation() + FVector(120.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
	bReward &= Magazine->Open(Pawn) && Converse(EMissionGiverAction::Complete);
	bReward &= Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(TEXT("R02_Magazine"))) == 1
		&& Progress->IsItemUnlocked(TEXT("RustyFlintlock")) && State->GetAssignedSlot(2) == FName(TEXT("RustyFlintlock"))
		&& State->EquippedWeaponRow == FName(TEXT("WornCutlass"))
		&& CompletionToast == TEXT("Rusty Flintlock: press 2 to draw it");
	const int64 RewardGold = Economy->GetGoldTotal();
	bReward &= !WrecktideMissionLogic::CompleteRow(State, TEXT("R02_Magazine")) && Economy->GetGoldTotal() == RewardGold;

	// Exercise the Belt row too, through its normal menu action, after the mission assigned it.
	PC->ToggleInventory();
	PC->SetChestTab(EWrecktideChestTab::Weapons);
	bool bBelt = false;
	for (int32 Index = 0; Index < PC->GetMenuRows().Num(); ++Index)
	{
		if (PC->GetMenuRows()[Index].Label == TEXT("Rusty Flintlock"))
		{
			PC->MenuClick(Index); bBelt = State->EquippedWeaponRow == FName(TEXT("RustyFlintlock")); break;
		}
	}
	if (PC->IsInventoryOpen()) { PC->ToggleInventory(); }
	bReward &= bBelt;
	const FString ProbeSlot = TEXT("Wrecktide_PistolGunsmithProbeSlot");
	UGameplayStatics::DeleteGameInSlot(ProbeSlot, 0);
	const bool bWrote = Progress->SaveToSlot(ProbeSlot);
	FWrecktideSaveData Cleared = Progress->BuildSaveData();
	Cleared.UnlockedItems.Remove(FName(TEXT("RustyFlintlock")));
	Cleared.AssignedSlots[1] = NAME_None;
	Cleared.EquippedWeaponRow = TEXT("WornCutlass");
	Progress->ApplySaveData(Cleared);
	const bool bCleared = !Progress->IsItemUnlocked(TEXT("RustyFlintlock")) && State->GetAssignedSlot(2).IsNone();
	const bool bRead = Progress->LoadFromSlot(ProbeSlot);
	const bool bSave = bWrote && bCleared && bRead && Progress->BuildSaveData().SaveVersion == 6
		&& Progress->IsItemUnlocked(TEXT("RustyFlintlock")) && Progress->IsItemUnlocked(TEXT("NavyPistol"))
		&& State->GetAssignedSlot(2) == FName(TEXT("RustyFlintlock")) && State->GetAssignedSlot(1) == FName(TEXT("WornCutlass"))
		&& State->EquippedWeaponRow == FName(TEXT("RustyFlintlock"));
	UGameplayStatics::DeleteGameInSlot(ProbeSlot, 0);
	const bool bPass = bStall && bOrder && bFilter && bGate && bBuy && bReward && bSave;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-GUNSMITH-PROBE %s (stall=%d order=%d filter=%d gate=%d buy=%d reward=%d save=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bStall, bOrder, bFilter, bGate, bBuy, bReward, bSave);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::ArmPistolCapture()
{
	// Reuse the proven scene setup, then stop its verdict sampler. Only the capture owns time now.
	RunShootProbe();
	GetWorld()->GetTimerManager().ClearTimer(ShootProbeTimer);
	ACombatEnemy* Enemy = ShootProbeEnemy.Get();
	ACombatCharacter* Pawn = Cast<ACombatCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Enemy || !Pawn) { return; }
	Enemy->SetActorLocation(Pawn->GetActorLocation() + FVector(700.0f, 0.0f, 0.0f));
	GetWorld()->GetTimerManager().SetTimer(PistolCaptureTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::SamplePistolCapture), 0.1f, true);
}

void UWrecktideRewardSubsystem::SamplePistolCapture()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	ACombatEnemy* Enemy = ShootProbeEnemy.Get();
	if (!PC || !Pawn || !Enemy) { GetWorld()->GetTimerManager().ClearTimer(PistolCaptureTimer); return; }
	if (PistolCaptureStage == 0)
	{
		FVector CameraLocation; FRotator CameraRotation;
		PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
		PC->SetControlRotation((Enemy->GetActorLocation() - CameraLocation).Rotation());
		++PistolCaptureStage;
	}
	else if (PistolCaptureStage == 1)
	{
		Pawn->DoComboAttackStart();
		++PistolCaptureStage;
	}
	else if (Pawn->GetPistolReloadFraction() >= 0.5f)
	{
		GetWorld()->GetTimerManager().ClearTimer(PistolCaptureTimer);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PISTOL-CAPTURE ready target=%d reload=%.2f mesh=%s worldTime=%.2f"),
			Pawn->IsPistolTargetLivingEnemy(), Pawn->GetPistolReloadFraction(),
			Pawn->GetWeaponMesh()->GetStaticMesh() ? *Pawn->GetWeaponMesh()->GetStaticMesh()->GetName() : TEXT("None"),
			GetWorld()->GetTimeSeconds());
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PISTOL-CAPTURE camera=%s pawnForward=%s meshX=%s meshY=%s meshZ=%s"),
			*Pawn->GetFollowCamera()->GetComponentLocation().ToString(), *Pawn->GetActorForwardVector().ToString(),
			*Pawn->GetWeaponMesh()->GetForwardVector().ToString(), *Pawn->GetWeaponMesh()->GetRightVector().ToString(),
			*Pawn->GetWeaponMesh()->GetUpVector().ToString());
		// Freeze this real shot halfway through its real reload so the capture timing cannot miss it.
		UGameplayStatics::SetGamePaused(this, true);
	}
}

void UWrecktideRewardSubsystem::ArmGunsmithCapture()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	AWrecktidePlayerState* State = PC ? PC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (!Pawn || !State) { return; }
	for (TActorIterator<ACombatEnemy> It(GetWorld()); It; ++It)
	{
		It->SetActorTickEnabled(false);
		It->GetCharacterMovement()->DisableMovement();
		if (AController* EnemyController = It->GetController()) { EnemyController->StopMovement(); EnemyController->SetActorTickEnabled(false); }
	}
	for (TActorIterator<AWrecktideShopStall> It(GetWorld()); It; ++It)
	{
		if (It->StallClass != EWrecktideStallClass::Shooting) { continue; }
		Pawn->SetActorLocationAndRotation(It->GetActorLocation() + FVector(0.0f, -200.0f, 110.0f),
			FRotator(0.0f, 90.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->GetCharacterMovement()->DisableMovement();
		PC->SetControlRotation(FRotator(5.0f, 90.0f, 0.0f));
		Pawn->GetCameraBoom()->TargetArmLength = 650.0f;
		It->OpenShopMenu(Pawn);
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PC->GetHUD())) { HUD->ShowToast(It->PromptName, 90.0f); }
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-GUNSMITH-CAPTURE ready at=%s offers=%d"), *It->GetActorLocation().ToString(), It->GetOfferCount());
		break;
	}
}
#endif

void UWrecktideRewardSubsystem::RunXpSkillProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	AWrecktideShopStall* Stall = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
		{
			Stall = *It;
			break;
		}
	}

	const FName EnemyRowName(TEXT("NavyCadet"));
	const FEnemyTableRow* EnemyRow = Data ? Data->GetEnemyRow(EnemyRowName) : nullptr;
	const FWeaponTableRow* CutlassRow = Data ? Data->GetWeaponRow(FName(TEXT("WornCutlass"))) : nullptr;
	if (!World || !PlayerPawn || !PlayerState || !Economy || !Skill || !Progress || !Data || !HUD || !Stall
		|| !EnemyRow || !CutlassRow)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-XPSKILL-PROBE FAIL (wiring world=%d pawn=%d ps=%d economy=%d skill=%d progress=%d data=%d hud=%d stall=%d enemy=%d cutlass=%d)"),
			World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, Economy != nullptr, Skill != nullptr,
			Progress != nullptr, Data != nullptr, HUD != nullptr, Stall != nullptr, EnemyRow != nullptr, CutlassRow != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	MissionProbeEnemyClass.Reset();
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			MissionProbeEnemyClass = It->GetClass();
			MissionProbeSpawnTransform = It->GetActorTransform();
			break;
		}
	}
	if (!MissionProbeEnemyClass.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-XPSKILL-PROBE FAIL (no live enemy spawn recipe)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	auto TotalXP = [Skill](EWrecktideSkill Which) -> int64
	{
		int64 Total = Skill->GetSkillXP(Which);
		for (int32 Level = 1; Level < Skill->GetSkillLevel(Which); ++Level)
		{
			Total += Skill->GetXPToNext(Level);
		}
		return Total;
	};

	const int64 ExpectedXP = EnemyRow->XPAward;
	PlayerState->EquipWeapon(NAME_None);
	const int64 MeleeBeforeA = TotalXP(EWrecktideSkill::Melee);
	const int64 ShootingBeforeA = TotalXP(EWrecktideSkill::Shooting);
	const bool bFiredA = FireMissionProbeKill(EnemyRowName);
	const bool bFeedA = HUD->GetRewardFeedLine(0).Contains(TEXT("Melee XP"));
	const bool bStageA = bFiredA
		&& TotalXP(EWrecktideSkill::Melee) - MeleeBeforeA == ExpectedXP
		&& TotalXP(EWrecktideSkill::Shooting) == ShootingBeforeA && bFeedA;

	PlayerPawn->SetActorLocation(Stall->GetActorLocation() + Stall->GetActorRightVector() * 150.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Stall->OpenShopMenu(PlayerPawn);
	for (int32 Index = 0; Index < Stall->GetOfferCount() && Stall->GetSelectedOfferRow() != FName(TEXT("WornCutlass")); ++Index)
	{
		Stall->MoveMenuSelection(1);
	}
	Economy->ApplyGoldDelta(FMath::Max<int64>(CutlassRow->Price, 1), EGoldSource::Earned,
		EGoldReason::Adjustment, FName(TEXT("XpSkillProbe")));
	Stall->ConfirmMenuSelection();
	const bool bPurchased = Economy->HasPurchasedRow(FName(TEXT("WornCutlass")))
		&& PlayerState->EquippedWeaponRow == FName(TEXT("WornCutlass"));
	if (Stall->IsShopMenuOpen())
	{
		Stall->ToggleShopMenu(PlayerPawn);
	}
	const int64 MeleeBeforeB = TotalXP(EWrecktideSkill::Melee);
	const int64 ShootingBeforeB = TotalXP(EWrecktideSkill::Shooting);
	const bool bFiredB = FireMissionProbeKill(EnemyRowName);
	const bool bFeedB = HUD->GetRewardFeedLine(0).Contains(TEXT("Melee XP"));
	const bool bStageB = bPurchased && bFiredB
		&& TotalXP(EWrecktideSkill::Melee) - MeleeBeforeB == ExpectedXP
		&& TotalXP(EWrecktideSkill::Shooting) == ShootingBeforeB && bFeedB;

	const bool bForcedShooting = PlayerState->EquipWeapon(FName(TEXT("RustyFlintlock")));
	const int64 MeleeBeforeC = TotalXP(EWrecktideSkill::Melee);
	const int64 ShootingBeforeC = TotalXP(EWrecktideSkill::Shooting);
	const bool bFiredC = FireMissionProbeKill(EnemyRowName);
	const bool bFeedC = HUD->GetRewardFeedLine(0).Contains(TEXT("Shooting XP"));
	const bool bStageC = bForcedShooting && bFiredC
		&& TotalXP(EWrecktideSkill::Shooting) - ShootingBeforeC == ExpectedXP
		&& TotalXP(EWrecktideSkill::Melee) == MeleeBeforeC && bFeedC;

	const bool bPass = bStageA && bStageB && bStageC;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-XPSKILL-PROBE %s (unarmed=%d cutlass=%d shooting=%d purchased=%d xp=%lld feeds=%d/%d/%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bStageA ? 1 : 0, bStageB ? 1 : 0, bStageC ? 1 : 0,
		bPurchased ? 1 : 0, ExpectedXP, bFeedA ? 1 : 0, bFeedB ? 1 : 0, bFeedC ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunShopProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	if (!World || !PlayerPawn || !PlayerState || !Economy)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHOP-PROBE FAIL reason=wiring world=%d pawn=%d playerstate=%d economy=%d"),
			World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, Economy != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	AWrecktideShopStall* Stall = nullptr;
	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		Stall = *It;
		break;
	}
	if (!Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHOP-PROBE FAIL reason=no-stall"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	ShopProbeStall = Stall;
	PlayerPawn->SetActorLocation(Stall->GetActorLocation() + Stall->GetActorRightVector() * 150.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Stall->OpenShopMenu(PlayerPawn);
	bShopProbeMenuOK = Stall->IsShopMenuOpen() && Stall->GetOfferCount() > 0;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE menu open=%d offers=%d"),
		Stall->IsShopMenuOpen() ? 1 : 0, Stall->GetOfferCount());
	if (!bShopProbeMenuOK)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE FAIL menu=0 buy=0 cycle=0"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	Stall->MoveMenuSelection(1);
	const FName SelectedRow = Stall->GetSelectedOfferRow();
	UGameInstance* GameInstance = World->GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FWeaponTableRow* WeaponRow = DataSubsystem ? DataSubsystem->GetWeaponRow(SelectedRow) : nullptr;
	if (!WeaponRow)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHOP-PROBE FAIL reason=selected-row-missing row=%s"), *SelectedRow.ToString());
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE FAIL menu=1 buy=0 cycle=0"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	if (USkillComponent* Skill = PlayerState->GetSkill())
	{
		if (!PlayerState->CanEquipWeapon(SelectedRow))
		{
			const int64 NeededXP = Skill->GetNotorietyXPForLevel(WeaponRow->RequiredLevel) - Skill->GetNotorietyXP();
			Skill->AwardXP(WeaponRow->CombatClass, FMath::Max<int64>(NeededXP, 1), FName(TEXT("ShopProbe")));
		}
	}
	const int64 Grant = FMath::Max<int64>(WeaponRow->Price, 1);
	Economy->ApplyGoldDelta(Grant, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("ShopProbe")));
	ShopProbeGoldBefore = Economy->GetGoldTotal();
	ShopProbeEquippedBefore = PlayerState->EquippedWeaponRow;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE buy row=%s grant=%lld goldBefore=%lld equippedBefore=%s"),
		*SelectedRow.ToString(), Grant, ShopProbeGoldBefore, *ShopProbeEquippedBefore.ToString());
	Stall->ConfirmMenuSelection();

	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckShopProbePurchase), 1.0f, false);
}

void UWrecktideRewardSubsystem::CheckShopProbePurchase()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	AWrecktideShopStall* Stall = ShopProbeStall.Get();
	if (!World || !PlayerPawn || !PlayerState || !Economy || !Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHOP-PROBE FAIL reason=state-lost"));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE FAIL menu=%d buy=0 cycle=0"), bShopProbeMenuOK ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const int64 GoldAfter = Economy->GetGoldTotal();
	const FName EquippedAfterBuy = PlayerState->EquippedWeaponRow;
	const bool bEquipChanged = EquippedAfterBuy != ShopProbeEquippedBefore;
	const bool bAlreadyEquipped = EquippedAfterBuy == ShopProbeEquippedBefore
		&& EquippedAfterBuy == Stall->GetSelectedOfferRow();
	const bool bBuyOK = GoldAfter < ShopProbeGoldBefore && (bEquipChanged || bAlreadyEquipped);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE purchase %s gold %lld->%lld equip %s->%s result=%s"),
		bBuyOK ? TEXT("OK") : TEXT("FAIL"), ShopProbeGoldBefore, GoldAfter,
		*ShopProbeEquippedBefore.ToString(), *EquippedAfterBuy.ToString(),
		bEquipChanged ? TEXT("changed") : (bAlreadyEquipped ? TEXT("already-equipped") : TEXT("unchanged")));

	// Key 1 no longer cycles (spec C17): it WIELDS slot 1, which the purchase above just filled.
	// The assertion moved with the behaviour: empty the hand, press 1, the bought weapon is back,
	// and pressing 1 again keeps it rather than cycling on to unarmed.
	Stall->ToggleShopMenu(PlayerPawn);
	const FName BoughtRow = PlayerState->EquippedWeaponRow;
	PlayerState->EquipWeapon(NAME_None);
	PlayerPawn->UseEquipSlot1();
	const FName WieldOne = PlayerState->EquippedWeaponRow;
	PlayerPawn->UseEquipSlot1();
	const FName WieldTwo = PlayerState->EquippedWeaponRow;
	const bool bCycleOK = WieldOne == BoughtRow && WieldTwo == BoughtRow && BoughtRow != NAME_None;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE wield %s bought=%s ->%s->%s"),
		bCycleOK ? TEXT("OK") : TEXT("FAIL"),
		*BoughtRow.ToString(), *WieldOne.ToString(), *WieldTwo.ToString());

	const bool bPass = bShopProbeMenuOK && bBuyOK && bCycleOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHOP-PROBE %s menu=%d buy=%d cycle=%d"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bShopProbeMenuOK ? 1 : 0, bBuyOK ? 1 : 0, bCycleOK ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunQuestProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	const USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!World || !PlayerPawn || !PlayerState || !Progress || !Economy || !Skill || !Data)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (wiring pawn=%d playerstate=%d progress=%d economy=%d skill=%d data=%d)"),
			PlayerPawn != nullptr, PlayerState != nullptr, Progress != nullptr, Economy != nullptr, Skill != nullptr, Data != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const FName CutthroatBounty(TEXT("CutthroatBounty"));
	const FName SkeletonPurge(TEXT("SkeletonPurge"));
	const FName FirstBlood(TEXT("FirstBlood"));
	const FName CutthroatEnemy(TEXT("Cutthroat"));
	const FName NavyCadetEnemy(TEXT("NavyCadet"));

	// Raw GetSkillXP is NOT monotonic: AwardXP subtracts the cost of every level it grants, so a
	// level-up during the probe would read as a NEGATIVE reward. Cumulative XP is the honest measure.
	auto TotalMeleeXP = [Skill]() -> int64
	{
		const int32 Level = Skill->GetSkillLevel(EWrecktideSkill::Melee);
		int64 Total = Skill->GetSkillXP(EWrecktideSkill::Melee);
		for (int32 PastLevel = 1; PastLevel < Level; ++PastLevel)
		{
			Total += Skill->GetXPToNext(PastLevel);
		}
		return Total;
	};

	// STEP 1: the board exists and the table loaded its four bounties.
	AWrecktideQuestBoard* Board = nullptr;
	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		Board = *It;
		break;
	}

	const TArray<TPair<FName, const FQuestTableRow*>> QuestRows = Data->GetQuestRows();
	// What the board DRAWS is the offered rows, not every row in the table: a bounty whose
	// RequireFlag is not set yet is not listed at all (finding 2, 2026-09-07). Counted through the
	// shipped rule so this expectation cannot drift from the board's own listing loop.
	auto OfferedRowCount = [Progress, Data, &QuestRows]() -> int32
	{
		int32 Offered = 0;
		for (const TPair<FName, const FQuestTableRow*>& Entry : QuestRows)
		{
			Offered += WrecktideQuestLogic::IsQuestOffered(Progress, Data, Entry.Key) ? 1 : 0;
		}
		return Offered;
	};
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE step1 board=%d rows=%d offered=%d"),
		Board != nullptr, QuestRows.Num(), OfferedRowCount());
	// The probe drives the four v0 rows by name; extra bounties (the Captain's Head, 2026-09-04) are
	// fine, fewer than four means the table it was written against is gone.
	if (QuestRows.Num() < 4)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-DATA Quests.csv gave %d rows, the probe expects at least 4"), QuestRows.Num());
	}
	if (!Board || QuestRows.Num() < 4)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step1 board=%d rows=%d)"), Board != nullptr, QuestRows.Num());
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 2: walk into the trigger and open the menu through the real entry path.
	PlayerPawn->SetActorLocation(Board->GetActorLocation() + Board->GetActorRightVector() * 150.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Board->OpenBoardMenu(PlayerPawn);
	const int32 ExpectedLines = OfferedRowCount() + 1;
	const bool bStep2 = Board->IsMenuOpen() && Board->GetMenuLineCount() == ExpectedLines;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE step2 open=%d lines=%d expected=%d"),
		Board->IsMenuOpen() ? 1 : 0, Board->GetMenuLineCount(), ExpectedLines);
	if (!bStep2)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step2 menu open=%d lines=%d)"),
			Board->IsMenuOpen() ? 1 : 0, Board->GetMenuLineCount());
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 3: accept one bounty through the same select+confirm path the Enter key uses, and prove
	// a second accept is refused while it is active.
	const bool bPicked = Board->SelectQuestRow(CutthroatBounty);
	Board->ConfirmMenuSelection();
	const int32 AcceptedState = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(CutthroatBounty));
	const int32 AcceptedKills = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(CutthroatBounty));
	const FName ActiveAfterAccept = WrecktideQuestLogic::GetActiveQuest(Progress, Data);

	const bool bPickedSecond = Board->SelectQuestRow(SkeletonPurge);
	Board->ConfirmMenuSelection();
	const int32 SecondState = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(SkeletonPurge));
	const FName ActiveAfterSecond = WrecktideQuestLogic::GetActiveQuest(Progress, Data);

	const bool bStep3 = bPicked && bPickedSecond
		&& AcceptedState == WrecktideQuestLogic::QuestState_Active && AcceptedKills == 0
		&& ActiveAfterAccept == CutthroatBounty
		&& SecondState == WrecktideQuestLogic::QuestState_Available
		&& ActiveAfterSecond == CutthroatBounty;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE step3 state=%d kills=%d active=%s secondState=%d activeAfterSecond=%s"),
		AcceptedState, AcceptedKills, *ActiveAfterAccept.ToString(), SecondState, *ActiveAfterSecond.ToString());
	if (!bStep3)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step3 accept picked=%d/%d state=%d kills=%d active=%s second=%d)"),
			bPicked ? 1 : 0, bPickedSecond ? 1 : 0, AcceptedState, AcceptedKills, *ActiveAfterAccept.ToString(), SecondState);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 4: real kills through NotifyEnemyKilled. The probe enemies are spawned exactly the way
	// ScheduleRespawn does it (a live enemy's own class and transform), because the C++ base class
	// has no life-bar widget and its BeginPlay would fail the check on one.
	TSubclassOf<ACombatEnemy> ProbeEnemyClass = nullptr;
	FTransform ProbeSpawnTransform = FTransform::Identity;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			ProbeEnemyClass = It->GetClass();
			ProbeSpawnTransform = It->GetActorTransform();
			break;
		}
	}
	if (!ProbeEnemyClass)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step4 no live enemy to source a spawn class from)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	auto FireKill = [this, World, ProbeEnemyClass, ProbeSpawnTransform, PlayerPawn](FName EnemyRow) -> bool
	{
		ACombatEnemy* ProbeEnemy = World->SpawnActorDeferred<ACombatEnemy>(
			ProbeEnemyClass,
			ProbeSpawnTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!ProbeEnemy)
		{
			return false;
		}

		ProbeEnemy->EnemyRowName = EnemyRow;
		UGameplayStatics::FinishSpawningActor(ProbeEnemy, ProbeSpawnTransform);
		NotifyEnemyKilled(ProbeEnemy, PlayerPawn);
		return true;
	};

	bool bKillsSpawned = true;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!FireKill(CutthroatEnemy))
		{
			bKillsSpawned = false;
		}
	}
	const int32 KillsAfterFour = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(CutthroatBounty));

	// The wrong row is fired BEFORE the last needed kill; fired after it the cap would hide the bug.
	if (!FireKill(NavyCadetEnemy))
	{
		bKillsSpawned = false;
	}
	const int32 KillsAfterWrongRow = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(CutthroatBounty));

	if (!FireKill(CutthroatEnemy))
	{
		bKillsSpawned = false;
	}
	const int32 KillsAfterFive = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(CutthroatBounty));

	if (!FireKill(CutthroatEnemy))
	{
		bKillsSpawned = false;
	}
	const int32 KillsAfterCap = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(CutthroatBounty));

	const FString Objective = WrecktideQuestLogic::ObjectiveLine(Progress, Data);
	const bool bStep4 = bKillsSpawned && KillsAfterFour == 4 && KillsAfterWrongRow == 4
		&& KillsAfterFive == 5 && KillsAfterCap == 5 && Objective.Contains(TEXT("5/5"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE step4 spawned=%d kills4=%d wrongRow=%d kills5=%d capped=%d objective=%s"),
		bKillsSpawned ? 1 : 0, KillsAfterFour, KillsAfterWrongRow, KillsAfterFive, KillsAfterCap, *Objective);
	if (!bStep4)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step4 spawned=%d kills4=%d wrongRow=%d kills5=%d capped=%d objective=%s)"),
			bKillsSpawned ? 1 : 0, KillsAfterFour, KillsAfterWrongRow, KillsAfterFive, KillsAfterCap, *Objective);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 5: turn in through the menu. Gold and XP are sampled in the SAME frame as the confirm, so
	// nothing else can move them and the deltas are the reward exactly.
	const bool bPickedTurnIn = Board->SelectQuestRow(CutthroatBounty);
	const int64 GoldBeforeTurnIn = Economy->GetGoldTotal();
	const int64 XPBeforeTurnIn = TotalMeleeXP();
	Board->ConfirmMenuSelection();
	const int64 GoldDelta = Economy->GetGoldTotal() - GoldBeforeTurnIn;
	const int64 XPDelta = TotalMeleeXP() - XPBeforeTurnIn;
	const int32 RepeatState = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(CutthroatBounty));
	const int32 RepeatKills = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(CutthroatBounty));
	const FName ActiveAfterTurnIn = WrecktideQuestLogic::GetActiveQuest(Progress, Data);

	const bool bStep5 = bPickedTurnIn && GoldDelta == 30 && XPDelta == 90
		&& RepeatState == WrecktideQuestLogic::QuestState_Available && RepeatKills == 0
		&& ActiveAfterTurnIn.IsNone();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE step5 gold+%lld xp+%lld state=%d kills=%d active=%s"),
		GoldDelta, XPDelta, RepeatState, RepeatKills, *ActiveAfterTurnIn.ToString());
	if (!bStep5)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step5 gold+%lld expected 30, xp+%lld expected 90, state=%d kills=%d active=%s)"),
			GoldDelta, XPDelta, RepeatState, RepeatKills, *ActiveAfterTurnIn.ToString());
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 6: a non-repeatable bounty is gone for good once paid.
	const bool bPickedFirstBlood = Board->SelectQuestRow(FirstBlood);
	Board->ConfirmMenuSelection();
	const FName ActiveFirstBlood = WrecktideQuestLogic::GetActiveQuest(Progress, Data);

	bool bAnyRowKills = true;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!FireKill(NavyCadetEnemy))
		{
			bAnyRowKills = false;
		}
	}
	const int32 FirstBloodKills = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(FirstBlood));

	Board->SelectQuestRow(FirstBlood);
	Board->ConfirmMenuSelection();
	const int32 DoneState = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(FirstBlood));
	const bool bCanAcceptAgain = WrecktideQuestLogic::CanAccept(Progress, Data, FirstBlood);

	Board->SelectQuestRow(FirstBlood);
	Board->ConfirmMenuSelection();
	const int32 DoneStateAfterRetry = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(FirstBlood));
	const FName ActiveAfterRetry = WrecktideQuestLogic::GetActiveQuest(Progress, Data);

	const bool bStep6 = bPickedFirstBlood && bAnyRowKills && ActiveFirstBlood == FirstBlood
		&& FirstBloodKills == 3
		&& DoneState == WrecktideQuestLogic::QuestState_Completed && !bCanAcceptAgain
		&& DoneStateAfterRetry == WrecktideQuestLogic::QuestState_Completed && ActiveAfterRetry.IsNone();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE step6 kills=%d state=%d canAcceptAgain=%d stateAfterRetry=%d active=%s"),
		FirstBloodKills, DoneState, bCanAcceptAgain ? 1 : 0, DoneStateAfterRetry, *ActiveAfterRetry.ToString());
	if (!bStep6)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step6 kills=%d state=%d canAcceptAgain=%d stateAfterRetry=%d active=%s)"),
			FirstBloodKills, DoneState, bCanAcceptAgain ? 1 : 0, DoneStateAfterRetry, *ActiveAfterRetry.ToString());
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 7: the RequireFlag gate. MarineWatch and OfficerHunt hunt enemies that stand only on
	// Gallows Reach, so until Cobb's H04 sets Story_Reach the camp board must neither list them nor
	// accept one (bounties are one at a time with no abandon, so taking one early used to strand
	// M05_Bounty and every story row behind it). With the flag set, both appear and both accept.
	const FName MarineWatch(TEXT("MarineWatch"));
	const FName OfficerHunt(TEXT("OfficerHunt"));
	const bool bGatedRowsExist = Data->GetQuestRow(MarineWatch) != nullptr && Data->GetQuestRow(OfficerHunt) != nullptr;
	const int32 LockedLines = Board->GetMenuLineCount();
	const bool bLockedListed = Board->SelectQuestRow(MarineWatch) || Board->SelectQuestRow(OfficerHunt);
	const bool bLockedAccept = WrecktideQuestLogic::CanAccept(Progress, Data, MarineWatch)
		|| WrecktideQuestLogic::CanAccept(Progress, Data, OfficerHunt);

	Progress->SetQuestFlag(FName(TEXT("Story_Reach")), 1);

	// Walk away and back: the board rebuilds its list on open, which is exactly how a player who
	// earned the flag at sea sees the new bounties.
	Board->ToggleBoardMenu(PlayerPawn);
	PlayerPawn->SetActorLocation(Board->GetActorLocation() + Board->GetActorRightVector() * 150.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Board->OpenBoardMenu(PlayerPawn);
	const int32 OpenLines = Board->GetMenuLineCount();
	const bool bOpenListed = Board->SelectQuestRow(MarineWatch) && Board->SelectQuestRow(OfficerHunt);
	const bool bOpenAccept = WrecktideQuestLogic::CanAccept(Progress, Data, MarineWatch)
		&& WrecktideQuestLogic::CanAccept(Progress, Data, OfficerHunt);
	Board->SelectQuestRow(MarineWatch);
	Board->ConfirmMenuSelection();
	const bool bGatedAccepted = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(MarineWatch))
		== WrecktideQuestLogic::QuestState_Active;

	const bool bStep7 = bGatedRowsExist && !bLockedListed && !bLockedAccept
		&& bOpenListed && bOpenAccept && bGatedAccepted && OpenLines == LockedLines + 2;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE step7 gate rows=%d lockedListed=%d lockedAccept=%d lines=%d->%d openListed=%d openAccept=%d accepted=%d"),
		bGatedRowsExist ? 1 : 0, bLockedListed ? 1 : 0, bLockedAccept ? 1 : 0, LockedLines, OpenLines,
		bOpenListed ? 1 : 0, bOpenAccept ? 1 : 0, bGatedAccepted ? 1 : 0);
	if (!bStep7)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-PROBE FAIL (step7 gate rows=%d lockedListed=%d lockedAccept=%d lines=%d->%d openListed=%d openAccept=%d accepted=%d)"),
			bGatedRowsExist ? 1 : 0, bLockedListed ? 1 : 0, bLockedAccept ? 1 : 0, LockedLines, OpenLines,
			bOpenListed ? 1 : 0, bOpenAccept ? 1 : 0, bGatedAccepted ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-PROBE PASS (menu=1 accept=1 kills=1 turnin=1 done=1 gate=1)"));
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunFeelProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	ACombatPlayerController* CombatController = Cast<ACombatPlayerController>(PlayerController);
	if (!World || !PlayerPawn || !HUD || !CombatController)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (wiring pawn=%d hud=%d controller=%d)"),
			PlayerPawn != nullptr, HUD != nullptr, CombatController != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 1: one REAL kill through NotifyEnemyKilled must push exactly one feed line. The probe
	// enemy is cloned off a live enemy's class and transform, the same way RunQuestProbe does it,
	// because the C++ base class has no life-bar widget and its BeginPlay would fail on one.
	TSubclassOf<ACombatEnemy> ProbeEnemyClass = nullptr;
	FTransform ProbeSpawnTransform = FTransform::Identity;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			ProbeEnemyClass = It->GetClass();
			ProbeSpawnTransform = It->GetActorTransform();
			break;
		}
	}
	if (!ProbeEnemyClass)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (step1 no live enemy to source a spawn class from)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const int32 FeedBefore = HUD->GetRewardFeedCount();
	ACombatEnemy* ProbeEnemy = World->SpawnActorDeferred<ACombatEnemy>(
		ProbeEnemyClass,
		ProbeSpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!ProbeEnemy)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (step1 probe enemy did not spawn)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	ProbeEnemy->EnemyRowName = FName(TEXT("Cutthroat"));
	UGameplayStatics::FinishSpawningActor(ProbeEnemy, ProbeSpawnTransform);
	NotifyEnemyKilled(ProbeEnemy, PlayerPawn);

	const int32 FeedAfter = HUD->GetRewardFeedCount();
	FeelProbeFeedLine = HUD->GetRewardFeedLine(0);
	bFeelProbeFeedOK = FeedAfter == FeedBefore + 1
		&& FeelProbeFeedLine.Contains(TEXT("gold"))
		&& FeelProbeFeedLine.Contains(TEXT("XP"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FEEL-PROBE step1 feed %s before=%d after=%d line=%s"),
		bFeelProbeFeedOK ? TEXT("OK") : TEXT("BAD"), FeedBefore, FeedAfter, *FeelProbeFeedLine);
	if (!bFeelProbeFeedOK)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (step1 feed before=%d after=%d line=%s)"),
			FeedBefore, FeedAfter, *FeelProbeFeedLine);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 2: pause through the SAME function the Esc key calls. Synchronous on purpose: timers do
	// NOT tick while the game is paused, so a probe that paused and then scheduled its own unpause
	// would hang until the watchdog fired.
	CombatController->TogglePauseMenu();
	const bool bPausedNow = UGameplayStatics::IsGamePaused(World);
	const bool bPanelOpen = HUD->IsPauseMenuOpen() && CombatController->IsPauseMenuOpen();
	CombatController->TogglePauseMenu();
	const bool bResumed = !UGameplayStatics::IsGamePaused(World);
	const bool bPanelClosed = !HUD->IsPauseMenuOpen() && !CombatController->IsPauseMenuOpen();

	bFeelProbePauseOK = bPausedNow && bPanelOpen && bResumed && bPanelClosed;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FEEL-PROBE step2 pause %s paused=%d panel=%d resumed=%d panelClosed=%d"),
		bFeelProbePauseOK ? TEXT("OK") : TEXT("BAD"),
		bPausedNow ? 1 : 0, bPanelOpen ? 1 : 0, bResumed ? 1 : 0, bPanelClosed ? 1 : 0);
	if (!bFeelProbePauseOK)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (step2 pause paused=%d panel=%d resumed=%d panelClosed=%d)"),
			bPausedNow ? 1 : 0, bPanelOpen ? 1 : 0, bResumed ? 1 : 0, bPanelClosed ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 3: die once, the way the loco probe does. StatsComponent caps any single hit at 40% of
	// max health (anti-one-shot rule), so no lone TakeDamage can kill from full HP.
	FeelProbeOldPawn = PlayerPawn;
	int32 KillHits = 0;
	FDamageEvent DamageEvent;
	while (PlayerPawn->Stats && PlayerPawn->Stats->IsAlive() && KillHits < 10)
	{
		PlayerPawn->TakeDamage(100000.0f, DamageEvent, nullptr, PlayerPawn);
		++KillHits;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FEEL-PROBE step3 kill hits=%d aliveAfter=%d"),
		KillHits, (PlayerPawn->Stats && PlayerPawn->Stats->IsAlive()) ? 1 : 0);

	FTimerHandle DeathCheckTimer;
	World->GetTimerManager().SetTimer(DeathCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckFeelProbeDeath), 1.0f, false);
}

void UWrecktideRewardSubsystem::CheckFeelProbeDeath()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	if (!World || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (step3 HUD lost during the death wait)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const bool bDeathUp = HUD->IsDeathScreenActive();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FEEL-PROBE step3 death %s youDied=%d"),
		bDeathUp ? TEXT("OK") : TEXT("BAD"), bDeathUp ? 1 : 0);
	if (!bDeathUp)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (step3 YOU DIED never came up)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Poll rather than guess: the respawn delay lives on the pawn, not here.
	FeelProbeRespawnDeadline = World->GetTimeSeconds() + 8.0f;
	World->GetTimerManager().SetTimer(FeelProbeRespawnTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckFeelProbeRespawn), 0.25f, true);
}

void UWrecktideRewardSubsystem::CheckFeelProbeRespawn()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* NewPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;

	const bool bNewPawn = NewPawn != nullptr && NewPawn != FeelProbeOldPawn.Get();
	const bool bDeathCleared = HUD != nullptr && !HUD->IsDeathScreenActive();
	const bool bTimedOut = !World || World->GetTimeSeconds() >= FeelProbeRespawnDeadline;

	// keep polling until both land or the window closes
	if ((!bNewPawn || !bDeathCleared) && !bTimedOut)
	{
		return;
	}

	if (World)
	{
		World->GetTimerManager().ClearTimer(FeelProbeRespawnTimer);
	}

	bFeelProbeDeathOK = bNewPawn && bDeathCleared;
	const bool bPass = bFeelProbeFeedOK && bFeelProbePauseOK && bFeelProbeDeathOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FEEL-PROBE step3 respawn %s newPawn=%d deathCleared=%d timedOut=%d"),
		bFeelProbeDeathOK ? TEXT("OK") : TEXT("BAD"), bNewPawn ? 1 : 0, bDeathCleared ? 1 : 0, bTimedOut ? 1 : 0);
	if (!bFeelProbeDeathOK)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FEEL-PROBE FAIL (step3 respawn newPawn=%d deathCleared=%d)"),
			bNewPawn ? 1 : 0, bDeathCleared ? 1 : 0);
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FEEL-PROBE %s (feed=%d pause=%d death=%d line=%s)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bFeelProbeFeedOK ? 1 : 0, bFeelProbePauseOK ? 1 : 0, bFeelProbeDeathOK ? 1 : 0, *FeelProbeFeedLine);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::NotifyEnemyKilled(ACombatEnemy* Enemy, AActor* KillerActor)
{
	if (!Enemy)
	{
		return;
	}

	UWorld* World = Enemy->GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	const FName RowName = Enemy->EnemyRowName;
	const FEnemyTableRow* EnemyRow = DataSubsystem ? DataSubsystem->GetEnemyRow(RowName) : nullptr;
	if (!EnemyRow)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-FUNGATE-ROWMISS %s"), *RowName.ToString());
	}

	APawn* KillerPawn = Cast<APawn>(KillerActor);
	AWrecktidePlayerState* PlayerState = KillerPawn ? KillerPawn->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (PlayerState && EnemyRow)
	{
		const EWrecktideSkill AwardedSkill = PlayerState->GetEquippedCombatClass();
		const int32 GoldMin = static_cast<int32>(EnemyRow->GoldDropMin);
		const int32 GoldMax = static_cast<int32>(EnemyRow->GoldDropMax);
		const int64 GoldRoll = FMath::RandRange(FMath::Min(GoldMin, GoldMax), FMath::Max(GoldMin, GoldMax));

		if (UEconomyComponent* Economy = PlayerState->GetEconomy())
		{
			Economy->ApplyGoldDelta(GoldRoll, EGoldSource::Earned, EGoldReason::LootPickup, RowName);
		}

		if (USkillComponent* Skill = PlayerState->GetSkill())
		{
			Skill->AwardXP(AwardedSkill, EnemyRow->XPAward, RowName);
		}

		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-KILL row=%s gold=%lld xp=%lld skill=%d"),
			*RowName.ToString(),
			GoldRoll,
			EnemyRow->XPAward,
			static_cast<int32>(AwardedSkill));

		// feedback sounds (2026-09-05): every paid kill is heard, gold adds the coin
		UWrecktideAudio::PlaySfx(this, TEXT("kill"));
		if (GoldRoll > 0)
		{
			UWrecktideAudio::PlaySfx(this, TEXT("coin"), 0.7f);
		}

		// Bounty progress rides the SAME kill event as the gold and XP, after them, so a bounty can
		// never count a kill that paid nothing (bounty board, 2026-09-04). All the filtering and
		// capping lives in the quest logic; this call is unconditional past the reward gate.
		WrecktideQuestLogic::NotifyKill(PlayerState, RowName);

		// Story missions ride the same kill event, after the bounty, for the same reason: a mission
		// must never advance on a kill that paid nothing (missions v0, 2026-09-04).
		WrecktideMissionLogic::NotifyKill(PlayerState, RowName);

		// Kill feed (feel pass, 2026-09-04): the HUD is TOLD what was already awarded above and
		// never computes a reward of its own, so the feed cannot drift from the ledger.
		APlayerController* FeedController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
		if (AWrecktideHUD* HUD = FeedController ? Cast<AWrecktideHUD>(FeedController->GetHUD()) : nullptr)
		{
			const UEnum* SkillEnum = StaticEnum<EWrecktideSkill>();
			const FString SkillName = SkillEnum
				? SkillEnum->GetNameStringByValue(static_cast<int64>(AwardedSkill))
				: FString();
			const FString EnemyName = EnemyRow->DisplayName.IsEmpty() ? RowName.ToString() : EnemyRow->DisplayName.ToString();
			HUD->PushRewardLine(FString::Printf(TEXT("+%lld gold   +%lld %s XP  (%s)"),
				GoldRoll, EnemyRow->XPAward, *SkillName, *EnemyName));
		}
	}

	ScheduleRespawn(Enemy);
}

void UWrecktideRewardSubsystem::SpawnShopStall(UWorld* World) const
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	APlayerStart* FirstPlayerStart = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		FirstPlayerStart = *It;
		break;
	}

	if (!FirstPlayerStart)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING No PlayerStart found for shop stall"));
		return;
	}

	FVector SpawnLocation = FirstPlayerStart->GetActorLocation() + FirstPlayerStart->GetActorRightVector() * 400.0f;
	const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 500.0f);
	const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 2000.0f);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility))
	{
		// the Synty shop mesh has a base pivot: stand it on the sand, no cube-centre lift
		SpawnLocation.Z = Hit.ImpactPoint.Z;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AWrecktideShopStall* CampMarket = World->SpawnActor<AWrecktideShopStall>(AWrecktideShopStall::StaticClass(), SpawnLocation, FirstPlayerStart->GetActorRotation(), SpawnParameters);
	if (!CampMarket)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING Shop stall spawn failed"));
		return;
	}

	// Camp must remain first for MarketStall mission targets. No level asset is saved.
	FVector ReachLocation = GunsmithLocation;
	FHitResult Ground;
	if (World->LineTraceSingleByChannel(Ground, ReachLocation + FVector(0.0f, 0.0f, 900.0f),
		ReachLocation - FVector(0.0f, 0.0f, 2000.0f), ECC_Visibility))
	{
		ReachLocation.Z = Ground.ImpactPoint.Z;
	}
	const FTransform GunsmithTransform(GunsmithRotation, ReachLocation);
	AWrecktideShopStall* Gunsmith = World->SpawnActorDeferred<AWrecktideShopStall>(AWrecktideShopStall::StaticClass(),
		GunsmithTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Gunsmith)
	{
		Gunsmith->StallClass = EWrecktideStallClass::Shooting;
		Gunsmith->PromptName = TEXT("Tobias Reed, gunsmith");
		Gunsmith->SignText = TEXT("GUNSMITH");
		UGameplayStatics::FinishSpawningActor(Gunsmith, GunsmithTransform);
	}
	TActorIterator<AWrecktideShopStall> FirstStall(World);
	const bool bCampFirst = FirstStall && *FirstStall == CampMarket;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-GUNSMITH spawn=%d campFirst=%d at=%s"), Gunsmith != nullptr, bCampFirst, *ReachLocation.ToString());
	if (!bCampFirst || !Gunsmith)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-GUNSMITH-WIRING stall order or spawn failed"));
	}
}

void UWrecktideRewardSubsystem::SpawnQuestBoard(UWorld* World) const
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	APlayerStart* FirstPlayerStart = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		FirstPlayerStart = *It;
		break;
	}

	if (!FirstPlayerStart)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING No PlayerStart found for the quest board"));
		return;
	}

	// Opposite side of the PlayerStart from the shop stall, so the two 300uu triggers can never
	// overlap and the player is never standing in both menus' range at once.
	FVector SpawnLocation = FirstPlayerStart->GetActorLocation() - FirstPlayerStart->GetActorRightVector() * 400.0f;
	const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 500.0f);
	const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 2000.0f);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility))
	{
		SpawnLocation.Z = Hit.ImpactPoint.Z + 70.0f;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (!World->SpawnActor<AWrecktideQuestBoard>(AWrecktideQuestBoard::StaticClass(), SpawnLocation, FirstPlayerStart->GetActorRotation(), SpawnParameters))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING Quest board spawn failed"));
	}

	// GALLOWS REACH (C34, 2026-09-07): a second board on the garrison square, so the new navy
	// bounties can be worked without sailing home. Spawned AFTER the camp board on purpose: the
	// TurnInBounty resolvers (WrecktideMissionLogic::ResolveSpotLocation, WrecktideMap::
	// ResolveMissionTarget) take the FIRST board a TActorIterator hands back, and the camp board
	// has to stay that one or M05_Bounty and H03_PaidInFull would point across the sea.
	// -Island2Probe asserts exactly that ordering.
	FVector ReachBoardLocation(WrecktideMap::GallowsReach::BoardX, WrecktideMap::GallowsReach::BoardY,
		FirstPlayerStart->GetActorLocation().Z);
	FHitResult ReachHit;
	if (World->LineTraceSingleByChannel(ReachHit, ReachBoardLocation + FVector(0.0f, 0.0f, 900.0f),
		ReachBoardLocation - FVector(0.0f, 0.0f, 2000.0f), ECC_Visibility))
	{
		ReachBoardLocation.Z = ReachHit.ImpactPoint.Z + 70.0f;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-QUEST-WIRING no ground under the Gallows Reach board at X=%.0f Y=%.0f"),
			ReachBoardLocation.X, ReachBoardLocation.Y);
	}
	if (!World->SpawnActor<AWrecktideQuestBoard>(AWrecktideQuestBoard::StaticClass(), ReachBoardLocation,
		FRotator(0.0f, -90.0f, 0.0f), SpawnParameters))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING Gallows Reach quest board spawn failed"));
	}
	// Third board follows both existing boards, preserving the camp's iterator priority.
	FVector HollowBoardLocation(WrecktideMap::MangroveHollow::BoardX, WrecktideMap::MangroveHollow::BoardY, 130.0f);
	FHitResult HollowHit;
	if (World->LineTraceSingleByChannel(HollowHit, HollowBoardLocation + FVector(0, 0, 900),
		HollowBoardLocation - FVector(0, 0, 2000), ECC_Visibility))
	{
		HollowBoardLocation.Z = HollowHit.ImpactPoint.Z + 70.0f;
		if (!World->SpawnActor<AWrecktideQuestBoard>(AWrecktideQuestBoard::StaticClass(), HollowBoardLocation,
			FRotator(0, -90, 0), SpawnParameters))
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ISLAND3 village board spawn failed"));
		}
	}
	else { UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ISLAND3 no ground under village board")); }
}

void UWrecktideRewardSubsystem::SpawnMissionGiver(UWorld* World) const
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	APlayerStart* FirstPlayerStart = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		FirstPlayerStart = *It;
		break;
	}

	if (!FirstPlayerStart)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING No PlayerStart found for the quest giver"));
		return;
	}

	// The four NPCs of the hand-off chain (C22, 2026-09-06; spread across the island by C30 the same
	// evening). Two kinds of spot, because two kinds of NPC:
	//   CAMP NPCs are placed off the PlayerStart like every other camp prop, so the whole camp moves
	//   together if the start ever moves. On this island Right is world -X and Forward is world +Y.
	//     Bell   (-650, -4750) unchanged from missions v0
	//     Jerold (-1150, -5220) at the smithy bench, inside its 300 uu trigger on purpose
	//   ISLAND NPCs carry a WORLD spot, because the landmark they belong to is level geometry that
	//   does not follow the PlayerStart:
	//     Marta (2900, -1100) by the smugglers' stash. StashChest sits at (3150, -520), so she is
	//           631 uu from it: outside its trigger, close enough to read as her camp.
	//     Cobb  (150, -7000) on the pier deck at its landward head. The deck spans X -300..+300 and
	//           Y -9100..-6900 with its top at z = -40; X = 150 keeps the centre line walkable (the
	//           boat walk probe drives the pawn down it) and stays 160 uu inside the rail at +310.
	// Every one of these carries a 300 uu trigger, so no two interact spots may sit closer than
	// 600 uu or the player would stand inside two menus at once. Closest pairs now: Bell to Jerold
	// 686 uu, Marta to the stash chest 631 uu, Cobb to the boat 1,476 uu (its board range is 1,000,
	// so his F and the boat's F never overlap) and 1,900 uu to the pier's supply barrel. Jerold to
	// the SMITHY is deliberately 130 uu: he works that bench.
	// The NAMES are not here: they live in Npcs.csv beside the gate flag and the brush-off line, so
	// one row is the whole NPC (C30).
	struct FGiverSpawn
	{
		FName Id;
		EWrecktideGiverSkin Skin;

		/** Camp NPC: offsets from the PlayerStart. Island NPC (bWorldSpot): world X and Y. */
		float ForwardOrX;
		float RightOrY;
		bool bWorldSpot;
		bool bOpensSmithy;
	};
	static const FGiverSpawn Givers[] =
	{
		{FName(TEXT("Bell")),   EWrecktideGiverSkin::FirstMate,  450.0f,   650.0f, false, false},
		{FName(TEXT("Jerold")), EWrecktideGiverSkin::Seaman,     -20.0f,  1150.0f, false, true},
		{FName(TEXT("Marta")),  EWrecktideGiverSkin::Wench,    2900.0f, -1100.0f, true,  false},
		{FName(TEXT("Cobb")),   EWrecktideGiverSkin::Governor,  150.0f, -7000.0f, true,  false},
		// GALLOWS REACH (C34, 2026-09-07). Ada Kell stands on the garrison square's north-east
		// corner, on the +40 tier Tools\island2_build.py laid there. 1,166 uu from the gallows post
		// and 2,000 uu from that island's bounty board, so her 300 uu trigger meets nothing else.
		// Her gate flag Story_Reach comes from Cobb's H04, which is why she is unreachable content
		// until the first island's chain is finished.
		{FName(TEXT("Ada")),    EWrecktideGiverSkin::Sergeant, 12400.0f, -15400.0f, true, false},
		{FName(TEXT("Oduya")), EWrecktideGiverSkin::Wench,
			WrecktideMap::MangroveHollow::OduyaX, WrecktideMap::MangroveHollow::OduyaY, true, false}
	};

	const UGameInstance* SpawnGameInstance = World->GetGameInstance();
	const UWrecktideDataSubsystem* NpcData = SpawnGameInstance ? SpawnGameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	for (const FGiverSpawn& Spawn : Givers)
	{
		FVector SpawnLocation = Spawn.bWorldSpot
			? FVector(Spawn.ForwardOrX, Spawn.RightOrY, FirstPlayerStart->GetActorLocation().Z)
			: FirstPlayerStart->GetActorLocation()
				+ FirstPlayerStart->GetActorForwardVector() * Spawn.ForwardOrX
				+ FirstPlayerStart->GetActorRightVector() * Spawn.RightOrY;
		// Traced from 500 uu up to 2,000 uu down. The pier deck (-40) and the central field (0) both
		// sit inside that window from the PlayerStart's z of 60, so one trace serves camp and island.
		const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 500.0f);
		const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 2000.0f);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility))
		{
			// the Synty character mesh has a foot pivot: stand him on the sand, no capsule lift
			SpawnLocation.Z = Hit.ImpactPoint.Z;
		}
		else
		{
			// Fail LOUD: a spot with no ground under it would leave an NPC hovering or falling, and
			// the mission that names him would point at nothing.
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MISSION-GIVER no ground under %s at X=%.0f Y=%.0f; standing him at the trace height"),
				*Spawn.Id.ToString(), SpawnLocation.X, SpawnLocation.Y);
		}

		// Names out of Npcs.csv. Empty strings leave the actor's own defaults in place, which is the
		// documented fail-open of a missing table.
		const FNpcTableRow* NpcRow = NpcData ? NpcData->GetNpcRow(Spawn.Id) : nullptr;
		const FString PlateName = NpcRow ? NpcRow->DisplayName.ToString() : FString();
		const FString PromptName = NpcRow ? NpcRow->PromptName.ToString() : FString();
		if (!NpcRow)
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-NPC-DATA Npcs.csv has no row for %s; he keeps the default name and no gate"),
				*Spawn.Id.ToString());
		}

		// Facing the spawn point, so the player walks out of the PlayerStart into his eyes.
		FRotator SpawnRotation = (FirstPlayerStart->GetActorLocation() - SpawnLocation).Rotation();
		SpawnRotation.Pitch = 0.0f;
		SpawnRotation.Roll = 0.0f;

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// Deferred so the skin is swapped BEFORE BeginPlay wires the retarget onto it; configuring
		// after FinishSpawning would leave the anim instance bound to the previous mesh.
		AWrecktideMissionGiver* Giver = World->SpawnActorDeferred<AWrecktideMissionGiver>(
			AWrecktideMissionGiver::StaticClass(), FTransform(SpawnRotation, SpawnLocation),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Giver)
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING Quest giver spawn failed id=%s"), *Spawn.Id.ToString());
			continue;
		}
		Giver->ConfigureGiver(Spawn.Id, PlateName, PromptName, Spawn.Skin, Spawn.bOpensSmithy);
		Giver->FinishSpawning(FTransform(SpawnRotation, SpawnLocation));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-GIVER spawned id=%s at X=%.0f Y=%.0f Z=%.0f"),
			*Spawn.Id.ToString(), SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z);
	}
}

void UWrecktideRewardSubsystem::TickMissionLocation()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (!Pawn || !PlayerState)
	{
		// Between respawns there is nobody to place. Silent on purpose: this runs twice a second.
		return;
	}

	WrecktideMissionLogic::NotifyLocation(PlayerState, Pawn->GetActorLocation());
	CheckIslandArrival(PlayerState, Pawn->GetActorLocation());
}

void UWrecktideRewardSubsystem::CheckIslandArrival(AWrecktidePlayerState* PlayerState, const FVector& PlayerLocation) const
{
	// Each arrival is once per save through QuestFlags, sampled by the mission location timer.
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	if (!Progress)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	struct FMangroveArrivalRow { const TCHAR* Flag; const TCHAR* Name; float X, Y, Radius; int32 Island; };
	const FMangroveArrivalRow Arrivals[] = {
		{TEXT("Seen_GallowsReach"), TEXT("Gallows Reach"), WrecktideMap::GallowsReach::CentreX,
			WrecktideMap::GallowsReach::CentreY, WrecktideMap::GallowsReach::ArrivalRadius, 2},
		{TEXT("Seen_MangroveHollow"), TEXT("Mangrove Hollow"), WrecktideMap::MangroveHollow::CentreX,
			WrecktideMap::MangroveHollow::CentreY, WrecktideMap::MangroveHollow::ArrivalRadius, 3}
	};
	for (const FMangroveArrivalRow& Arrival : Arrivals)
	{
		const FName Flag(Arrival.Flag);
		if (Progress->GetQuestFlag(Flag) != 0 || FVector::DistSquared2D(PlayerLocation,
			FVector(Arrival.X, Arrival.Y, 0)) > FMath::Square(Arrival.Radius)) { continue; }
		Progress->SetQuestFlag(Flag, 1);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND%d arrival at X=%.0f Y=%.0f (flag %s set)"),
			Arrival.Island, PlayerLocation.X, PlayerLocation.Y, Arrival.Flag);
		if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
		{
			HUD->ShowToast(FString::Printf(TEXT("Land ho: %s"), Arrival.Name), 4.0f);
		}
	}
}

void UWrecktideRewardSubsystem::ScheduleRespawn(ACombatEnemy* Enemy) const
{
	if (!Enemy)
	{
		return;
	}

	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		return;
	}

	TSubclassOf<ACombatEnemy> EnemyClass = Enemy->GetClass();
	const FName RowName = Enemy->EnemyRowName;
	const FTransform SpawnTransform = Enemy->GetActorTransform();
	TWeakObjectPtr<UWorld> WeakWorld(World);

	FTimerDelegate RespawnDelegate = FTimerDelegate::CreateLambda([WeakWorld, EnemyClass, RowName, SpawnTransform]()
	{
		UWorld* SpawnWorld = WeakWorld.Get();
		if (!SpawnWorld)
		{
			return;
		}

		int32 AliveEnemies = 0;
		for (TActorIterator<ACombatEnemy> It(SpawnWorld); It; ++It)
		{
			ACombatEnemy* ExistingEnemy = *It;
			if (ExistingEnemy && ExistingEnemy->Stats && ExistingEnemy->Stats->IsAlive())
			{
				++AliveEnemies;
			}
		}

		if (AliveEnemies >= MaxAliveEnemies)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-RESPAWN row=%s skipped=cap"), *RowName.ToString());
			return;
		}

		ACombatEnemy* RespawnedEnemy = SpawnWorld->SpawnActorDeferred<ACombatEnemy>(
			EnemyClass,
			SpawnTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

		if (!RespawnedEnemy)
		{
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-FUNGATE-RESPAWN row=%s failed=spawn"), *RowName.ToString());
			return;
		}

		RespawnedEnemy->EnemyRowName = RowName;
		UGameplayStatics::FinishSpawningActor(RespawnedEnemy, SpawnTransform);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-RESPAWN row=%s"), *RowName.ToString());
	});

	FTimerHandle RespawnTimer;
	World->GetTimerManager().SetTimer(RespawnTimer, RespawnDelegate, EnemyRespawnDelay, false);
}

namespace
{
/** Sidecar beside the probe .sav holding the values the READ phase of -SaveProbe must observe. */
FString SaveProbeExpectPath(const FString& Slot)
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".expect.txt"));
}

/** Walkable spot on the beach the save probes teleport to before writing, so the saved position is
 *  a known number the next process can assert against. */
const FVector SaveProbeSpot(1500.0f, -4200.0f, 60.0f);

/** How far from the saved spot a restored pirate may land and still count as "put back". */
constexpr float SaveProbePlacementTolerance = 100.0f;

/** Reads back the "Location=X,Y" line the write phase put in the sidecar. */
bool ReadSaveProbeSpot(const FString& Expect, FVector2D& OutSpot)
{
	FString Value;
	// bShouldStopOnSeparator = false: the default stops the token at the comma and hands back "1500".
	if (!FParse::Value(*Expect, TEXT("Location="), Value, false))
	{
		return false;
	}
	FString Left;
	FString Right;
	if (!Value.Split(TEXT(","), &Left, &Right))
	{
		return false;
	}
	OutSpot = FVector2D(FCString::Atof(*Left), FCString::Atof(*Right));
	return true;
}

/** Sidecar beside the wardrobe probe save, proving the READ phase expects the authored final pick. */
FString WardrobeProbeExpectPath(const FString& Slot)
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".expect.txt"));
}

FName FindWardrobeProbeLegBone(const USkeletalMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return NAME_None;
	}
	const FName Candidates[] =
	{
		FName(TEXT("foot_l")), FName(TEXT("calf_l")), FName(TEXT("thigh_l")),
		FName(TEXT("foot_r")), FName(TEXT("calf_r")), FName(TEXT("thigh_r"))
	};
	for (const FName Candidate : Candidates)
	{
		if (Mesh->GetBoneIndex(Candidate) != INDEX_NONE)
		{
			return Candidate;
		}
	}
	return NAME_None;
}

bool WardrobeProbeMaterialsMatch(const UWrecktideWardrobeSubsystem& Wardrobe, FName OutfitId, int32 Variant,
	const USkeletalMesh& ExpectedMesh, const USkeletalMeshComponent& CosmeticMesh)
{
	const TArray<FSkeletalMaterial>& Defaults = ExpectedMesh.GetMaterials();
	if (Defaults.Num() == 0 || CosmeticMesh.GetNumMaterials() < Defaults.Num())
	{
		return false;
	}

	if (Variant == 0)
	{
		for (int32 Slot = 0; Slot < Defaults.Num(); ++Slot)
		{
			if (CosmeticMesh.GetMaterial(Slot) != Defaults[Slot].MaterialInterface.Get())
			{
				return false;
			}
		}
		return true;
	}

	const FString Name = Wardrobe.GetVariantName(OutfitId, Variant).ToString();
	if (!Name.StartsWith(TEXT("Colourway ")))
	{
		return false;
	}
	const FString Suffix = Name.RightChop(10);
	if (Suffix.Len() != 2 || Suffix[0] < TEXT('1') || Suffix[0] > TEXT('4')
		|| Suffix[1] < TEXT('A') || Suffix[1] > TEXT('C'))
	{
		return false;
	}

	const FString AssetName = FString::Printf(TEXT("MI_PolygonPirates_0%c_%c"), Suffix[0], Suffix[1]);
	const FString ObjectPath = FString::Printf(TEXT("/Game/PolygonPirates/Materials/%s.%s"), *AssetName, *AssetName);
	UMaterialInterface* ExpectedMaterial = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
	if (!ExpectedMaterial)
	{
		return false;
	}
	for (int32 Slot = 0; Slot < Defaults.Num(); ++Slot)
	{
		if (CosmeticMesh.GetMaterial(Slot) != ExpectedMaterial)
		{
			return false;
		}
	}
	return true;
}

/** What the real purchase path did, so a probe can assert the TRANSITION and not just the end state
 *  (review 2026-09-02: a reused slot or a stale ownership record used to pass on end state alone). */
struct FProbePurchase
{
	FName Row = NAME_None;
	int64 Price = 0;
	bool bOwnedBefore = false;
	bool bOwnedAfter = false;
	int64 GoldBefore = 0;
	int64 GoldAfter = 0;
	FName EquippedAfter = NAME_None;

	/** A genuine first purchase: unowned before, owned and equipped after, gold debited unless free. */
	bool IsFreshPurchase() const
	{
		return !Row.IsNone() && !bOwnedBefore && bOwnedAfter && EquippedAfter == Row && (Price == 0 || GoldAfter < GoldBefore);
	}
};

/**
 * The wardrobe SCREEN cases (2026-09-06): what the player actually does, driven through the real
 * menu model rather than through SetOutfit. Proves three things the outfit loop above cannot:
 * the change room round trip (he comes back to the exact spot he left), the autosave guard (a save
 * written while the room has him stores the WORLD spot), and the live colourway preview (Left/Right
 * re-dresses the pirate without persisting anything). Runs synchronously: the menu pauses the world
 * and world timers stop with it, so nothing here may wait on one.
 */
bool RunWardrobeScreenCases(ACombatCharacter& PlayerPawn, AWrecktidePlayerState& PlayerState,
	ACombatPlayerController& Controller, UWrecktideWardrobeSubsystem& Wardrobe, const FString& Slot)
{
	USkeletalMeshComponent* CosmeticMesh = PlayerPawn.GetCosmeticMesh();
	if (!CosmeticMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-ROOM FAIL reason=no-cosmetic-mesh"));
		return false;
	}

	const FVector BeforeLocation = PlayerPawn.GetActorLocation();
	const UMaterialInterface* BeforeMaterial = CosmeticMesh->GetMaterial(0);
	const int32 BeforeVariant = PlayerState.GetOutfitVariant();
	const FName BeforeOutfit = PlayerState.GetOutfitId();

	Controller.TogglePauseMenu();
	Controller.MenuOpenScreen(EWrecktideMenuScreen::Wardrobe);

	const bool bInRoom = Wardrobe.IsInChangeRoom();
	const FVector RoomLocation = PlayerPawn.GetActorLocation();
	const float RoomDistance = FVector::Distance(RoomLocation, BeforeLocation);
	// The room is 40,000 uu out: anything less than a kilometre means he never left the island.
	const bool bMoved = bInRoom && RoomDistance > 1000.0f;

	// The autosave guard: written with the screen open, read back off disk.
	const bool bSavedInRoom = PlayerState.SaveNow();
	FVector SavedLocation = FVector::ZeroVector;
	bool bSavedLocationRead = false;
	if (const UWrecktideSaveGame* SaveGame = Cast<UWrecktideSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0)))
	{
		SavedLocation = SaveGame->Data.PlayerLocation;
		bSavedLocationRead = SaveGame->Data.bHasLocation;
	}
	const float SavedError = FVector::Distance(SavedLocation, BeforeLocation);
	const bool bGuardOK = bSavedInRoom && bSavedLocationRead && SavedError <= 1.0f;

	// The live colourway: select the Colourway row and press Right.
	int32 ColourwayRow = INDEX_NONE;
	const TArray<FWrecktideMenuRow>& Rows = Controller.GetMenuRows();
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].Label.Equals(TEXT("Colourway")))
		{
			ColourwayRow = Index;
			break;
		}
	}
	bool bLiveOK = false;
	bool bNotPersisted = false;
	FString RowValue;
	if (ColourwayRow != INDEX_NONE)
	{
		Controller.MenuSetSelected(ColourwayRow);
		Controller.MenuAdjust(1);
		const TArray<FWrecktideMenuRow>& AfterRows = Controller.GetMenuRows();
		RowValue = AfterRows.IsValidIndex(ColourwayRow) ? AfterRows[ColourwayRow].Value : FString();
		// The pirate wears the new colourway NOW, and the save still holds the old one.
		bLiveOK = CosmeticMesh->GetMaterial(0) != BeforeMaterial;
		bNotPersisted = PlayerState.GetOutfitVariant() == BeforeVariant && PlayerState.GetOutfitId() == BeforeOutfit;
	}

	// Esc out of the wardrobe, then out of the pause tree.
	Controller.MenuBack();
	Controller.TogglePauseMenu();

	const FVector AfterLocation = PlayerPawn.GetActorLocation();
	const float ReturnError = FVector::Distance(AfterLocation, BeforeLocation);
	const bool bReturned = !Wardrobe.IsInChangeRoom() && ReturnError <= 1.0f;
	// Browsing is not wearing: the look he leaves in is the look he arrived in.
	const bool bReverted = CosmeticMesh->GetMaterial(0) == BeforeMaterial;

	const bool bPass = bMoved && bGuardOK && bLiveOK && bNotPersisted && bReturned && bReverted;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE-ROOM %s (room=%d moved=%.0f guard=%d savedErr=%.2f live=%d row=%s notPersisted=%d returned=%d returnErr=%.2f reverted=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bInRoom ? 1 : 0, RoomDistance, bGuardOK ? 1 : 0, SavedError,
		bLiveOK ? 1 : 0, *RowValue, bNotPersisted ? 1 : 0, bReturned ? 1 : 0, ReturnError, bReverted ? 1 : 0);
	return bPass;
}

/**
 * The EARNED wardrobe (spec C9 item 3): the starting outfit is owned, a buy below the level gate is
 * refused without touching the purse, a buy with the level but not the gold is refused, and a buy
 * with both goes through, debits exactly the price and records ownership (the read phase then proves
 * that ownership survived the process). Also checks the colourway ladder at three levels.
 * Gating level = AWrecktidePlayerState::GetGatingLevel (the notoriety level since 2026-09-06).
 */
bool RunWardrobeGateCases(ACombatCharacter& PlayerPawn, AWrecktidePlayerState& PlayerState,
	UWrecktideWardrobeSubsystem& Wardrobe)
{
	UEconomyComponent* Economy = PlayerState.GetEconomy();
	USkillComponent* Skill = PlayerState.GetSkill();
	if (!Economy || !Skill)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-GATE FAIL reason=wiring economy=%d skill=%d"),
			Economy != nullptr, Skill != nullptr);
		return false;
	}

	const FName StartRow(TEXT("Deckhand"));
	const FName TopRow(TEXT("Turncoat"));
	const FName BuyRow(TEXT("Seaman"));
	const int64 BuyPrice = 150;

	// The broke start: he owns the clothes he stands in and nothing else.
	const bool bStartOwned = Wardrobe.IsOutfitOwned(&PlayerPawn, StartRow);
	const bool bTopUnowned = !Wardrobe.IsOutfitOwned(&PlayerPawn, TopRow);

	// Below the level gate, with plenty of gold: refused, and the gold stays where it was.
	Economy->ApplyGoldDelta(20000, EGoldSource::Earned, EGoldReason::StoreGoldGrant, FName("WardrobeProbe"));
	const int64 GoldBeforeLevelTry = Economy->GetGoldTotal();
	const EWrecktideOutfitBuyResult LevelTry = Wardrobe.BuyOutfit(&PlayerPawn, TopRow);
	const bool bLevelRefused = LevelTry == EWrecktideOutfitBuyResult::LevelTooLow
		&& Economy->GetGoldTotal() == GoldBeforeLevelTry && !Wardrobe.IsOutfitOwned(&PlayerPawn, TopRow);

	// Now the other half of the double gate: level high enough, purse empty.
	Economy->ApplyGoldDelta(-GoldBeforeLevelTry, EGoldSource::Purchased, EGoldReason::Adjustment, FName("WardrobeProbe"));
	// 1,200 XP: enough for NOTORIETY level 5 (30 * 5^2.2 = 1,034), the Seaman gate, now that GetGatingLevel
	// reads notoriety (merged 2026-09-06); 700 only reached Melee 6 / notoriety 4 and starved the buy cases.
	Skill->AwardXP(EWrecktideSkill::Melee, 1200, FName("WardrobeProbe"));
	const int32 GateLevel = PlayerState.GetGatingLevel();
	const EWrecktideOutfitBuyResult GoldTry = Wardrobe.BuyOutfit(&PlayerPawn, BuyRow);
	const bool bGoldRefused = GoldTry == EWrecktideOutfitBuyResult::NotEnoughGold
		&& !Wardrobe.IsOutfitOwned(&PlayerPawn, BuyRow);

	// Both gates met: bought, owned, and exactly the price left the purse.
	Economy->ApplyGoldDelta(BuyPrice, EGoldSource::Earned, EGoldReason::StoreGoldGrant, FName("WardrobeProbe"));
	const int64 GoldBeforeBuy = Economy->GetGoldTotal();
	const EWrecktideOutfitBuyResult BuyTry = Wardrobe.BuyOutfit(&PlayerPawn, BuyRow);
	const int64 GoldAfterBuy = Economy->GetGoldTotal();
	const bool bBought = BuyTry == EWrecktideOutfitBuyResult::Bought
		&& Wardrobe.IsOutfitOwned(&PlayerPawn, BuyRow) && GoldAfterBuy == GoldBeforeBuy - BuyPrice;

	// The colourway ladder: three from the start, the fourth at 8, the whole palette by 72.
	const int32 Total = Wardrobe.GetVariantCount();
	const bool bLadder = Wardrobe.GetUnlockedVariantCount(1) == 3
		&& Wardrobe.GetUnlockedVariantCount(8) == 4
		&& Wardrobe.GetUnlockedVariantCount(72) == Total;

	const bool bPass = bStartOwned && bTopUnowned && bLevelRefused && bGoldRefused && bBought && bLadder
		&& GateLevel >= 5;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE-GATE %s (startOwned=%d topUnowned=%d levelRefused=%d(%d) level=%d goldRefused=%d(%d) bought=%d(%d) gold=%lld->%lld ladder=%d total=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bStartOwned ? 1 : 0, bTopUnowned ? 1 : 0,
		bLevelRefused ? 1 : 0, static_cast<int32>(LevelTry), GateLevel, bGoldRefused ? 1 : 0,
		static_cast<int32>(GoldTry), bBought ? 1 : 0, static_cast<int32>(BuyTry),
		GoldBeforeBuy, GoldAfterBuy, bLadder ? 1 : 0, Total);
	return bPass;
}

/**
 * Buys the stall's first offer through the REAL menu path (OpenShopMenu resets the selection to 0,
 * ConfirmMenuSelection calls Buy), after force-levelling and granting the price the same way
 * -ShopProbe does. Leaves the menu OPEN. Row stays NAME_None when setup failed.
 */
FProbePurchase ProbeBuyFirstOffer(AWrecktideShopStall& Stall, ACombatCharacter& PlayerPawn, AWrecktidePlayerState& PlayerState,
	UEconomyComponent& Economy, USkillComponent& Skill, const UWrecktideDataSubsystem& DataSubsystem, const TCHAR* Marker)
{
	FProbePurchase Result;
	PlayerPawn.SetActorLocation(Stall.GetActorLocation() + Stall.GetActorRightVector() * 150.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Stall.OpenShopMenu(&PlayerPawn);
	const FName Row = Stall.GetSelectedOfferRow();
	const FWeaponTableRow* WeaponRow = DataSubsystem.GetWeaponRow(Row);
	if (!Stall.IsShopMenuOpen() || Row.IsNone() || !WeaponRow)
	{
		UE_LOG(LogTemp, Error, TEXT("%s buy setup failed menu=%d offers=%d row=%s"),
			Marker, Stall.IsShopMenuOpen() ? 1 : 0, Stall.GetOfferCount(), *Row.ToString());
		return Result;
	}

	if (!PlayerState.CanEquipWeapon(Row))
	{
		const int64 NeededXP = Skill.GetNotorietyXPForLevel(WeaponRow->RequiredLevel) - Skill.GetNotorietyXP();
		Skill.AwardXP(WeaponRow->CombatClass, FMath::Max<int64>(NeededXP, 1), FName(Marker));
	}
	Economy.ApplyGoldDelta(FMath::Max<int64>(WeaponRow->Price, 1), EGoldSource::Earned, EGoldReason::Adjustment, FName(Marker));

	Result.Row = Row;
	Result.Price = WeaponRow->Price;
	Result.bOwnedBefore = Economy.HasPurchasedRow(Row);
	Result.GoldBefore = Economy.GetGoldTotal();
	Stall.ConfirmMenuSelection();
	Result.bOwnedAfter = Economy.HasPurchasedRow(Row);
	Result.GoldAfter = Economy.GetGoldTotal();
	Result.EquippedAfter = PlayerState.EquippedWeaponRow;
	UE_LOG(LogTemp, Log, TEXT("%s buy row=%s price=%lld gold %lld->%lld owned %d->%d equipped=%s fresh=%d"),
		Marker, *Row.ToString(), Result.Price, Result.GoldBefore, Result.GoldAfter,
		Result.bOwnedBefore ? 1 : 0, Result.bOwnedAfter ? 1 : 0, *Result.EquippedAfter.ToString(),
		Result.IsFreshPurchase() ? 1 : 0);
	return Result;
}
}

void UWrecktideRewardSubsystem::RunOwnershipProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideShopStall* Stall = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
		{
			Stall = *It;
			break;
		}
	}
	if (!World || !PlayerPawn || !PlayerState || !Economy || !Skill || !DataSubsystem || !Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-OWNERSHIP-PROBE FAIL reason=wiring world=%d pawn=%d ps=%d economy=%d skill=%d data=%d stall=%d"),
			World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, Economy != nullptr, Skill != nullptr,
			DataSubsystem != nullptr, Stall != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Phase 1: buy the first offer through the real stall path, and assert the unowned -> owned
	// transition, not just the end state.
	const FProbePurchase Purchase = ProbeBuyFirstOffer(*Stall, *PlayerPawn, *PlayerState, *Economy, *Skill, *DataSubsystem, TEXT("WRECKTIDE-OWNERSHIP-PROBE"));
	const FName Row = Purchase.Row;
	const bool bBuyOK = Purchase.IsFreshPurchase();
	if (Stall->IsShopMenuOpen())
	{
		Stall->ToggleShopMenu(PlayerPawn);
	}
	if (!bBuyOK)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-OWNERSHIP-PROBE FAIL buy=0 flood=0 cycle=0 rebuy=0"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Phase 2: flood the gold ledger far past its 256-entry cap with kill-sized credits. Before the
	// 2026-08-21 fix ownership was answered by scanning that ledger, so this exact loop lost the weapon.
	constexpr int32 FloodEvents = 300;
	for (int32 Index = 0; Index < FloodEvents; ++Index)
	{
		Economy->ApplyGoldDelta(1, EGoldSource::Earned, EGoldReason::LootPickup, FName(TEXT("OwnershipProbeFlood")));
	}
	const bool bFloodOK = Economy->HasPurchasedRow(Row);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-OWNERSHIP-PROBE flood events=%d ownedAfter=%d gold=%lld"),
		FloodEvents, bFloodOK ? 1 : 0, Economy->GetGoldTotal());

	// Phase 3: key 1 still reaches the row from unarmed (spec C17: it wields slot 1, and slot 1 kept
	// the assignment when the hand emptied).
	PlayerState->EquipWeapon(NAME_None);
	PlayerPawn->UseEquipSlot1();
	const FName CycledRow = PlayerState->EquippedWeaponRow;
	const bool bCycleOK = CycledRow == Row;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-OWNERSHIP-PROBE cycle Unarmed->%s ok=%d"), *CycledRow.ToString(), bCycleOK ? 1 : 0);

	// Phase 4: Buy on an owned row equips without charging.
	PlayerState->EquipWeapon(NAME_None);
	const int64 GoldBeforeRebuy = Economy->GetGoldTotal();
	Stall->OpenShopMenu(PlayerPawn);
	const bool bSameOffer = Stall->IsShopMenuOpen() && Stall->GetSelectedOfferRow() == Row;
	Stall->ConfirmMenuSelection();
	const int64 GoldAfterRebuy = Economy->GetGoldTotal();
	const bool bRebuyOK = bSameOffer && GoldAfterRebuy == GoldBeforeRebuy && PlayerState->EquippedWeaponRow == Row;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-OWNERSHIP-PROBE rebuy sameOffer=%d gold %lld->%lld equipped=%s ok=%d"),
		bSameOffer ? 1 : 0, GoldBeforeRebuy, GoldAfterRebuy, *PlayerState->EquippedWeaponRow.ToString(), bRebuyOK ? 1 : 0);
	if (Stall->IsShopMenuOpen())
	{
		Stall->ToggleShopMenu(PlayerPawn);
	}

	const bool bPass = bBuyOK && bFloodOK && bCycleOK && bRebuyOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-OWNERSHIP-PROBE %s buy=%d flood=%d cycle=%d rebuy=%d row=%s"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bBuyOK ? 1 : 0, bFloodOK ? 1 : 0, bCycleOK ? 1 : 0, bRebuyOK ? 1 : 0, *Row.ToString());
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunSaveProbe()
{
	FString Phase;
	FParse::Value(FCommandLine::Get(), TEXT("SaveProbe="), Phase);
	Phase = Phase.ToLower();
	const FString Slot = AWrecktidePlayerState::GetSaveSlot();

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideShopStall* Stall = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
		{
			Stall = *It;
			break;
		}
	}
	if (!World || !PlayerPawn || !PlayerState || !Economy || !Skill || !Progress || !DataSubsystem || !Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAVE-PROBE FAIL phase=%s reason=wiring world=%d pawn=%d ps=%d economy=%d skill=%d progress=%d data=%d stall=%d"),
			*Phase, World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, Economy != nullptr, Skill != nullptr,
			Progress != nullptr, DataSubsystem != nullptr, Stall != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// The probe must never touch the player's real slot, and it needs persistence ON for its own.
	if (AWrecktidePlayerState::IsPersistenceSuppressed() || Slot == AWrecktidePlayerState::GetDefaultSaveSlot())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAVE-PROBE FAIL phase=%s reason=slot-guard slot=%s suppressed=%d (launch with -WrecktideSaveSlot=WrecktideProbe)"),
			*Phase, *Slot, AWrecktidePlayerState::IsPersistenceSuppressed() ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	if (Phase == TEXT("write"))
	{
		// The write phase must start from NOTHING: a reused slot would let the owned-row branch skip
		// the purchase and still "pass" (review 2026-09-02). save_probe.ps1 deletes the slot first.
		if (PlayerState->WasLoadedFromSlot())
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE-PROBE WRITE FAIL slot=%s reason=slot-not-fresh (delete %s.sav before the write phase)"), *Slot, *Slot);
			ScheduleProbeQuit(2.0f);
			return;
		}

		// Dirty every persisted field through the real paths: gold, XP and level, ownership, equip.
		Economy->ApplyGoldDelta(500, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("SaveProbe")));
		const FProbePurchase Purchase = ProbeBuyFirstOffer(*Stall, *PlayerPawn, *PlayerState, *Economy, *Skill, *DataSubsystem, TEXT("WRECKTIDE-SAVE-PROBE"));
		if (Stall->IsShopMenuOpen())
		{
			Stall->ToggleShopMenu(PlayerPawn);
		}
		const FName Row = Purchase.Row;
		const bool bBought = Purchase.IsFreshPurchase();

		// Non-default skill proof: the first offer needs level 1, which is the default, so push Melee
		// to level 2 with an XP remainder and persist BOTH numbers (AwardXP carries the remainder).
		Skill->AwardXP(EWrecktideSkill::Melee, Skill->GetXPToNext(Skill->GetSkillLevel(EWrecktideSkill::Melee)) + 38, FName(TEXT("SaveProbe")));
		const int32 MeleeLevel = Skill->GetSkillLevel(EWrecktideSkill::Melee);
		const int64 MeleeXP = Skill->GetSkillXP(EWrecktideSkill::Melee);
		const bool bSkillNonDefault = MeleeLevel >= 2 && MeleeXP > 0;
		const int64 NotorietyXP = Skill->GetNotorietyXP();
		const int32 NotorietyLevel = Skill->GetNotorietyLevel();
		const bool bNotorietyNonDefault = NotorietyXP > 0 && NotorietyLevel >= 2;

		// Stand the pirate on a known walkable spot on the beach BEFORE the flush, so the saved
		// position is a number this and the -MenuProbe=continue process can both check. The teleport
		// leaves the movement mode alone (still walking), which is what BuildSaveData requires.
		PlayerPawn->SetActorLocation(SaveProbeSpot, false, nullptr, ETeleportType::TeleportPhysics);
		const FVector SavedSpot = PlayerPawn->GetActorLocation();

		const bool bSaved = PlayerState->SaveNow();
		const bool bExists = UGameplayStatics::DoesSaveGameExist(Slot, 0);
		const FString Expect = FString::Printf(TEXT("gold=%lld\nequipped=%s\nrow=%s\nmelee=%d\nxp=%lld\nnotorietyxp=%lld\nnotorietylevel=%d\nunlocked=%d\nLocation=%.0f,%.0f\n"),
			Economy->GetGoldTotal(), *PlayerState->EquippedWeaponRow.ToString(), *Row.ToString(), MeleeLevel, MeleeXP,
			NotorietyXP, NotorietyLevel, Progress->UnlockedItems.Num(), SavedSpot.X, SavedSpot.Y);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE-PROBE WRITE position saved at %s"), *SavedSpot.ToString());
		const bool bSidecar = FFileHelper::SaveStringToFile(Expect, *SaveProbeExpectPath(Slot));

		const bool bPass = bBought && bSkillNonDefault && bNotorietyNonDefault && bSaved && bExists && bSidecar;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE-PROBE WRITE %s slot=%s bought=%d skill=%d notoriety=%d saved=%d exists=%d sidecar=%d gold=%lld equipped=%s melee=%d xp=%lld notorietyXP=%lld notorietyLevel=%d unlocked=%d"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), *Slot, bBought ? 1 : 0, bSkillNonDefault ? 1 : 0,
			bNotorietyNonDefault ? 1 : 0, bSaved ? 1 : 0, bExists ? 1 : 0, bSidecar ? 1 : 0,
			Economy->GetGoldTotal(), *PlayerState->EquippedWeaponRow.ToString(), MeleeLevel, MeleeXP,
			NotorietyXP, NotorietyLevel, Progress->UnlockedItems.Num());
		ScheduleProbeQuit(2.0f);
		return;
	}

	if (Phase == TEXT("read"))
	{
		FString Expect;
		if (!FFileHelper::LoadFileToString(Expect, *SaveProbeExpectPath(Slot)))
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAVE-PROBE READ FAIL reason=no-sidecar path=%s"), *SaveProbeExpectPath(Slot));
			ScheduleProbeQuit(2.0f);
			return;
		}

		int64 ExpectedGold = 0;
		FString ExpectedEquipped;
		FString ExpectedRow;
		int32 ExpectedMelee = 0;
		int64 ExpectedXP = -1;
		int64 ExpectedNotorietyXP = -1;
		int32 ExpectedNotorietyLevel = 0;
		int32 ExpectedUnlocked = 0;
		FParse::Value(*Expect, TEXT("gold="), ExpectedGold);
		FParse::Value(*Expect, TEXT("equipped="), ExpectedEquipped);
		FParse::Value(*Expect, TEXT("row="), ExpectedRow);
		FParse::Value(*Expect, TEXT("melee="), ExpectedMelee);
		FParse::Value(*Expect, TEXT("xp="), ExpectedXP);
		FParse::Value(*Expect, TEXT("notorietyxp="), ExpectedNotorietyXP);
		FParse::Value(*Expect, TEXT("notorietylevel="), ExpectedNotorietyLevel);
		FParse::Value(*Expect, TEXT("unlocked="), ExpectedUnlocked);

		// Everything below was restored by the PlayerState's OWN BeginPlay load in this fresh process;
		// the probe only reads. Level AND XP must match, and the level must be non-default, or the
		// skill half of the save is not proven.
		const bool bLoaded = PlayerState->WasLoadedFromSlot();
		const int64 Gold = Economy->GetGoldTotal();
		const bool bGoldOK = Gold == ExpectedGold;
		const bool bOwnedOK = Economy->HasPurchasedRow(FName(*ExpectedRow));
		const FString Equipped = PlayerState->EquippedWeaponRow.ToString();
		const bool bEquippedOK = Equipped == ExpectedEquipped;
		const int32 MeleeLevel = Skill->GetSkillLevel(EWrecktideSkill::Melee);
		const int64 MeleeXP = Skill->GetSkillXP(EWrecktideSkill::Melee);
		const bool bMeleeOK = MeleeLevel == ExpectedMelee && MeleeXP == ExpectedXP && ExpectedMelee >= 2 && ExpectedXP > 0;
		const int64 NotorietyXP = Skill->GetNotorietyXP();
		const int32 NotorietyLevel = Skill->GetNotorietyLevel();
		const bool bNotorietyOK = NotorietyXP == ExpectedNotorietyXP
			&& NotorietyLevel == ExpectedNotorietyLevel && ExpectedNotorietyXP > 0 && ExpectedNotorietyLevel >= 2;
		const int32 Unlocked = Progress->UnlockedItems.Num();
		const bool bUnlockedOK = Unlocked == ExpectedUnlocked;

		const bool bPass = bLoaded && bGoldOK && bOwnedOK && bEquippedOK && bMeleeOK && bNotorietyOK && bUnlockedOK;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE-PROBE READ %s slot=%s loaded=%d gold=%d(%lld/%lld) owned=%d(%s) equipped=%d(%s/%s) melee=%d(%d/%d xp %lld/%lld) notoriety=%d(level %d/%d xp %lld/%lld) unlocked=%d(%d/%d)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), *Slot, bLoaded ? 1 : 0, bGoldOK ? 1 : 0, Gold, ExpectedGold,
			bOwnedOK ? 1 : 0, *ExpectedRow, bEquippedOK ? 1 : 0, *Equipped, *ExpectedEquipped,
			bMeleeOK ? 1 : 0, MeleeLevel, ExpectedMelee, MeleeXP, ExpectedXP,
			bNotorietyOK ? 1 : 0, NotorietyLevel, ExpectedNotorietyLevel, NotorietyXP, ExpectedNotorietyXP,
			bUnlockedOK ? 1 : 0, Unlocked, ExpectedUnlocked);
		ScheduleProbeQuit(2.0f);
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAVE-PROBE FAIL reason=bad-phase phase=%s (use -SaveProbe=write or -SaveProbe=read)"), *Phase);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunWardrobeProbe()
{
	FString Phase;
	FParse::Value(FCommandLine::Get(), TEXT("WardrobeProbe="), Phase);
	Phase = Phase.ToLower();
	const FString Slot = AWrecktidePlayerState::GetSaveSlot();

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UWrecktideWardrobeSubsystem* Wardrobe = GameInstance ? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!World || !PlayerPawn || !PlayerState || !Wardrobe || !DataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-PROBE FAIL (phase=%s reason=wiring world=%d pawn=%d ps=%d wardrobe=%d data=%d)"),
			*Phase, World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, Wardrobe != nullptr, DataSubsystem != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	if (AWrecktidePlayerState::IsPersistenceSuppressed() || Slot == AWrecktidePlayerState::GetDefaultSaveSlot())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-PROBE FAIL (phase=%s reason=slot-guard slot=%s suppressed=%d)"),
			*Phase, *Slot, AWrecktidePlayerState::IsPersistenceSuppressed() ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	if (Phase == TEXT("read"))
	{
		FString Expect;
		const bool bSidecarRead = FFileHelper::LoadFileToString(Expect, *WardrobeProbeExpectPath(Slot));
		Expect.TrimStartAndEndInline();
		const bool bSidecarOK = bSidecarRead && Expect == TEXT("Outfit=FirstMate Variant=4");
		const FName ExpectedId(TEXT("FirstMate"));
		const FOutfitTableRow* Row = DataSubsystem->GetOutfitRow(ExpectedId);
		USkeletalMesh* ExpectedMesh = Row ? Row->SkinMesh.LoadSynchronous() : nullptr;
		USkeletalMeshComponent* CosmeticMesh = PlayerPawn->GetCosmeticMesh();
		const bool bCurrentOK = Wardrobe->GetCurrentOutfitId(PlayerPawn) == ExpectedId
			&& Wardrobe->GetCurrentVariant(PlayerPawn) == 4;
		const bool bMeshOK = ExpectedMesh && CosmeticMesh && CosmeticMesh->GetSkeletalMeshAsset() == ExpectedMesh;
		const bool bMaterialsOK = ExpectedMesh && CosmeticMesh
			&& WardrobeProbeMaterialsMatch(*Wardrobe, ExpectedId, 4, *ExpectedMesh, *CosmeticMesh);
		const bool bRetargetOK = PlayerPawn->IsCosmeticRetargeterLive();
		// The outfit BOUGHT in the write process must still be owned here: ownership is saved state,
		// not a live flag (spec C9 item 3).
		const bool bOwnedOK = Wardrobe->IsOutfitOwned(PlayerPawn, FName("Deckhand"))
			&& Wardrobe->IsOutfitOwned(PlayerPawn, FName("Seaman"))
			&& !Wardrobe->IsOutfitOwned(PlayerPawn, FName("Turncoat"));
		const bool bPass = PlayerState->WasLoadedFromSlot() && bSidecarOK && bCurrentOK && bMeshOK && bMaterialsOK
			&& bRetargetOK && bOwnedOK;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE-PROBE %s (phase=read slot=%s loaded=%d sidecar=%d current=%d mesh=%d materials=%d retarget=%d owned=%d)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), *Slot, PlayerState->WasLoadedFromSlot() ? 1 : 0, bSidecarOK ? 1 : 0,
			bCurrentOK ? 1 : 0, bMeshOK ? 1 : 0, bMaterialsOK ? 1 : 0, bRetargetOK ? 1 : 0, bOwnedOK ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	if (Phase != TEXT("write"))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-PROBE FAIL (reason=bad-phase phase=%s use write or read)"), *Phase);
		ScheduleProbeQuit(2.0f);
		return;
	}

	if (PlayerState->WasLoadedFromSlot())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-PROBE FAIL (phase=write reason=slot-not-fresh slot=%s)"), *Slot);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const TArray<FWrecktideOutfitInfo> Outfits = Wardrobe->GetOutfits();
	WardrobeProbeOutfitIds.Reset();
	WardrobeProbeVariantCounts.Reset();
	bWardrobeProbeAllOK = Outfits.Num() > 0 && Outfits.Num() == DataSubsystem->GetOutfitRows().Num();
	for (const FWrecktideOutfitInfo& Outfit : Outfits)
	{
		WardrobeProbeOutfitIds.Add(Outfit.Id);
		WardrobeProbeVariantCounts.Add(Outfit.VariantCount);
		bWardrobeProbeAllOK &= Outfit.VariantCount >= 2;
	}

	WardrobeProbeOutfitIndex = 0;
	WardrobeProbeVariantChoice = 0;
	WardrobeProbeCases = 0;
	WardrobeProbeAnchor = PlayerPawn->GetActorTransform();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE-PHASE catalogue rows=%d variants=%d ready=%d"),
		Outfits.Num(), Outfits.Num() > 0 ? Outfits[0].VariantCount : 0, bWardrobeProbeAllOK ? 1 : 0);
	StartNextWardrobeProbeCase();
}

void UWrecktideRewardSubsystem::StartNextWardrobeProbeCase()
{
	if (WardrobeProbeOutfitIndex >= WardrobeProbeOutfitIds.Num())
	{
		FinishWardrobeProbeWrite();
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UWrecktideWardrobeSubsystem* Wardrobe = GameInstance ? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!World || !PlayerPawn || !Wardrobe || !DataSubsystem || !WardrobeProbeVariantCounts.IsValidIndex(WardrobeProbeOutfitIndex))
	{
		bWardrobeProbeAllOK = false;
		FinishWardrobeProbeWrite();
		return;
	}

	PlayerPawn->SetActorTransform(WardrobeProbeAnchor, false, nullptr, ETeleportType::TeleportPhysics);
	PlayerPawn->GetCharacterMovement()->StopMovementImmediately();
	const FName OutfitId = WardrobeProbeOutfitIds[WardrobeProbeOutfitIndex];
	const int32 VariantCount = WardrobeProbeVariantCounts[WardrobeProbeOutfitIndex];
	const int32 Variant = WardrobeProbeVariantChoice == 0 ? 0
		: (WardrobeProbeVariantChoice == 1 ? 1 : VariantCount - 1);
	const FOutfitTableRow* Row = DataSubsystem->GetOutfitRow(OutfitId);
	USkeletalMesh* ExpectedMesh = Row ? Row->SkinMesh.LoadSynchronous() : nullptr;
	const bool bSet = Wardrobe->SetOutfit(PlayerPawn, OutfitId, Variant);
	USkeletalMeshComponent* CosmeticMesh = PlayerPawn->GetCosmeticMesh();
	const bool bMeshOK = ExpectedMesh && CosmeticMesh && CosmeticMesh->GetSkeletalMeshAsset() == ExpectedMesh;
	const bool bMaterialsOK = ExpectedMesh && CosmeticMesh
		&& WardrobeProbeMaterialsMatch(*Wardrobe, OutfitId, Variant, *ExpectedMesh, *CosmeticMesh);
	const bool bRetargetOK = PlayerPawn->IsCosmeticRetargeterLive();
	WardrobeProbeBone = FindWardrobeProbeLegBone(CosmeticMesh);
	bWardrobeProbeCaseOK = bSet && bMeshOK && bMaterialsOK && bRetargetOK && !WardrobeProbeBone.IsNone();
	WardrobeProbeMaxBoneMove = 0.0f;
	WardrobeProbeBoneStart = !WardrobeProbeBone.IsNone()
		? CosmeticMesh->GetSocketTransform(WardrobeProbeBone, RTS_Component).GetLocation() : FVector::ZeroVector;

	World->GetTimerManager().SetTimer(WardrobeProbeDriveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		ACombatCharacter* InnerPawn = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr;
		USkeletalMeshComponent* InnerMesh = InnerPawn ? InnerPawn->GetCosmeticMesh() : nullptr;
		if (!InnerPawn || !InnerMesh || WardrobeProbeBone.IsNone())
		{
			bWardrobeProbeCaseOK = false;
			return;
		}
		InnerPawn->DoMove(0.0f, 1.0f);
		const FVector BoneLocation = InnerMesh->GetSocketTransform(WardrobeProbeBone, RTS_Component).GetLocation();
		WardrobeProbeMaxBoneMove = FMath::Max(WardrobeProbeMaxBoneMove, FVector::Distance(BoneLocation, WardrobeProbeBoneStart));
	}), 0.016f, true);
	World->GetTimerManager().SetTimer(WardrobeProbeFinishTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishWardrobeProbeCase), 0.65f, false);
}

void UWrecktideRewardSubsystem::FinishWardrobeProbeCase()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WardrobeProbeDriveTimer);
	}

	const FName OutfitId = WardrobeProbeOutfitIds.IsValidIndex(WardrobeProbeOutfitIndex)
		? WardrobeProbeOutfitIds[WardrobeProbeOutfitIndex] : NAME_None;
	const int32 VariantCount = WardrobeProbeVariantCounts.IsValidIndex(WardrobeProbeOutfitIndex)
		? WardrobeProbeVariantCounts[WardrobeProbeOutfitIndex] : 0;
	const int32 Variant = WardrobeProbeVariantChoice == 0 ? 0
		: (WardrobeProbeVariantChoice == 1 ? 1 : VariantCount - 1);
	const bool bMotionOK = WardrobeProbeMaxBoneMove > 5.0f;
	const bool bCasePass = bWardrobeProbeCaseOK && bMotionOK;
	bWardrobeProbeAllOK &= bCasePass;
	++WardrobeProbeCases;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE-CASE outfit=%s variant=%d static=%d bone=%s move=%.1f result=%s"),
		*OutfitId.ToString(), Variant, bWardrobeProbeCaseOK ? 1 : 0, *WardrobeProbeBone.ToString(),
		WardrobeProbeMaxBoneMove, bCasePass ? TEXT("PASS") : TEXT("FAIL"));

	++WardrobeProbeVariantChoice;
	if (WardrobeProbeVariantChoice >= 3)
	{
		WardrobeProbeVariantChoice = 0;
		++WardrobeProbeOutfitIndex;
	}
	StartNextWardrobeProbeCase();
}

void UWrecktideRewardSubsystem::FinishWardrobeProbeWrite()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(WardrobeProbeDriveTimer);
		World->GetTimerManager().ClearTimer(WardrobeProbeFinishTimer);
	}
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UWrecktideWardrobeSubsystem* Wardrobe = GameInstance ? GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	// The screen cases run BEFORE the final pick: they leave the pirate exactly as they found him,
	// so the outfit the read phase expects is still decided below.
	ACombatPlayerController* MenuController = Cast<ACombatPlayerController>(PlayerController);
	if (PlayerPawn && PlayerState && Wardrobe && MenuController)
	{
		bWardrobeProbeAllOK &= RunWardrobeScreenCases(*PlayerPawn, *PlayerState, *MenuController, *Wardrobe,
			AWrecktidePlayerState::GetSaveSlot());
		bWardrobeProbeAllOK &= RunWardrobeGateCases(*PlayerPawn, *PlayerState, *Wardrobe);
	}
	else
	{
		bWardrobeProbeAllOK = false;
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-ROOM FAIL reason=wiring pawn=%d ps=%d wardrobe=%d controller=%d"),
			PlayerPawn != nullptr, PlayerState != nullptr, Wardrobe != nullptr, MenuController != nullptr);
	}

	const FName FinalId(TEXT("FirstMate"));
	const int32 FinalVariant = 4;
	const FOutfitTableRow* Row = DataSubsystem ? DataSubsystem->GetOutfitRow(FinalId) : nullptr;
	USkeletalMesh* ExpectedMesh = Row ? Row->SkinMesh.LoadSynchronous() : nullptr;
	const bool bSet = Wardrobe && PlayerPawn && Wardrobe->SetOutfit(PlayerPawn, FinalId, FinalVariant);
	USkeletalMeshComponent* CosmeticMesh = PlayerPawn ? PlayerPawn->GetCosmeticMesh() : nullptr;
	const bool bFinalOK = bSet && ExpectedMesh && CosmeticMesh && CosmeticMesh->GetSkeletalMeshAsset() == ExpectedMesh
		&& WardrobeProbeMaterialsMatch(*Wardrobe, FinalId, FinalVariant, *ExpectedMesh, *CosmeticMesh)
		&& PlayerPawn->IsCosmeticRetargeterLive();
	const bool bSaved = PlayerState && PlayerState->SaveNow();
	const FString Slot = AWrecktidePlayerState::GetSaveSlot();
	const bool bExists = UGameplayStatics::DoesSaveGameExist(Slot, 0);
	const bool bSidecar = FFileHelper::SaveStringToFile(TEXT("Outfit=FirstMate Variant=4"), *WardrobeProbeExpectPath(Slot));
	const int32 ExpectedCases = WardrobeProbeOutfitIds.Num() * 3;
	const bool bPass = bWardrobeProbeAllOK && WardrobeProbeCases == ExpectedCases && bFinalOK && bSaved && bExists && bSidecar;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE-PROBE %s (phase=write slot=%s cases=%d/%d final=%d saved=%d exists=%d sidecar=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *Slot, WardrobeProbeCases, ExpectedCases, bFinalOK ? 1 : 0,
		bSaved ? 1 : 0, bExists ? 1 : 0, bSidecar ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -PoseProbe (2026-09-04): numeric leg-pose gate. A play test screenshot showed the pirate skin
// standing with bent knees (a half-squat) while the hidden driver mannequin stands straight, and
// worse while moving. Every earlier round judged this by eye from far captures; this probe reads
// the bones. For the driver mesh (retarget SOURCE) and the cosmetic skin (retarget TARGET) it logs,
// in component space: knee angle (0 = straight), leg extension (hip-to-ankle distance over
// thigh+calf length, 1.0 = straight), pelvis height and foot heights, plus the same numbers for each
// mesh asset's bind pose (the retargeter measures its chain lengths from that pose). Samples are
// bucketed by speed: idle (<10 uu/s) and moving (>100 uu/s); the pawn is driven forward with DoMove
// for part of the window exactly like -LocoProbe. Verdict: the skin may bend at most 8 degrees more
// than the driver while idle and 12 degrees more while moving.
// ---------------------------------------------------------------------------------------------
namespace WrecktidePoseProbe
{
	struct FLegSample
	{
		/** Knee lateral deviation from the hip->ankle line (cm, + = outward) and thigh splay in the
		 *  frontal plane (deg, + = outward), per side: the numbers that exposed the running bow leg. */
		float KneeLatL = 0.0f;
		float KneeLatR = 0.0f;
		float ThighAbdL = 0.0f;
		float ThighAbdR = 0.0f;
		float KneeL = 0.0f;
		float KneeR = 0.0f;
		float ExtL = 0.0f;
		float ExtR = 0.0f;
		float LegLenL = 0.0f;
		float LegLenR = 0.0f;
		float PelvisZ = 0.0f;
		float FootLZ = 0.0f;
		float FootRZ = 0.0f;
		bool bOK = false;
	};

	static void LegMetrics(const FVector& Thigh, const FVector& Calf, const FVector& Foot, float& OutKnee, float& OutExt, float& OutLen)
	{
		const FVector A = Calf - Thigh;
		const FVector B = Foot - Calf;
		const float LenA = static_cast<float>(A.Size());
		const float LenB = static_cast<float>(B.Size());
		OutLen = LenA + LenB;
		OutExt = OutLen > UE_KINDA_SMALL_NUMBER ? static_cast<float>((Foot - Thigh).Size()) / OutLen : 0.0f;
		float Cos = 1.0f;
		if (LenA > UE_KINDA_SMALL_NUMBER && LenB > UE_KINDA_SMALL_NUMBER)
		{
			Cos = static_cast<float>(FVector::DotProduct(A / LenA, B / LenB));
		}
		OutKnee = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.0f, 1.0f)));
	}

	static bool LiveBonePos(const USkeletalMeshComponent* Comp, const TCHAR* Bone, FVector& Out)
	{
		if (!Comp || Comp->GetBoneIndex(FName(Bone)) == INDEX_NONE)
		{
			return false;
		}
		Out = Comp->GetSocketTransform(FName(Bone), RTS_Component).GetLocation();
		return true;
	}

	static bool RefBonePos(const USkeletalMesh* Mesh, const TCHAR* Bone, FVector& Out)
	{
		if (!Mesh)
		{
			return false;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const int32 Index = Ref.FindBoneIndex(FName(Bone));
		if (Index == INDEX_NONE)
		{
			return false;
		}
		const TArray<FTransform>& Local = Ref.GetRefBonePose();
		FTransform Transform = Local[Index];
		for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
		{
			Transform = Transform * Local[Parent];
		}
		Out = Transform.GetLocation();
		return true;
	}

	template <typename TSource, typename TGetter>
	static FLegSample SampleWith(const TSource* Source, TGetter Getter)
	{
		FLegSample Sample;
		FVector Pelvis, ThighL, CalfL, FootL, ThighR, CalfR, FootR;
		const bool bAll = Getter(Source, TEXT("pelvis"), Pelvis)
			&& Getter(Source, TEXT("thigh_l"), ThighL) && Getter(Source, TEXT("calf_l"), CalfL) && Getter(Source, TEXT("foot_l"), FootL)
			&& Getter(Source, TEXT("thigh_r"), ThighR) && Getter(Source, TEXT("calf_r"), CalfR) && Getter(Source, TEXT("foot_r"), FootR);
		if (!bAll)
		{
			return Sample;
		}
		LegMetrics(ThighL, CalfL, FootL, Sample.KneeL, Sample.ExtL, Sample.LegLenL);
		LegMetrics(ThighR, CalfR, FootR, Sample.KneeR, Sample.ExtR, Sample.LegLenR);
		// body frame from the hips: lateral = right hip minus left hip, up = Z
		const FVector Lateral = (ThighR - ThighL).GetSafeNormal();
		auto KneeLateral = [&](const FVector& Thigh, const FVector& Calf, const FVector& Foot, float Sign)
		{
			const FVector Axis = (Foot - Thigh).GetSafeNormal();
			const FVector V = Calf - Thigh;
			const FVector Perp = V - Axis * FVector::DotProduct(V, Axis);
			return Sign * static_cast<float>(FVector::DotProduct(Perp, Lateral));
		};
		auto ThighAbduction = [&](const FVector& Thigh, const FVector& Calf, float Sign)
		{
			const FVector T = Calf - Thigh;
			return Sign * FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(FVector::DotProduct(T, Lateral)), static_cast<float>(-T.Z)));
		};
		Sample.KneeLatL = KneeLateral(ThighL, CalfL, FootL, -1.0f);
		Sample.KneeLatR = KneeLateral(ThighR, CalfR, FootR, 1.0f);
		Sample.ThighAbdL = ThighAbduction(ThighL, CalfL, -1.0f);
		Sample.ThighAbdR = ThighAbduction(ThighR, CalfR, 1.0f);
		Sample.PelvisZ = static_cast<float>(Pelvis.Z);
		Sample.FootLZ = static_cast<float>(FootL.Z);
		Sample.FootRZ = static_cast<float>(FootR.Z);
		Sample.bOK = true;
		return Sample;
	}

	static FLegSample SampleLive(const USkeletalMeshComponent* Comp)
	{
		return SampleWith(Comp, &LiveBonePos);
	}

	static FLegSample SampleRef(const USkeletalMesh* Mesh)
	{
		return SampleWith(Mesh, &RefBonePos);
	}

	/** Raw component-space positions of the leg chain plus the pelvis rotation, for offline analysis
	 *  (stride width, foot crossing, knee lateral deviation, foot yaw): one compact line per mesh. */
	static FString DumpBones(const USkeletalMeshComponent* Comp)
	{
		if (!Comp)
		{
			return TEXT("none");
		}
		FString Out;
		for (const TCHAR* Bone : {TEXT("pelvis"), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r")})
		{
			FVector P;
			if (LiveBonePos(Comp, Bone, P))
			{
				Out += FString::Printf(TEXT("%s=%.1f,%.1f,%.1f;"), Bone, P.X, P.Y, P.Z);
			}
		}
		const int32 PelvisIndex = Comp->GetBoneIndex(FName(TEXT("pelvis")));
		if (PelvisIndex != INDEX_NONE)
		{
			const FRotator R = Comp->GetSocketTransform(FName(TEXT("pelvis")), RTS_Component).GetRotation().Rotator();
			Out += FString::Printf(TEXT("pelvisRot=%.1f,%.1f,%.1f;"), R.Pitch, R.Yaw, R.Roll);
		}
		return Out;
	}

	/** Bind-pose twin of DumpBones for a mesh asset (component space from the reference skeleton). */
	static FString DumpRefBones(const USkeletalMesh* Mesh)
	{
		if (!Mesh)
		{
			return TEXT("none");
		}
		FString Out;
		for (const TCHAR* Bone : {TEXT("pelvis"), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r")})
		{
			FVector P;
			if (RefBonePos(Mesh, Bone, P))
			{
				Out += FString::Printf(TEXT("%s=%.1f,%.1f,%.1f;"), Bone, P.X, P.Y, P.Z);
			}
		}
		return Out;
	}

	static FString Describe(const TCHAR* Tag, const FLegSample& S)
	{
		if (!S.bOK)
		{
			return FString::Printf(TEXT("%s (bones missing)"), Tag);
		}
		return FString::Printf(TEXT("%s kneeL=%.1f kneeR=%.1f extL=%.3f extR=%.3f legL=%.1f legR=%.1f pelvZ=%.1f footLz=%.1f footRz=%.1f"),
			Tag, S.KneeL, S.KneeR, S.ExtL, S.ExtR, S.LegLenL, S.LegLenR, S.PelvisZ, S.FootLZ, S.FootRZ);
	}
}

void UWrecktideRewardSubsystem::RunPoseProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	USkeletalMeshComponent* Driver = PlayerPawn ? PlayerPawn->GetMesh() : nullptr;
	USkeletalMeshComponent* Skin = PlayerPawn ? PlayerPawn->GetCosmeticMesh() : nullptr;
	if (!PlayerPawn || !Driver || !Skin || !Skin->GetSkeletalMeshAsset())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-POSE-PROBE FAIL reason=no-pawn-or-skin pawn=%d driver=%d skin=%d"),
			PlayerPawn != nullptr, Driver != nullptr, (Skin && Skin->GetSkeletalMeshAsset()) ? 1 : 0);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE FAIL (nothing to measure)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// bind-pose geometry of both mesh assets: the retargeter measures chain lengths and the pelvis
	// height off these, so a bent bind pose on the skin would show up here before any animation runs
	const FString DriverName = Driver->GetSkeletalMeshAsset() ? Driver->GetSkeletalMeshAsset()->GetName() : TEXT("None");
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE ref driver=%s %s"), *DriverName,
		*WrecktidePoseProbe::Describe(TEXT("DRV"), WrecktidePoseProbe::SampleRef(Driver->GetSkeletalMeshAsset())));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE ref skin=%s retargeterLive=%d %s"), *Skin->GetSkeletalMeshAsset()->GetName(),
		PlayerPawn->IsCosmeticRetargeterLive() ? 1 : 0,
		*WrecktidePoseProbe::Describe(TEXT("SKIN"), WrecktidePoseProbe::SampleRef(Skin->GetSkeletalMeshAsset())));

	// quiet spot: the far end of the pier, facing north along its 22 m deck. Enemies chase now (navmesh
	// 2026-09-04) and the spawn is a brawl within seconds; a hit reaction or a launch mid-sample would
	// poison the gait numbers. The drive runs the deck's length, well inside the surf-wall gap.
	PlayerPawn->SetActorLocation(FVector(0.0f, -8950.0f, 60.0f), false, nullptr, ETeleportType::TeleportPhysics);
	PlayerPawn->SetActorRotation(FRotator(0.0f, 90.0f, 0.0f));
	PlayerController->SetControlRotation(FRotator(-8.0f, 90.0f, 0.0f));

	// bind poses in the same raw format so the offline analyzer can compare frontal-plane geometry
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-RAW s=0 speed=0 DRVREF %s"), *WrecktidePoseProbe::DumpRefBones(Driver->GetSkeletalMeshAsset()));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-RAW s=0 speed=0 SKINREF %s"), *WrecktidePoseProbe::DumpRefBones(Skin->GetSkeletalMeshAsset()));

	PoseProbeStartTime = World->GetTimeSeconds();
	PoseProbeSamples = 0;
	PoseProbeCount[0] = PoseProbeCount[1] = 0;
	PoseProbeMaxKneeDelta = 0.0;
	PoseProbeSumKneeLatDelta = 0.0;
	PoseProbeSumThighAbdDelta = 0.0;
	for (int32 Bucket = 0; Bucket < 2; ++Bucket)
	{
		for (int32 Mesh = 0; Mesh < 2; ++Mesh)
		{
			PoseProbeSumKnee[Bucket][Mesh] = 0.0;
			PoseProbeSumExt[Bucket][Mesh] = 0.0;
		}
	}

	World->GetTimerManager().SetTimer(PoseProbeSampleTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::SamplePoseProbe), 0.2f, true);

	// idle for 3s, then drive forward for 4s (DoMove, the same programmatic entry -LocoProbe uses),
	// then idle again; FinishPoseProbe closes the window
	FTimerHandle DriveStartTimer;
	World->GetTimerManager().SetTimer(DriveStartTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		if (!InnerWorld)
		{
			return;
		}
		InnerWorld->GetTimerManager().SetTimer(PoseProbeDriveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* DriveWorld = GetWorld();
			APlayerController* DriveController = DriveWorld ? UGameplayStatics::GetPlayerController(DriveWorld, 0) : nullptr;
			if (ACombatCharacter* DrivePawn = DriveController ? Cast<ACombatCharacter>(DriveController->GetPawn()) : nullptr)
			{
				DrivePawn->DoMove(0.0f, 1.0f);
			}
		}), 0.016f, true);
	}), 3.0f, false);

	FTimerHandle DriveStopTimer;
	World->GetTimerManager().SetTimer(DriveStopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (UWorld* InnerWorld = GetWorld())
		{
			InnerWorld->GetTimerManager().ClearTimer(PoseProbeDriveTimer);
		}
	}), 7.0f, false);

	FTimerHandle FinishTimer;
	World->GetTimerManager().SetTimer(FinishTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishPoseProbe), 11.0f, false);
}

void UWrecktideRewardSubsystem::SamplePoseProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		return;
	}
	const USkeletalMeshComponent* Driver = PlayerPawn->GetMesh();
	const USkeletalMeshComponent* Skin = PlayerPawn->GetCosmeticMesh();
	const WrecktidePoseProbe::FLegSample DriverSample = WrecktidePoseProbe::SampleLive(Driver);
	const WrecktidePoseProbe::FLegSample SkinSample = WrecktidePoseProbe::SampleLive(Skin);
	const float Speed = static_cast<float>(PlayerPawn->GetVelocity().Size2D());
	const double T = World->GetTimeSeconds() - PoseProbeStartTime;
	++PoseProbeSamples;

	int32 Bucket = -1;
	if (Speed < 10.0f)
	{
		Bucket = 0;
	}
	else if (Speed > 100.0f)
	{
		Bucket = 1;
	}

	double KneeDelta = 0.0;
	if (DriverSample.bOK && SkinSample.bOK)
	{
		const double DriverKnee = 0.5 * (DriverSample.KneeL + DriverSample.KneeR);
		const double SkinKnee = 0.5 * (SkinSample.KneeL + SkinSample.KneeR);
		KneeDelta = SkinKnee - DriverKnee;
		PoseProbeMaxKneeDelta = FMath::Max(PoseProbeMaxKneeDelta, KneeDelta);
		if (Bucket >= 0)
		{
			PoseProbeSumKnee[Bucket][0] += DriverKnee;
			PoseProbeSumKnee[Bucket][1] += SkinKnee;
			PoseProbeSumExt[Bucket][0] += 0.5 * (DriverSample.ExtL + DriverSample.ExtR);
			PoseProbeSumExt[Bucket][1] += 0.5 * (SkinSample.ExtL + SkinSample.ExtR);
			++PoseProbeCount[Bucket];
		}
		if (Bucket == 1)
		{
			// the running bow leg lived here: knee sideways offset and thigh splay, skin minus driver
			PoseProbeSumKneeLatDelta += 0.5 * (FMath::Abs(SkinSample.KneeLatL - DriverSample.KneeLatL) + FMath::Abs(SkinSample.KneeLatR - DriverSample.KneeLatR));
			PoseProbeSumThighAbdDelta += 0.5 * (FMath::Abs(SkinSample.ThighAbdL - DriverSample.ThighAbdL) + FMath::Abs(SkinSample.ThighAbdR - DriverSample.ThighAbdR));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE s=%d t=%.1f speed=%.0f bucket=%s kneeDelta=%.1f | %s | %s"),
		PoseProbeSamples, T, Speed, Bucket == 0 ? TEXT("idle") : (Bucket == 1 ? TEXT("moving") : TEXT("mixed")), KneeDelta,
		*WrecktidePoseProbe::Describe(TEXT("DRV"), DriverSample), *WrecktidePoseProbe::Describe(TEXT("SKIN"), SkinSample));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-RAW s=%d speed=%.0f DRV %s"), PoseProbeSamples, Speed, *WrecktidePoseProbe::DumpBones(Driver));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-RAW s=%d speed=%.0f SKIN %s"), PoseProbeSamples, Speed, *WrecktidePoseProbe::DumpBones(Skin));
}

void UWrecktideRewardSubsystem::FinishPoseProbe()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PoseProbeSampleTimer);
		World->GetTimerManager().ClearTimer(PoseProbeDriveTimer);
	}

	double MeanKnee[2][2] = {{0.0, 0.0}, {0.0, 0.0}};
	double MeanExt[2][2] = {{0.0, 0.0}, {0.0, 0.0}};
	for (int32 Bucket = 0; Bucket < 2; ++Bucket)
	{
		for (int32 Mesh = 0; Mesh < 2; ++Mesh)
		{
			MeanKnee[Bucket][Mesh] = PoseProbeCount[Bucket] > 0 ? PoseProbeSumKnee[Bucket][Mesh] / PoseProbeCount[Bucket] : 0.0;
			MeanExt[Bucket][Mesh] = PoseProbeCount[Bucket] > 0 ? PoseProbeSumExt[Bucket][Mesh] / PoseProbeCount[Bucket] : 0.0;
		}
	}
	const double IdleDelta = MeanKnee[0][1] - MeanKnee[0][0];
	const double MovingDelta = MeanKnee[1][1] - MeanKnee[1][0];
	const double KneeLatDelta = PoseProbeCount[1] > 0 ? PoseProbeSumKneeLatDelta / PoseProbeCount[1] : 0.0;
	const double ThighAbdDelta = PoseProbeCount[1] > 0 ? PoseProbeSumThighAbdDelta / PoseProbeCount[1] : 0.0;
	const bool bEnoughSamples = PoseProbeCount[0] >= 5 && PoseProbeCount[1] >= 5;
	const bool bIdleOK = IdleDelta <= 8.0;
	const bool bMovingOK = MovingDelta <= 12.0;
	// frontal plane (2026-09-04 night): with the leg pole-vector op on, these read 13 to 17 cm and
	// 30 to 37 deg; with it off, 0.2 cm and 0.7 deg. The gate sits well inside that gap.
	const bool bFrontalOK = KneeLatDelta <= 3.0 && ThighAbdDelta <= 5.0;
	const bool bPass = bEnoughSamples && bIdleOK && bMovingOK && bFrontalOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE frontal moving kneeLatDelta=%.1fcm<=3 thighAbdDelta=%.1fdeg<=5 %s"),
		KneeLatDelta, ThighAbdDelta, bFrontalOK ? TEXT("OK") : TEXT("BAD"));

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE summary idle n=%d driverKnee=%.1f skinKnee=%.1f delta=%.1f driverExt=%.3f skinExt=%.3f"),
		PoseProbeCount[0], MeanKnee[0][0], MeanKnee[0][1], IdleDelta, MeanExt[0][0], MeanExt[0][1]);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE summary moving n=%d driverKnee=%.1f skinKnee=%.1f delta=%.1f driverExt=%.3f skinExt=%.3f maxDelta=%.1f"),
		PoseProbeCount[1], MeanKnee[1][0], MeanKnee[1][1], MovingDelta, MeanExt[1][0], MeanExt[1][1], PoseProbeMaxKneeDelta);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-POSE-PROBE %s (samples=%d idleDelta=%.1f<=8 movingDelta=%.1f<=12 enough=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), PoseProbeSamples, IdleDelta, MovingDelta, bEnoughSamples ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -BoatProbe (2026-09-04): the sailing loop through its authoritative paths. Finds the moored
// AWrecktideBoat, walks the pirate into its trigger, boards through Board(), holds sail through
// HoldSail() for two seconds and requires real displacement, then goes ashore through GoAshore()
// beside the pier and requires the pirate back under the controller, alive, colliding, walking, and
// standing above the water line. A boat that drifted off or a shore test that dropped the pirate
// into the sea both fail loudly.
// ---------------------------------------------------------------------------------------------
namespace
{
void PressBoatProbeF(APlayerController& PlayerController)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	PlayerController.InputKey(FInputKeyParams(EKeys::F, IE_Pressed, 1.0));
	PlayerController.InputKey(FInputKeyParams(EKeys::F, IE_Released, 0.0));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
}

void UWrecktideRewardSubsystem::SpawnShipwright(UWorld* World) const
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	APlayerStart* FirstPlayerStart = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		FirstPlayerStart = *It;
		break;
	}
	if (!FirstPlayerStart)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-WIRING No PlayerStart found for the shipwright bench"));
		return;
	}

	// The mirror of the smithy, on the bounty board's flank, because the harbourmaster stands by the
	// board. Every interact surface here carries a 300 uu trigger, so the bench has to sit more than
	// 600 uu from the shop, the board and the quest giver: this spot is 765 uu from the board and
	// 1,560 uu from the market.
	FVector SpawnLocation = FirstPlayerStart->GetActorLocation()
		+ FirstPlayerStart->GetActorForwardVector() * -150.0f + FirstPlayerStart->GetActorRightVector() * -1150.0f;
	const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 500.0f);
	const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 2000.0f);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility))
	{
		SpawnLocation.Z = Hit.ImpactPoint.Z;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (AWrecktideShipwright* Bench = World->SpawnActor<AWrecktideShipwright>(
		AWrecktideShipwright::StaticClass(), SpawnLocation, FirstPlayerStart->GetActorRotation(), SpawnParameters))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT bench at %s"), *Bench->GetActorLocation().ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-WIRING Shipwright bench spawn failed"));
	}
}

namespace
{
/** -ShipwrightProbe phases: settle, measure, buy, settle, measure. Same shape as the sail probe. */
constexpr float ShipwrightProbeStep = 0.1f;
constexpr float ShipwrightProbePhaseSeconds[] = { 3.0f, 1.0f, 3.5f, 1.0f };
constexpr int32 ShipwrightProbePhaseCount = UE_ARRAY_COUNT(ShipwrightProbePhaseSeconds);
const FVector ShipwrightProbeSpot(0.0f, -11000.0f, -120.0f);
constexpr float ShipwrightProbeHeading = -90.0f;
/** The Canvas step from BoatUpgrades.csv, and how far the measured ratio may sit from it. */
constexpr float ShipwrightProbeExpectedRise = 0.08f;
constexpr float ShipwrightProbeRiseTolerance = 0.03f;
}

void UWrecktideRewardSubsystem::RunShipwrightProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	AWrecktideShipwright* Bench = nullptr;
	for (TActorIterator<AWrecktideShipwright> It(World); It; ++It)
	{
		Bench = *It;
		break;
	}
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	if (!World || !PlayerPawn || !PlayerState || !Economy || !Bench || !Boat)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE FAIL (reason=wiring pawn=%d state=%d economy=%d bench=%d boat=%d)"),
			PlayerPawn != nullptr, PlayerState != nullptr, Economy != nullptr, Bench != nullptr, Boat != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// 1. The gate, BEFORE any notoriety is granted: tier 1 is gated at notoriety 3, and a fresh
	// pirate is level 1, so this sale must be refused with no gold moved.
	Economy->ApplyGoldDelta(3000, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("ShipwrightProbe")));
	PlayerPawn->SetActorLocation(Bench->GetActorLocation() + FVector(0.0f, 0.0f, 130.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Bench->OpenShipwrightMenu(PlayerPawn);
	ShipwrightProbeLevelAtRefusal = PlayerState->GetGatingLevel();
	ShipwrightProbeGoldAtRefusal = Economy->GetGoldTotal();
	bShipwrightProbeRefused = !Bench->BuyUpgrade(WrecktideSea::UpgradeCanvas);
	ShipwrightProbeGoldAfterRefusal = Economy->GetGoldTotal();
	if (Bench->IsMenuOpen())
	{
		Bench->ToggleShipwrightMenu(PlayerPawn);
	}

	// 2. Out to open water on a fixed heading with the wind across, so the measurement window reads
	// the authored speed and nothing else (the sail probe's rule).
	PlayerPawn->SetActorLocation(Boat->GetActorLocation() + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (!Boat->Board(PlayerPawn))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE FAIL (reason=boarding refused)"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	ShipwrightProbeOrigin = ShipwrightProbeSpot;
	Boat->SetActorLocation(ShipwrightProbeOrigin, false, nullptr, ETeleportType::TeleportPhysics);
	Boat->SetActorRotation(FRotator(0.0f, ShipwrightProbeHeading, 0.0f));
	WrecktideWind::SetOverride(ShipwrightProbeHeading + 90.0f);
	Boat->SetSailState(EWrecktideSailState::Full);
	Boat->HoldSail(0.0f, 0.0f, 2.0f);

	ShipwrightProbeBoat = Boat;
	ShipwrightProbeBench = Bench;
	ShipwrightProbePhase = 0;
	ShipwrightProbePhaseElapsed = 0.0f;
	ShipwrightProbeMark = Boat->GetActorLocation();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE start refused=%d notoriety=%d gold=%lld->%lld"),
		bShipwrightProbeRefused ? 1 : 0, ShipwrightProbeLevelAtRefusal,
		ShipwrightProbeGoldAtRefusal, ShipwrightProbeGoldAfterRefusal);

	World->GetTimerManager().SetTimer(ShipwrightProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::TickShipwrightProbe), ShipwrightProbeStep, true);
}

void UWrecktideRewardSubsystem::TickShipwrightProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	AWrecktideBoat* Boat = ShipwrightProbeBoat.Get();
	AWrecktideShipwright* Bench = ShipwrightProbeBench.Get();
	if (!World || !Boat || !Bench || !Economy || !Skill || !Progress)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(ShipwrightProbeTimer);
		}
		WrecktideWind::ClearOverride();
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE FAIL (reason=lost an actor mid run)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	ShipwrightProbePhaseElapsed += ShipwrightProbeStep;
	const int32 Phase = ShipwrightProbePhase;
	if (Phase >= ShipwrightProbePhaseCount)
	{
		return;
	}
	Boat->HoldSail(0.0f, 0.0f, 1.0f);
	if (ShipwrightProbePhaseElapsed < ShipwrightProbePhaseSeconds[Phase])
	{
		return;
	}

	const float Window = ShipwrightProbePhaseSeconds[Phase];
	const float Travel = FVector::Dist2D(Boat->GetActorLocation(), ShipwrightProbeMark);

	if (Phase == 1)
	{
		ShipwrightProbeSpeedBefore = Travel / Window;

		// 3. Back to the pier and ASHORE through the real path, because the bench serves the pirate,
		// not the hull: a rider is attached with his collision off, so the trigger that names the
		// shipwright's customer has already let him go. Then earn the notoriety the gate wants and
		// buy the tier that was refused a moment ago.
		Boat->SetActorLocation(Boat->GetMooringLocation(), false, nullptr, ETeleportType::TeleportPhysics);
		const bool bAshore = Boat->GoAshore();
		ACombatCharacter* Pirate = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
		if (!bAshore || !Pirate)
		{
			World->GetTimerManager().ClearTimer(ShipwrightProbeTimer);
			WrecktideWind::ClearOverride();
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE FAIL (reason=could not put the pirate ashore ashore=%d pirate=%d)"),
				bAshore ? 1 : 0, Pirate != nullptr);
			ScheduleProbeQuit(2.0f);
			return;
		}
		Pirate->SetActorLocation(Bench->GetActorLocation() + FVector(0.0f, 0.0f, 130.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Bench->OpenShipwrightMenu(Pirate);
		for (int32 Attempt = 0; Attempt < 80 && PlayerState->GetGatingLevel() < 3; ++Attempt)
		{
			Skill->AwardXP(EWrecktideSkill::Sailing, 250, FName(TEXT("ShipwrightProbe")));
		}
		const int64 GoldBeforeBuy = Economy->GetGoldTotal();
		bShipwrightProbeBought = Bench->BuyUpgrade(WrecktideSea::UpgradeCanvas);
		ShipwrightProbeGoldAfterBuy = Economy->GetGoldTotal();
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE buy bought=%d notoriety=%d gold=%lld->%lld tier=%d"),
			bShipwrightProbeBought ? 1 : 0, PlayerState->GetGatingLevel(), GoldBeforeBuy,
			ShipwrightProbeGoldAfterBuy, Progress->GetBoatUpgradeTier(WrecktideSea::UpgradeCanvas));
		if (Bench->IsMenuOpen())
		{
			Bench->ToggleShipwrightMenu(Pirate);
		}

		// And back out to the same water on the same heading for the second measurement.
		Pirate->SetActorLocation(Boat->GetActorLocation() + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
			false, nullptr, ETeleportType::TeleportPhysics);
		if (!Boat->Board(Pirate))
		{
			World->GetTimerManager().ClearTimer(ShipwrightProbeTimer);
			WrecktideWind::ClearOverride();
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE FAIL (reason=re-boarding refused)"));
			ScheduleProbeQuit(2.0f);
			return;
		}
		Boat->SetActorRotation(FRotator(0.0f, ShipwrightProbeHeading, 0.0f));
		Boat->SetSailState(EWrecktideSailState::Full);
	}
	else if (Phase == 3)
	{
		ShipwrightProbeSpeedAfter = Travel / Window;

		// 4. Persistence: the save bridge, in memory, so the player's real slot is never touched.
		const FWrecktideSaveData Saved = Progress->BuildSaveData();
		Progress->SetBoatUpgradeTier(WrecktideSea::UpgradeCanvas, 0);
		const int32 TierWiped = Progress->GetBoatUpgradeTier(WrecktideSea::UpgradeCanvas);
		Progress->ApplySaveData(Saved);
		const int32 TierRestored = Progress->GetBoatUpgradeTier(WrecktideSea::UpgradeCanvas);

		const float Rise = ShipwrightProbeSpeedBefore > 1.0f
			? (ShipwrightProbeSpeedAfter / ShipwrightProbeSpeedBefore) - 1.0f : 0.0f;
		const bool bGateOK = bShipwrightProbeRefused && ShipwrightProbeLevelAtRefusal < 3
			&& ShipwrightProbeGoldAfterRefusal == ShipwrightProbeGoldAtRefusal;
		const bool bBoughtOK = bShipwrightProbeBought && ShipwrightProbeGoldAfterBuy < ShipwrightProbeGoldAtRefusal;
		const bool bSpeedOK = FMath::Abs(Rise - ShipwrightProbeExpectedRise) <= ShipwrightProbeRiseTolerance;
		const bool bSaveOK = TierWiped == 0 && TierRestored == 1;
		const bool bPass = bGateOK && bBoughtOK && bSpeedOK && bSaveOK;

		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-PROBE %s (refusedAtLv=%d gold=%lld/%lld boughtGold=%lld before=%.0f after=%.0f rise=%.3f/%.3f tier=%d->%d gateOK=%d buyOK=%d speedOK=%d saveOK=%d)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"),
			ShipwrightProbeLevelAtRefusal, ShipwrightProbeGoldAtRefusal, ShipwrightProbeGoldAfterRefusal,
			ShipwrightProbeGoldAfterBuy, ShipwrightProbeSpeedBefore, ShipwrightProbeSpeedAfter,
			Rise, ShipwrightProbeExpectedRise, TierWiped, TierRestored,
			bGateOK ? 1 : 0, bBoughtOK ? 1 : 0, bSpeedOK ? 1 : 0, bSaveOK ? 1 : 0);

		World->GetTimerManager().ClearTimer(ShipwrightProbeTimer);
		WrecktideWind::ClearOverride();
		ScheduleProbeQuit(2.0f);
		return;
	}

	++ShipwrightProbePhase;
	ShipwrightProbePhaseElapsed = 0.0f;
	// Snap back so a long run never reaches the 22,000 uu range clamp mid-measurement.
	Boat->SetActorLocation(ShipwrightProbeOrigin, false, nullptr, ETeleportType::TeleportPhysics);
	ShipwrightProbeMark = Boat->GetActorLocation();
}

void UWrecktideRewardSubsystem::SpawnSupplyBarrel(UWorld* World) const
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	const AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	if (!Boat)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD-WIRING No boat found, so no pier to stand the supply barrel on"));
		return;
	}

	// On the pier deck, south of the mooring. RECON B: the deck spans X -300..+300 and Y -9100..-6900
	// with its top at z = -40, the boat is moored abeam at (-700, -8300) and carries a 650 uu board
	// trigger. X = 0 keeps clear of the rails at +-310, and 920 uu from the hull keeps the barrel's F
	// out of the boat's F.
	FVector SpawnLocation(0.0f, -8900.0f, -40.0f);
	const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 600.0f);
	const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 600.0f);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility))
	{
		// The Synty barrel has a base pivot: stand it on the planks, no cube-centre lift.
		SpawnLocation.Z = Hit.ImpactPoint.Z;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (AWrecktideSupplyBarrel* Barrel = World->SpawnActor<AWrecktideSupplyBarrel>(
		AWrecktideSupplyBarrel::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParameters))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD barrel at %s, %.0f uu from the hull"),
			*Barrel->GetActorLocation().ToString(),
			FVector::Dist2D(Barrel->GetActorLocation(), Boat->GetActorLocation()));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD-WIRING Supply barrel spawn failed"));
	}
}

void UWrecktideRewardSubsystem::SpawnPatrolBoat(UWorld* World) const
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	const AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	if (!Boat)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PATROL-WIRING No player boat found, so no mooring to keep the patrol away from"));
		return;
	}

	// Build the same route the patrol will build for herself and start her on the point FARTHEST
	// from the player's mooring. Two reasons, both deliberate: the player meets her by sailing out
	// rather than by leaving the pier, and every other probe (the sail feel measurements at
	// (0, -11000), -BoatProbe, -SwimProbe, -MapProbe) runs a whole island away from her, so adding
	// a hostile actor to the world cannot quietly redden somebody else's gate.
	const AWrecktidePatrolBoat* Defaults = GetDefault<AWrecktidePatrolBoat>();
	TArray<FVector> Route;
	AWrecktidePatrolBoat::BuildRoute(Route, Defaults->RouteBeachHalfExtent, Defaults->RouteOffsetFromBeach,
		Defaults->RouteCornerSteps, Defaults->WaterZ);
	if (Route.Num() < 2)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PATROL-WIRING route came back with %d points"), Route.Num());
		return;
	}

	int32 Farthest = 0;
	float FarthestDistanceSquared = -1.0f;
	for (int32 Index = 0; Index < Route.Num(); ++Index)
	{
		const float DistanceSquared = FVector::DistSquared2D(Route[Index], Boat->GetActorLocation());
		if (DistanceSquared > FarthestDistanceSquared)
		{
			FarthestDistanceSquared = DistanceSquared;
			Farthest = Index;
		}
	}
	const FVector SpawnLocation = Route[Farthest];
	const FVector Next = Route[(Farthest + 1) % Route.Num()];
	const FRotator SpawnRotation(0.0f, (Next - SpawnLocation).Rotation().Yaw, 0.0f);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (AWrecktidePatrolBoat* Patrol = World->SpawnActor<AWrecktidePatrolBoat>(
		AWrecktidePatrolBoat::StaticClass(), SpawnLocation, SpawnRotation, SpawnParameters))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL spawned at X=%.0f Y=%.0f yaw=%.0f, %.0f uu from the mooring, route %d points %.0f uu off the beach"),
			Patrol->GetActorLocation().X, Patrol->GetActorLocation().Y, SpawnRotation.Yaw,
			FVector::Dist2D(Patrol->GetActorLocation(), Boat->GetActorLocation()),
			Route.Num(), Defaults->RouteOffsetFromBeach);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PATROL-WIRING Patrol boat spawn failed"));
	}
}

void UWrecktideRewardSubsystem::RunHoldProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	AWrecktideSupplyBarrel* Barrel = nullptr;
	for (TActorIterator<AWrecktideSupplyBarrel> It(World); It; ++It)
	{
		Barrel = *It;
		break;
	}
	if (!World || !PlayerPawn || !Economy || !Boat || !Barrel)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD-PROBE FAIL (reason=wiring pawn=%d economy=%d boat=%d barrel=%d)"),
			PlayerPawn != nullptr, Economy != nullptr, Boat != nullptr, Barrel != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// 1. Buy planks at the barrel through the REAL sale path, gold and all.
	Economy->ApplyGoldDelta(400, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("HoldProbe")));
	PlayerPawn->SetActorLocation(Barrel->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Barrel->OpenBarrelMenu(PlayerPawn);
	HoldProbeGoldBefore = Economy->GetGoldTotal();
	bHoldProbeBought = Barrel->BuySupply(WrecktideSea::SupplyPlanks) && Barrel->BuySupply(WrecktideSea::SupplyPlanks);
	HoldProbeGoldAfterBuy = Economy->GetGoldTotal();
	HoldProbeSlotsAfterBuy = Boat->GetHoldSlotsUsed();
	HoldProbePlanksAfterBuy = Boat->GetHoldUnits(WrecktideSea::SupplyPlanks);
	if (Barrel->IsMenuOpen())
	{
		Barrel->ToggleBarrelMenu(PlayerPawn);
	}

	// 2. Board, take her out to open water, and take real damage through the real path.
	PlayerPawn->SetActorLocation(Boat->GetActorLocation() + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (!Boat->Board(PlayerPawn))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD-PROBE FAIL (reason=boarding refused)"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	Boat->SetActorLocation(FVector(0.0f, -11000.0f, -120.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Boat->ApplyHullDamage(40, TEXT("HoldProbe"));
	HoldProbeHullAfterDamage = Boat->GetHullHealth();

	// 3. Patch through the hold-to-use path, which needs RepairHoldSeconds of world time to finish.
	HoldProbeBoat = Boat;
	Boat->BeginRepairHold();
	FTimerHandle RepairTimer;
	World->GetTimerManager().SetTimer(RepairTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishHoldProbe), 2.2f, false);
}

void UWrecktideRewardSubsystem::FinishHoldProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	AWrecktideBoat* Boat = HoldProbeBoat.Get();
	if (!Boat || !Progress)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD-PROBE FAIL (reason=lost boat or progress mid run)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const int32 HullAfterRepair = Boat->GetHullHealth();
	const int32 PlanksAfterRepair = Boat->GetHoldUnits(WrecktideSea::SupplyPlanks);

	// 4. Persistence: the save bridge itself, in memory, so the player's real slot is never touched.
	// Build, wipe the live state through the real accessors, apply, and read back.
	const FWrecktideSaveData Saved = Progress->BuildSaveData();
	Progress->SetBoatHull(7);
	Progress->ClearBoatHold();
	Progress->ApplySaveData(Saved);
	const int32 HullRestored = Boat->GetHullHealth();
	const int32 PlanksRestored = Boat->GetHoldUnits(WrecktideSea::SupplyPlanks);

	// And the version-independent migration: a file that never held boat state reads a full hull.
	FWrecktideSaveData Legacy = Saved;
	Legacy.bHasBoatState = false;
	Legacy.BoatHullHealth = 0;
	Legacy.BoatHold.Reset();
	Progress->ApplySaveData(Legacy);
	const int32 HullLegacy = Boat->GetHullHealth();
	Progress->ApplySaveData(Saved);

	const int32 HullMax = Boat->GetHullMax();
	const bool bGoldMoved = HoldProbeGoldAfterBuy < HoldProbeGoldBefore;
	const bool bSlotTaken = HoldProbeSlotsAfterBuy >= 1 && HoldProbePlanksAfterBuy == 2;
	const bool bDamaged = HoldProbeHullAfterDamage == HullMax - 40;
	const bool bRepaired = HullAfterRepair == HoldProbeHullAfterDamage + Boat->PlankRepairAmount
		&& PlanksAfterRepair == HoldProbePlanksAfterBuy - 1;
	const bool bPersisted = HullRestored == HullAfterRepair && PlanksRestored == PlanksAfterRepair;
	const bool bLegacyOK = HullLegacy == HullMax;
	const bool bPass = bHoldProbeBought && bGoldMoved && bSlotTaken && bDamaged && bRepaired && bPersisted && bLegacyOK;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD-PROBE %s (bought=%d gold=%lld->%lld slots=%d planks=%d hullMax=%d damaged=%d repaired=%d planksLeft=%d restoredHull=%d restoredPlanks=%d legacyHull=%d goldOK=%d slotOK=%d dmgOK=%d repairOK=%d saveOK=%d legacyOK=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bHoldProbeBought ? 1 : 0, HoldProbeGoldBefore, HoldProbeGoldAfterBuy,
		HoldProbeSlotsAfterBuy, HoldProbePlanksAfterBuy, HullMax,
		HoldProbeHullAfterDamage, HullAfterRepair, PlanksAfterRepair,
		HullRestored, PlanksRestored, HullLegacy,
		bGoldMoved ? 1 : 0, bSlotTaken ? 1 : 0, bDamaged ? 1 : 0, bRepaired ? 1 : 0, bPersisted ? 1 : 0, bLegacyOK ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

namespace
{
/**
 * -SailFeelProbe phase table (spec S1.7). Every stage is a SETTLE followed by a MEASURE window, so
 * one repeating ticker and an index replace a dozen nested timers. Seconds are the settle length for
 * an even phase and the measurement length for an odd one. The numbers are sized off the model's own
 * ramps: the coast down crosses the full sail speed in SailCoastDownSeconds, so a drop from Full to
 * Half needs a little over two seconds to finish.
 */
constexpr float SailProbeStep = 0.1f;
constexpr float SailProbePhaseSeconds[] =
{
	3.0f,  // 0 settle at Full, wind across
	1.0f,  // 1 measure the Full speed
	4.5f,  // 2 settle at Half
	1.0f,  // 3 measure the Half speed
	3.5f,  // 4 settle at Full with the wind astern
	1.0f,  // 5 measure it running with the wind
	5.5f,  // 6 settle at Full with the wind ahead
	1.0f,  // 7 measure it sailing into the wind
	1.5f,  // 8 settle, rudder hard over at Full
	1.0f,  // 9 measure the turn rate at Full
	4.5f,  // 10 settle at Half, rudder amidships
	1.0f,  // 11 measure the turn rate at Half
	4.5f,  // 12 settle furled until she has no way on
	2.0f   // 13 measure the furled pivot
};
constexpr int32 SailProbePhaseCount = UE_ARRAY_COUNT(SailProbePhaseSeconds);
/** Open water south of the pier: outside the 7,000 uu beach ring and well inside BoatRangeLimit. */
const FVector SailProbeSpot(0.0f, -11000.0f, -120.0f);
constexpr float SailProbeHeading = -90.0f;
/** Tolerances the verdict is judged on. */
constexpr float SailProbeSpeedTolerance = 0.10f;
constexpr float SailProbeRatioTolerance = 0.10f;
constexpr float SailProbePivotMinDegrees = 60.0f;
constexpr float SailProbePivotMaxTravel = 150.0f;
}

void UWrecktideRewardSubsystem::RunSailFeelProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	if (!World || !PlayerPawn || !Boat)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL-PROBE FAIL (reason=wiring pawn=%d boat=%d)"),
			PlayerPawn != nullptr, Boat != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Board through the SINGLE authoritative path, exactly as -BoatProbe does.
	PlayerPawn->SetActorLocation(Boat->GetActorLocation() + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (!Boat->Board(PlayerPawn))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL-PROBE FAIL (reason=boarding refused)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Out into open water on a fixed heading, so every measurement is on the same course.
	SailProbeOrigin = SailProbeSpot;
	Boat->SetActorLocation(SailProbeOrigin, false, nullptr, ETeleportType::TeleportPhysics);
	Boat->SetActorRotation(FRotator(0.0f, SailProbeHeading, 0.0f));
	// Wind ACROSS the heading: the factor is exactly 1.0 there, so phases 1 and 3 measure the
	// authored tunables and nothing else.
	WrecktideWind::SetOverride(SailProbeHeading + 90.0f);
	Boat->SetSailState(EWrecktideSailState::Full);
	Boat->HoldSail(0.0f, 0.0f, 2.0f);

	SailProbeBoat = Boat;
	SailProbePhase = 0;
	SailProbePhaseElapsed = 0.0f;
	SailProbeMark = Boat->GetActorLocation();
	SailProbeMarkYaw = Boat->GetActorRotation().Yaw;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL-PROBE start at %s heading=%.0f windTo=%.0f fullTunable=%.0f halfTunable=%.0f"),
		*SailProbeOrigin.ToString(), SailProbeHeading, WrecktideWind::ToYaw(this),
		Boat->GetMaxSpeedForState(EWrecktideSailState::Full), Boat->GetMaxSpeedForState(EWrecktideSailState::Half));

	World->GetTimerManager().SetTimer(SailProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::TickSailFeelProbe), SailProbeStep, true);
}

void UWrecktideRewardSubsystem::SailProbeNextPhase()
{
	++SailProbePhase;
	SailProbePhaseElapsed = 0.0f;
	AWrecktideBoat* Boat = SailProbeBoat.Get();
	if (!Boat)
	{
		return;
	}
	// Snap back to the origin at every phase change: a run this long would otherwise reach the
	// 22,000 uu range clamp, which stops the boat dead and would read as a speed failure.
	Boat->SetActorLocation(SailProbeOrigin, false, nullptr, ETeleportType::TeleportPhysics);
	SailProbeMark = Boat->GetActorLocation();
	SailProbeMarkYaw = Boat->GetActorRotation().Yaw;
}

void UWrecktideRewardSubsystem::TickSailFeelProbe()
{
	UWorld* World = GetWorld();
	AWrecktideBoat* Boat = SailProbeBoat.Get();
	if (!World || !Boat)
	{
		World ? World->GetTimerManager().ClearTimer(SailProbeTimer) : void();
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL-PROBE FAIL (reason=boat lost mid run)"));
		WrecktideWind::ClearOverride();
		ScheduleProbeQuit(2.0f);
		return;
	}

	SailProbePhaseElapsed += SailProbeStep;
	const int32 Phase = SailProbePhase;
	if (Phase >= SailProbePhaseCount)
	{
		return;
	}

	// The rider's key polling zeroes the steer the moment no key is down, so the probe re-arms its
	// hold every tick; the throttle stays at 0 because SetSailState is the authoritative sail path.
	const float Steer = (Phase == 8 || Phase == 9 || Phase == 11 || Phase == 12 || Phase == 13) ? 1.0f : 0.0f;
	Boat->HoldSail(0.0f, Steer, 1.0f);

	if (SailProbePhaseElapsed < SailProbePhaseSeconds[Phase])
	{
		return;
	}

	const float Window = SailProbePhaseSeconds[Phase];
	const float Travel = FVector::Dist2D(Boat->GetActorLocation(), SailProbeMark);
	const float YawDelta = FMath::Abs(FRotator::NormalizeAxis(Boat->GetActorRotation().Yaw - SailProbeMarkYaw));

	switch (Phase)
	{
	case 1:
		SailProbeSpeedFull = Travel / Window;
		Boat->SetSailState(EWrecktideSailState::Half);
		break;
	case 3:
		SailProbeSpeedHalf = Travel / Window;
		Boat->SetSailState(EWrecktideSailState::Full);
		WrecktideWind::SetOverride(Boat->GetActorRotation().Yaw);
		break;
	case 5:
		SailProbeSpeedAstern = Travel / Window;
		WrecktideWind::SetOverride(Boat->GetActorRotation().Yaw + 180.0f);
		break;
	case 7:
		SailProbeSpeedAhead = Travel / Window;
		// Rudder work runs with the wind astern, where the wind rudder factor is exactly 1.0, so the
		// two turn rates differ by SPEED alone, which is the thing under test.
		WrecktideWind::SetOverride(Boat->GetActorRotation().Yaw);
		break;
	case 9:
		SailProbeTurnFull = YawDelta / Window;
		Boat->SetSailState(EWrecktideSailState::Half);
		break;
	case 11:
		SailProbeTurnHalf = YawDelta / Window;
		Boat->SetSailState(EWrecktideSailState::Furled);
		break;
	case 12:
		WrecktideWind::SetOverride(Boat->GetActorRotation().Yaw);
		break;
	case 13:
	{
		SailProbePivotYaw = YawDelta;
		SailProbePivotTravel = Travel;

		const float FullTunable = Boat->GetMaxSpeedForState(EWrecktideSailState::Full);
		const float HalfTunable = Boat->GetMaxSpeedForState(EWrecktideSailState::Half);
		const bool bFullOK = FMath::Abs(SailProbeSpeedFull - FullTunable) <= FullTunable * SailProbeSpeedTolerance;
		const bool bHalfOK = FMath::Abs(SailProbeSpeedHalf - HalfTunable) <= HalfTunable * SailProbeSpeedTolerance;
		const float ExpectedRatio = WrecktideWind::RunningFactor / WrecktideWind::IntoFactor;
		const float Ratio = SailProbeSpeedAhead > 1.0f ? SailProbeSpeedAstern / SailProbeSpeedAhead : 0.0f;
		const bool bWindOK = FMath::Abs(Ratio - ExpectedRatio) <= ExpectedRatio * SailProbeRatioTolerance;
		const bool bRudderOK = SailProbeTurnFull > 0.0f && SailProbeTurnFull < SailProbeTurnHalf;
		const bool bPivotOK = SailProbePivotYaw > SailProbePivotMinDegrees && SailProbePivotTravel < SailProbePivotMaxTravel;
		const bool bPass = bFullOK && bHalfOK && bWindOK && bRudderOK && bPivotOK;

		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL-PROBE %s (full=%.0f/%.0f half=%.0f/%.0f astern=%.0f ahead=%.0f ratio=%.2f/%.2f turnFull=%.1f turnHalf=%.1f pivot=%.0fdeg travel=%.0fuu speedOK=%d/%d windOK=%d rudderOK=%d pivotOK=%d)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"),
			SailProbeSpeedFull, FullTunable, SailProbeSpeedHalf, HalfTunable,
			SailProbeSpeedAstern, SailProbeSpeedAhead, Ratio, ExpectedRatio,
			SailProbeTurnFull, SailProbeTurnHalf, SailProbePivotYaw, SailProbePivotTravel,
			bFullOK ? 1 : 0, bHalfOK ? 1 : 0, bWindOK ? 1 : 0, bRudderOK ? 1 : 0, bPivotOK ? 1 : 0);

		World->GetTimerManager().ClearTimer(SailProbeTimer);
		WrecktideWind::ClearOverride();
		ScheduleProbeQuit(2.0f);
		return;
	}
	default:
		break;
	}

	SailProbeNextPhase();
}

namespace
{
/**
 * -HelmProbe phase table (spec C28.5). The -SailFeelProbe shape: one repeating ticker and a phase
 * index. Every window is sized off the wheel model itself, which is what makes the numbers assertable
 * rather than hopeful: at WheelTurnRateDegPerSecond 240 a full 150 degree lock takes 0.63 s, so a one
 * second window always finishes the swing, and the spec's own 1.5 s recentring budget IS the phase
 * length rather than a separate timer.
 */
constexpr float HelmProbeStep = 0.1f;
constexpr float HelmProbePhaseSeconds[] =
{
	0.6f,  // 0 settle aboard with the wheel amidships
	1.0f,  // 1 hard a port, then read the wheel
	1.5f,  // 2 let it go and time the recentring (the spec's budget is this window)
	1.0f,  // 3 hard a starboard, then read the wheel
	1.2f,  // 4 the navy's own wheel, plus the pirate's station and grip
	0.8f   // 5 ashore, then the verdict
};
constexpr int32 HelmProbePhaseCount = UE_ARRAY_COUNT(HelmProbePhaseSeconds);
/** Open water south of the pier, the sail probe's spot: outside the beach ring, well inside the
 *  22,000 uu range clamp, and far enough from the patrol's loop that she never turns on him. */
const FVector HelmProbeSpot(0.0f, -11000.0f, -120.0f);
constexpr float HelmProbeHeading = -90.0f;
/** Tolerances, all from the spec: 15 degrees off full lock, 20 degrees counts as centred, the rider
 *  within 120 uu of the rim and facing within 20 degrees of the bow. */
constexpr float HelmProbeLockTolerance = 15.0f;
constexpr float HelmProbeCentreTolerance = 20.0f;
constexpr float HelmProbeRiderMaxGap = 120.0f;
constexpr float HelmProbeRiderMaxYaw = 20.0f;
/** How far off her own course the probe throws the navy, so her steering HAS to answer with full
 *  lock. She corrects at 22 degrees a second, so a 90 degree throw is still most of a lock a second
 *  and a bit later, which is when her wheel is read. */
constexpr float HelmProbePatrolThrowDegrees = 90.0f;
}

void UWrecktideRewardSubsystem::RunHelmProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	AWrecktidePatrolBoat* Patrol = nullptr;
	for (TActorIterator<AWrecktidePatrolBoat> It(World); It; ++It)
	{
		Patrol = *It;
		break;
	}
	if (!World || !PlayerPawn || !Boat || !Patrol)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM-PROBE FAIL (reason=wiring pawn=%d boat=%d patrol=%d)"),
			PlayerPawn != nullptr, Boat != nullptr, Patrol != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Aboard through the SINGLE authoritative path, exactly as -BoatProbe and -SailFeelProbe do it.
	PlayerPawn->SetActorLocation(Boat->GetActorLocation() + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (!Boat->Board(PlayerPawn))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM-PROBE FAIL (reason=boarding refused)"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	Boat->SetActorLocation(HelmProbeSpot, false, nullptr, ETeleportType::TeleportPhysics);
	Boat->SetActorRotation(FRotator(0.0f, HelmProbeHeading, 0.0f));
	// Sails FURLED for the whole run: the wheel answers the steering input and nothing else, so a
	// measurement of it must not also be a measurement of the sail model.
	Boat->SetSailState(EWrecktideSailState::Furled);
	Boat->HoldSail(0.0f, 0.0f, 1.0f);

	HelmProbeBoat = Boat;
	HelmProbePatrol = Patrol;
	HelmProbeRider = PlayerPawn;
	HelmProbePhase = 0;
	HelmProbePhaseElapsed = 0.0f;
	HelmProbeCentreSeconds = -1.0f;
	bHelmProbeGripAboard = false;
	bHelmProbeGripAshore = true;
	bHelmProbeWentAshore = false;

	const FVector BoatWheelLocal = Boat->GetHelmWheelLocalOffset();
	const FVector PatrolWheelLocal = Patrol->GetHelmWheelLocalOffset();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM-PROBE start boatWheel=%s patrolWheel=%s boatHull=%s maxTurn=%.0f/%.0f wheelMesh=%d/%d"),
		*BoatWheelLocal.ToString(), *PatrolWheelLocal.ToString(), *Boat->GetHullBoxExtent().ToString(),
		Boat->GetHelmMaxWheelTurn(), Patrol->GetHelmMaxWheelTurn(),
		Boat->HasHelmWheel() ? 1 : 0, Patrol->HasHelmWheel() ? 1 : 0);

	World->GetTimerManager().SetTimer(HelmProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::TickHelmProbe), HelmProbeStep, true);
}

void UWrecktideRewardSubsystem::TickHelmProbe()
{
	UWorld* World = GetWorld();
	AWrecktideBoat* Boat = HelmProbeBoat.Get();
	if (!World || !Boat)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(HelmProbeTimer);
		}
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM-PROBE FAIL (reason=boat lost mid run)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	HelmProbePhaseElapsed += HelmProbeStep;
	const int32 Phase = HelmProbePhase;
	if (Phase >= HelmProbePhaseCount)
	{
		return;
	}

	// The rider's key polling zeroes the steer the moment no key is down, so the hold is re-armed
	// every tick; the throttle stays 0 because the sail is not what is under test here.
	const float Steer = (Phase == 1) ? -1.0f : ((Phase == 3) ? 1.0f : 0.0f);
	Boat->HoldSail(0.0f, Steer, 1.0f);

	// The recentring is TIMED, not sampled at the end: the question the spec asks is how long the
	// wheel takes to come back, and only a poll can answer that.
	if (Phase == 2 && HelmProbeCentreSeconds < 0.0f && FMath::Abs(Boat->GetHelmWheelAngle()) <= HelmProbeCentreTolerance)
	{
		HelmProbeCentreSeconds = HelmProbePhaseElapsed;
	}

	if (HelmProbePhaseElapsed < HelmProbePhaseSeconds[Phase])
	{
		return;
	}

	switch (Phase)
	{
	case 1:
		HelmProbeAngleLeft = Boat->GetHelmWheelAngle();
		break;

	case 2:
		HelmProbeAngleCentre = Boat->GetHelmWheelAngle();
		break;

	case 3:
		HelmProbeAngleRight = Boat->GetHelmWheelAngle();
		// Throw the navy off her own course so her steering has to answer with full lock. This is a
		// synthetic push, not something she does on her own: her route legs are long and straight,
		// and a wheel that only moves at a corner is not a wheel a probe can time.
		if (AWrecktidePatrolBoat* Patrol = HelmProbePatrol.Get())
		{
			Patrol->SetActorRotation(FRotator(0.0f, Patrol->GetActorRotation().Yaw + HelmProbePatrolThrowDegrees, 0.0f));
		}
		break;

	case 4:
	{
		if (const AWrecktidePatrolBoat* Patrol = HelmProbePatrol.Get())
		{
			HelmProbePatrolWheel = Patrol->GetHelmWheelAngle();
		}
		// The pirate's station, read while he is still aboard: how far he stands from the rim and
		// how far his facing is off the bow.
		if (const ACombatCharacter* Rider = HelmProbeRider.Get())
		{
			HelmProbeRiderGap = FVector::Dist(Rider->GetActorLocation(), Boat->GetHelmWheelWorldLocation());
			HelmProbeRiderYawError = FMath::Abs(FRotator::NormalizeAxis(Rider->GetActorRotation().Yaw - Boat->GetActorRotation().Yaw));
		}
		bHelmProbeGripAboard = Boat->IsHelmGripPlaying();
		// Back to the mooring BOTH ways round: the ashore search looks for the pier abeam, so a hull
		// put back on the right spot with the wrong heading finds nothing to step onto.
		Boat->SetActorLocationAndRotation(Boat->GetMooringLocation(), Boat->GetMooringRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		bHelmProbeWentAshore = Boat->GoAshore();
		break;
	}

	case 5:
		FinishHelmProbe();
		return;

	default:
		break;
	}

	++HelmProbePhase;
	HelmProbePhaseElapsed = 0.0f;
}

void UWrecktideRewardSubsystem::FinishHelmProbe()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(HelmProbeTimer);
	}
	AWrecktideBoat* Boat = HelmProbeBoat.Get();
	AWrecktidePatrolBoat* Patrol = HelmProbePatrol.Get();
	const ACombatCharacter* Rider = HelmProbeRider.Get();
	if (!Boat || !Patrol)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM-PROBE FAIL (reason=actors lost boat=%d patrol=%d)"), Boat != nullptr, Patrol != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// The grip has to be GONE ashore, and it is read off the pirate himself rather than off the boat,
	// which no longer has a rider to ask.
	bHelmProbeGripAshore = Rider != nullptr && Rider->IsHelmGripPlaying();

	const FVector BoatWheelLocal = Boat->GetHelmWheelLocalOffset();
	const FVector PatrolWheelLocal = Patrol->GetHelmWheelLocalOffset();
	const FVector BoatHull = Boat->GetHullBoxExtent();
	const FVector PatrolHull = Patrol->GetHullBoxExtent();
	// At the stern means aft of centre AND inside her own planking, which is why the hull's own box
	// is asked for rather than a second copy of the numbers that built it.
	const bool bBoatSternOK = Boat->HasHelmWheel() && BoatWheelLocal.X < 0.0f
		&& FMath::Abs(BoatWheelLocal.X) <= BoatHull.X && FMath::Abs(BoatWheelLocal.Y) <= BoatHull.Y;
	const bool bPatrolSternOK = Patrol->HasHelmWheel() && PatrolWheelLocal.X < 0.0f
		&& FMath::Abs(PatrolWheelLocal.X) <= PatrolHull.X && FMath::Abs(PatrolWheelLocal.Y) <= PatrolHull.Y;
	const bool bSternOK = bBoatSternOK && bPatrolSternOK;

	const float MaxTurn = Boat->GetHelmMaxWheelTurn();
	const bool bLeftOK = FMath::Abs(HelmProbeAngleLeft + MaxTurn) <= HelmProbeLockTolerance;
	const bool bRightOK = FMath::Abs(HelmProbeAngleRight - MaxTurn) <= HelmProbeLockTolerance;
	const bool bCentreOK = HelmProbeCentreSeconds >= 0.0f
		&& HelmProbeCentreSeconds <= HelmProbePhaseSeconds[2]
		&& FMath::Abs(HelmProbeAngleCentre) <= HelmProbeCentreTolerance;
	// Her wheel is judged on MAGNITUDE plus a negative sign: the throw is to port, and a wheel that
	// went hard over the other way would be a sign error rather than a working helm.
	const bool bPatrolOK = FMath::Abs(FMath::Abs(HelmProbePatrolWheel) - Patrol->GetHelmMaxWheelTurn()) <= HelmProbeLockTolerance
		&& HelmProbePatrolWheel < 0.0f;
	const bool bRiderOK = HelmProbeRiderGap <= HelmProbeRiderMaxGap && HelmProbeRiderYawError <= HelmProbeRiderMaxYaw;
	const bool bGripOK = bHelmProbeGripAboard && bHelmProbeWentAshore && !bHelmProbeGripAshore;

	const bool bPass = bSternOK && bLeftOK && bCentreOK && bRightOK && bPatrolOK && bRiderOK && bGripOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM-PROBE %s (boatWheelX=%.0f/%.0f boatWheelY=%.0f patrolWheelX=%.0f left=%.0f/%.0f centre=%.0f in %.1fs right=%.0f patrolWheel=%.0f riderGap=%.0f riderYaw=%.1f grip=%d ashore=%d gripAshore=%d sternOK=%d leftOK=%d centreOK=%d rightOK=%d patrolOK=%d riderOK=%d gripOK=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		BoatWheelLocal.X, BoatHull.X, BoatWheelLocal.Y, PatrolWheelLocal.X,
		HelmProbeAngleLeft, MaxTurn, HelmProbeAngleCentre, HelmProbeCentreSeconds,
		HelmProbeAngleRight, HelmProbePatrolWheel,
		HelmProbeRiderGap, HelmProbeRiderYawError,
		bHelmProbeGripAboard ? 1 : 0, bHelmProbeWentAshore ? 1 : 0, bHelmProbeGripAshore ? 1 : 0,
		bSternOK ? 1 : 0, bLeftOK ? 1 : 0, bCentreOK ? 1 : 0, bRightOK ? 1 : 0,
		bPatrolOK ? 1 : 0, bRiderOK ? 1 : 0, bGripOK ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

namespace
{
/**
 * -PatrolProbe phase budgets (spec S3.4). Unlike -SailFeelProbe's fixed measure windows, every stage
 * here is a QUESTION WITH A DEADLINE ("is she alongside within 8 s"), so each phase carries a budget
 * and an early exit and the verdict reports the seconds it actually took.
 */
constexpr float PatrolProbeStep = 0.1f;
constexpr float PatrolProbeCloseBudget = 8.0f;    // spec: hostile and alongside within 8 s
constexpr float PatrolProbeRamBudget = 8.0f;
constexpr float PatrolProbeLeashBudget = 14.0f;
constexpr float PatrolProbeReturnBudget = 8.0f;
constexpr float PatrolProbeReAggroBudget = 9.0f;
constexpr float PatrolProbeBlindBudget = 6.0f;
/** Alongside = inside twice her 300 uu station, measured centre to centre. */
constexpr float PatrolProbeAlongsideGap = 600.0f;
/** Where the probe puts the player's boat for each stage. Every one of these is more than two hull
 *  lengths (840 uu) from her, so a teleport can never leave two kinematic boxes interpenetrating. */
constexpr float PatrolProbeApproachAhead = 1500.0f;
constexpr float PatrolProbeApproachBeam = 700.0f;
constexpr float PatrolProbeSternGap = 1100.0f;
/** How far PAST her leash the run stage puts him, and how far up her course, so he is outside her
 *  leash but still in front of her bow (dropping him astern would test the blind arc by accident). */
constexpr float PatrolProbeLeashOvershoot = 2000.0f;
constexpr float PatrolProbeLeashLead = 3000.0f;
/** The measured push: step size, how many steps, and the radius neither hull may be sent past
 *  (both carry a 22,000 uu range clamp that would stop them dead). */
constexpr float PatrolProbeLeashPushStep = 400.0f;
constexpr int32 PatrolProbeLeashPushSteps = 60;
constexpr float PatrolProbeMaxRadius = 21000.0f;
/** Ground she must make back toward the route to count as returning. */
constexpr float PatrolProbeReturnGain = 300.0f;
/** The furled pivot itself, the same shape -SailFeelProbe asserts. */
constexpr float PatrolProbePivotMinDegrees = 60.0f;
constexpr float PatrolProbePivotMaxTravel = 150.0f;
constexpr float PatrolProbeWaterZ = -120.0f;

/** Red ship glyphs the map model is handing the HUD right now (spec S3.3). Counted through
 *  WrecktideMap::CollectMarkers, the SAME call the minimap and the full map draw from, so this
 *  cannot pass while the drawn map disagrees. */
int32 CountPatrolMapGlyphs(const UObject* WorldContext)
{
	TArray<WrecktideMap::FMapMarker> Markers;
	WrecktideMap::CollectMarkers(WorldContext, Markers);
	int32 Count = 0;
	for (const WrecktideMap::FMapMarker& Marker : Markers)
	{
		if (Marker.Icon == WrecktideMap::EMapIcon::Patrol)
		{
			++Count;
		}
	}
	return Count;
}
}

void UWrecktideRewardSubsystem::RunPatrolProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	AWrecktidePatrolBoat* Patrol = nullptr;
	for (TActorIterator<AWrecktidePatrolBoat> It(World); It; ++It)
	{
		Patrol = *It;
		break;
	}
	if (!World || !PlayerPawn || !Boat || !Patrol)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL-PROBE FAIL (reason=wiring pawn=%d boat=%d patrol=%d)"),
			PlayerPawn != nullptr, Boat != nullptr, Patrol != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Aboard through the SINGLE authoritative path, exactly as -BoatProbe and -SailFeelProbe do it.
	// She only hunts an OCCUPIED boat, so this is not scene dressing: without it she ignores him.
	PlayerPawn->SetActorLocation(Boat->GetActorLocation() + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (!Boat->Board(PlayerPawn))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL-PROBE FAIL (reason=boarding refused)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Stage one of the spec: teleport the player's boat into her range. Across her bow rather than
	// on top of her, so what is under test is her turning onto him and closing, not a spawn overlap.
	const FVector Ahead = Patrol->GetActorLocation()
		+ Patrol->GetActorForwardVector() * PatrolProbeApproachAhead
		+ Patrol->GetActorRightVector() * PatrolProbeApproachBeam;
	Boat->SetActorLocation(FVector(Ahead.X, Ahead.Y, PatrolProbeWaterZ), false, nullptr, ETeleportType::TeleportPhysics);
	Boat->SetActorRotation(FRotator(0.0f, Patrol->GetActorRotation().Yaw, 0.0f));
	Boat->SetSailState(EWrecktideSailState::Furled);

	PatrolProbeBoat = Boat;
	PatrolProbePatrol = Patrol;
	PatrolProbePhase = 0;
	PatrolProbePhaseElapsed = 0.0f;
	PatrolProbeHostileSeconds = -1.0f;
	PatrolProbeAlongsideSeconds = -1.0f;
	PatrolProbeClosestGap = TNumericLimits<float>::Max();
	PatrolProbeHullBefore = 0;
	PatrolProbeHullAfter = 0;
	PatrolProbeLeashRouteDistance = 0.0f;
	PatrolProbeReturnStartDistance = 0.0f;
	PatrolProbeReturnEndDistance = 0.0f;
	PatrolProbeReAggroSeconds = -1.0f;
	PatrolProbeBreakSeconds = -1.0f;
	PatrolProbePivotYaw = 0.0f;
	PatrolProbePivotTravel = 0.0f;
	bPatrolProbeLeashed = false;
	PatrolProbeGlyphHostile = -1;
	PatrolProbeGlyphAfterBreak = -1;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL-PROBE start boat X=%.0f Y=%.0f patrol X=%.0f Y=%.0f gap=%.0f aggro=%.0f hull=%d"),
		Boat->GetActorLocation().X, Boat->GetActorLocation().Y,
		Patrol->GetActorLocation().X, Patrol->GetActorLocation().Y,
		FVector::Dist2D(Boat->GetActorLocation(), Patrol->GetActorLocation()),
		Patrol->AggroRadius, Boat->GetHullHealth());

	World->GetTimerManager().SetTimer(PatrolProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::TickPatrolProbe), PatrolProbeStep, true);
}

void UWrecktideRewardSubsystem::PatrolProbeNextPhase()
{
	++PatrolProbePhase;
	PatrolProbePhaseElapsed = 0.0f;

	AWrecktideBoat* Boat = PatrolProbeBoat.Get();
	AWrecktidePatrolBoat* Patrol = PatrolProbePatrol.Get();
	if (!Boat || !Patrol)
	{
		return;
	}
	const FVector PatrolLocation = Patrol->GetActorLocation();

	switch (PatrolProbePhase)
	{
	case 1:
		// Ram stage: remember the hull the real path is about to take points off.
		PatrolProbeHullBefore = Boat->GetHullHealth();
		break;

	case 2:
	{
		// Run stage: put him outside her leash but still up her course, so she gives up on DISTANCE
		// and not on her blind arc. Two details the first run taught (2026-09-07):
		//   the lead up her bow keeps him out of that arc, and
		//   a fixed radial push is NOT enough near the route's rounded corners, where radial from the
		//   island's centre cuts across the loop instead of leaving it, so the push is MEASURED with
		//   her own DistanceToRoute until it is really past the leash (the first run pushed 8,000 uu
		//   and landed 4,235 uu off the route, inside the 6,000 she leashes at).
		const FVector Outward = FVector(PatrolLocation.X, PatrolLocation.Y, 0.0f).GetSafeNormal();
		const float Wanted = Patrol->LeashRouteRadius + PatrolProbeLeashOvershoot;
		FVector Far = PatrolLocation + Patrol->GetActorForwardVector() * PatrolProbeLeashLead;
		for (int32 Push = 0; Push < PatrolProbeLeashPushSteps; ++Push)
		{
			if (Patrol->DistanceToRoute(Far) >= Wanted)
			{
				break;
			}
			const FVector Next = Far + Outward * PatrolProbeLeashPushStep;
			// Both hulls stop dead at their own range limit, so never ask for a spot outside it.
			if (FVector2D(Next.X, Next.Y).Size() > PatrolProbeMaxRadius)
			{
				break;
			}
			Far = Next;
		}
		Boat->SetActorLocation(FVector(Far.X, Far.Y, PatrolProbeWaterZ), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL-PROBE run him out to X=%.0f Y=%.0f routeDist=%.0f leash=%.0f radius=%.0f"),
			Boat->GetActorLocation().X, Boat->GetActorLocation().Y,
			Patrol->DistanceToRoute(Boat->GetActorLocation()), Patrol->LeashRouteRadius,
			FVector2D(Boat->GetActorLocation().X, Boat->GetActorLocation().Y).Size());
		break;
	}

	case 3:
		// Return stage: how far off her route she is the moment she gives up.
		PatrolProbeReturnStartDistance = Patrol->DistanceToRoute(PatrolLocation);
		PatrolProbeReturnEndDistance = PatrolProbeReturnStartDistance;
		break;

	case 4:
	{
		// Back across her bow so she takes him again: a break has to be breakable twice.
		const FVector Ahead = PatrolLocation
			+ Patrol->GetActorForwardVector() * PatrolProbeApproachAhead
			+ Patrol->GetActorRightVector() * PatrolProbeApproachBeam;
		Boat->SetActorLocation(FVector(Ahead.X, Ahead.Y, PatrolProbeWaterZ), false, nullptr, ETeleportType::TeleportPhysics);
		Boat->SetActorRotation(FRotator(0.0f, Patrol->GetActorRotation().Yaw, 0.0f));
		break;
	}

	case 5:
	{
		// Blind arc stage. The probe puts him dead astern of her (the position a furled pivot buys)
		// and then he PIVOTS there for real: sails furled, rudder hard over. Two things are under
		// test at once, and the verdict reports both: the pivot is a real furled pivot (better than
		// PatrolProbePivotMinDegrees of yaw for less than PatrolProbePivotMaxTravel of travel), and
		// she loses him from her blind arc while he does it.
		const FVector Astern = PatrolLocation - Patrol->GetActorForwardVector() * PatrolProbeSternGap;
		Boat->SetActorLocation(FVector(Astern.X, Astern.Y, PatrolProbeWaterZ), false, nullptr, ETeleportType::TeleportPhysics);
		Boat->SetActorRotation(FRotator(0.0f, Patrol->GetActorRotation().Yaw, 0.0f));
		Boat->SetSailState(EWrecktideSailState::Furled);
		// Wind pinned astern of his heading so the pivot is measured on the rudder alone: sailing
		// into it halves rudder authority by design (spec S1.4).
		WrecktideWind::SetOverride(Boat->GetActorRotation().Yaw);
		PatrolProbeMark = Boat->GetActorLocation();
		PatrolProbeMarkYaw = Boat->GetActorRotation().Yaw;
		break;
	}

	default:
		break;
	}
}

void UWrecktideRewardSubsystem::TickPatrolProbe()
{
	UWorld* World = GetWorld();
	AWrecktideBoat* Boat = PatrolProbeBoat.Get();
	AWrecktidePatrolBoat* Patrol = PatrolProbePatrol.Get();
	if (!World || !Boat || !Patrol)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(PatrolProbeTimer);
		}
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL-PROBE FAIL (reason=actor lost mid run boat=%d patrol=%d)"),
			Boat != nullptr, Patrol != nullptr);
		WrecktideWind::ClearOverride();
		ScheduleProbeQuit(2.0f);
		return;
	}

	PatrolProbePhaseElapsed += PatrolProbeStep;
	const float Elapsed = PatrolProbePhaseElapsed;
	const float Gap = FVector::Dist2D(Boat->GetActorLocation(), Patrol->GetActorLocation());
	const float RouteDistance = Patrol->DistanceToRoute(Patrol->GetActorLocation());

	switch (PatrolProbePhase)
	{
	case 0:
		// Hostile, then alongside, both inside the spec's 8 s.
		if (Patrol->IsHostile())
		{
			if (PatrolProbeHostileSeconds < 0.0f)
			{
				PatrolProbeHostileSeconds = Elapsed;
				PatrolProbeGlyphHostile = CountPatrolMapGlyphs(this);
			}
			PatrolProbeClosestGap = FMath::Min(PatrolProbeClosestGap, Gap);
			if (PatrolProbeAlongsideSeconds < 0.0f && Gap <= PatrolProbeAlongsideGap)
			{
				PatrolProbeAlongsideSeconds = Elapsed;
			}
		}
		if (PatrolProbeAlongsideSeconds >= 0.0f || Elapsed >= PatrolProbeCloseBudget)
		{
			PatrolProbeNextPhase();
		}
		break;

	case 1:
		// A ram lands hull damage. The lunge is forced rather than waited out so the probe does not
		// spend a six second beat proving a clock; the CONTACT and the damage are the real path.
		if (Patrol->GetRamContactCount() <= 0)
		{
			Patrol->ForceRamLunge();
		}
		if (Patrol->GetRamContactCount() > 0 || Elapsed >= PatrolProbeRamBudget)
		{
			PatrolProbeHullAfter = Boat->GetHullHealth();
			PatrolProbeNextPhase();
		}
		break;

	case 2:
		// Sailing beyond the leash makes her give up. Recorded at the tick she stops being hostile,
		// so the printed distance is the one that broke it.
		if (!Patrol->IsHostile())
		{
			bPatrolProbeLeashed = true;
			PatrolProbeLeashRouteDistance = RouteDistance;
			PatrolProbeNextPhase();
		}
		else if (Elapsed >= PatrolProbeLeashBudget)
		{
			PatrolProbeLeashRouteDistance = RouteDistance;
			PatrolProbeNextPhase();
		}
		break;

	case 3:
		// And she goes back to the route.
		PatrolProbeReturnEndDistance = RouteDistance;
		if (Patrol->GetPatrolState() == EPatrolBoatState::Patrolling || Elapsed >= PatrolProbeReturnBudget)
		{
			PatrolProbeNextPhase();
		}
		break;

	case 4:
		if (Patrol->IsHostile())
		{
			PatrolProbeReAggroSeconds = Elapsed;
			PatrolProbeNextPhase();
		}
		else if (Elapsed >= PatrolProbeReAggroBudget)
		{
			PatrolProbeNextPhase();
		}
		break;

	case 5:
	{
		// The rider's key polling zeroes the steer the moment no key is down, so the hold is re-armed
		// every tick, exactly as -SailFeelProbe does it.
		Boat->HoldSail(0.0f, 1.0f, 1.0f);
		PatrolProbePivotYaw = FMath::Abs(FRotator::NormalizeAxis(Boat->GetActorRotation().Yaw - PatrolProbeMarkYaw));
		PatrolProbePivotTravel = FVector::Dist2D(Boat->GetActorLocation(), PatrolProbeMark);
		if (!Patrol->IsHostile())
		{
			PatrolProbeBreakSeconds = Elapsed;
			FinishPatrolProbe();
			return;
		}
		if (Elapsed >= PatrolProbeBlindBudget)
		{
			FinishPatrolProbe();
			return;
		}
		break;
	}

	default:
		FinishPatrolProbe();
		return;
	}
}

void UWrecktideRewardSubsystem::FinishPatrolProbe()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(PatrolProbeTimer);
	}
	WrecktideWind::ClearOverride();

	const AWrecktidePatrolBoat* Patrol = PatrolProbePatrol.Get();
	PatrolProbeGlyphAfterBreak = CountPatrolMapGlyphs(this);
	const float LeashRadius = Patrol ? Patrol->LeashRouteRadius : 0.0f;
	const int32 Contacts = Patrol ? Patrol->GetRamContactCount() : 0;
	const int32 Dealt = Patrol ? Patrol->GetRamDamageDealt() : 0;

	const bool bHostileOK = PatrolProbeHostileSeconds >= 0.0f && PatrolProbeHostileSeconds <= PatrolProbeCloseBudget;
	const bool bAlongsideOK = PatrolProbeAlongsideSeconds >= 0.0f && PatrolProbeAlongsideSeconds <= PatrolProbeCloseBudget;
	const bool bRamOK = Contacts > 0 && PatrolProbeHullAfter < PatrolProbeHullBefore;
	// 0.9 of the leash rather than the leash itself: the state flips inside her tick and this samples
	// at 10 Hz, so the recorded distance is up to one tick of travel short of the trigger.
	const bool bLeashOK = bPatrolProbeLeashed && PatrolProbeLeashRouteDistance >= LeashRadius * 0.9f;
	const bool bReturnOK = (PatrolProbeReturnStartDistance - PatrolProbeReturnEndDistance) >= PatrolProbeReturnGain;
	const bool bBlindOK = PatrolProbeBreakSeconds >= 0.0f
		&& PatrolProbePivotYaw > PatrolProbePivotMinDegrees
		&& PatrolProbePivotTravel < PatrolProbePivotMaxTravel;
	// The red ship is on the map while she hunts and gone when she stops (spec S3.3).
	const bool bGlyphOK = PatrolProbeGlyphHostile == 1 && PatrolProbeGlyphAfterBreak == 0;
	const bool bPass = bHostileOK && bAlongsideOK && bRamOK && bLeashOK && bReturnOK && bBlindOK && bGlyphOK;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL-PROBE %s (hostile=%.1fs alongside=%.1fs gap=%.0fuu ram=%d->%d contacts=%d damage=%d leash=%.0f/%.0fuu return=%.0f->%.0fuu reaggro=%.1fs blind=%.1fs pivot=%.0fdeg travel=%.0fuu glyph=%d/%d hostileOK=%d alongsideOK=%d ramOK=%d leashOK=%d returnOK=%d blindOK=%d glyphOK=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		PatrolProbeHostileSeconds, PatrolProbeAlongsideSeconds, PatrolProbeClosestGap,
		PatrolProbeHullBefore, PatrolProbeHullAfter, Contacts, Dealt,
		PatrolProbeLeashRouteDistance, LeashRadius,
		PatrolProbeReturnStartDistance, PatrolProbeReturnEndDistance,
		PatrolProbeReAggroSeconds, PatrolProbeBreakSeconds,
		PatrolProbePivotYaw, PatrolProbePivotTravel,
		PatrolProbeGlyphHostile, PatrolProbeGlyphAfterBreak,
		bHostileOK ? 1 : 0, bAlongsideOK ? 1 : 0, bRamOK ? 1 : 0,
		bLeashOK ? 1 : 0, bReturnOK ? 1 : 0, bBlindOK ? 1 : 0, bGlyphOK ? 1 : 0);

	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunBoatProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	if (!PlayerPawn || !Boat)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BOAT-PROBE FAIL reason=missing pawn=%d boat=%d"), PlayerPawn != nullptr, Boat != nullptr);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE FAIL (no boat or pawn)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// stand on the pier beside the hull (the pier deck top is -40; the boat is moored at its west rail)
	// dockside = the pier deck beside the hull, derived from the boat (moored bow-south at the west
	// rail, its right vector points at the deck); in range is asserted, not just logged (audit 2026-09-04)
	const FVector BoatStart = Boat->GetActorLocation();
	const FVector Dockside = BoatStart + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f);
	PlayerPawn->SetActorLocation(Dockside, false, nullptr, ETeleportType::TeleportPhysics);
	const bool bInRange = Boat->IsCharacterInRange(PlayerPawn);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE start boat=%s pawn=%s inRange=%d"),
		*BoatStart.ToString(), *Dockside.ToString(), bInRange ? 1 : 0);
	if (!bInRange)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE FAIL (dockside spot outside the boat trigger)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	BoatProbeBoat = Boat;
	BoatProbeStart = BoatStart;
	FTimerHandle BoardTimer;
	World->GetTimerManager().SetTimer(BoardTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		ACombatCharacter* Pirate = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr;
		AWrecktideBoat* InnerBoat = BoatProbeBoat.Get();
		if (!Pirate || !InnerBoat)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE FAIL (pawn or boat lost before boarding)"));
			ScheduleProbeQuit(2.0f);
			return;
		}
		const bool bBoarded = InnerBoat->Board(Pirate);
		const bool bPossessed = InnerController->GetPawn() == InnerBoat;
		const bool bRiderParked = InnerBoat->GetRider() == Pirate && Pirate->GetAttachParentActor() == InnerBoat && !Pirate->GetActorEnableCollision();
		bBoatProbeBoardOK = bBoarded && bPossessed && bRiderParked;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE board %s boarded=%d possessed=%d parked=%d"),
			bBoatProbeBoardOK ? TEXT("OK") : TEXT("BAD"), bBoarded ? 1 : 0, bPossessed ? 1 : 0, bRiderParked ? 1 : 0);
		if (!bBoatProbeBoardOK)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE FAIL (boarding)"));
			ScheduleProbeQuit(2.0f);
			return;
		}
		// sail: full throttle for 2 s through the same path the keys use
		InnerBoat->HoldSail(1.0f, 0.0f, 2.0f);
		FTimerHandle SailTimer;
		InnerWorld->GetTimerManager().SetTimer(SailTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckBoatProbeSail), 2.2f, false);
	}), 1.0f, false);
}

void UWrecktideRewardSubsystem::CheckBoatProbeSail()
{
	UWorld* World = GetWorld();
	AWrecktideBoat* Boat = BoatProbeBoat.Get();
	if (!World || !Boat)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE FAIL (boat lost while sailing)"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	const float Sailed = FVector::Dist2D(Boat->GetActorLocation(), BoatProbeStart);
	const bool bSailOK = Sailed > 300.0f && FMath::IsNearlyEqual(Boat->GetActorLocation().Z, -120.0f, 5.0f);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE sail %s dist=%.0f z=%.1f speed=%.0f"),
		bSailOK ? TEXT("OK") : TEXT("BAD"), Sailed, Boat->GetActorLocation().Z, Boat->GetVelocity().Size());
	bBoatProbeSailOK = bSailOK;

	// back to the mooring so the shore test has the pier beside the hull, then go ashore
	Boat->HoldSail(0.0f, 0.0f, 0.1f);
	Boat->SetActorLocation(BoatProbeStart, false, nullptr, ETeleportType::TeleportPhysics);
	Boat->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
	FTimerHandle AshoreTimer;
	World->GetTimerManager().SetTimer(AshoreTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckBoatProbeAshore), 0.5f, false);
}

void UWrecktideRewardSubsystem::CheckBoatProbeAshore()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktideBoat* Boat = BoatProbeBoat.Get();
	if (!PlayerController || !Boat)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE FAIL (controller or boat lost before going ashore)"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	const bool bAshore = Boat->GoAshore();
	ACombatCharacter* Pirate = Cast<ACombatCharacter>(PlayerController->GetPawn());
	const bool bBack = Pirate != nullptr && !Boat->IsOccupied();
	const bool bAlive = Pirate && Pirate->Stats && Pirate->Stats->IsAlive();
	const bool bCollides = Pirate && Pirate->GetActorEnableCollision() && Pirate->GetAttachParentActor() == nullptr;
	const bool bWalking = Pirate && Pirate->GetCharacterMovement() && Pirate->GetCharacterMovement()->MovementMode != MOVE_None;
	const bool bAboveWater = Pirate && Pirate->GetActorLocation().Z > -100.0f;
	const bool bPass = bBoatProbeBoardOK && bBoatProbeSailOK && bAshore && bBack && bAlive && bCollides && bWalking && bAboveWater;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE ashore %s ashore=%d back=%d alive=%d collides=%d walking=%d aboveWater=%d at=%s"),
		(bAshore && bBack && bAlive && bCollides && bWalking && bAboveWater) ? TEXT("OK") : TEXT("BAD"),
		bAshore ? 1 : 0, bBack ? 1 : 0, bAlive ? 1 : 0, bCollides ? 1 : 0, bWalking ? 1 : 0, bAboveWater ? 1 : 0,
		Pirate ? *Pirate->GetActorLocation().ToString() : TEXT("none"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT-PROBE %s (board=%d sail=%d ashore=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bBoatProbeBoardOK ? 1 : 0, bBoatProbeSailOK ? 1 : 0, bAshore ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunBoatWalkProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	AWrecktideBoat* Boat = nullptr;
	AStaticMeshActor* DecorativeHull = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
		{
			Boat = *It;
			break;
		}
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const UStaticMeshComponent* MeshComponent = It->GetStaticMeshComponent();
			const UStaticMesh* Mesh = MeshComponent ? MeshComponent->GetStaticMesh() : nullptr;
			if (Mesh && Mesh->GetName().Contains(TEXT("SM_Veh_Boat_Medium_01_Hull")))
			{
				DecorativeHull = *It;
				break;
			}
		}
	}
	if (!World || !PlayerController || !PlayerPawn || !HUD || !Boat || !DecorativeHull)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BOATWALK-PROBE FAIL (wiring world=%d controller=%d pawn=%d hud=%d boat=%d decor=%d)"),
			World != nullptr, PlayerController != nullptr, PlayerPawn != nullptr, HUD != nullptr, Boat != nullptr, DecorativeHull != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	BoatWalkProbeBoat = Boat;
	bBoatWalkProbeDecorOK = false;
	bBoatWalkProbePierOK = false;
	bBoatWalkProbeAshoreOK = false;
	bBoatWalkProbeSwimHoldOK = false;

	// First prove the large display hull does not silently eat F. This goes through the pirate's
	// bound key, and the exact toast is read back from the HUD.
	PlayerPawn->SetActorLocation(DecorativeHull->GetActorLocation() + FVector(0.0f, 0.0f, 200.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	PressBoatProbeF(*PlayerController);
	FTimerHandle DecorCheckTimer;
	World->GetTimerManager().SetTimer(DecorCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::BeginBoatWalkProbePier), 0.25f, false);
}

void UWrecktideRewardSubsystem::BeginBoatWalkProbePier()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	AWrecktideBoat* Boat = BoatWalkProbeBoat.Get();
	if (!World || !PlayerController || !PlayerPawn || !HUD || !Boat)
	{
		FinishBoatWalkProbe();
		return;
	}

	const FString DecorToast = HUD->GetToastText();
	bBoatWalkProbeDecorOK = !Boat->IsOccupied()
		&& DecorToast.Contains(TEXT("not yours")) && DecorToast.Contains(TEXT("small one off the west rail"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOATWALK-PROBE decor %s boarded=%d toast=%s"),
		bBoatWalkProbeDecorOK ? TEXT("OK") : TEXT("BAD"), Boat->IsOccupied() ? 1 : 0, *DecorToast);

	// Walk the real pawn down the pier's centre line from its landward end. The centre line never
	// enters the old 650 uu overlap sphere, so the F press below can only reach the pirate binding.
	if (UCharacterMovementComponent* Movement = PlayerPawn->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
	}
	PlayerPawn->SetActorLocation(FVector(0.0f, -7400.0f, 60.0f), false, nullptr, ETeleportType::TeleportPhysics);
	PlayerPawn->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
	PlayerController->SetControlRotation(FRotator(0.0f, -90.0f, 0.0f));
	BoatWalkProbeDeadline = World->GetTimeSeconds() + 5.0f;
	World->GetTimerManager().SetTimer(BoatWalkProbeDriveTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::DriveBoatWalkProbePier), 0.016f, true);
}

void UWrecktideRewardSubsystem::DriveBoatWalkProbePier()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!World || !PlayerController || !PlayerPawn || !BoatWalkProbeBoat.IsValid())
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(BoatWalkProbeDriveTimer);
		}
		FinishBoatWalkProbe();
		return;
	}

	PlayerPawn->DoMove(0.0f, 1.0f);
	if (PlayerPawn->GetActorLocation().Y <= -8275.0f)
	{
		World->GetTimerManager().ClearTimer(BoatWalkProbeDriveTimer);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOATWALK-PROBE pier reached at %s inRange=%d"),
			*PlayerPawn->GetActorLocation().ToString(), BoatWalkProbeBoat->IsCharacterInRange(PlayerPawn) ? 1 : 0);
		PressBoatProbeF(*PlayerController);
		FTimerHandle CheckTimer;
		World->GetTimerManager().SetTimer(CheckTimer,
			FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckBoatWalkProbeBoarded), 1.0f, false);
		return;
	}

	if (World->GetTimeSeconds() >= BoatWalkProbeDeadline)
	{
		World->GetTimerManager().ClearTimer(BoatWalkProbeDriveTimer);
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BOATWALK-PROBE pier walk timed out at %s"),
			*PlayerPawn->GetActorLocation().ToString());
		FinishBoatWalkProbe();
	}
}

void UWrecktideRewardSubsystem::CheckBoatWalkProbeBoarded()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktideBoat* Boat = BoatWalkProbeBoat.Get();
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	bBoatWalkProbePierOK = PlayerController && Boat && Boat->IsOccupied() && PlayerController->GetPawn() == Boat
		&& HUD && HUD->GetInteractionPrompt() == TEXT("F: go ashore");
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOATWALK-PROBE pier board %s occupied=%d possessed=%d prompt=%s"),
		bBoatWalkProbePierOK ? TEXT("OK") : TEXT("BAD"), Boat && Boat->IsOccupied() ? 1 : 0,
		PlayerController && PlayerController->GetPawn() == Boat ? 1 : 0,
		HUD ? *HUD->GetInteractionPrompt() : TEXT("None"));
	if (!bBoatWalkProbePierOK || !World || !PlayerController)
	{
		FinishBoatWalkProbe();
		return;
	}

	PressBoatProbeF(*PlayerController);
	FTimerHandle CheckTimer;
	World->GetTimerManager().SetTimer(CheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckBoatWalkProbeAshore), 0.75f, false);
}

void UWrecktideRewardSubsystem::CheckBoatWalkProbeAshore()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideBoat* Boat = BoatWalkProbeBoat.Get();
	UCharacterMovementComponent* Movement = PlayerPawn ? PlayerPawn->GetCharacterMovement() : nullptr;
	bBoatWalkProbeAshoreOK = PlayerPawn && Boat && !Boat->IsOccupied() && PlayerPawn->GetActorEnableCollision()
		&& Movement && Movement->MovementMode == MOVE_Walking;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOATWALK-PROBE ashore %s pawn=%d occupied=%d mode=%d"),
		bBoatWalkProbeAshoreOK ? TEXT("OK") : TEXT("BAD"), PlayerPawn != nullptr,
		Boat && Boat->IsOccupied() ? 1 : 0, Movement ? static_cast<int32>(Movement->MovementMode) : -1);
	if (!bBoatWalkProbeAshoreOK || !World || !PlayerController || !PlayerPawn || !Boat || !Movement)
	{
		FinishBoatWalkProbe();
		return;
	}

	// Swim toward the hull for two seconds. The pawn must remain in MOVE_Swimming (no automatic
	// pier haul-out), retain the persistent board prompt, then board through the same F input path.
	Movement->StopMovementImmediately();
	PlayerPawn->SetActorLocation(Boat->GetActorLocation() + FVector(-450.0f, 0.0f, -40.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	Movement->SetMovementMode(MOVE_Swimming);
	PlayerController->SetControlRotation(FRotator(0.0f, 0.0f, 0.0f));
	World->GetTimerManager().SetTimer(BoatWalkProbeDriveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		if (ACombatCharacter* InnerPawn = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr)
		{
			InnerPawn->DoMove(0.0f, 0.1f);
		}
	}), 0.016f, true);
	FTimerHandle HoldTimer;
	World->GetTimerManager().SetTimer(HoldTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckBoatWalkProbeSwimHold), 2.0f, false);
}

void UWrecktideRewardSubsystem::CheckBoatWalkProbeSwimHold()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(BoatWalkProbeDriveTimer);
	}
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideBoat* Boat = BoatWalkProbeBoat.Get();
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	bBoatWalkProbeSwimHoldOK = PlayerPawn && Boat && PlayerPawn->IsSwimming()
		&& Boat->IsPointWithinHullRange(PlayerPawn->GetActorLocation(), 500.0f)
		&& HUD && HUD->GetInteractionPrompt() == TEXT("Press F to board the boat");
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOATWALK-PROBE swim hold %s swimming=%d hullRange=%d prompt=%s"),
		bBoatWalkProbeSwimHoldOK ? TEXT("OK") : TEXT("BAD"), PlayerPawn && PlayerPawn->IsSwimming() ? 1 : 0,
		PlayerPawn && Boat && Boat->IsPointWithinHullRange(PlayerPawn->GetActorLocation(), 500.0f) ? 1 : 0,
		HUD ? *HUD->GetInteractionPrompt() : TEXT("None"));
	if (!bBoatWalkProbeSwimHoldOK || !World || !PlayerController)
	{
		FinishBoatWalkProbe();
		return;
	}

	PressBoatProbeF(*PlayerController);
	FTimerHandle FinishTimer;
	World->GetTimerManager().SetTimer(FinishTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishBoatWalkProbe), 1.0f, false);
}

void UWrecktideRewardSubsystem::FinishBoatWalkProbe()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(BoatWalkProbeDriveTimer);
	}
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktideBoat* Boat = BoatWalkProbeBoat.Get();
	const bool bSwimBoarded = PlayerController && Boat && Boat->IsOccupied() && PlayerController->GetPawn() == Boat
		&& Boat->GetRider() != nullptr;
	const bool bPass = bBoatWalkProbeDecorOK && bBoatWalkProbePierOK && bBoatWalkProbeAshoreOK
		&& bBoatWalkProbeSwimHoldOK && bSwimBoarded;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOATWALK-PROBE %s (decor=%d pier=%d ashore=%d swimHold=%d swimBoard=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bBoatWalkProbeDecorOK ? 1 : 0, bBoatWalkProbePierOK ? 1 : 0,
		bBoatWalkProbeAshoreOK ? 1 : 0, bBoatWalkProbeSwimHoldOK ? 1 : 0, bSwimBoarded ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -ChestProbe (2026-09-04): treasure chests pay once. Walks the pirate to the first placed chest,
// opens it through Open() (the same path the F key uses), requires the exact gold delta and the
// persisted Chest_<Id> flag, then requires a second Open() to REFUSE with no further gold.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunChestProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	AWrecktideTreasureChest* Chest = nullptr;
	int32 ChestCount = 0;
	for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
	{
		if (!Chest)
		{
			Chest = *It;
		}
		++ChestCount;
	}
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	if (!PlayerPawn || !Chest || !Economy || !Progress)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CHEST-PROBE FAIL reason=wiring pawn=%d chests=%d economy=%d progress=%d"),
			PlayerPawn != nullptr, ChestCount, Economy != nullptr, Progress != nullptr);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST-PROBE FAIL (wiring)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// stand beside the chest (inside its 260 uu trigger), then open through the authoritative path
	const FVector ChestLocation = Chest->GetActorLocation();
	PlayerPawn->SetActorLocation(ChestLocation + FVector(120.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
	const int64 GoldBefore = Economy->GetGoldTotal();
	const bool bWasOpened = Chest->IsOpened();
	const bool bOpened = Chest->Open(PlayerPawn);
	const int64 GoldAfterOpen = Economy->GetGoldTotal();
	const bool bFlagSet = Progress->GetQuestFlag(Chest->FlagKey()) != 0;
	const bool bPaid = (GoldAfterOpen - GoldBefore) == Chest->GetPayoutGold();
	const bool bRefused = !Chest->Open(PlayerPawn);
	const int64 GoldAfterRetry = Economy->GetGoldTotal();
	const bool bNoDouble = GoldAfterRetry == GoldAfterOpen;
	const bool bPass = ChestCount >= 1 && !bWasOpened && bOpened && bFlagSet && bPaid && bRefused && bNoDouble;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST-PROBE chest=%s chests=%d wasOpened=%d opened=%d flag=%d gold %lld->%lld (expected +%lld) retryRefused=%d noDouble=%d"),
		*Chest->ChestId.ToString(), ChestCount, bWasOpened ? 1 : 0, bOpened ? 1 : 0, bFlagSet ? 1 : 0,
		GoldBefore, GoldAfterOpen, Chest->GetPayoutGold(), bRefused ? 1 : 0, bNoDouble ? 1 : 0);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST-PROBE %s"), bPass ? TEXT("PASS") : TEXT("FAIL (see the line above)"));
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunEconomyProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	AWrecktideTreasureChest* CampChest = nullptr;
	AWrecktideShopStall* Stall = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
		{
			if (It->ChestId == FName(TEXT("CampChest")))
			{
				CampChest = *It;
				break;
			}
		}
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
		{
			Stall = *It;
			break;
		}
	}
	if (!World || !PlayerPawn || !PlayerState || !Economy || !Skill || !Data || !HUD || !CampChest || !Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-ECONOMY-PROBE FAIL (wiring world=%d pawn=%d ps=%d economy=%d skill=%d data=%d hud=%d campChest=%d stall=%d)"),
			World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, Economy != nullptr, Skill != nullptr,
			Data != nullptr, HUD != nullptr, CampChest != nullptr, Stall != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	struct FNamedAmount { const TCHAR* Name; int64 Amount; };
	const FNamedAmount Chests[] =
	{
		// research retune 2026-09-06 (SPEC C3 correction): chests worth three to five kills
		{ TEXT("CampChest"), 25 }, { TEXT("StashChest"), 60 }, { TEXT("GraveChest"), 25 },
		{ TEXT("WreckChest"), 60 }, { TEXT("FortChest"), 150 }
	};
	bool bChestDataOK = true;
	for (const FNamedAmount& Expected : Chests)
	{
		const FChestTableRow* Row = Data->GetChestRow(FName(Expected.Name));
		bChestDataOK &= Row && Row->Gold == Expected.Amount;
	}

	const int64 GoldBefore = Economy->GetGoldTotal();
	const FChestTableRow* CampRow = Data->GetChestRow(FName(TEXT("CampChest")));
	PlayerPawn->SetActorLocation(CampChest->GetActorLocation() + FVector(120.0f, 0.0f, 100.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	const bool bOpened = CampChest->Open(PlayerPawn);
	const int64 ChestDelta = Economy->GetGoldTotal() - GoldBefore;
	const bool bBrokeStart = GoldBefore == 0;
	const bool bChestOverrideOK = CampRow && CampChest->GoldAmount != CampRow->Gold
		&& CampChest->GetPayoutGold() == CampRow->Gold && bOpened && ChestDelta == CampRow->Gold;

	struct FEnemyGold { const TCHAR* Name; int64 Min; int64 Max; };
	const FEnemyGold Enemies[] =
	{
		// kill gold per enemy level L = 5+3(L-1) .. 11+6(L-1), the boss x3 (research retune 2026-09-06)
		{ TEXT("NavyCadet"), 5, 11 }, { TEXT("Cutthroat"), 8, 17 }, { TEXT("SkeletonDeckhand"), 11, 23 },
		{ TEXT("NavyOfficer"), 14, 29 }, { TEXT("VoodooAcolyte"), 17, 35 }, { TEXT("SkeletonCaptain"), 51, 105 }
	};
	bool bEnemyDataOK = true;
	for (const FEnemyGold& Expected : Enemies)
	{
		const FEnemyTableRow* Row = Data->GetEnemyRow(FName(Expected.Name));
		bEnemyDataOK &= Row && Row->GoldDropMin == Expected.Min && Row->GoldDropMax == Expected.Max;
	}

	const FNamedAmount Missions[] =
	{
		{ TEXT("M01_Wake"), 5 }, { TEXT("M02_FirstSteel"), 15 }, { TEXT("M03_CampStash"), 10 },
		{ TEXT("M04_Redcoats"), 30 }, { TEXT("M05_Bounty"), 20 }, { TEXT("M06_SetSail"), 25 },
		{ TEXT("M07_BoneCaptain"), 120 }
	};
	bool bMissionDataOK = true;
	for (const FNamedAmount& Expected : Missions)
	{
		const FMissionTableRow* Row = Data->GetMissionRow(FName(Expected.Name));
		bMissionDataOK &= Row && Row->RewardGold == Expected.Amount;
	}

	const FNamedAmount Bounties[] =
	{
		{ TEXT("FirstBlood"), 15 }, { TEXT("CutthroatBounty"), 30 }, { TEXT("SkeletonPurge"), 45 },
		{ TEXT("RedcoatPatrol"), 35 }, { TEXT("CaptainsHead"), 100 }
	};
	bool bBountyDataOK = true;
	for (const FNamedAmount& Expected : Bounties)
	{
		const FQuestTableRow* Row = Data->GetQuestRow(FName(Expected.Name));
		bBountyDataOK &= Row && Row->RewardGold == Expected.Amount;
	}

	struct FWeaponGate { const TCHAR* Name; int32 Level; int64 Price; bool bPurchasable; };
	const FWeaponGate Weapons[] =
	{
		// weapon ladder x2.3 with notoriety gates (research retune 2026-09-06)
		{ TEXT("RustyFlintlock"), 1, 0, false }, { TEXT("WornCutlass"), 1, 25, true },
		{ TEXT("BoardingAxe"), 3, 90, true }, { TEXT("HexTalisman"), 6, 220, true },
		{ TEXT("NavyPistol"), 6, 220, true }, { TEXT("OfficersSabre"), 12, 520, true }
	};
	bool bWeaponDataOK = true;
	for (const FWeaponGate& Expected : Weapons)
	{
		const FWeaponTableRow* Row = Data->GetWeaponRow(FName(Expected.Name));
		bWeaponDataOK &= Row && Row->RequiredLevel == Expected.Level && Row->Price == Expected.Price
			&& Row->bPurchasableWithGold == Expected.bPurchasable;
	}
	const TArray<TPair<FName, const FWeaponTableRow*>> ShopRows = Data->GetMeleeShopRows();
	bWeaponDataOK &= ShopRows.Num() == 3
		&& ShopRows[0].Key == FName(TEXT("WornCutlass"))
		&& ShopRows[1].Key == FName(TEXT("BoardingAxe"))
		&& ShopRows[2].Key == FName(TEXT("OfficersSabre"));

	// RequiredLevel is the player's notoriety, not the weapon's combat skill. Try the level-3 axe
	// while fresh at level 1, then award exactly the cumulative threshold and retry the same offer.
	const FName GatedRow(TEXT("BoardingAxe"));
	const FWeaponTableRow* GatedWeapon = Data->GetWeaponRow(GatedRow);
	PlayerPawn->SetActorLocation(Stall->GetActorLocation() + Stall->GetActorRightVector() * 150.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Stall->OpenShopMenu(PlayerPawn);
	for (int32 Index = 0; Index < Stall->GetOfferCount() && Stall->GetSelectedOfferRow() != GatedRow; ++Index)
	{
		Stall->MoveMenuSelection(1);
	}
	const bool bSelectedGate = Stall->IsShopMenuOpen() && Stall->GetSelectedOfferRow() == GatedRow && GatedWeapon;
	const int64 Price = GatedWeapon ? GatedWeapon->Price : 0;
	Economy->ApplyGoldDelta(FMath::Max<int64>(Price, 1), EGoldSource::Earned,
		EGoldReason::Adjustment, FName(TEXT("EconomyProbe")));
	const int64 GoldBeforeLock = Economy->GetGoldTotal();
	const bool bLevelOne = Skill->GetNotorietyLevel() == 1;
	Stall->ConfirmMenuSelection();
	const bool bLockRefused = bSelectedGate && bLevelOne && Economy->GetGoldTotal() == GoldBeforeLock
		&& !Economy->HasPurchasedRow(GatedRow) && PlayerState->EquippedWeaponRow != GatedRow
		&& HUD->GetToastText() == FString::Printf(TEXT("Lv %d needed"), GatedWeapon ? GatedWeapon->RequiredLevel : 0);

	const int64 XPNeeded = Skill->GetNotorietyXPForLevel(GatedWeapon ? GatedWeapon->RequiredLevel : 3)
		- Skill->GetNotorietyXP();
	Skill->AwardXP(EWrecktideSkill::Melee, FMath::Max<int64>(XPNeeded, 1), FName(TEXT("EconomyProbe")));
	const bool bAtRequiredLevel = GatedWeapon && Skill->GetNotorietyLevel() == GatedWeapon->RequiredLevel;
	const int64 GoldBeforeBuy = Economy->GetGoldTotal();
	Stall->ConfirmMenuSelection();
	const bool bBuyAccepted = bAtRequiredLevel && Economy->HasPurchasedRow(GatedRow)
		&& PlayerState->EquippedWeaponRow == GatedRow && GoldBeforeBuy - Economy->GetGoldTotal() == Price;

	const bool bPass = bBrokeStart && bChestDataOK && bChestOverrideOK && bEnemyDataOK
		&& bMissionDataOK && bBountyDataOK && bWeaponDataOK && bLockRefused && bBuyAccepted;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ECONOMY-PROBE %s (broke=%d chestRows=%d chestOverride=%d delta=%lld enemies=%d missions=%d bounties=%d weapons=%d lock=%d buyAtLevel=%d notoriety=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bBrokeStart ? 1 : 0, bChestDataOK ? 1 : 0,
		bChestOverrideOK ? 1 : 0, ChestDelta, bEnemyDataOK ? 1 : 0, bMissionDataOK ? 1 : 0,
		bBountyDataOK ? 1 : 0, bWeaponDataOK ? 1 : 0, bLockRefused ? 1 : 0,
		bBuyAccepted ? 1 : 0, Skill->GetNotorietyLevel());
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunNotorietyProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance
		? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FName EnemyRowName(TEXT("NavyCadet"));
	const FEnemyTableRow* EnemyRow = Data ? Data->GetEnemyRow(EnemyRowName) : nullptr;
	if (!World || !PlayerState || !Skill || !Progress || !HUD || !EnemyRow)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-NOTORIETY-PROBE FAIL (wiring world=%d ps=%d skill=%d progress=%d hud=%d enemy=%d)"),
			World != nullptr, PlayerState != nullptr, Skill != nullptr, Progress != nullptr, HUD != nullptr,
			EnemyRow != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	MissionProbeEnemyClass.Reset();
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			MissionProbeEnemyClass = It->GetClass();
			MissionProbeSpawnTransform = It->GetActorTransform();
			break;
		}
	}
	if (!MissionProbeEnemyClass.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-NOTORIETY-PROBE FAIL (no live enemy spawn recipe)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const bool bKnownCurve = Skill->GetNotorietyLevelForXP(0) == 1
		&& Skill->GetNotorietyLevelForXP(138) == 2
		&& Skill->GetNotorietyLevelForXP(4755) == 10;
	const int64 NotorietyBeforeKill = Skill->GetNotorietyXP();
	const bool bKilled = FireMissionProbeKill(EnemyRowName);
	const int64 NotorietyAfterKill = Skill->GetNotorietyXP();
	const bool bRealKillAward = bKilled
		&& NotorietyAfterKill - NotorietyBeforeKill == EnemyRow->XPAward;
	bNotorietyProbeAwardOK = bKnownCurve && bRealKillAward;
	const int32 LevelAfterKill = Skill->GetNotorietyLevel();
	const int64 FloorXP = Skill->GetNotorietyXPForLevel(LevelAfterKill);
	const int64 CeilingXP = Skill->GetNotorietyXPForLevel(LevelAfterKill + 1);
	NotorietyProbeExpectedFill = CeilingXP > FloorXP
		? static_cast<float>(NotorietyAfterKill - FloorXP) / static_cast<float>(CeilingXP - FloorXP) : 1.0f;

	FTimerHandle BarCheckTimer;
	World->GetTimerManager().SetTimer(BarCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckNotorietyProbeBar), 0.75f, false);
}

void UWrecktideRewardSubsystem::CheckNotorietyProbeBar()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	if (!World || !Skill || !Progress || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-NOTORIETY-PROBE FAIL (state lost during bar check)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const float ActualFill = HUD->GetNotorietyBarFillFraction();
	bNotorietyProbeBarOK = HUD->IsNotorietyBarVisible()
		&& FMath::IsNearlyEqual(ActualFill, NotorietyProbeExpectedFill, 0.01f);

	const int64 XPToLevelTwo = Skill->GetNotorietyXPForLevel(2) - Skill->GetNotorietyXP();
	if (XPToLevelTwo > 0)
	{
		Skill->AwardXP(EWrecktideSkill::Melee, XPToLevelTwo, FName(TEXT("NotorietyProbe")));
	}
	const bool bLevelUpFeed = Skill->GetNotorietyLevel() == 2
		&& HUD->GetRewardFeedLine(0) == TEXT("Notoriety level 2");

	// A version-3 payload has no notoriety property. Its skill level 2 plus 38 remainder represents
	// 100 + 38 lifetime XP and must therefore migrate to notoriety XP 138 / level 2.
	FWrecktideSaveData OldData;
	OldData.SaveVersion = 3;
	FSkillState OldMelee;
	OldMelee.Level = 2;
	OldMelee.XP = 38;
	OldData.Skills.Add(EWrecktideSkill::Melee, OldMelee);
	Progress->ApplySaveData(OldData);
	const bool bOldSaveDerived = Skill->GetNotorietyXP() == 138 && Skill->GetNotorietyLevel() == 2;
	const bool bHUDLine = HUD->GetNotorietyLine() == TEXT("Notoriety Lv 2");
	bNotorietyProbeLegacyOK = bLevelUpFeed && bOldSaveDerived && bHUDLine;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NOTORIETY-PROBE bar visible=%d fill=%.3f expected=%.3f"),
		HUD->IsNotorietyBarVisible() ? 1 : 0, ActualFill, NotorietyProbeExpectedFill);

	FTimerHandle HideCheckTimer;
	World->GetTimerManager().SetTimer(HideCheckTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishNotorietyProbe),
		HUD->GetNotorietyBarVisibleSeconds() + 0.25f, false);
}

void UWrecktideRewardSubsystem::FinishNotorietyProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	const bool bHidden = HUD && !HUD->IsNotorietyBarVisible();
	const bool bPass = bNotorietyProbeAwardOK && bNotorietyProbeBarOK
		&& bNotorietyProbeLegacyOK && bHidden;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NOTORIETY-PROBE %s (award=%d bar=%d fill=%.3f hidden=%d legacy=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bNotorietyProbeAwardOK ? 1 : 0,
		bNotorietyProbeBarOK ? 1 : 0, NotorietyProbeExpectedFill, bHidden ? 1 : 0,
		bNotorietyProbeLegacyOK ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -NavProbe (2026-09-04): the island's three tiers must be ONE nav island. Before today the agent
// step height (35) was below the 40 uu tier steps, so a field enemy could never path onto the beach
// where the player spawns, nor up to the fort. Synchronous path queries from the field to the beach
// spawn and to the fort spawner; a partial or missing path fails.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunNavProbe()
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-NAV-PROBE FAIL reason=no-navigation-system"));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NAV-PROBE FAIL (no nav system)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// field centre (z 0), beach spawn (z -40), fort spawner on the hill (z +40): the Lvl_Island layout
	const FVector Field(0.0f, -3000.0f, 0.0f);
	const FVector Beach(0.0f, -5200.0f, -40.0f);
	const FVector Fort(2000.0f, 1800.0f, 40.0f);

	auto Query = [&](const TCHAR* Label, const FVector& From, const FVector& To) -> bool
	{
		UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(World, From, To);
		const bool bValid = Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() >= 2;
		const float Length = Path ? Path->GetPathLength() : -1.0f;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NAV-PROBE %s %s points=%d partial=%d length=%.0f"),
			Label, bValid ? TEXT("OK") : TEXT("BAD"), Path ? Path->PathPoints.Num() : 0,
			(Path && Path->IsPartial()) ? 1 : 0, Length);
		return bValid;
	};

	// nav data truth: which navmesh, its agent settings, and whether each tier projects onto it at all
	int32 NavDataCount = 0;
	for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
	{
		++NavDataCount;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NAV-PROBE navdata %s stepHeight=%.1f radius=%.1f height=%.1f cell=%.1f cellH=%.1f tiles=%d"),
			*It->GetName(), It->GetAgentMaxStepHeight(ENavigationDataResolution::Default), It->AgentRadius, It->AgentHeight, It->GetCellSize(ENavigationDataResolution::Default),
			It->GetCellHeight(ENavigationDataResolution::Default), It->GetNavMeshTilesCount());
	}
	const FVector HillCentre(2600.0f, 2600.0f, 40.0f);
	// coverage first. The level ships an editor-built navmesh (DynamicModifiersOnly), so it is either
	// there at load or never; the short retry only covers modifier settling. Note this spins the game
	// thread, which is why it is capped at 2 s (audit 2026-09-04).
	const double CoverageStart = FPlatformTime::Seconds();
	bool bAllOn = false;
	while (!bAllOn && FPlatformTime::Seconds() - CoverageStart < 2.0)
	{
		bAllOn = true;
		for (const TPair<const TCHAR*, FVector>& Spot : TArray<TPair<const TCHAR*, FVector>>{
			{TEXT("field"), Field}, {TEXT("beach"), Beach}, {TEXT("fort"), Fort}, {TEXT("hillCentre"), HillCentre}})
		{
			FNavLocation Projected;
			const bool bOn = NavSys->ProjectPointToNavigation(Spot.Value, Projected, FVector(200.0f, 200.0f, 400.0f));
			bAllOn &= bOn;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NAV-PROBE project %s %s -> %s"), Spot.Key, bOn ? TEXT("ON") : TEXT("OFF"),
				bOn ? *Projected.Location.ToString() : TEXT("none"));
		}
		if (!bAllOn)
		{
			NavSys->Tick(0.5f);
			FPlatformProcess::Sleep(0.5f);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NAV-PROBE coverage %s after %.1fs"), bAllOn ? TEXT("complete") : TEXT("INCOMPLETE"), FPlatformTime::Seconds() - CoverageStart);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NAV-PROBE navDataActors=%d buildInProgress=%d"), NavDataCount, NavSys->IsNavigationBuildInProgress() ? 1 : 0);

	const bool bBeach = Query(TEXT("field->beach"), Field, Beach);
	const bool bFort = Query(TEXT("field->fort"), Field, Fort);
	const bool bBack = Query(TEXT("fort->beach"), Fort, Beach);
	Query(TEXT("field->hillCentre"), Field, HillCentre);
	// edge diagnostics: which link is missing, the field side, the 40 uu step, or the hill's own mesh
	const FVector FieldNearHill(1000.0f, 2600.0f, 0.0f);
	const FVector HillEdge(1250.0f, 2600.0f, 40.0f);
	const FVector HillFar(3600.0f, 1400.0f, 40.0f);
	Query(TEXT("field->fieldNearHill"), Field, FieldNearHill);
	Query(TEXT("fieldNearHill->hillEdge"), FieldNearHill, HillEdge);
	Query(TEXT("hillEdge->fort"), HillEdge, Fort);
	Query(TEXT("fort->hillFar"), Fort, HillFar);
	Query(TEXT("beach->field"), Beach, Field);
	const bool bPass = bBeach && bFort && bBack;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-NAV-PROBE %s (beach=%d fort=%d back=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bBeach ? 1 : 0, bFort ? 1 : 0, bBack ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -ChaseProbe (2026-09-04): enemies must actually COME to the player now that the island has a
// navmesh. Teleports the pirate next to the +X spawner ring, then samples every living enemy's
// distance and speed for 12 s. PASS when at least one enemy closes by 300 uu or more and shows
// real ground speed; enemies that only turn in place (the five-week "Idling" island) fail it.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunChaseProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHASE-PROBE FAIL (no pawn)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// stand 900 uu inside the +X spawner (2800, 0): inside sight range, outside melee range
	PlayerPawn->SetActorLocation(FVector(1900.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
	ChaseProbeStartDistances.Reset();
	ChaseProbeBestClose = 0.0f;
	ChaseProbePeakSpeed = 0.0f;
	ChaseProbeSamples = 0;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			ChaseProbeStartDistances.Add(*It, static_cast<float>(FVector::Dist2D(It->GetActorLocation(), PlayerPawn->GetActorLocation())));
		}
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHASE-PROBE start enemies=%d player=%s"), ChaseProbeStartDistances.Num(), *PlayerPawn->GetActorLocation().ToString());

	World->GetTimerManager().SetTimer(ChaseProbeSampleTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		APawn* Player = InnerController ? InnerController->GetPawn() : nullptr;
		if (!Player)
		{
			return;
		}
		++ChaseProbeSamples;
		for (TPair<TWeakObjectPtr<ACombatEnemy>, float>& Entry : ChaseProbeStartDistances)
		{
			ACombatEnemy* Enemy = Entry.Key.Get();
			if (!Enemy)
			{
				continue;
			}
			const float Now = static_cast<float>(FVector::Dist2D(Enemy->GetActorLocation(), Player->GetActorLocation()));
			const float Speed = static_cast<float>(Enemy->GetVelocity().Size2D());
			ChaseProbeBestClose = FMath::Max(ChaseProbeBestClose, Entry.Value - Now);
			ChaseProbePeakSpeed = FMath::Max(ChaseProbePeakSpeed, Speed);
			if (ChaseProbeSamples % 3 == 0 && Entry.Value < 3500.0f)
			{
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHASE-PROBE t=%d %s dist %.0f -> %.0f speed=%.0f"),
					ChaseProbeSamples, *Enemy->EnemyRowName.ToString(), Entry.Value, Now, Speed);
			}
		}
	}), 1.0f, true);

	FTimerHandle VerdictTimer;
	World->GetTimerManager().SetTimer(VerdictTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckChaseProbe), 13.0f, false);
}

void UWrecktideRewardSubsystem::CheckChaseProbe()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChaseProbeSampleTimer);
	}
	const bool bClosed = ChaseProbeBestClose >= 300.0f;
	const bool bMoved = ChaseProbePeakSpeed >= 80.0f;
	const bool bPass = ChaseProbeStartDistances.Num() > 0 && bClosed && bMoved;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHASE-PROBE %s (enemies=%d bestClose=%.0f>=300 peakSpeed=%.0f>=80 samples=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), ChaseProbeStartDistances.Num(), ChaseProbeBestClose, ChaseProbePeakSpeed, ChaseProbeSamples);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -LeashProbe (2026-09-06): proves the +X guard stays at its post outside aggro range, acquires and
// closes on a nearby player, drops a distant target and walks home at full health, then re-acquires.
// Damage before the return keeps the health assertion non-vacuous.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunLeashProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!World || !PlayerPawn)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE FAIL (no world or player pawn)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const FVector ExpectedHome(2800.0f, 0.0f, 100.0f);
	ACombatEnemy* Target = nullptr;
	float BestHomeDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (!It->Stats || !It->Stats->IsAlive())
		{
			continue;
		}
		const float CandidateDistance = FVector::Dist2D(It->GetHomeLocation(), ExpectedHome);
		if (CandidateDistance < BestHomeDistance)
		{
			BestHomeDistance = CandidateDistance;
			Target = *It;
		}
	}

	if (!Target || BestHomeDistance > 500.0f)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE FAIL (no +X enemy nearest=%.0f)"), BestHomeDistance);
		ScheduleProbeQuit(2.0f);
		return;
	}

	LeashProbeEnemy = Target;
	LeashProbeStage = 1;
	bLeashProbeAllStagesOK = true;
	bLeashProbeStage1Idle = Target->GetAggroState() == EEnemyAggroState::Idle;
	LeashProbeStage1MaxHomeDistance = FVector::Distance(Target->GetActorLocation(), Target->GetHomeLocation());
	const FVector Home = Target->GetHomeLocation();
	PlayerPawn->SetActorLocation(FVector(Home.X - 2200.0f, Home.Y, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE stage1 begin enemy=%s row=%s home=%s aggro=%.0f leash=%.0f"),
		*Target->GetName(), *Target->EnemyRowName.ToString(), *Home.ToString(), Target->GetAggroRadius(), Target->GetLeashRadius());

	World->GetTimerManager().SetTimer(LeashProbeSampleTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::SampleLeashProbe), 0.25f, true);
	World->GetTimerManager().SetTimer(LeashProbeStageTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckLeashProbeStage1), 4.0f, false);
}

void UWrecktideRewardSubsystem::SampleLeashProbe()
{
	UWorld* World = GetWorld();
	ACombatEnemy* Enemy = LeashProbeEnemy.Get();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!World || !Enemy || !PlayerPawn)
	{
		bLeashProbeAllStagesOK = false;
		return;
	}

	const float HomeDistance = FVector::Dist2D(Enemy->GetActorLocation(), Enemy->GetHomeLocation());  // 2D: the post is recorded before the deferred spawn settles onto the ground
	const float PlayerDistance = FVector::Distance(Enemy->GetActorLocation(), PlayerPawn->GetActorLocation());
	switch (LeashProbeStage)
	{
	case 1:
		bLeashProbeStage1Idle &= Enemy->GetAggroState() == EEnemyAggroState::Idle;
		LeashProbeStage1MaxHomeDistance = FMath::Max(LeashProbeStage1MaxHomeDistance, HomeDistance);
		break;

	case 2:
		bLeashProbeStage2Chasing |= Enemy->GetAggroState() == EEnemyAggroState::Chasing;
		LeashProbeStage2MinPlayerDistance = FMath::Min(LeashProbeStage2MinPlayerDistance, PlayerDistance);
		break;

	case 3:
	{
		const float Elapsed = World->GetTimeSeconds() - LeashProbeStageStartTime;
		if (Elapsed <= Enemy->GetDeaggroSeconds() + 12.0f)
		{
			bLeashProbeStage3State |= Enemy->GetAggroState() == EEnemyAggroState::Returning
				|| Enemy->GetAggroState() == EEnemyAggroState::Idle;
		}
		const bool bFullHealth = Enemy->Stats
			&& FMath::IsNearlyEqual(Enemy->Stats->GetHealth(), Enemy->Stats->GetMaxHealth(), 0.01f);
		bLeashProbeStage3HomeHealthy |= bLeashProbeStage3Damaged
			&& HomeDistance <= 150.0f && bFullHealth;
		if (bLeashProbeStage3State && bLeashProbeStage3HomeHealthy
			&& Enemy->GetAggroState() == EEnemyAggroState::Idle)
		{
			CheckLeashProbeStage3();
		}
		break;
	}

	case 4:
		bLeashProbeStage4Chasing |= Enemy->GetAggroState() == EEnemyAggroState::Chasing;
		break;

	default:
		break;
	}
}

void UWrecktideRewardSubsystem::CheckLeashProbeStage1()
{
	ACombatEnemy* Enemy = LeashProbeEnemy.Get();
	const bool bPass = Enemy && bLeashProbeStage1Idle && LeashProbeStage1MaxHomeDistance <= 150.0f;
	bLeashProbeAllStagesOK &= bPass;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE stage1 %s (idle=%d maxHome=%.0f<=150)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bLeashProbeStage1Idle ? 1 : 0, LeashProbeStage1MaxHomeDistance);

	if (!Enemy || !GetWorld())
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE FAIL (stage1 lost enemy or world)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	LeashProbeStage = 2;
	bLeashProbeStage2Chasing = false;
	LeashProbeStage2MinPlayerDistance = 1000000.0f;
	const FVector Home = Enemy->GetHomeLocation();
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* PlayerPawn = PlayerController->GetPawn())
		{
			PlayerPawn->SetActorLocation(FVector(Home.X - 700.0f, Home.Y, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	GetWorld()->GetTimerManager().SetTimer(LeashProbeStageTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckLeashProbeStage2), 5.0f, false);
}

void UWrecktideRewardSubsystem::CheckLeashProbeStage2()
{
	UWorld* World = GetWorld();
	ACombatEnemy* Enemy = LeashProbeEnemy.Get();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	const bool bPass = Enemy && bLeashProbeStage2Chasing && LeashProbeStage2MinPlayerDistance < 400.0f;
	bLeashProbeAllStagesOK &= bPass;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE stage2 %s (chasing=%d minPlayer=%.0f<400)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bLeashProbeStage2Chasing ? 1 : 0, LeashProbeStage2MinPlayerDistance);

	if (!World || !Enemy || !PlayerPawn || !Enemy->Stats)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE FAIL (stage2 lost required actor)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Put real damage through the enemy path before kiting it, so full health at home proves healing.
	Enemy->ApplyDamage(10.0f, PlayerPawn, Enemy->GetActorLocation(), FVector::ZeroVector);
	bLeashProbeStage3Damaged = Enemy->Stats->GetHealth() < Enemy->Stats->GetMaxHealth();
	bLeashProbeStage3State = false;
	bLeashProbeStage3HomeHealthy = false;
	LeashProbeStage = 3;
	LeashProbeStageStartTime = World->GetTimeSeconds();
	const FVector Home = Enemy->GetHomeLocation();
	PlayerPawn->SetActorLocation(FVector(Home.X - 4000.0f, Home.Y, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
	World->GetTimerManager().SetTimer(LeashProbeStageTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckLeashProbeStage3), 20.0f, false);
}

void UWrecktideRewardSubsystem::CheckLeashProbeStage3()
{
	UWorld* World = GetWorld();
	ACombatEnemy* Enemy = LeashProbeEnemy.Get();
	if (World)
	{
		World->GetTimerManager().ClearTimer(LeashProbeStageTimer);
	}
	const float HomeDistance = Enemy ? FVector::Dist2D(Enemy->GetActorLocation(), Enemy->GetHomeLocation()) : 1000000.0f;
	const float Health = Enemy && Enemy->Stats ? Enemy->Stats->GetHealth() : -1.0f;
	const float MaxHealth = Enemy && Enemy->Stats ? Enemy->Stats->GetMaxHealth() : -1.0f;
	const bool bPass = Enemy && bLeashProbeStage3Damaged && bLeashProbeStage3State && bLeashProbeStage3HomeHealthy;
	bLeashProbeAllStagesOK &= bPass;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE stage3 %s (damaged=%d returnOrIdle=%d homeHealthy=%d home=%.0f health=%.0f/%.0f)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bLeashProbeStage3Damaged ? 1 : 0,
		bLeashProbeStage3State ? 1 : 0, bLeashProbeStage3HomeHealthy ? 1 : 0,
		HomeDistance, Health, MaxHealth);
	BeginLeashProbeStage4();
}

void UWrecktideRewardSubsystem::BeginLeashProbeStage4()
{
	UWorld* World = GetWorld();
	ACombatEnemy* Enemy = LeashProbeEnemy.Get();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!World || !Enemy || !PlayerPawn)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE FAIL (stage4 lost required actor)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	LeashProbeStage = 4;
	bLeashProbeStage4Chasing = false;
	const FVector Home = Enemy->GetHomeLocation();
	PlayerPawn->SetActorLocation(FVector(Home.X - 700.0f, Home.Y, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
	World->GetTimerManager().SetTimer(LeashProbeStageTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckLeashProbeStage4), 5.0f, false);
}

void UWrecktideRewardSubsystem::CheckLeashProbeStage4()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(LeashProbeSampleTimer);
	}
	const bool bPass = LeashProbeEnemy.IsValid() && bLeashProbeStage4Chasing;
	bLeashProbeAllStagesOK &= bPass;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE stage4 %s (chasing=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bLeashProbeStage4Chasing ? 1 : 0);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-LEASH-PROBE %s (stage1=%d stage2=%d stage3=%d stage4=%d)"),
		bLeashProbeAllStagesOK ? TEXT("PASS") : TEXT("FAIL"),
		(bLeashProbeStage1Idle && LeashProbeStage1MaxHomeDistance <= 150.0f) ? 1 : 0,
		(bLeashProbeStage2Chasing && LeashProbeStage2MinPlayerDistance < 400.0f) ? 1 : 0,
		(bLeashProbeStage3Damaged && bLeashProbeStage3State && bLeashProbeStage3HomeHealthy) ? 1 : 0,
		bLeashProbeStage4Chasing ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -SwimProbe (2026-09-06): the sea is a real water APhysicsVolume, so the engine's own
// MOVE_Swimming carries the pirate. Three stages, each with its own line:
//   float  - dropped into open water he ends up swimming at the surface, not falling forever
//   shore  - swimming at the beach gets him back onto land (the haul-out over the 80 uu lip)
//   pier   - stepping off the pier deck lands him in the water rather than in free fall
// Judge: WRECKTIDE-SWIM-PROBE PASS or FAIL.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunSwimProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE FAIL (no pawn)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// stage 1: drop him into open water 2000 uu west of the beach edge, from above the surface so
	// the entry itself is part of the test
	PlayerPawn->SetActorLocation(FVector(-9000.0f, -3000.0f, 60.0f), false, nullptr, ETeleportType::TeleportPhysics);
	PlayerController->SetControlRotation(FRotator(0.0f, 0.0f, 0.0f));
	SwimProbeDrive = FVector2D::ZeroVector;
	SwimProbeDeadline = World->GetTimeSeconds() + 6.0f;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE start at %s"), *PlayerPawn->GetActorLocation().ToString());

	World->GetTimerManager().SetTimer(SwimProbePollTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckSwimProbeFloat), 0.25f, true);
}

void UWrecktideRewardSubsystem::CheckSwimProbeFloat()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!World || !PlayerPawn)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE FAIL (pawn lost in stage float)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	SwimProbeFloatZ = static_cast<float>(PlayerPawn->GetActorLocation().Z);
	// Band note: the spec asked for [-200, -20]. The float equilibrium is arithmetic, not taste -
	// the CMC balances at ImmersionDepth = 1/Buoyancy, which for Buoyancy 1.12 and an 86 uu capsule
	// half height puts the capsule CENTRE at waterZ + 86 - 172/1.12 = -188 (head 18 uu clear of the
	// surface). The floor is -260 so the settle has room; anything below that is sinking.
	const bool bFloating = PlayerPawn->IsSwimming() && SwimProbeFloatZ >= -260.0f && SwimProbeFloatZ <= -20.0f;
	if (!bFloating && World->GetTimeSeconds() < SwimProbeDeadline)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(SwimProbePollTimer);
	bSwimProbeFloatOK = bFloating;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE float %s swimming=%d z=%.0f (band -260..-20)"),
		bSwimProbeFloatOK ? TEXT("OK") : TEXT("BAD"), PlayerPawn->IsSwimming() ? 1 : 0, SwimProbeFloatZ);

	// rig + camera evidence: the tilt and the camera lift are the two things a capture judges by eye,
	// so the probe prints the numbers behind them and a bad pose can never hide behind a green verdict
	if (const USkeletalMeshComponent* Rig = PlayerPawn->GetMesh())
	{
		const float CameraZ = PlayerController->PlayerCameraManager
			? static_cast<float>(PlayerController->PlayerCameraManager->GetCameraLocation().Z) : 0.0f;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE rig pitch=%.1f roll=%.1f yaw=%.1f relZ=%.1f socketZ=%.0f cameraZ=%.0f (water -120)"),
			Rig->GetRelativeRotation().Pitch, Rig->GetRelativeRotation().Roll, Rig->GetRelativeRotation().Yaw,
			Rig->GetRelativeLocation().Z,
			PlayerPawn->GetCameraBoom() ? PlayerPawn->GetCameraBoom()->SocketOffset.Z : 0.0f, CameraZ);
	}

	// stage 2: swim east at the beach and get out of the water
	SwimProbeDrive = FVector2D(0.0f, 1.0f);
	PlayerController->SetControlRotation(FRotator(0.0f, 0.0f, 0.0f));
	SwimProbeDeadline = World->GetTimeSeconds() + 15.0f;
	World->GetTimerManager().SetTimer(SwimProbeDriveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		if (ACombatCharacter* InnerPawn = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr)
		{
			InnerPawn->DoMove(SwimProbeDrive.X, SwimProbeDrive.Y);
		}
	}), 0.016f, true);
	World->GetTimerManager().SetTimer(SwimProbePollTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckSwimProbeShore), 0.5f, true);
}

void UWrecktideRewardSubsystem::CheckSwimProbeShore()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!World || !PlayerPawn)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE FAIL (pawn lost in stage shore)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	const UCharacterMovementComponent* Movement = PlayerPawn->GetCharacterMovement();
	const EMovementMode Mode = Movement ? Movement->MovementMode.GetValue() : MOVE_None;
	SwimProbeShoreZ = static_cast<float>(PlayerPawn->GetActorLocation().Z);
	const bool bAshore = Mode == MOVE_Walking && SwimProbeShoreZ >= -60.0f;
	if (!bAshore && World->GetTimeSeconds() < SwimProbeDeadline)
	{
		// the swim pose and the camera, sampled while he is actually swimming at speed: this is the
		// series a capture is judged against (tilt should reach the moving value, camera should sit
		// above the water plane at -120)
		if (const USkeletalMeshComponent* Rig = PlayerPawn->GetMesh())
		{
			const float CameraZ = PlayerController->PlayerCameraManager
				? static_cast<float>(PlayerController->PlayerCameraManager->GetCameraLocation().Z) : 0.0f;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE sample speed=%.0f rigRoll=%.1f socketZ=%.0f pawnZ=%.0f cameraZ=%.0f"),
				PlayerPawn->GetVelocity().Size2D(), Rig->GetRelativeRotation().Roll,
				PlayerPawn->GetCameraBoom() ? PlayerPawn->GetCameraBoom()->SocketOffset.Z : 0.0f,
				SwimProbeShoreZ, CameraZ);
		}
		return;
	}

	World->GetTimerManager().ClearTimer(SwimProbePollTimer);
	World->GetTimerManager().ClearTimer(SwimProbeDriveTimer);
	bSwimProbeShoreOK = bAshore;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE shore %s mode=%d z=%.0f x=%.0f (walking and z>=-60)"),
		bSwimProbeShoreOK ? TEXT("OK") : TEXT("BAD"), static_cast<int32>(Mode), SwimProbeShoreZ,
		PlayerPawn->GetActorLocation().X);

	RunSwimProbeStagePier();
}

void UWrecktideRewardSubsystem::RunSwimProbeStagePier()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!World || !PlayerPawn)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE FAIL (pawn lost before stage pier)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Stage 3 places him a step OFF the pier's east rail (deck is 600 wide at x +-300, rails at
	// +-310, deck top -40) at deck height over open water, facing down the pier, and drives him on.
	// The spec's own coordinates (on the deck, driving -Y) cannot leave the deck: island_dress.py
	// rails the pier on all three open sides, so that drive only ever walks into the end cap. This
	// is the same claim - leave the pier deck and you are swimming, not falling - at a spot the
	// level actually allows.
	PlayerPawn->SetActorLocation(FVector(420.0f, -8300.0f, 40.0f), false, nullptr, ETeleportType::TeleportPhysics);
	PlayerPawn->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
	PlayerController->SetControlRotation(FRotator(0.0f, -90.0f, 0.0f));
	SwimProbeDrive = FVector2D(0.0f, 1.0f);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE pier step-off at %s"), *PlayerPawn->GetActorLocation().ToString());

	World->GetTimerManager().SetTimer(SwimProbeDriveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UWorld* InnerWorld = GetWorld();
		APlayerController* InnerController = InnerWorld ? UGameplayStatics::GetPlayerController(InnerWorld, 0) : nullptr;
		if (ACombatCharacter* InnerPawn = InnerController ? Cast<ACombatCharacter>(InnerController->GetPawn()) : nullptr)
		{
			InnerPawn->DoMove(SwimProbeDrive.X, SwimProbeDrive.Y);
		}
	}), 0.016f, true);

	FTimerHandle PierTimer;
	World->GetTimerManager().SetTimer(PierTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishSwimProbe), 3.0f, false);
}

void UWrecktideRewardSubsystem::FinishSwimProbe()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(SwimProbeDriveTimer);
		World->GetTimerManager().ClearTimer(SwimProbePollTimer);
	}
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;

	bool bSwimmingNow = false;
	if (PlayerPawn)
	{
		SwimProbePierZ = static_cast<float>(PlayerPawn->GetActorLocation().Z);
		bSwimmingNow = PlayerPawn->IsSwimming();
	}
	// swimming AND still near the surface: the old bug was a four minute fall, so a pawn that is
	// hundreds of units below the water line has not been caught by the sea volume at all
	bSwimProbePierOK = bSwimmingNow && SwimProbePierZ >= -400.0f;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE pier %s swimming=%d z=%.0f"),
		bSwimProbePierOK ? TEXT("OK") : TEXT("BAD"), bSwimmingNow ? 1 : 0, SwimProbePierZ);

	const bool bPass = bSwimProbeFloatOK && bSwimProbeShoreOK && bSwimProbePierOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SWIM-PROBE %s (float=%d z=%.0f shore=%d z=%.0f pier=%d z=%.0f)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bSwimProbeFloatOK ? 1 : 0, SwimProbeFloatZ,
		bSwimProbeShoreOK ? 1 : 0, SwimProbeShoreZ,
		bSwimProbePierOK ? 1 : 0, SwimProbePierZ);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -MissionProbe (2026-09-04): the story spine end to end. Drives all seven missions through the
// giver's REAL menu path and the REAL progress hooks (NotifyEnemyKilled, the chest's Open, the
// boat's Board, the board's TurnIn, the 2 Hz location timer), asserting the persisted flags and an
// exact gold and XP delta on every payout. Split into phases only where the engine forces it: the
// ReachSpot leg waits on the location timer, the sailing leg waits on two possessions.
// ---------------------------------------------------------------------------------------------
namespace WrecktideMissionProbe
{
/** Cumulative XP. Raw GetSkillXP is NOT monotonic: AwardXP subtracts the cost of every level it
 *  grants, so a level-up mid probe would read as a NEGATIVE reward. */
static int64 TotalXP(const USkillComponent* Skill, EWrecktideSkill Which)
{
	if (!Skill)
	{
		return 0;
	}
	const int32 Level = Skill->GetSkillLevel(Which);
	int64 Total = Skill->GetSkillXP(Which);
	for (int32 PastLevel = 1; PastLevel < Level; ++PastLevel)
	{
		Total += Skill->GetXPToNext(PastLevel);
	}
	return Total;
}
}

bool UWrecktideRewardSubsystem::FireMissionProbeKill(FName EnemyRow)
{
	UWorld* World = GetWorld();
	UClass* EnemyClass = MissionProbeEnemyClass.Get();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* KillerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!World || !EnemyClass || !KillerPawn)
	{
		return false;
	}

	// Spawned exactly the way ScheduleRespawn does it (a live enemy's own class and transform),
	// because the C++ base class has no life-bar widget and its BeginPlay would fail the check on one.
	ACombatEnemy* ProbeEnemy = World->SpawnActorDeferred<ACombatEnemy>(
		EnemyClass,
		MissionProbeSpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!ProbeEnemy)
	{
		return false;
	}

	ProbeEnemy->EnemyRowName = EnemyRow;
	UGameplayStatics::FinishSpawningActor(ProbeEnemy, MissionProbeSpawnTransform);
	NotifyEnemyKilled(ProbeEnemy, KillerPawn);
	return true;
}

bool UWrecktideRewardSubsystem::OpenMissionProbeGiver()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktideMissionGiver* Giver = MissionProbeGiver.Get();
	if (!PlayerPawn || !Giver)
	{
		return false;
	}

	// Always from scratch: an already open menu returns early from OpenGiverMenu and would leave the
	// probe reading the line list from before the last kill.
	Giver->CloseGiverMenu();
	PlayerPawn->SetActorLocation(Giver->GetActorLocation() + Giver->GetActorForwardVector() * 120.0f + FVector(0.0f, 0.0f, 100.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	Giver->OpenGiverMenu(PlayerPawn);
	return Giver->IsMenuOpen();
}

namespace
{
/** The Upgrades.csv row that carries the Edge track, looked up the way the smithy does it. */
void DataSubsystem_UpgradeTrackLookup(const UWrecktideDataSubsystem* Data, FName& OutRow)
{
	OutRow = NAME_None;
	if (Data)
	{
		Data->GetUpgradeRowForTrack(EWrecktideUpgradeTrack::Edge, &OutRow);
	}
}
}

bool UWrecktideRewardSubsystem::OpenGiverForFlow(AWrecktideMissionGiver* Giver)
{
	MissionProbeGiver = Giver;
	return OpenMissionProbeGiver();
}

void UWrecktideRewardSubsystem::RunQuestFlowProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	// The four NPCs by NAME, never by "first actor found": four of them share one class now.
	AWrecktideMissionGiver* Bell = nullptr;
	AWrecktideMissionGiver* Jerold = nullptr;
	AWrecktideMissionGiver* Marta = nullptr;
	AWrecktideMissionGiver* Cobb = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
		{
			const FName Id = It->GetGiverId();
			if (Id == FName(TEXT("Bell"))) { Bell = *It; }
			else if (Id == FName(TEXT("Jerold"))) { Jerold = *It; }
			else if (Id == FName(TEXT("Marta"))) { Marta = *It; }
			else if (Id == FName(TEXT("Cobb"))) { Cobb = *It; }
		}
	}
	AWrecktideSmithStall* Smithy = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It)
		{
			Smithy = *It;
			break;
		}
	}

	if (!World || !PlayerPawn || !PlayerState || !Progress || !Economy || !Skill || !Data || !Bell || !Jerold || !Marta || !Cobb || !Smithy)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUESTFLOW-PROBE FAIL (wiring pawn=%d state=%d data=%d bell=%d jerold=%d marta=%d cobb=%d smithy=%d)"),
			PlayerPawn != nullptr, PlayerState != nullptr, Data != nullptr,
			Bell != nullptr, Jerold != nullptr, Marta != nullptr, Cobb != nullptr, Smithy != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// The kill recipe, captured once off a live enemy (the mission probe's recipe).
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			MissionProbeEnemyClass = It->GetClass();
			MissionProbeSpawnTransform = It->GetActorTransform();
			break;
		}
	}
	if (!MissionProbeEnemyClass.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUESTFLOW-PROBE FAIL (no live enemy to copy a kill from)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	auto Fail = [this](const FString& Why)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUESTFLOW-PROBE FAIL (%s)"), *Why);
		ScheduleProbeQuit(2.0f);
	};

	// One conversation with an NPC: open his menu, take the named action, close. Everything the
	// player would do with F, arrow keys and Enter.
	auto Converse = [this](AWrecktideMissionGiver* Giver, EMissionGiverAction Action) -> bool
	{
		if (!OpenGiverForFlow(Giver))
		{
			return false;
		}
		const bool bLine = Giver->SelectActionLine(Action);
		if (bLine)
		{
			Giver->ConfirmMenuSelection();
		}
		Giver->CloseGiverMenu();
		return bLine;
	};

	auto Offer = [Progress, Data](FName GiverId)
	{
		return WrecktideMissionLogic::GetOfferForGiver(Progress, Data, GiverId);
	};
	auto Done = [Progress](const TCHAR* Row)
	{
		return Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(FName(Row))) == 1;
	};

	// C30: what a still-locked NPC looks like from the player's side of the conversation. F on him
	// draws exactly two lines, his own words out of Npcs.csv and Close, with no Accept and no bench.
	auto BrushedOff = [this, Data](AWrecktideMissionGiver* Giver) -> bool
	{
		if (!Giver || !OpenGiverForFlow(Giver))
		{
			return false;
		}
		const FString Drawn = Giver->GetMenuLine(0);
		const int32 Lines = Giver->GetMenuLineCount();
		const bool bNoAccept = !Giver->SelectActionLine(EMissionGiverAction::Accept);
		const bool bNoBench = !Giver->SelectActionLine(EMissionGiverAction::Smithy);
		Giver->CloseGiverMenu();

		const FName Id = Giver->GetGiverId();
		const FString Expected = WrecktideMissionLogic::GetGiverBrushOff(Data, Id);
		const bool bOK = bNoAccept && bNoBench && Lines == 2 && !Expected.IsEmpty() && Drawn == Expected;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW brushoff id=%s ok=%d lines=%d line=%s"),
			*Id.ToString(), bOK ? 1 : 0, Lines, *Drawn);
		return bOK;
	};

	// The GiverIds the journal is advertising under On offer, sorted and joined with +. A locked NPC
	// must never appear in it: the journal reads the same offer rule the menu does.
	auto OfferedGivers = [Data, PlayerState]() -> FString
	{
		TArray<WrecktideMissionLogic::FMissionJournalEntry> Entries;
		WrecktideMissionLogic::GetJournalEntries(PlayerState, Entries);
		TArray<FString> Ids;
		for (const WrecktideMissionLogic::FMissionJournalEntry& Entry : Entries)
		{
			if (Entry.State != WrecktideMissionLogic::EMissionEntryState::Available)
			{
				continue;
			}
			const FMissionTableRow* OfferRow = Data->GetMissionRow(Entry.Row);
			const FName OfferGiver = (OfferRow && !OfferRow->GiverId.IsNone()) ? OfferRow->GiverId : FName(TEXT("Bell"));
			Ids.AddUnique(OfferGiver.ToString());
		}
		Ids.Sort();
		return FString::Join(Ids, TEXT("+"));
	};

	// Every interact surface in this game carries a 300 uu trigger, so two of them closer than
	// 600 uu would put the player inside two menus at once. Jerold and the smithy bench are the ONE
	// authored exception, which is why this takes something to ignore.
	auto NearestInteract = [World](const AActor* Subject, const AActor* Ignore, FString& OutWhat) -> float
	{
		float Nearest = TNumericLimits<float>::Max();
		OutWhat.Reset();
		auto Consider = [&Nearest, &OutWhat, Subject, Ignore](const AActor* Other)
		{
			if (!Other || Other == Subject || Other == Ignore)
			{
				return;
			}
			const float Distance = static_cast<float>(FVector::Dist2D(Subject->GetActorLocation(), Other->GetActorLocation()));
			if (Distance < Nearest)
			{
				Nearest = Distance;
				OutWhat = Other->GetClass()->GetName();
			}
		};
		for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It) { Consider(*It); }
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It) { Consider(*It); }
		for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It) { Consider(*It); }
		for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It) { Consider(*It); }
		for (TActorIterator<AWrecktideShipwright> It(World); It; ++It) { Consider(*It); }
		for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It) { Consider(*It); }
		for (TActorIterator<AWrecktideSupplyBarrel> It(World); It; ++It) { Consider(*It); }
		return Nearest;
	};

	// STAGE 1: the table. Every Order unique, every giver present, the new columns parsed.
	const TArray<TPair<FName, const FMissionTableRow*>> Rows = Data->GetMissionRows();
	TSet<int32> SeenOrders;
	int32 Repeatables = 0;
	bool bOrdersUnique = true;
	for (const TPair<FName, const FMissionTableRow*>& Entry : Rows)
	{
		bOrdersUnique &= !SeenOrders.Contains(Entry.Value->Order);
		SeenOrders.Add(Entry.Value->Order);
		Repeatables += Entry.Value->bRepeatable ? 1 : 0;
	}
	const FMissionTableRow* M08Row = Data->GetMissionRow(FName(TEXT("M08_SteelThatBites")));
	// Repeatables was pinned at 8, the number Wreck Cove's four NPCs ship. Ada Kell adds two on
	// Gallows Reach (C34, 2026-09-07) and every later island will add more, so the check is now a
	// floor: it still catches a bRepeatable column that failed to parse (which would read 0) while
	// leaving content free to grow.
	const bool bTableOK = Rows.Num() >= 26 && bOrdersUnique && Repeatables >= 8
		&& M08Row && M08Row->Type == EMissionObjective::TalkTo && M08Row->TargetName == FName(TEXT("Jerold"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage1 rows=%d uniqueOrders=%d repeatables=%d m08TalkTo=%d"),
		Rows.Num(), bOrdersUnique ? 1 : 0, Repeatables, (M08Row && M08Row->Type == EMissionObjective::TalkTo) ? 1 : 0);
	if (!bTableOK)
	{
		Fail(FString::Printf(TEXT("stage1 rows=%d unique=%d repeatables=%d"), Rows.Num(), bOrdersUnique ? 1 : 0, Repeatables));
		return;
	}

	// STAGE 2: the chapter gate. M01 to M06 are fast forwarded by flag (the shipped -MissionProbe
	// already proves those seven links through their real paths, and this probe is about what
	// happens AFTER them), then M07 runs for real: accepted at Bell's menu, killed through
	// NotifyEnemyKilled, paid out at Bell's menu. One kill must open three chains at once.
	static const TCHAR* EarlyRows[] = { TEXT("M01_Wake"), TEXT("M02_FirstSteel"), TEXT("M03_CampStash"),
		TEXT("M04_Redcoats"), TEXT("M05_Bounty"), TEXT("M06_SetSail") };
	for (const TCHAR* Row : EarlyRows)
	{
		Progress->SetQuestFlag(WrecktideMissionLogic::DoneKey(FName(Row)), 1);
	}
	Progress->SetQuestFlag(FName(TEXT("Story_Sailed")), 1);

	const bool bAcceptM07 = Converse(Bell, EMissionGiverAction::Accept);
	const bool bM07Active = WrecktideMissionLogic::IsMissionActive(Progress, FName(TEXT("M07_BoneCaptain")));
	const FName GateBefore = Offer(FName(TEXT("Jerold")));
	FireMissionProbeKill(FName(TEXT("SkeletonCaptain")));
	const bool bCompleteM07 = Converse(Bell, EMissionGiverAction::Complete);
	const int32 StoryComplete = Progress->GetQuestFlag(FName(TEXT("Story_Complete")));
	const FName BellOffer = Offer(FName(TEXT("Bell")));
	const FName JeroldOffer = Offer(FName(TEXT("Jerold")));
	const FName MartaOffer = Offer(FName(TEXT("Marta")));
	const FName CobbOffer = Offer(FName(TEXT("Cobb")));
	// C30 (2026-09-06): the Bone Captain is BELL's chapter gate and nobody else's. The other three
	// brush the player off until their own turn comes, and the journal advertises Bell alone.
	const bool bJeroldBrush = BrushedOff(Jerold);
	const bool bMartaBrush = BrushedOff(Marta);
	const bool bCobbBrush = BrushedOff(Cobb);
	const FString GateOffered = OfferedGivers();
	const bool bGateOK = bAcceptM07 && bM07Active && bCompleteM07 && StoryComplete == 1
		&& GateBefore.IsNone()
		&& BellOffer == FName(TEXT("M08_SteelThatBites"))
		&& JeroldOffer.IsNone() && MartaOffer.IsNone() && CobbOffer.IsNone()
		&& bJeroldBrush && bMartaBrush && bCobbBrush
		&& GateOffered == TEXT("Bell");
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage2 gate accept=%d complete=%d storyComplete=%d before=%s bell=%s jerold=%s marta=%s cobb=%s brush=%d%d%d onOffer=%s"),
		bAcceptM07 ? 1 : 0, bCompleteM07 ? 1 : 0, StoryComplete, *GateBefore.ToString(),
		*BellOffer.ToString(), *JeroldOffer.ToString(), *MartaOffer.ToString(), *CobbOffer.ToString(),
		bJeroldBrush ? 1 : 0, bMartaBrush ? 1 : 0, bCobbBrush ? 1 : 0, *GateOffered);
	if (!bGateOK)
	{
		Fail(TEXT("stage2 the Bone Captain did not leave Bell holding the thread with the other three brushing the player off"));
		return;
	}

	// STAGE 2b: WHERE they stand (C30.3). Spread across the island, and every one of them more than
	// 600 uu from any other interact trigger, so no two menus can ever be in range at once.
	const float MartaFromBell = static_cast<float>(FVector::Dist2D(Marta->GetActorLocation(), Bell->GetActorLocation()));
	const float CobbFromBell = static_cast<float>(FVector::Dist2D(Cobb->GetActorLocation(), Bell->GetActorLocation()));
	FString BellNear;
	FString JeroldNear;
	FString MartaNear;
	FString CobbNear;
	const float BellClear = NearestInteract(Bell, nullptr, BellNear);
	const float JeroldClear = NearestInteract(Jerold, Smithy, JeroldNear);
	const float MartaClear = NearestInteract(Marta, nullptr, MartaNear);
	const float CobbClear = NearestInteract(Cobb, nullptr, CobbNear);

	// Two NPC menus in range at once is the failure that actually costs the player a keypress, so
	// every giver PAIR is asserted, all four of them.
	float NpcPairClosest = TNumericLimits<float>::Max();
	for (TActorIterator<AWrecktideMissionGiver> Outer(World); Outer; ++Outer)
	{
		for (TActorIterator<AWrecktideMissionGiver> Inner(World); Inner; ++Inner)
		{
			if (*Outer != *Inner)
			{
				NpcPairClosest = FMath::Min(NpcPairClosest,
					static_cast<float>(FVector::Dist2D(Outer->GetActorLocation(), Inner->GetActorLocation())));
			}
		}
	}

	AWrecktideBoat* FlowBoat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		FlowBoat = *It;
		break;
	}
	const float CobbFromBoat = FlowBoat ? static_cast<float>(FVector::Dist2D(Cobb->GetActorLocation(), FlowBoat->GetActorLocation())) : 0.0f;
	// Cobb's own trigger is 300 uu, so the player standing anywhere in it must still be outside the
	// boat's board range: that is the "keep him clear of the boarding range" claim, measured.
	// Bell's and Jerold's clearances are MEASURED AND LOGGED but not asserted, and that is a
	// deliberate, documented gap: Bell has stood 515 uu from the market stall and Jerold 309 uu from
	// the camp chest since missions v0, both of which predate C30 and neither of which this lane
	// placed. Asserting them here would fail on somebody else's camp; hiding the numbers would let
	// the whole rule rot. The two spots C30 DID author are asserted hard.
	const bool bSpreadOK = MartaFromBell > 2500.0f && CobbFromBell > 1500.0f
		&& MartaClear > 600.0f && CobbClear > 600.0f && NpcPairClosest > 600.0f
		&& FlowBoat != nullptr && CobbFromBoat > FlowBoat->BoardRange + 300.0f;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage2b spread martaFromBell=%.0f cobbFromBell=%.0f clear marta=%.0f(%s) cobb=%.0f(%s) npcPair=%.0f cobbToBoat=%.0f board=%.0f | camp bell=%.0f(%s) jerold=%.0f(%s)"),
		MartaFromBell, CobbFromBell, MartaClear, *MartaNear, CobbClear, *CobbNear, NpcPairClosest,
		CobbFromBoat, FlowBoat ? FlowBoat->BoardRange : 0.0f,
		BellClear, *BellNear, JeroldClear, *JeroldNear);
	if (!bSpreadOK)
	{
		Fail(TEXT("stage2b the givers are not spread out, or one stands inside another interact trigger"));
		return;
	}

	// STAGE 3: the hand-off. M08 is a TalkTo: accepted at Bell, it pays out the moment Jerold's menu
	// opens, and Jerold offers his own first job in the same conversation.
	const bool bAcceptM08 = Converse(Bell, EMissionGiverAction::Accept);
	const bool bTrackedM08 = WrecktideMissionLogic::GetTrackedMissionRow(Progress, Data) == FName(TEXT("M08_SteelThatBites"));
	const bool bJeroldMenu = OpenGiverForFlow(Jerold);
	const bool bM08Done = Done(TEXT("M08_SteelThatBites"));
	const bool bJeroldOffersJ01 = Jerold->GetSelectedAction() == EMissionGiverAction::Accept
		|| Jerold->SelectActionLine(EMissionGiverAction::Accept);
	Jerold->CloseGiverMenu();
	// C30: M08 opens JEROLD. Marta and Cobb are still not talking, and the journal still hides them.
	const bool bMartaLockedAfterM08 = BrushedOff(Marta);
	const bool bCobbLockedAfterM08 = BrushedOff(Cobb);
	const FString HandoffOffered = OfferedGivers();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage3 handoff accept=%d tracked=%d menu=%d m08done=%d j01offered=%d martaLocked=%d cobbLocked=%d onOffer=%s"),
		bAcceptM08 ? 1 : 0, bTrackedM08 ? 1 : 0, bJeroldMenu ? 1 : 0, bM08Done ? 1 : 0, bJeroldOffersJ01 ? 1 : 0,
		bMartaLockedAfterM08 ? 1 : 0, bCobbLockedAfterM08 ? 1 : 0, *HandoffOffered);
	if (!bAcceptM08 || !bTrackedM08 || !bJeroldMenu || !bM08Done || !bJeroldOffersJ01
		|| !bMartaLockedAfterM08 || !bCobbLockedAfterM08 || HandoffOffered != TEXT("Bell+Jerold"))
	{
		Fail(TEXT("stage3 the hand-off did not open Jerold alone and leave Marta and Cobb brushing the player off"));
		return;
	}

	// STAGE 4: Jerold's chain. Upgrade, KillRow, OpenChest, then the TalkTo that hands you to Marta.
	Economy->ApplyGoldDelta(5000, EGoldSource::Earned, EGoldReason::StoreGoldGrant, FName(TEXT("QuestFlowProbe")));
	// A weapon he owns and a real track row name, exactly the way -SmithProbe arms the bench: the
	// smithy only forges owned weapons and the track rows are named by the Upgrades table.
	const FName CutlassRow(TEXT("WornCutlass"));
	Progress->UnlockItem(CutlassRow);
	PlayerState->EquipWeapon(CutlassRow);
	FName EdgeTrackRow = NAME_None;
	DataSubsystem_UpgradeTrackLookup(Data, EdgeTrackRow);
	const bool bAcceptJ01 = Converse(Jerold, EMissionGiverAction::Accept);
	const bool bBought = !EdgeTrackRow.IsNone() && Smithy->BuyUpgrade(CutlassRow, EdgeTrackRow);
	const bool bJ01Ready = WrecktideMissionLogic::IsRowReadyToComplete(Progress, Data, FName(TEXT("J01_BladeWorthKeeping")));
	const bool bJ01Done = Converse(Jerold, EMissionGiverAction::Complete) && Done(TEXT("J01_BladeWorthKeeping"));

	Converse(Jerold, EMissionGiverAction::Accept);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FireMissionProbeKill(FName(TEXT("SkeletonDeckhand")));
	}
	const bool bJ02Done = Converse(Jerold, EMissionGiverAction::Complete) && Done(TEXT("J02_BoneDust"));

	Converse(Jerold, EMissionGiverAction::Accept);
	WrecktideMissionLogic::NotifyChestOpened(PlayerState, FName(TEXT("WreckChest")));
	const bool bJ03Done = Converse(Jerold, EMissionGiverAction::Complete) && Done(TEXT("J03_WrecksIron"));

	const bool bAcceptJ04 = Converse(Jerold, EMissionGiverAction::Accept);
	const bool bMartaMenu = OpenGiverForFlow(Marta);
	// The same hand-off shape as Bell to Jerold: J04 pays out the moment her menu opens, the flag it
	// sets is what unlocks her, and she offers her own first job in that same conversation.
	const bool bMartaOffersT01 = Marta->GetSelectedAction() == EMissionGiverAction::Accept
		|| Marta->SelectActionLine(EMissionGiverAction::Accept);
	Marta->CloseGiverMenu();
	const bool bJ04Done = Done(TEXT("J04_MartasOrder"));
	const bool bMartaOpen = Offer(FName(TEXT("Marta"))) == FName(TEXT("T01_RumRunners"));
	const bool bCobbLockedAfterJ04 = BrushedOff(Cobb);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage4 jerold j01=%d(buy=%d ready=%d) j02=%d j03=%d j04=%d(accept=%d marta=%d t01=%d open=%d cobbLocked=%d)"),
		bJ01Done ? 1 : 0, bBought ? 1 : 0, bJ01Ready ? 1 : 0, bJ02Done ? 1 : 0, bJ03Done ? 1 : 0,
		bJ04Done ? 1 : 0, bAcceptJ04 ? 1 : 0, bMartaMenu ? 1 : 0, bMartaOffersT01 ? 1 : 0,
		bMartaOpen ? 1 : 0, bCobbLockedAfterJ04 ? 1 : 0);
	if (!bAcceptJ01 || !bBought || !bJ01Ready || !bJ01Done || !bJ02Done || !bJ03Done || !bJ04Done
		|| !bMartaOffersT01 || !bMartaOpen || !bCobbLockedAfterJ04)
	{
		Fail(TEXT("stage4 Jerold's chain did not run to a Marta hand-off that opens her and leaves Cobb locked"));
		return;
	}

	// STAGE 5: Marta. KillRow, a ReachSpot that must be RETURNED to her, then a Pay.
	Converse(Marta, EMissionGiverAction::Accept);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FireMissionProbeKill(FName(TEXT("Cutthroat")));
	}
	const bool bT01Done = Converse(Marta, EMissionGiverAction::Complete) && Done(TEXT("T01_RumRunners"));

	Converse(Marta, EMissionGiverAction::Accept);
	FVector BoatSpot = FVector::ZeroVector;
	const FMissionTableRow* T02Row = Data->GetMissionRow(FName(TEXT("T02_EmptyBarrels")));
	const bool bBoatSpot = T02Row && WrecktideMissionLogic::ResolveSpotLocation(this, *T02Row, BoatSpot);
	WrecktideMissionLogic::NotifyLocation(PlayerState, BoatSpot);
	// The return visit rule: standing there counts the objective but must NOT pay it out.
	const bool bT02Held = !Done(TEXT("T02_EmptyBarrels"))
		&& WrecktideMissionLogic::IsRowReadyToComplete(Progress, Data, FName(TEXT("T02_EmptyBarrels")));
	const bool bT02Done = Converse(Marta, EMissionGiverAction::Complete) && Done(TEXT("T02_EmptyBarrels"));
	// Two of her three done and Cobb is STILL not talking: only her last job opens the harbour.
	const bool bCobbLockedBeforeT03 = BrushedOff(Cobb);

	Converse(Marta, EMissionGiverAction::Accept);
	const int64 GoldBeforePay = Economy->GetGoldTotal();
	const bool bPayLine = Converse(Marta, EMissionGiverAction::Pay);
	const int64 GoldAfterPay = Economy->GetGoldTotal();
	const bool bT03Done = Converse(Marta, EMissionGiverAction::Complete) && Done(TEXT("T03_RoundForCrew"));
	const bool bCobbOpenAfterT03 = Offer(FName(TEXT("Cobb"))) == FName(TEXT("H01_CastOff"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage5 marta t01=%d t02=%d(spot=%d held=%d) t03=%d pay=%d gold %lld->%lld cobbLocked=%d cobbOpen=%d"),
		bT01Done ? 1 : 0, bT02Done ? 1 : 0, bBoatSpot ? 1 : 0, bT02Held ? 1 : 0, bT03Done ? 1 : 0,
		bPayLine ? 1 : 0, GoldBeforePay, GoldAfterPay, bCobbLockedBeforeT03 ? 1 : 0, bCobbOpenAfterT03 ? 1 : 0);
	if (!bT01Done || !bBoatSpot || !bT02Held || !bT02Done || !bPayLine || (GoldBeforePay - GoldAfterPay) != 40 || !bT03Done
		|| !bCobbLockedBeforeT03 || !bCobbOpenAfterT03)
	{
		Fail(TEXT("stage5 Marta's chain did not run, the 40 gold round did not leave the purse, or her last job did not open Cobb"));
		return;
	}

	// STAGE 6: Cobb. BoardBoat, a ReachSpot at sea, a bounty turn in.
	Converse(Cobb, EMissionGiverAction::Accept);
	WrecktideMissionLogic::NotifyBoarded(PlayerState);
	const bool bH01Done = Converse(Cobb, EMissionGiverAction::Complete) && Done(TEXT("H01_CastOff"));

	Converse(Cobb, EMissionGiverAction::Accept);
	const FMissionTableRow* H02Row = Data->GetMissionRow(FName(TEXT("H02_FarRock")));
	FVector RockSpot = FVector::ZeroVector;
	const bool bRockSpot = H02Row && WrecktideMissionLogic::ResolveSpotLocation(this, *H02Row, RockSpot);
	WrecktideMissionLogic::NotifyLocation(PlayerState, RockSpot);
	const bool bH02Done = Converse(Cobb, EMissionGiverAction::Complete) && Done(TEXT("H02_FarRock"));

	Converse(Cobb, EMissionGiverAction::Accept);
	WrecktideMissionLogic::NotifyBountyTurnedIn(PlayerState, FName(TEXT("CutthroatBounty")));
	const bool bH03Done = Converse(Cobb, EMissionGiverAction::Complete) && Done(TEXT("H03_PaidInFull"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage6 cobb h01=%d h02=%d(spot=%d) h03=%d"),
		bH01Done ? 1 : 0, bH02Done ? 1 : 0, bRockSpot ? 1 : 0, bH03Done ? 1 : 0);
	if (!bH01Done || !bRockSpot || !bH02Done || !bH03Done)
	{
		Fail(TEXT("stage6 Cobb's chain H01 to H03 did not run"));
		return;
	}

	// STAGE 6b (C34, 2026-09-07): H04_GallowsReach is Cobb's FOURTH link and the door to the second
	// island. It has to run here, or stage 7 would ask a man who is still mid-story for casual work
	// and get a story row back (a repeatable is only offered once every one of his own rows is
	// done). The same ReachSpot path H02 uses, then Ada Kell must wake on the other island.
	const FName H04Offer = Offer(FName(TEXT("Cobb")));
	Converse(Cobb, EMissionGiverAction::Accept);
	const FMissionTableRow* H04Row = Data->GetMissionRow(FName(TEXT("H04_GallowsReach")));
	FVector LandingSpot = FVector::ZeroVector;
	const bool bLandingSpot = H04Row && WrecktideMissionLogic::ResolveSpotLocation(this, *H04Row, LandingSpot);
	WrecktideMissionLogic::NotifyLocation(PlayerState, LandingSpot);
	const bool bH04Done = Converse(Cobb, EMissionGiverAction::Complete) && Done(TEXT("H04_GallowsReach"));
	const int32 ReachFlag = Progress->GetQuestFlag(FName(TEXT("Story_Reach")));
	const FName AdaFirst = Offer(FName(TEXT("Ada")));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage6b reach offer=%s spot=%d(%s) done=%d storyReach=%d ada=%s"),
		*H04Offer.ToString(), bLandingSpot ? 1 : 0, *LandingSpot.ToString(), bH04Done ? 1 : 0,
		ReachFlag, *AdaFirst.ToString());
	if (H04Offer != FName(TEXT("H04_GallowsReach")) || !bLandingSpot || !bH04Done || ReachFlag != 1
		|| AdaFirst != FName(TEXT("R01_ThinTheWatch")))
	{
		Fail(TEXT("stage6b Cobb's H04 did not run, or it did not open Ada Kell on Gallows Reach"));
		return;
	}

	// STAGE 7: repeatable work and its cooldown, at Cobb. Complete one, the next is offered, force
	// the cooldown on and he has nothing, clear it and it comes back. The clock is rewound by
	// writing the flag rather than waiting three minutes; the RULE that reads it is the shipped one.
	const FName Work1 = Offer(FName(TEXT("Cobb")));
	Converse(Cobb, EMissionGiverAction::Accept);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FireMissionProbeKill(FName(TEXT("NavyCadet")));
	}
	const int64 GoldBeforeWork = Economy->GetGoldTotal();
	const bool bWorkPaid = Converse(Cobb, EMissionGiverAction::Complete);
	const int64 WorkPay = Economy->GetGoldTotal() - GoldBeforeWork;
	const int32 WorkCount = Progress->GetQuestFlag(WrecktideMissionLogic::CountKey(Work1));
	const FName Work2 = Offer(FName(TEXT("Cobb")));

	// Notoriety scaling: base 22 gold at level L pays round(22 * (1 + 0.1 * L)).
	const int32 Level = FMath::Max(Skill->GetNotorietyLevel(), 1);
	const FMissionTableRow* Work1Row = Data->GetMissionRow(Work1);
	const int64 ExpectedPay = Work1Row ? FMath::RoundToInt64(static_cast<double>(Work1Row->RewardGold) * (1.0 + 0.1 * static_cast<double>(Level))) : -1;

	Progress->SetQuestFlag(WrecktideMissionLogic::CooldownKey(Work2), static_cast<int32>(FDateTime::UtcNow().ToUnixTimestamp()) + 3600);
	const FName WorkCooling = Offer(FName(TEXT("Cobb")));
	Progress->SetQuestFlag(WrecktideMissionLogic::CooldownKey(Work2), 0);
	const FName WorkBack = Offer(FName(TEXT("Cobb")));
	const bool bWorkOK = !Work1.IsNone() && bWorkPaid && WorkCount == 1 && Work2 != Work1 && !Work2.IsNone()
		&& WorkCooling.IsNone() && WorkBack == Work2 && WorkPay == ExpectedPay && ExpectedPay > 0;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage7 work first=%s count=%d pay=%lld expected=%lld(level %d) next=%s cooling=%s back=%s"),
		*Work1.ToString(), WorkCount, WorkPay, ExpectedPay, Level, *Work2.ToString(), *WorkCooling.ToString(), *WorkBack.ToString());
	if (!bWorkOK)
	{
		Fail(TEXT("stage7 repeatable work did not rotate, pay on the notoriety curve and honour its cooldown"));
		return;
	}

	// STAGE 8: Deliver. Bell sets an errand at Jerold; talking to Jerold counts it, and only Bell
	// pays it. Plus the journal: it must show the active job as tracked and name its giver.
	// Bell's two templates are tied at zero runs, and the tie breaks on Order, so his kill errand
	// comes first. Run it out of the way so the DELIVER template is the least run one and this stage
	// actually exercises the type it is here to prove.
	Converse(Bell, EMissionGiverAction::Accept);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FireMissionProbeKill(FName(TEXT("Cutthroat")));
	}
	Converse(Bell, EMissionGiverAction::Complete);

	const bool bAcceptWork = Converse(Bell, EMissionGiverAction::Accept);
	const FName BellWork = Offer(FName(TEXT("Bell")));
	bool bDeliverRan = false;
	if (BellWork == FName(TEXT("W02_BellsErrand")))
	{
		OpenGiverForFlow(Jerold);
		Jerold->CloseGiverMenu();
		const bool bCounted = WrecktideMissionLogic::IsRowReadyToComplete(Progress, Data, BellWork);
		const bool bNotPaidAtJerold = Progress->GetQuestFlag(WrecktideMissionLogic::CountKey(BellWork)) == 0;
		const bool bPaidAtBell = Converse(Bell, EMissionGiverAction::Complete)
			&& Progress->GetQuestFlag(WrecktideMissionLogic::CountKey(BellWork)) == 1;
		bDeliverRan = bCounted && bNotPaidAtJerold && bPaidAtBell;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage8 deliver row=%s counted=%d heldAtTarget=%d paidAtGiver=%d"),
			*BellWork.ToString(), bCounted ? 1 : 0, bNotPaidAtJerold ? 1 : 0, bPaidAtBell ? 1 : 0);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUESTFLOW stage8 Bell offered %s, not the Deliver template"), *BellWork.ToString());
	}
	if (!bDeliverRan)
	{
		Fail(TEXT("stage8 the Deliver errand did not count at the target NPC and pay at its giver"));
		return;
	}

	// Journal: one read API, and it must agree with the flags.
	Converse(Marta, EMissionGiverAction::Accept);
	TArray<WrecktideMissionLogic::FMissionJournalEntry> Journal;
	WrecktideMissionLogic::GetJournalEntries(PlayerState, Journal);
	int32 ActiveEntries = 0;
	int32 TrackedEntries = 0;
	int32 AvailableEntries = 0;
	int32 CompletedEntries = 0;
	FString TrackedGiver;
	for (const WrecktideMissionLogic::FMissionJournalEntry& Entry : Journal)
	{
		ActiveEntries += Entry.State == WrecktideMissionLogic::EMissionEntryState::Active ? 1 : 0;
		AvailableEntries += Entry.State == WrecktideMissionLogic::EMissionEntryState::Available ? 1 : 0;
		CompletedEntries += Entry.State == WrecktideMissionLogic::EMissionEntryState::Completed ? 1 : 0;
		if (Entry.bTracked)
		{
			++TrackedEntries;
			TrackedGiver = Entry.Giver;
		}
	}
	// C30: On offer may only advertise NPCs whose turn has come. By now all four are unlocked, so
	// this reads 1; the stage 2 and stage 3 assertions are the ones that catch a leak.
	bool bOnOfferUnlocked = true;
	for (const WrecktideMissionLogic::FMissionJournalEntry& Entry : Journal)
	{
		if (Entry.State != WrecktideMissionLogic::EMissionEntryState::Available)
		{
			continue;
		}
		const FMissionTableRow* OfferRow = Data->GetMissionRow(Entry.Row);
		const FName OfferGiver = (OfferRow && !OfferRow->GiverId.IsNone()) ? OfferRow->GiverId : FName(TEXT("Bell"));
		bOnOfferUnlocked &= WrecktideMissionLogic::IsGiverUnlocked(Progress, Data, OfferGiver);
	}

	const FName TrackedRow = WrecktideMissionLogic::GetTrackedMissionRow(Progress, Data);
	const bool bJournalOK = ActiveEntries >= 1 && TrackedEntries == 1 && AvailableEntries >= 1
		&& CompletedEntries >= 14 && !TrackedGiver.IsEmpty() && bOnOfferUnlocked;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage8 journal entries=%d active=%d tracked=%d(%s giver=%s) available=%d completed=%d onOffer=%s unlockedOnly=%d"),
		Journal.Num(), ActiveEntries, TrackedEntries, *TrackedRow.ToString(), *TrackedGiver, AvailableEntries, CompletedEntries,
		*OfferedGivers(), bOnOfferUnlocked ? 1 : 0);
	if (!bAcceptWork || !bJournalOK)
	{
		Fail(TEXT("stage8 the journal did not report one tracked active mission with its giver, or it advertised a locked NPC"));
		return;
	}

	// Tracking API: two jobs from two different NPCs at once, then move the beacon between them.
	// This is the whole point of model v2, so the probe holds two before it asserts.
	Converse(Cobb, EMissionGiverAction::Accept);
	TArray<FName> ActiveRows;
	WrecktideMissionLogic::GetActiveMissionRows(Progress, Data, ActiveRows);
	bool bTrackMoved = ActiveRows.Num() >= 2;
	if (bTrackMoved)
	{
		const FName Other = ActiveRows[0] == TrackedRow ? ActiveRows[1] : ActiveRows[0];
		bTrackMoved = WrecktideMissionLogic::SetTrackedMission(PlayerState, Other)
			&& WrecktideMissionLogic::GetTrackedMissionRow(Progress, Data) == Other;
	}
	const bool bRefuseUnknown = !WrecktideMissionLogic::SetTrackedMission(PlayerState, FName(TEXT("M01_Wake")));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW stage8 tracking active=%d moved=%d refusesInactive=%d"),
		ActiveRows.Num(), bTrackMoved ? 1 : 0, bRefuseUnknown ? 1 : 0);
	if (!bTrackMoved || !bRefuseUnknown)
	{
		Fail(TEXT("stage8 tracking did not move between two active missions, or accepted an inactive one"));
		return;
	}

	const int32 BrushOffs = (bJeroldBrush ? 1 : 0) + (bMartaBrush ? 1 : 0) + (bCobbBrush ? 1 : 0)
		+ (bMartaLockedAfterM08 ? 1 : 0) + (bCobbLockedAfterM08 ? 1 : 0)
		+ (bCobbLockedAfterJ04 ? 1 : 0) + (bCobbLockedBeforeT03 ? 1 : 0);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUESTFLOW-PROBE PASS (gate=strict brushoffs=%d spread=1 handoff=1 jerold=4 marta=3 cobb=3 work=1 pay=%lld deliver=%d journal=%d active=%d track=1)"),
		BrushOffs, WorkPay, bDeliverRan ? 1 : 0, Journal.Num(), ActiveRows.Num());
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunMissionProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	const USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	// There are four givers in the camp since C22, so this probe must name the one it means:
	// the story chain M01..M07 belongs to Bell and picking "the first actor found" would be a
	// hash-order coin toss.
	AWrecktideMissionGiver* Giver = nullptr;
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->GetGiverId() == FName(TEXT("Bell")))
		{
			Giver = *It;
			break;
		}
	}

	if (!World || !PlayerPawn || !PlayerState || !Progress || !Economy || !Skill || !Data || !Giver)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (wiring pawn=%d playerstate=%d progress=%d economy=%d skill=%d data=%d giver=%d)"),
			PlayerPawn != nullptr, PlayerState != nullptr, Progress != nullptr, Economy != nullptr,
			Skill != nullptr, Data != nullptr, Giver != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}
	MissionProbeGiver = Giver;

	// STEP 1: the giver stands in the camp and Missions.csv loaded the seven v0 rows in chain order.
	const TArray<TPair<FName, const FMissionTableRow*>> Rows = Data->GetMissionRows();
	static const FName ExpectedRows[] =
	{
		FName(TEXT("M01_Wake")), FName(TEXT("M02_FirstSteel")), FName(TEXT("M03_CampStash")),
		FName(TEXT("M04_Redcoats")), FName(TEXT("M05_Bounty")), FName(TEXT("M06_SetSail")),
		FName(TEXT("M07_BoneCaptain"))
	};
	bool bChainOK = Rows.Num() >= 7;
	for (int32 Index = 0; bChainOK && Index < 7; ++Index)
	{
		bChainOK = Rows.IsValidIndex(Index) && Rows[Index].Key == ExpectedRows[Index] && Rows[Index].Value->Order == Index + 1;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step1 giver=1 rows=%d chainOrder=%d"), Rows.Num(), bChainOK ? 1 : 0);
	if (!bChainOK)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step1 rows=%d in the wrong order or missing)"), Rows.Num());
		ScheduleProbeQuit(2.0f);
		return;
	}

	// The kill recipe for the whole chain, captured once off a live enemy.
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			MissionProbeEnemyClass = It->GetClass();
			MissionProbeSpawnTransform = It->GetActorTransform();
			break;
		}
	}
	AWrecktideShopStall* Stall = nullptr;
	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		Stall = *It;
		break;
	}
	if (!MissionProbeEnemyClass.IsValid() || !Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step1 enemyClass=%d stall=%d)"),
			MissionProbeEnemyClass.IsValid(), Stall != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 2: accept M01 through the giver's own menu, then walk to the market and let the 2 Hz
	// location timer finish it. Nothing here calls the mission logic directly.
	const bool bMenuOpen = OpenMissionProbeGiver();
	const bool bAcceptLine = bMenuOpen && Giver->SelectActionLine(EMissionGiverAction::Accept);
	if (bAcceptLine)
	{
		Giver->ConfirmMenuSelection();
	}
	const FName AfterAccept = WrecktideMissionLogic::GetCurrentMissionRow(Progress, Data);
	const int32 CurrentAfterAccept = Progress->GetQuestFlag(WrecktideMissionLogic::CurrentKey());
	Giver->CloseGiverMenu();

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step2 menu=%d acceptLine=%d active=%s current=%d"),
		bMenuOpen ? 1 : 0, bAcceptLine ? 1 : 0, *AfterAccept.ToString(), CurrentAfterAccept);
	if (!bMenuOpen || !bAcceptLine || AfterAccept != ExpectedRows[0] || CurrentAfterAccept != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step2 menu=%d acceptLine=%d active=%s current=%d)"),
			bMenuOpen ? 1 : 0, bAcceptLine ? 1 : 0, *AfterAccept.ToString(), CurrentAfterAccept);
		ScheduleProbeQuit(2.0f);
		return;
	}

	MissionProbeGoldBefore = Economy->GetGoldTotal();
	MissionProbeXPBefore = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee);
	PlayerPawn->SetActorLocation(Stall->GetActorLocation() + Stall->GetActorRightVector() * 150.0f + FVector(0.0f, 0.0f, 120.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	MissionProbeReachDeadline = World->GetTimeSeconds() + 3.0f;
	World->GetTimerManager().SetTimer(MissionProbeReachTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckMissionProbeReach), 0.25f, true);
}

void UWrecktideRewardSubsystem::CheckMissionProbeReach()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	const USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	if (!World || !Progress || !Economy || !Skill)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step2 player state lost while walking to the market)"));
		if (World)
		{
			World->GetTimerManager().ClearTimer(MissionProbeReachTimer);
		}
		ScheduleProbeQuit(2.0f);
		return;
	}

	const int32 Done = Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(FName(TEXT("M01_Wake"))));
	if (Done == 1)
	{
		World->GetTimerManager().ClearTimer(MissionProbeReachTimer);
		const int64 GoldDelta = Economy->GetGoldTotal() - MissionProbeGoldBefore;
		const int64 XPDelta = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee) - MissionProbeXPBefore;
		const int32 Current = Progress->GetQuestFlag(WrecktideMissionLogic::CurrentKey());
		bMissionProbeReachOK = GoldDelta == 5 && XPDelta == 20 && Current == 0;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step2 reached the market done=1 gold+%lld xp+%lld current=%d"),
			GoldDelta, XPDelta, Current);
		if (!bMissionProbeReachOK)
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step2 payout gold+%lld expected 5, xp+%lld expected 20, current=%d expected 0)"),
				GoldDelta, XPDelta, Current);
			ScheduleProbeQuit(2.0f);
			return;
		}
		RunMissionProbeMidChain();
		return;
	}

	if (World->GetTimeSeconds() > MissionProbeReachDeadline)
	{
		World->GetTimerManager().ClearTimer(MissionProbeReachTimer);
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step2 standing at the market for 3s and M01_Wake never completed; the 2Hz location timer or the spot lookup is dead)"));
		ScheduleProbeQuit(2.0f);
	}
}

void UWrecktideRewardSubsystem::RunMissionProbeMidChain()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	const USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideMissionGiver* Giver = MissionProbeGiver.Get();
	const bool bAlive = PlayerPawn && PlayerPawn->Stats && PlayerPawn->Stats->IsAlive();
	if (!World || !PlayerPawn || !Progress || !Economy || !Skill || !Data || !Giver || !bAlive)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (midchain wiring pawn=%d alive=%d progress=%d data=%d giver=%d)"),
			PlayerPawn != nullptr, bAlive ? 1 : 0, Progress != nullptr, Data != nullptr, Giver != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// One leg of the chain: accept at the giver, do the deed, complete at the giver, and require the
	// exact reward. Returns false the moment anything is off, with the numbers in the log.
	auto AcceptCurrent = [this, Giver, Progress, Data](FName ExpectedRow) -> bool
	{
		const bool bOpen = OpenMissionProbeGiver();
		const bool bLine = bOpen && Giver->SelectActionLine(EMissionGiverAction::Accept);
		if (bLine)
		{
			Giver->ConfirmMenuSelection();
		}
		Giver->CloseGiverMenu();
		const FName Active = WrecktideMissionLogic::GetCurrentMissionRow(Progress, Data);
		if (!bOpen || !bLine || Active != ExpectedRow)
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (accept %s open=%d line=%d active=%s)"),
				*ExpectedRow.ToString(), bOpen ? 1 : 0, bLine ? 1 : 0, *Active.ToString());
			return false;
		}
		return true;
	};

	auto CompleteCurrent = [this, Giver, Progress, Economy, Skill, Data](FName Row, int64 ExpectedGold, int64 ExpectedXP) -> bool
	{
		const bool bReady = WrecktideMissionLogic::IsReadyToComplete(Progress, Data);
		const bool bOpen = OpenMissionProbeGiver();
		MissionProbeGoldBefore = Economy->GetGoldTotal();
		MissionProbeXPBefore = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee);
		const bool bLine = bOpen && Giver->SelectActionLine(EMissionGiverAction::Complete);
		if (bLine)
		{
			Giver->ConfirmMenuSelection();
		}
		const int64 GoldDelta = Economy->GetGoldTotal() - MissionProbeGoldBefore;
		const int64 XPDelta = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee) - MissionProbeXPBefore;
		const int32 DoneFlag = Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(Row));
		Giver->CloseGiverMenu();
		const bool bOK = bReady && bOpen && bLine && DoneFlag == 1 && GoldDelta == ExpectedGold && XPDelta == ExpectedXP;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE complete row=%s ready=%d line=%d done=%d gold+%lld (expected %lld) xp+%lld (expected %lld)"),
			*Row.ToString(), bReady ? 1 : 0, bLine ? 1 : 0, DoneFlag, GoldDelta, ExpectedGold, XPDelta, ExpectedXP);
		if (!bOK)
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (complete %s ready=%d line=%d done=%d gold+%lld xp+%lld)"),
				*Row.ToString(), bReady ? 1 : 0, bLine ? 1 : 0, DoneFlag, GoldDelta, XPDelta);
		}
		return bOK;
	};

	// STEP 3: M02, two cutthroats through the real kill path.
	const FName M02(TEXT("M02_FirstSteel"));
	if (!AcceptCurrent(M02))
	{
		ScheduleProbeQuit(2.0f);
		return;
	}
	bool bKills = FireMissionProbeKill(FName(TEXT("Cutthroat")));
	bKills = FireMissionProbeKill(FName(TEXT("Cutthroat"))) && bKills;
	const int32 M02Count = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M02));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step3 kills=%d progress=%d"), bKills ? 1 : 0, M02Count);
	if (!bKills || M02Count != 2 || !CompleteCurrent(M02, 15, 60))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step3 kills=%d progress=%d)"), bKills ? 1 : 0, M02Count);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 4: M03, the camp chest through its own Open().
	const FName M03(TEXT("M03_CampStash"));
	AWrecktideTreasureChest* CampChest = nullptr;
	for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
	{
		if (It->ChestId == FName(TEXT("CampChest")))
		{
			CampChest = *It;
			break;
		}
	}
	if (!CampChest || !AcceptCurrent(M03))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step4 campChest=%d)"), CampChest != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}
	PlayerPawn->SetActorLocation(CampChest->GetActorLocation() + FVector(120.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
	const bool bChestOpened = CampChest->Open(PlayerPawn);
	const int32 M03Count = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M03));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step4 chest=%s opened=%d progress=%d"),
		*CampChest->ChestId.ToString(), bChestOpened ? 1 : 0, M03Count);
	if (!bChestOpened || M03Count != 1 || !CompleteCurrent(M03, 10, 40))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step4 opened=%d progress=%d)"), bChestOpened ? 1 : 0, M03Count);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 5: M04, three navy cadets, and the HUD mission line must be readable while it runs.
	const FName M04(TEXT("M04_Redcoats"));
	if (!AcceptCurrent(M04))
	{
		ScheduleProbeQuit(2.0f);
		return;
	}
	bool bCadets = FireMissionProbeKill(FName(TEXT("NavyCadet")));
	bCadets = FireMissionProbeKill(FName(TEXT("NavyCadet"))) && bCadets;
	const int32 M04AfterTwo = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M04));

	// The wrong row must not count, and it is fired BEFORE the last needed kill: fired after it the
	// cap would hide the bug.
	bCadets = FireMissionProbeKill(FName(TEXT("Cutthroat"))) && bCadets;
	const int32 M04AfterWrongRow = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M04));

	bCadets = FireMissionProbeKill(FName(TEXT("NavyCadet"))) && bCadets;
	const int32 M04Count = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M04));
	const FString HudLine = WrecktideMissionLogic::LogLine(Progress, Data);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step5 kills=%d after2=%d wrongRow=%d progress=%d hud=%s"),
		bCadets ? 1 : 0, M04AfterTwo, M04AfterWrongRow, M04Count, *HudLine);
	if (!bCadets || M04AfterTwo != 2 || M04AfterWrongRow != 2 || M04Count != 3
		|| !HudLine.Contains(TEXT("Mission:")) || !CompleteCurrent(M04, 30, 100))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step5 kills=%d after2=%d wrongRow=%d progress=%d hud=%s)"),
			bCadets ? 1 : 0, M04AfterTwo, M04AfterWrongRow, M04Count, *HudLine);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 6: M05, a real bounty accepted, hunted and turned in at the board.
	const FName M05(TEXT("M05_Bounty"));
	const FName CutthroatBounty(TEXT("CutthroatBounty"));
	AWrecktideQuestBoard* Board = nullptr;
	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		Board = *It;
		break;
	}
	if (!Board || !AcceptCurrent(M05))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step6 board=%d)"), Board != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}
	PlayerPawn->SetActorLocation(Board->GetActorLocation() + Board->GetActorRightVector() * 150.0f, false, nullptr, ETeleportType::TeleportPhysics);
	Board->OpenBoardMenu(PlayerPawn);
	const bool bBountyPicked = Board->SelectQuestRow(CutthroatBounty);
	Board->ConfirmMenuSelection();
	bool bBountyKills = true;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		bBountyKills = FireMissionProbeKill(FName(TEXT("Cutthroat"))) && bBountyKills;
	}
	Board->SelectQuestRow(CutthroatBounty);
	Board->ConfirmMenuSelection();
	const int32 M05Count = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M05));
	// Hand the keys and the HUD line list back before the giver's menu asks for them.
	Board->ToggleBoardMenu(PlayerPawn);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step6 bountyPicked=%d kills=%d progress=%d boardMenu=%d"),
		bBountyPicked ? 1 : 0, bBountyKills ? 1 : 0, M05Count, Board->IsMenuOpen() ? 1 : 0);
	if (!bBountyPicked || !bBountyKills || M05Count != 1 || !CompleteCurrent(M05, 20, 60))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step6 picked=%d kills=%d progress=%d)"),
			bBountyPicked ? 1 : 0, bBountyKills ? 1 : 0, M05Count);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 7: M06, board the moored boat through Board(). The pier spot is the boat probe's.
	const FName M06(TEXT("M06_SetSail"));
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	if (!Boat || !AcceptCurrent(M06))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step7 boat=%d)"), Boat != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}
	const FVector Mooring = Boat->GetActorLocation();
	PlayerPawn->SetActorLocation(Mooring + Boat->GetActorRightVector() * 550.0f + FVector(0.0f, 0.0f, 180.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	const bool bBoarded = Boat->Board(PlayerPawn);
	const int32 M06Count = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M06));
	bMissionProbeBoardOK = bBoarded && M06Count == 1;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step7 boarded=%d progress=%d"), bBoarded ? 1 : 0, M06Count);
	if (!bMissionProbeBoardOK)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step7 boarded=%d progress=%d)"), bBoarded ? 1 : 0, M06Count);
		ScheduleProbeQuit(2.0f);
		return;
	}

	bMissionProbeMidChainOK = true;
	FTimerHandle AshoreTimer;
	World->GetTimerManager().SetTimer(AshoreTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::CheckMissionProbeBoat), 0.6f, false);
}

void UWrecktideRewardSubsystem::CheckMissionProbeBoat()
{
	UWorld* World = GetWorld();
	AWrecktideBoat* Boat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		Boat = *It;
		break;
	}
	if (!World || !Boat)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step7 boat lost before going ashore)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Back on the sand: the rest of the chain happens on foot at the camp.
	const bool bAshore = Boat->GoAshore();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step7 ashore=%d occupied=%d"), bAshore ? 1 : 0, Boat->IsOccupied() ? 1 : 0);
	if (!bAshore || Boat->IsOccupied())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step7 ashore=%d occupied=%d)"), bAshore ? 1 : 0, Boat->IsOccupied() ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	FTimerHandle FinishTimer;
	World->GetTimerManager().SetTimer(FinishTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishMissionProbe), 0.6f, false);
}

void UWrecktideRewardSubsystem::FinishMissionProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	const USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideMissionGiver* Giver = MissionProbeGiver.Get();
	const bool bAlive = PlayerPawn && PlayerPawn->Stats && PlayerPawn->Stats->IsAlive();
	if (!World || !PlayerPawn || !Progress || !Economy || !Skill || !Data || !Giver || !bAlive)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (finish wiring pawn=%d alive=%d progress=%d data=%d giver=%d)"),
			PlayerPawn != nullptr, bAlive ? 1 : 0, Progress != nullptr, Data != nullptr, Giver != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const FName M06(TEXT("M06_SetSail"));
	const FName M07(TEXT("M07_BoneCaptain"));

	// STEP 7 payout: the sailing mission pays and stamps Story_Sailed.
	const bool bM06Ready = WrecktideMissionLogic::IsReadyToComplete(Progress, Data);
	const bool bM06Open = OpenMissionProbeGiver();
	MissionProbeGoldBefore = Economy->GetGoldTotal();
	MissionProbeXPBefore = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee);
	const bool bM06Line = bM06Open && Giver->SelectActionLine(EMissionGiverAction::Complete);
	if (bM06Line)
	{
		Giver->ConfirmMenuSelection();
	}
	const int64 M06Gold = Economy->GetGoldTotal() - MissionProbeGoldBefore;
	const int64 M06XP = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee) - MissionProbeXPBefore;
	const int32 Sailed = Progress->GetQuestFlag(FName(TEXT("Story_Sailed")));
	Giver->CloseGiverMenu();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step7 complete ready=%d line=%d gold+%lld (expected 25) xp+%lld (expected 80) Story_Sailed=%d"),
		bM06Ready ? 1 : 0, bM06Line ? 1 : 0, M06Gold, M06XP, Sailed);
	if (!bM06Ready || !bM06Line || M06Gold != 25 || M06XP != 80 || Sailed != 1
		|| Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(M06)) != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step7 payout gold+%lld xp+%lld sailed=%d)"), M06Gold, M06XP, Sailed);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// STEP 8: the boss, the finale, and the chain marker.
	const bool bM07Open = OpenMissionProbeGiver();
	const bool bM07Accept = bM07Open && Giver->SelectActionLine(EMissionGiverAction::Accept);
	if (bM07Accept)
	{
		Giver->ConfirmMenuSelection();
	}
	Giver->CloseGiverMenu();
	const FName ActiveBoss = WrecktideMissionLogic::GetCurrentMissionRow(Progress, Data);
	const bool bBossKill = FireMissionProbeKill(FName(TEXT("SkeletonCaptain")));
	const int32 BossCount = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(M07));

	const bool bBossOpen = OpenMissionProbeGiver();
	MissionProbeGoldBefore = Economy->GetGoldTotal();
	MissionProbeXPBefore = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee);
	const bool bBossLine = bBossOpen && Giver->SelectActionLine(EMissionGiverAction::Complete);
	if (bBossLine)
	{
		Giver->ConfirmMenuSelection();
	}
	const int64 BossGold = Economy->GetGoldTotal() - MissionProbeGoldBefore;
	const int64 BossXP = WrecktideMissionProbe::TotalXP(Skill, EWrecktideSkill::Melee) - MissionProbeXPBefore;
	const int32 StoryComplete = Progress->GetQuestFlag(FName(TEXT("Story_Complete")));
	const int32 CurrentAtEnd = Progress->GetQuestFlag(WrecktideMissionLogic::CurrentKey());
	const int32 BossDone = Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(M07));
	// C22 (2026-09-06): the story no longer STOPS at the Bone Captain. His death is the chapter
	// gate, so what has to be true here is Story_Complete plus Bell holding the hand-off M08, not
	// the old "chain finished" marker. Everything before this line is unchanged.
	const FName NextAfterBoss = WrecktideMissionLogic::GetOfferForGiver(Progress, Data, FName(TEXT("Bell")));
	const FString EndLine = WrecktideMissionLogic::LogLine(Progress, Data);
	Giver->CloseGiverMenu();

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step8 accept=%d active=%s kill=%d progress=%d gold+%lld (expected 120) xp+%lld (expected 300)"),
		bM07Accept ? 1 : 0, *ActiveBoss.ToString(), bBossKill ? 1 : 0, BossCount, BossGold, BossXP);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE step8 done=%d Story_Complete=%d Mission_Current=%d nextFromBell=%s hudLine='%s'"),
		BossDone, StoryComplete, CurrentAtEnd, *NextAfterBoss.ToString(), *EndLine);

	const bool bPass = bMissionProbeReachOK && bMissionProbeMidChainOK && bMissionProbeBoardOK
		&& bM07Accept && ActiveBoss == M07 && bBossKill && BossCount == 1 && bBossLine
		&& BossGold == 120 && BossXP == 300 && BossDone == 1
		&& StoryComplete == 1 && CurrentAtEnd == 0 && EndLine.IsEmpty()
		&& NextAfterBoss == FName(TEXT("M08_SteelThatBites"));
	if (!bPass)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step8 accept=%d kill=%d gold+%lld xp+%lld done=%d complete=%d current=%d)"),
			bM07Accept ? 1 : 0, bBossKill ? 1 : 0, BossGold, BossXP, BossDone, StoryComplete, CurrentAtEnd);
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-PROBE FAIL (step8 nextFromBell=%s expected M08_SteelThatBites, hudLine='%s')"),
			*NextAfterBoss.ToString(), *EndLine);
		ScheduleProbeQuit(2.0f);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROBE PASS (chain=7/7 reach=1 kills=1 chest=1 bounty=1 sail=1 boss=1 Story_Complete=1 handoff=M08_SteelThatBites)"));
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -SfxProbe (2026-09-05): the feedback sounds are real assets that load in THIS build (a cooked
// package included: /Game/Audio/SFX sits in DirectoriesToAlwaysCook) and one real gameplay path,
// the Melee level-up toast, reaches PlaySfx. Judge: WRECKTIDE-SFX-PROBE PASS or FAIL.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunSfxProbe()
{
	static const TCHAR* Names[] = { TEXT("hit"), TEXT("kill"), TEXT("coin"), TEXT("levelup"), TEXT("chest"), TEXT("fanfare"),
		TEXT("wood"), TEXT("ui_move"), TEXT("ui_confirm"), TEXT("hurt"), TEXT("death"), TEXT("accept"), TEXT("splash"),
		TEXT("pistol_shot"), TEXT("pistol_reload") };
	int32 Played = 0;
	for (const TCHAR* Name : Names)
	{
		const bool bPlayed = UWrecktideAudio::PlaySfx(this, FName(Name), 0.5f);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SFX-PROBE play %s %s"), Name, bPlayed ? TEXT("OK") : TEXT("MISSING"));
		Played += bPlayed ? 1 : 0;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	int32 HookPlays = 0;
	if (Skill)
	{
		const int32 Mark = UWrecktideAudio::GetPlayCount();
		Skill->AwardXP(EWrecktideSkill::Melee, 100000, TEXT("SfxProbe"));
		HookPlays = UWrecktideAudio::GetPlayCount() - Mark;
	}
	const bool bLevelUpHeard = HookPlays >= 1 && UWrecktideAudio::GetLastPlayed() == FName(TEXT("levelup"));
	const bool bAmbient = UWrecktideAudio::IsAmbientPlaying();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SFX-PROBE ambient playing=%d"), bAmbient ? 1 : 0);
	const bool bPass = Played == UE_ARRAY_COUNT(Names) && bLevelUpHeard && bAmbient;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SFX-PROBE played=%d/%d skill=%d hookPlays=%d last=%s"),
		Played, static_cast<int32>(UE_ARRAY_COUNT(Names)), Skill != nullptr ? 1 : 0, HookPlays,
		*UWrecktideAudio::GetLastPlayed().ToString());
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SFX-PROBE %s"), bPass ? TEXT("PASS") : TEXT("FAIL (see the line above)"));
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -MenuProbe (2026-09-05, extended 2026-09-06): stage 0 = the title menu owns the first frames (open,
// game paused, HUD panel showing New Voyage and Quit, Continue iff a slot exists); choosing New Voyage
// restarts the level. Modes: =continue, =slots (manual save slot round trip + the Load Voyage screen),
// =options (a value edited through the menu model, applied, and read back out of the ini FILE).
// Stage 1 (the fresh world, same process, the static counter survives the travel) = no title menu,
// not paused, panel closed, the pirate is possessed. Judge: WRECKTIDE-MENU-PROBE PASS or FAIL.
// Persistence is suppressed in probe runs, so New Game here never touches the real save file.
// ---------------------------------------------------------------------------------------------
namespace
{
int32 GMenuProbeStage = 0;
}

void UWrecktideRewardSubsystem::RunMenuProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatPlayerController* CombatController = Cast<ACombatPlayerController>(PlayerController);
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	if (!CombatController || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MENU-PROBE FAIL reason=wiring controller=%d hud=%d stage=%d"),
			CombatController != nullptr ? 1 : 0, HUD != nullptr ? 1 : 0, GMenuProbeStage);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE FAIL (wiring)"));
		// Every exit unpauses first: ScheduleProbeQuit is a WORLD timer, and a menu probe that fails
		// while the title menu still has the world paused hangs until the 180 s watchdog (trap 2).
		if (World)
		{
			UGameplayStatics::SetGamePaused(World, false);
		}
		ScheduleProbeQuit(2.0f);
		return;
	}

	const bool bPaused = UGameplayStatics::IsGamePaused(World);
	const bool bTitleOpen = CombatController->IsTitleMenuOpen();
	const TArray<FString>& Lines = HUD->GetPauseMenuLines();
	if (GMenuProbeStage == 0)
	{
		// the title waits for rendered frames; give it up to 60 more seconds before judging
		static int32 WaitTicks = 0;
		if (!bTitleOpen && WaitTicks++ < 60)
		{
			TWeakObjectPtr<UWrecktideRewardSubsystem> WeakThis(this);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
			{
				if (WeakThis.IsValid())
				{
					WeakThis->RunMenuProbe();
				}
				return false;
			}), 1.0f);
			return;
		}
		const bool bHasNewGame = Lines.Contains(TEXT("New Voyage"));
		const bool bHasQuit = Lines.Contains(TEXT("Quit"));
		const bool bSlotExists = UGameplayStatics::DoesSaveGameExist(AWrecktidePlayerState::GetSaveSlot(), 0);
		// -MenuProbe=continue runs on a probe slot seeded by the save probe's write phase, so persistence is
		// live and Continue must be offered; the plain probe suppresses persistence and Continue must be absent.
		FString Mode;
		FParse::Value(FCommandLine::Get(), TEXT("MenuProbe="), Mode);
		const bool bContinueMode = Mode.Equals(TEXT("continue"), ESearchCase::IgnoreCase);
		const bool bSlotsMode = Mode.Equals(TEXT("slots"), ESearchCase::IgnoreCase);
		const bool bOptionsMode = Mode.Equals(TEXT("options"), ESearchCase::IgnoreCase);
		const bool bContinueRight = bContinueMode ? Lines.Contains(TEXT("Continue")) : !Lines.Contains(TEXT("Continue"));
		const bool bStage0 = bTitleOpen && bPaused && HUD->IsPauseMenuOpen() && bHasNewGame && bHasQuit && bContinueRight;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE stage0 open=%d paused=%d panel=%d newgame=%d quit=%d continueAbsent=%d slotOnDisk=%d lines=%d %s"),
			bTitleOpen ? 1 : 0, bPaused ? 1 : 0, HUD->IsPauseMenuOpen() ? 1 : 0, bHasNewGame ? 1 : 0, bHasQuit ? 1 : 0,
			bContinueRight ? 1 : 0, bSlotExists ? 1 : 0, Lines.Num(), bStage0 ? TEXT("OK") : TEXT("BAD"));
		if (!bStage0)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE FAIL (stage0)"));
			UGameplayStatics::SetGamePaused(World, false);
			ScheduleProbeQuit(2.0f);
			return;
		}
		if (bContinueMode)
		{
			// Continue: the menu closes, the world runs, and the state loaded from the seeded slot is intact.
			AWrecktidePlayerState* PlayerState = CombatController->GetPlayerState<AWrecktidePlayerState>();
			UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
			const bool bChoseContinue = CombatController->TitleChoose(TEXT("Continue"));
			const bool bRunning = !UGameplayStatics::IsGamePaused(World) && !CombatController->IsTitleMenuOpen() && !HUD->IsPauseMenuOpen();
			const bool bLoaded = PlayerState && PlayerState->WasLoadedFromSlot();
			const int64 Gold = Economy ? Economy->GetGoldTotal() : -1;

			// Continue must put the pirate back where the seeding process left him, not at the
			// PlayerStart. The spot comes from the sidecar the -SaveProbe=write phase wrote.
			FString Expect;
			FVector2D ExpectedSpot = FVector2D::ZeroVector;
			const bool bSpotRead = FFileHelper::LoadFileToString(Expect, *SaveProbeExpectPath(AWrecktidePlayerState::GetSaveSlot()))
				&& ReadSaveProbeSpot(Expect, ExpectedSpot);
			const APawn* ContinuePawn = CombatController->GetPawn();
			const FVector PawnLocation = ContinuePawn ? ContinuePawn->GetActorLocation() : FVector::ZeroVector;
			const float SpotDistance = ContinuePawn
				? FVector2D::Distance(FVector2D(PawnLocation.X, PawnLocation.Y), ExpectedSpot)
				: TNumericLimits<float>::Max();
			const bool bPlaced = bSpotRead && ContinuePawn && SpotDistance <= SaveProbePlacementTolerance;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE continue position pawn=(%.0f,%.0f) saved=(%.0f,%.0f) distance=%.0f<=%.0f read=%d %s"),
				PawnLocation.X, PawnLocation.Y, ExpectedSpot.X, ExpectedSpot.Y, SpotDistance,
				SaveProbePlacementTolerance, bSpotRead ? 1 : 0, bPlaced ? TEXT("OK") : TEXT("BAD"));

			const bool bContinuePass = bChoseContinue && bRunning && bLoaded && Gold > 0 && bPlaced;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE continue chose=%d running=%d loadedFromSlot=%d gold=%lld placed=%d %s"),
				bChoseContinue ? 1 : 0, bRunning ? 1 : 0, bLoaded ? 1 : 0, Gold, bPlaced ? 1 : 0, bContinuePass ? TEXT("OK") : TEXT("BAD"));
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE %s"), bContinuePass ? TEXT("PASS") : TEXT("FAIL (see the line above)"));
			UGameplayStatics::SetGamePaused(World, false);
			ScheduleProbeQuit(2.0f);
			return;
		}
		if (bSlotsMode)
		{
			RunMenuSlotsProbe(*CombatController);
			return;
		}
		if (bOptionsMode)
		{
			RunMenuOptionsProbe(*CombatController);
			return;
		}
		GMenuProbeStage = 1;
		const bool bChosen = CombatController->TitleChoose(TEXT("New Voyage"));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE stage0 chose New Voyage=%d, waiting for the restarted world"), bChosen ? 1 : 0);
		if (!bChosen)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE FAIL (New Voyage refused)"));
			UGameplayStatics::SetGamePaused(World, false);
			ScheduleProbeQuit(2.0f);
		}
		return;
	}

	ACombatCharacter* Pawn = Cast<ACombatCharacter>(CombatController->GetPawn());
	const bool bPass = !bTitleOpen && !bPaused && !HUD->IsPauseMenuOpen() && Pawn != nullptr;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE stage1 open=%d paused=%d panel=%d pawn=%d %s"),
		bTitleOpen ? 1 : 0, bPaused ? 1 : 0, HUD->IsPauseMenuOpen() ? 1 : 0, Pawn != nullptr ? 1 : 0, bPass ? TEXT("OK") : TEXT("BAD"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE %s"), bPass ? TEXT("PASS") : TEXT("FAIL (see the line above)"));
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -MenuProbe=slots : the manual Save Voyage slots. Earns gold the same way -SaveProbe=write does,
// writes Slot 2, moves the gold, reads Slot 2 back and asserts the number returned AND that the slot
// summary the Load screen shows is a real line. Then drives the menu MODEL (not keys) into Load
// Voyage to prove the screen lists that slot, and back out again.
// The slot file is namespaced by AWrecktidePlayerState::GetManualSlot under a probe run, so this can
// never read or write the player's own Slot 1..3.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunMenuSlotsProbe(ACombatPlayerController& CombatController)
{
	AWrecktidePlayerState* PlayerState = CombatController.GetPlayerState<AWrecktidePlayerState>();
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	if (!PlayerState || !Economy)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE FAIL (slots wiring ps=%d economy=%d)"),
			PlayerState != nullptr ? 1 : 0, Economy != nullptr ? 1 : 0);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const FString Slot = AWrecktidePlayerState::GetManualSlot(2);
	// Start from nothing so a leftover file cannot make a broken write look like a pass.
	UGameplayStatics::DeleteGameInSlot(Slot, 0);

	Economy->ApplyGoldDelta(500, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("MenuProbe")));
	const int64 GoldAtSave = Economy->GetGoldTotal();

	// Position round trip: stand on a known walkable spot, save, walk the pirate 3000 uu away, load,
	// and the load must put him back. Teleporting leaves the movement mode alone (the world is
	// paused under the title menu anyway), which is what BuildSaveData needs to record a position.
	APawn* ProbePawn = CombatController.GetPawn();
	if (ProbePawn)
	{
		ProbePawn->SetActorLocation(SaveProbeSpot, false, nullptr, ETeleportType::TeleportPhysics);
	}
	const FVector SavedSpot = ProbePawn ? ProbePawn->GetActorLocation() : FVector::ZeroVector;

	const bool bSaved = PlayerState->SaveToManualSlot(2);
	const bool bOnDisk = UGameplayStatics::DoesSaveGameExist(Slot, 0);

	if (ProbePawn)
	{
		ProbePawn->SetActorLocation(SavedSpot + FVector(3000.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	const FVector MovedSpot = ProbePawn ? ProbePawn->GetActorLocation() : FVector::ZeroVector;

	// Move the number so a load that does nothing at all cannot pass.
	Economy->ApplyGoldDelta(250, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("MenuProbe")));
	const int64 GoldAfterChange = Economy->GetGoldTotal();

	const bool bLoaded = PlayerState->LoadManualSlot(2);
	const int64 GoldAfterLoad = Economy->GetGoldTotal();
	const FVector LoadedSpot = ProbePawn ? ProbePawn->GetActorLocation() : FVector::ZeroVector;
	const float PlacementDistance = ProbePawn ? FVector::Dist(LoadedSpot, SavedSpot) : TNumericLimits<float>::Max();
	const bool bPlaced = ProbePawn && PlacementDistance <= SaveProbePlacementTolerance
		&& FVector::Dist(MovedSpot, SavedSpot) > SaveProbePlacementTolerance;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE slots position saved=%s moved=%s loaded=%s distance=%.0f<=%.0f %s"),
		*SavedSpot.ToString(), *MovedSpot.ToString(), *LoadedSpot.ToString(), PlacementDistance,
		SaveProbePlacementTolerance, bPlaced ? TEXT("OK") : TEXT("BAD"));

	const FString Summary = AWrecktidePlayerState::GetSlotSummary(this, Slot);
	const bool bSummary = !Summary.IsEmpty() && !Summary.Equals(TEXT("Empty")) && !Summary.Equals(TEXT("Unreadable"));

	// The menu model itself: open Load Voyage and prove the slot shows up as a row with that summary.
	CombatController.MenuOpenScreen(EWrecktideMenuScreen::LoadVoyage);
	bool bRowFound = false;
	for (const FWrecktideMenuRow& Row : CombatController.GetMenuRows())
	{
		if (Row.Label.Equals(TEXT("Slot 2")) && Row.SubLine.Equals(Summary) && Row.bSelectable)
		{
			bRowFound = true;
			break;
		}
	}
	const bool bOnLoadScreen = CombatController.GetMenuScreen() == EWrecktideMenuScreen::LoadVoyage;
	CombatController.MenuBack();
	const bool bBackOnTitle = CombatController.GetMenuScreen() == EWrecktideMenuScreen::Main;

	const bool bPass = bSaved && bOnDisk && bLoaded && GoldAfterChange != GoldAtSave
		&& GoldAfterLoad == GoldAtSave && bSummary && bOnLoadScreen && bRowFound && bBackOnTitle && bPlaced;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE slots slot=%s saved=%d onDisk=%d loaded=%d goldAtSave=%lld goldChanged=%lld goldBack=%lld summary='%s' loadScreen=%d row=%d back=%d placed=%d %s"),
		*Slot, bSaved ? 1 : 0, bOnDisk ? 1 : 0, bLoaded ? 1 : 0, GoldAtSave, GoldAfterChange, GoldAfterLoad,
		*Summary, bOnLoadScreen ? 1 : 0, bRowFound ? 1 : 0, bBackOnTitle ? 1 : 0, bPlaced ? 1 : 0,
		bPass ? TEXT("OK") : TEXT("BAD"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE %s"), bPass ? TEXT("PASS") : TEXT("FAIL (see the line above)"));

	// The world is paused under the title menu and ScheduleProbeQuit is a world timer (trap 2).
	UGameplayStatics::SetGamePaused(&CombatController, false);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -MenuProbe=options : drives the Options screen through the controller's MODEL functions (open,
// select, Left/Right, Apply) and then reads GameUserSettings.ini back OFF DISK. Reading the file
// rather than the cached values is the point: it proves the setting survives the process.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunMenuOptionsProbe(ACombatPlayerController& CombatController)
{
	auto RowIndexOf = [&CombatController](const TCHAR* Label) -> int32
	{
		const TArray<FWrecktideMenuRow>& Rows = CombatController.GetMenuRows();
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			if (Rows[Index].Label.Equals(Label))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	};

	CombatController.MenuOpenScreen(EWrecktideMenuScreen::Options);
	const bool bOnOptions = CombatController.GetMenuScreen() == EWrecktideMenuScreen::Options;

	// Deterministic, whatever the ini started at: wind Master all the way down, then up to 40.
	const int32 MasterRow = RowIndexOf(TEXT("Master volume"));
	if (MasterRow != INDEX_NONE)
	{
		CombatController.MenuSetSelected(MasterRow);
		for (int32 Step = 0; Step < 12; ++Step)
		{
			CombatController.MenuAdjust(-1);
		}
		for (int32 Step = 0; Step < 4; ++Step)
		{
			CombatController.MenuAdjust(1);
		}
	}

	const int32 InvertRow = RowIndexOf(TEXT("Invert look Y"));
	if (InvertRow != INDEX_NONE && !FWrecktideOptions::Get().bInvertLookY)
	{
		CombatController.MenuSetSelected(InvertRow);
		CombatController.MenuAdjust(1);
	}

	const int32 MasterAfterEdit = FWrecktideOptions::Get().MasterVolume;
	const bool bInvertAfterEdit = FWrecktideOptions::Get().bInvertLookY;
	const bool bApplied = CombatController.MenuChoose(TEXT("Apply"));

	// The RESOLVED path, not GGameUserSettingsIni: that global is the base name "GameUserSettings".
	const FString IniPath = FWrecktideOptions::GetIniFilePath();
	FString IniText;
	const bool bReadIni = FFileHelper::LoadFileToString(IniText, *IniPath);
	const bool bSection = IniText.Contains(TEXT("[Wrecktide.Options]"));
	const bool bIniMaster = IniText.Contains(TEXT("MasterVolume=40"));
	const bool bIniInvert = IniText.Contains(TEXT("InvertLookY=True"));

	CombatController.MenuBack();
	const bool bBackOnTitle = CombatController.GetMenuScreen() == EWrecktideMenuScreen::Main;

	const bool bPass = bOnOptions && MasterAfterEdit == 40 && bInvertAfterEdit && bApplied
		&& bReadIni && bSection && bIniMaster && bIniInvert && bBackOnTitle;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE options screen=%d master=%d invert=%d applied=%d ini=%d section=%d iniMaster=%d iniInvert=%d back=%d file=%s %s"),
		bOnOptions ? 1 : 0, MasterAfterEdit, bInvertAfterEdit ? 1 : 0, bApplied ? 1 : 0, bReadIni ? 1 : 0,
		bSection ? 1 : 0, bIniMaster ? 1 : 0, bIniInvert ? 1 : 0, bBackOnTitle ? 1 : 0, *IniPath,
		bPass ? TEXT("OK") : TEXT("BAD"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MENU-PROBE %s"), bPass ? TEXT("PASS") : TEXT("FAIL (see the line above)"));

	UGameplayStatics::SetGamePaused(&CombatController, false);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunMapProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;

	if (!World || !PlayerPawn || !PlayerState || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MAP-PROBE FAIL reason=wiring world=%d pawn=%d state=%d hud=%d"),
			World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, HUD != nullptr);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP-PROBE FAIL (wiring)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	// 0. Spec C27: M cycles the radar, small to medium to full and back to small, the small state
	//    draws NO island plate, and the size the player lands on is written to the options file.
	//    Read the small state's drawn evidence FIRST, before anything cycles: the run launched on
	//    small (the default), so the last ten seconds of frames are its proof.
	ACombatPlayerController* MapController = Cast<ACombatPlayerController>(PlayerController);
	const bool bStartedSmall = MapController && MapController->GetRadarState() == EWrecktideRadarState::Small;
	const bool bSmallPlateless = !HUD->DidDrawMapPlate();
	const float SmallDiameter = HUD->GetDrawnRadarDiameter();
	bool bCycleOK = false;
	bool bOptionOK = false;
	if (MapController)
	{
		MapController->ToggleMapScreen();
		const bool bMedium = MapController->GetRadarState() == EWrecktideRadarState::Medium
			&& !MapController->IsMapScreenOpen();
		const int32 MediumOption = FWrecktideOptions::Get().RadarSize;

		MapController->ToggleMapScreen();
		const bool bFull = MapController->GetRadarState() == EWrecktideRadarState::Full
			&& MapController->IsMapScreenOpen() && UGameplayStatics::IsGamePaused(World);

		MapController->ToggleMapScreen();
		const bool bBackToSmall = MapController->GetRadarState() == EWrecktideRadarState::Small
			&& !MapController->IsMapScreenOpen() && !UGameplayStatics::IsGamePaused(World);
		const int32 SmallOption = FWrecktideOptions::Get().RadarSize;

		// The option must survive a write and a re-read of the real file, not just the cache.
		FWrecktideOptions::Load();
		const int32 ReloadedOption = FWrecktideOptions::Get().RadarSize;

		bCycleOK = bStartedSmall && bMedium && bFull && bBackToSmall;
		bOptionOK = MediumOption == 1 && SmallOption == 0 && ReloadedOption == 0;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP-PROBE radar startedSmall=%d plateless=%d diameter=%.0f medium=%d full=%d small=%d option %d->%d reloaded=%d"),
			bStartedSmall ? 1 : 0, bSmallPlateless ? 1 : 0, SmallDiameter, bMedium ? 1 : 0, bFull ? 1 : 0,
			bBackToSmall ? 1 : 0, MediumOption, SmallOption, ReloadedOption);
	}
	const bool bRadarOK = bCycleOK && bOptionOK && bSmallPlateless && SmallDiameter > 0.0f && SmallDiameter < 200.0f;

	// 1. The island plate loaded.
	const bool bTextureOK = HUD->IsMapTextureLoaded();

	// 2. The world-to-map transform puts two known landmarks in their expected UV boxes. Numbers, not
	//    a screenshot: the pier deck centre and the fort hill.
	const FVector2D PierUV = WrecktideMap::WorldToUV(FVector(0.0f, -8000.0f, 0.0f));
	const FVector2D FortUV = WrecktideMap::WorldToUV(FVector(2600.0f, 2600.0f, 0.0f));
	const FVector2D PierExpected(0.100f, 0.500f);
	const FVector2D FortExpected(0.630f, 0.370f);
	const bool bPierOK = FMath::Abs(PierUV.X - PierExpected.X) < 0.02f && FMath::Abs(PierUV.Y - PierExpected.Y) < 0.02f;
	const bool bFortOK = FMath::Abs(FortUV.X - FortExpected.X) < 0.02f && FMath::Abs(FortUV.Y - FortExpected.Y) < 0.02f;

	// The transform must also round trip, or the full map's screen-to-world readout lies.
	const FVector2D RoundTrip = WrecktideMap::UVToWorld(FortUV);
	const bool bRoundTripOK = FMath::Abs(RoundTrip.X - 2600.0f) < 1.0f && FMath::Abs(RoundTrip.Y - 2600.0f) < 1.0f;

	// 3. The icon set: the four camp landmarks and all five chests, none of them opened yet.
	TArray<WrecktideMap::FMapMarker> Markers;
	WrecktideMap::CollectMarkers(this, Markers);
	int32 StallCount = 0;
	int32 BoardCount = 0;
	int32 GiverCount = 0;
	int32 BoatCount = 0;
	int32 ChestCount = 0;
	for (const WrecktideMap::FMapMarker& Marker : Markers)
	{
		switch (Marker.Icon)
		{
		case WrecktideMap::EMapIcon::Stall: ++StallCount; break;
		case WrecktideMap::EMapIcon::Board: ++BoardCount; break;
		case WrecktideMap::EMapIcon::Giver: ++GiverCount; break;
		case WrecktideMap::EMapIcon::Boat: ++BoatCount; break;
		case WrecktideMap::EMapIcon::Chest: ++ChestCount; break;
		default: break;
		}
	}
	// The chest count used to be pinned at 5, the number Wreck Cove ships. Gallows Reach adds the
	// powder magazine chest (C34, 2026-09-07), so a literal there would fail on every new island
	// while testing nothing about the map. Compare against the LIVE unopened chests instead: that
	// is the thing the check was really for (CollectMarkers must drop opened chests and no other
	// one), and it gets stronger rather than weaker as islands are added.
	int32 UnopenedChests = 0;
	for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
	{
		UnopenedChests += It->IsOpened() ? 0 : 1;
	}
	const bool bIconsOK = StallCount >= 1 && BoardCount >= 1 && GiverCount >= 1 && BoatCount >= 1
		&& UnopenedChests >= 5 && ChestCount == UnopenedChests;

	// 4. The rim clamp. A target 5,000 uu along world +Y is due EAST on the map (map right = world
	//    +Y, see WrecktideMapView.h). Facing north (yaw 0) it must sit at +90 degrees clockwise from
	//    up; turning to face it (yaw 90) must bring it to 0.
	const FVector ProbeOrigin(0.0f, 0.0f, 0.0f);
	const FVector EastTarget(0.0f, 5000.0f, 0.0f);
	const float PixelsPerUU = 110.0f / WrecktideMap::MinimapRangeUU;
	const FVector2D EastAtNorth = WrecktideMap::WorldToRadarOffset(ProbeOrigin, 0.0f, EastTarget, PixelsPerUU);
	const FVector2D EastAtEast = WrecktideMap::WorldToRadarOffset(ProbeOrigin, 90.0f, EastTarget, PixelsPerUU);
	const float AngleAtNorth = WrecktideMap::RadarOffsetAngle(EastAtNorth);
	const float AngleAtEast = WrecktideMap::RadarOffsetAngle(EastAtEast);
	const FVector2D Clamped = WrecktideMap::ClampToCircle(EastAtNorth, 99.0f);
	const float ClampedAngle = WrecktideMap::RadarOffsetAngle(Clamped);
	const bool bAngleOK = FMath::Abs(AngleAtNorth - 90.0f) < 1.0f
		&& FMath::Abs(AngleAtEast) < 1.0f
		&& FMath::Abs(ClampedAngle - 90.0f) < 1.0f
		&& FMath::Abs(Clamped.Size() - 99.0f) < 0.5f;

	// The edge arrow's ray-versus-rectangle must land ON the rectangle, not inside it, and must keep
	// the direction: clamping the axes independently is the classic bug and this catches it.
	const FVector2D Corner = WrecktideMap::ClampToRect(FVector2D(1.0f, 1.0f).GetSafeNormal() * 100000.0f, FVector2D(400.0f, 200.0f));
	const bool bRectOK = FMath::Abs(Corner.Y - 200.0f) < 0.5f && FMath::Abs(Corner.X - 200.0f) < 0.5f;

	// 5. The beacon. Nothing accepted yet, so it must be dark.
	AWrecktideMissionBeacon* Beacon = AWrecktideMissionBeacon::FindBeacon(World, EWrecktideBeaconMode::Mission);
	const bool bBeaconExists = Beacon != nullptr;
	if (Beacon)
	{
		Beacon->RefreshTarget();
	}
	const bool bDarkBeforeAccept = bBeaconExists && !Beacon->IsLit();

	// M01 is ReachSpot MarketStall: accept it and the beam must stand on the stall.
	const bool bAcceptedFirst = WrecktideMissionLogic::Accept(PlayerState);
	FVector StallLocation = FVector::ZeroVector;
	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		StallLocation = It->GetActorLocation();
		break;
	}
	if (Beacon)
	{
		Beacon->RefreshTarget();
	}
	const float FirstDistance = bBeaconExists ? FVector::Dist2D(Beacon->GetActorLocation(), StallLocation) : 99999.0f;
	const bool bFirstTargetOK = bBeaconExists && Beacon->IsLit() && FirstDistance <= 50.0f;

	// Complete M01 by arriving, which leaves the chain between missions: the beam must go out. That
	// is the same code path the end of the chain takes, without driving seven missions in a probe.
	WrecktideMissionLogic::NotifyLocation(PlayerState, StallLocation);
	if (Beacon)
	{
		Beacon->RefreshTarget();
	}
	const bool bDarkBetween = bBeaconExists && !Beacon->IsLit();

	// M02 is KillRow Cutthroat: the beam must move onto a living cutthroat.
	const bool bAcceptedSecond = WrecktideMissionLogic::Accept(PlayerState);
	int32 LivingCutthroats = 0;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		const ACombatEnemy* Enemy = *It;
		if (Enemy && Enemy->EnemyRowName == FName(TEXT("Cutthroat")) && (!Enemy->Stats || Enemy->Stats->IsAlive()))
		{
			++LivingCutthroats;
		}
	}
	if (Beacon)
	{
		Beacon->RefreshTarget();
	}
	float SecondDistance = 99999.0f;
	if (bBeaconExists && LivingCutthroats > 0)
	{
		for (TActorIterator<ACombatEnemy> It(World); It; ++It)
		{
			const ACombatEnemy* Enemy = *It;
			if (!Enemy || Enemy->EnemyRowName != FName(TEXT("Cutthroat")) || (Enemy->Stats && !Enemy->Stats->IsAlive()))
			{
				continue;
			}
			// Against his POST, not his live position (spec C33, 2026-09-07): the beam anchors on
			// ACombatEnemy::GetHomeLocation so a chasing enemy cannot drag it across the island. At
			// probe time nobody has aggroed and the two are the same point; asserting the post is
			// what keeps this check honest once one of them moves.
			const FVector EnemyPost = Enemy->GetHomeLocation().IsNearlyZero() ? Enemy->GetActorLocation() : Enemy->GetHomeLocation();
			SecondDistance = FMath::Min(SecondDistance, FVector::Dist2D(Beacon->GetActorLocation(), EnemyPost));
		}
	}
	// Fail-open on the ONE thing this lane does not own: if the level has no living cutthroat at
	// probe time there is nothing to point at, so the check is reported SKIP rather than failing the
	// map gate for an enemy-spawning problem that belongs to another system.
	const bool bSecondSkipped = LivingCutthroats == 0;
	const bool bSecondTargetOK = bSecondSkipped || (bBeaconExists && Beacon->IsLit() && SecondDistance <= 50.0f);

	const bool bPass = bTextureOK && bPierOK && bFortOK && bRoundTripOK && bIconsOK && bAngleOK && bRectOK
		&& bBeaconExists && bDarkBeforeAccept && bAcceptedFirst && bFirstTargetOK && bDarkBetween
		&& bAcceptedSecond && bSecondTargetOK && bRadarOK;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP-PROBE texture=%d pierUV=(%.3f,%.3f)%s fortUV=(%.3f,%.3f)%s roundTrip=%d icons stall=%d board=%d giver=%d boat=%d chests=%d%s"),
		bTextureOK ? 1 : 0, PierUV.X, PierUV.Y, bPierOK ? TEXT("") : TEXT(" BAD"),
		FortUV.X, FortUV.Y, bFortOK ? TEXT("") : TEXT(" BAD"), bRoundTripOK ? 1 : 0,
		StallCount, BoardCount, GiverCount, BoatCount, ChestCount, bIconsOK ? TEXT("") : TEXT(" BAD"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP-PROBE clamp northUp=%.1f headingUp=%.1f rimAngle=%.1f rimLen=%.1f rect=(%.1f,%.1f) angleOK=%d rectOK=%d"),
		AngleAtNorth, AngleAtEast, ClampedAngle, Clamped.Size(), Corner.X, Corner.Y, bAngleOK ? 1 : 0, bRectOK ? 1 : 0);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP-PROBE beacon exists=%d darkBefore=%d m01accepted=%d m01dist=%.1f darkBetween=%d m02accepted=%d cutthroats=%d m02dist=%.1f%s"),
		bBeaconExists ? 1 : 0, bDarkBeforeAccept ? 1 : 0, bAcceptedFirst ? 1 : 0, FirstDistance,
		bDarkBetween ? 1 : 0, bAcceptedSecond ? 1 : 0, LivingCutthroats, SecondDistance,
		bSecondSkipped ? TEXT(" (m02 SKIPPED, no living cutthroat in the world)") : TEXT(""));

	if (bPass)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP-PROBE PASS (plate + %d icons + beacon on M01 and M02 + rim clamp)"), Markers.Num());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MAP-PROBE FAIL (texture=%d pier=%d fort=%d roundTrip=%d icons=%d angle=%d rect=%d beacon=%d darkBefore=%d m01=%d darkBetween=%d m02=%d radar=%d)"),
			bTextureOK ? 1 : 0, bPierOK ? 1 : 0, bFortOK ? 1 : 0, bRoundTripOK ? 1 : 0, bIconsOK ? 1 : 0,
			bAngleOK ? 1 : 0, bRectOK ? 1 : 0, bBeaconExists ? 1 : 0, bDarkBeforeAccept ? 1 : 0,
			bFirstTargetOK ? 1 : 0, bDarkBetween ? 1 : 0, bSecondTargetOK ? 1 : 0, bRadarOK ? 1 : 0);
	}

	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunBeaconProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideMissionBeacon* Beacon = World ? AWrecktideMissionBeacon::FindBeacon(World, EWrecktideBeaconMode::Mission) : nullptr;

	if (!World || !PlayerPawn || !PlayerState || !Progress || !Data || !Beacon)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE FAIL (wiring world=%d pawn=%d state=%d progress=%d data=%d beacon=%d)"),
			World != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, Progress != nullptr, Data != nullptr, Beacon != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Walk the chain the way -MapProbe does: M01 is a ReachSpot on the market stall, and completing
	// it by arriving leaves the KillRow row (M02, two cutthroats) as the tracked mission. Every step
	// below drives Beacon->RefreshTarget() by hand, so nothing waits on the 1 Hz beat.
	WrecktideMissionLogic::Accept(PlayerState);
	FVector StallLocation = FVector::ZeroVector;
	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		StallLocation = It->GetActorLocation();
		break;
	}
	WrecktideMissionLogic::NotifyLocation(PlayerState, StallLocation);
	const bool bAcceptedKillRow = WrecktideMissionLogic::Accept(PlayerState);
	const FName KillRowName = WrecktideMissionLogic::GetCurrentMissionRow(Progress, Data);
	const FMissionTableRow* KillRow = KillRowName.IsNone() ? nullptr : Data->GetMissionRow(KillRowName);
	const FName EnemyRowName = KillRow ? KillRow->TargetName : NAME_None;
	const bool bKillRowTracked = KillRow != nullptr
		&& (KillRow->Type == EMissionObjective::KillRow || KillRow->Type == EMissionObjective::KillBoss);

	const FVector Camp = PlayerPawn->GetActorLocation();
	TArray<ACombatEnemy*> Living;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		ACombatEnemy* Enemy = *It;
		if (Enemy && Enemy->EnemyRowName == EnemyRowName && (!Enemy->Stats || Enemy->Stats->IsAlive()))
		{
			Living.Add(Enemy);
		}
	}
	Living.Sort([&Camp](const ACombatEnemy& A, const ACombatEnemy& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Camp) < FVector::DistSquared2D(B.GetActorLocation(), Camp);
	});

	if (!bKillRowTracked || Living.Num() < 2)
	{
		// Fail closed, but say exactly what was missing: the beacon rules are what this probe proves,
		// and it cannot prove them without a kill row and two living targets of that row to move.
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE FAIL (setup accepted=%d row=%s enemyRow=%s living=%d, needs a tracked kill row and two living targets)"),
			bAcceptedKillRow ? 1 : 0, *KillRowName.ToString(), *EnemyRowName.ToString(), Living.Num());
		ScheduleProbeQuit(2.0f);
		return;
	}

	ACombatEnemy* Tracked = Living[0];
	ACombatEnemy* Second = Living[1];
	const FVector TrackedHome = Tracked->HomeLocation.IsNearlyZero() ? Tracked->GetActorLocation() : Tracked->HomeLocation;
	FVector AwayDirection = (TrackedHome - Camp).GetSafeNormal2D();
	if (AwayDirection.IsNearlyZero())
	{
		AwayDirection = FVector(1.0f, 0.0f, 0.0f);
	}

	// Park every OTHER living target of the row behind the tracked one, post included, so which enemy
	// the resolver calls "nearest" is never in doubt while the probe walks the player about. The
	// second one stands 4,000 uu out, well past BeaconRearmDistance, which is what makes step (d) a
	// real rearm rather than an accident of where the level happens to put its cutthroats.
	const FVector SecondHome = TrackedHome + AwayDirection * 4000.0f;
	Second->HomeLocation = SecondHome;
	Second->SetActorLocation(SecondHome, false, nullptr, ETeleportType::TeleportPhysics);
	for (int32 Index = 2; Index < Living.Num(); ++Index)
	{
		const FVector FarPost = TrackedHome + AwayDirection * (12000.0f + 1000.0f * Index);
		Living[Index]->HomeLocation = FarPost;
		Living[Index]->SetActorLocation(FarPost, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// (a) ANCHOR. Displace the tracked enemy 800 uu off his post, TOWARDS the player so he stays the
	//     nearest of the row, and the beam must stand on the POST, not on the man.
	const FVector Displaced = TrackedHome - AwayDirection * 800.0f;
	Tracked->SetActorLocation(FVector(Displaced.X, Displaced.Y, Tracked->GetActorLocation().Z),
		false, nullptr, ETeleportType::TeleportPhysics);
	Beacon->RefreshTarget();
	const float AnchorToHome = FVector::Dist2D(Beacon->GetActorLocation(), TrackedHome);
	const float AnchorToEnemy = FVector::Dist2D(Beacon->GetActorLocation(), Tracked->GetActorLocation());
	const bool bAnchorOK = Beacon->IsLit() && AnchorToHome <= 50.0f && AnchorToEnemy > 400.0f;

	// (sticky) The anchor sticks to the MAN it first named. Standing the player right next to the
	//     second target of the row makes that one the nearest, and the beam must NOT hop to his post:
	//     only the first one's death may move it, which step (d) then proves.
	const FVector NextToSecond = SecondHome - AwayDirection * 300.0f + FVector(0.0f, 0.0f, 120.0f);
	PlayerPawn->SetActorLocation(NextToSecond, false, nullptr, ETeleportType::TeleportPhysics);
	Beacon->RefreshTarget();
	const float StickyToFirstPost = FVector::Dist2D(Beacon->GetActorLocation(), TrackedHome);
	const float StickyToSecondPost = FVector::Dist2D(Beacon->GetActorLocation(), SecondHome);
	const bool bStickyOK = Beacon->IsLit() && StickyToFirstPost <= 50.0f && StickyToSecondPost > 2000.0f
		&& Beacon->GetAnchorEnemy() == Tracked;

	// (b) REACH. Stand him 400 uu from the post, on the line towards the displaced enemy so that
	//     enemy is still the nearest thing to him, and the beam must go out.
	const FVector ReachStand = TrackedHome - AwayDirection * 400.0f + FVector(0.0f, 0.0f, 120.0f);
	PlayerPawn->SetActorLocation(ReachStand, false, nullptr, ETeleportType::TeleportPhysics);
	Beacon->RefreshTarget();
	const float ReachDrift = FVector::Dist2D(Beacon->GetLastAnchor(), TrackedHome);
	const bool bReachOK = !Beacon->IsLit() && Beacon->HasReachedTarget() && ReachDrift <= 50.0f;

	// (c) STAY OUT. Walking away is not a reason to light it again.
	const FVector FarStand = TrackedHome - AwayDirection * 3000.0f + FVector(0.0f, 0.0f, 120.0f);
	PlayerPawn->SetActorLocation(FarStand, false, nullptr, ETeleportType::TeleportPhysics);
	Beacon->RefreshTarget();
	const float StayDrift = FVector::Dist2D(Beacon->GetLastAnchor(), TrackedHome);
	const bool bStayOK = !Beacon->IsLit() && StayDrift <= 50.0f;

	// (d) REARM. Kill the tracked target; the next living one of the row posts 4,000 uu away, past
	//     BeaconRearmDistance, so that is a NEW destination and the beam lights on the new post.
	for (int32 Hit = 0; Hit < 12 && Tracked->Stats && Tracked->Stats->IsAlive(); ++Hit)
	{
		Tracked->TakeDamage(1000.0f, FDamageEvent(), nullptr, nullptr);
	}
	const bool bTrackedDead = Tracked->Stats && !Tracked->Stats->IsAlive();
	const FName RowAfterKill = WrecktideMissionLogic::GetCurrentMissionRow(Progress, Data);
	Beacon->RefreshTarget();
	const float RearmToNewHome = FVector::Dist2D(Beacon->GetActorLocation(), SecondHome);
	const bool bRearmOK = bTrackedDead && RowAfterKill == KillRowName && Beacon->IsLit() && RearmToNewHome <= 50.0f;

	// (e) TALK. A TalkTo row had NO beam and NO arrow before C33. Force the first TalkTo row in the
	//     table active and tracked through the mission logic's own flag spellings, stand the player
	//     well clear of that NPC so the reach rule is not what is under test, and both flavours of
	//     the resolver must answer with him.
	FName TalkRowName;
	FName TalkGiverId;
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (Entry.Value && Entry.Value->Type == EMissionObjective::TalkTo)
		{
			TalkRowName = Entry.Key;
			TalkGiverId = Entry.Value->TargetName;
			break;
		}
	}
	FVector TalkGiverLocation = FVector::ZeroVector;
	bool bFoundTalkGiver = false;
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->GetGiverId() == TalkGiverId)
		{
			TalkGiverLocation = It->GetActorLocation();
			bFoundTalkGiver = true;
			break;
		}
	}
	bool bTalkTracked = false;
	if (!TalkRowName.IsNone())
	{
		Progress->SetQuestFlag(WrecktideMissionLogic::ProgressKey(TalkRowName), 0);
		Progress->SetQuestFlag(WrecktideMissionLogic::ActiveKey(TalkRowName), 1);
		bTalkTracked = WrecktideMissionLogic::SetTrackedMission(PlayerState, TalkRowName);
	}
	PlayerPawn->SetActorLocation(TalkGiverLocation + FVector(4000.0f, 0.0f, 200.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Beacon->RefreshTarget();
	FVector LiveTarget = FVector::ZeroVector;
	FString LiveLabel;
	const bool bLiveResolved = WrecktideMap::ResolveMissionTarget(this, PlayerState, LiveTarget, LiveLabel,
		WrecktideMap::EMissionTargetFlavour::Live);
	const float TalkBeaconDistance = FVector::Dist2D(Beacon->GetActorLocation(), TalkGiverLocation);
	const float TalkLiveDistance = bLiveResolved ? FVector::Dist2D(LiveTarget, TalkGiverLocation) : 99999.0f;
	const bool bTalkOK = bFoundTalkGiver && bTalkTracked && Beacon->IsLit()
		&& TalkBeaconDistance <= 200.0f && bLiveResolved && TalkLiveDistance <= 200.0f;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE row=%s enemyRow=%s living=%d home=(%.0f,%.0f) anchorToHome=%.1f anchorToEnemy=%.1f lit=%d"),
		*KillRowName.ToString(), *EnemyRowName.ToString(), Living.Num(), TrackedHome.X, TrackedHome.Y,
		AnchorToHome, AnchorToEnemy, bAnchorOK ? 1 : 0);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE sticky firstPost=%.1f secondPost=%.1f held=%d"),
		StickyToFirstPost, StickyToSecondPost, bStickyOK ? 1 : 0);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE reach drift=%.1f reached=%d stayDrift=%.1f killed=%d rowAfterKill=%s rearmDist=%.1f"),
		ReachDrift, bReachOK ? 1 : 0, StayDrift, bTrackedDead ? 1 : 0, *RowAfterKill.ToString(), RearmToNewHome);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE talk row=%s giver=%s found=%d tracked=%d beaconDist=%.1f liveDist=%.1f"),
		*TalkRowName.ToString(), *TalkGiverId.ToString(), bFoundTalkGiver ? 1 : 0, bTalkTracked ? 1 : 0,
		TalkBeaconDistance, TalkLiveDistance);

	const bool bPass = bAnchorOK && bStickyOK && bReachOK && bStayOK && bRearmOK && bTalkOK;
	if (bPass)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE PASS (anchor=1 sticky=1 reach=1 stay=1 rearm=1 talk=1)"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON-PROBE FAIL (anchor=%d sticky=%d reach=%d stay=%d rearm=%d talk=%d)"),
			bAnchorOK ? 1 : 0, bStickyOK ? 1 : 0, bReachOK ? 1 : 0, bStayOK ? 1 : 0, bRearmOK ? 1 : 0, bTalkOK ? 1 : 0);
	}

	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::SpawnSmithStall(UWorld* World) const
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	APlayerStart* FirstPlayerStart = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		FirstPlayerStart = *It;
		break;
	}

	if (!FirstPlayerStart)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-WIRING No PlayerStart found for the smith stall"));
		return;
	}

	// Out past the market on the same side, a touch back: every one of these interact surfaces
	// carries a 300 uu trigger, so the smithy has to sit more than 600 uu from the shop stall
	// (Right*400), the bounty board (Right*-400) and the quest giver (Forward*450 + Right*650) or
	// the player would stand inside two menus at once. This spot is 765, 1560 and 780 uu from them.
	FVector SpawnLocation = FirstPlayerStart->GetActorLocation()
		+ FirstPlayerStart->GetActorForwardVector() * -150.0f + FirstPlayerStart->GetActorRightVector() * 1150.0f;
	const FVector TraceStart = SpawnLocation + FVector(0.0f, 0.0f, 500.0f);
	const FVector TraceEnd = SpawnLocation - FVector(0.0f, 0.0f, 2000.0f);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility))
	{
		// the Synty table has a base pivot: stand it on the sand, no cube-centre lift
		SpawnLocation.Z = Hit.ImpactPoint.Z;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (!World->SpawnActor<AWrecktideSmithStall>(AWrecktideSmithStall::StaticClass(), SpawnLocation, FirstPlayerStart->GetActorRotation(), SpawnParameters))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-WIRING Smith stall spawn failed"));
	}
}

void UWrecktideRewardSubsystem::RunSmithProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	if (!World || !PlayerPawn || !PlayerState || !Economy || !Progress || !DataSubsystem || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-PROBE FAIL reason=wiring pawn=%d playerstate=%d economy=%d progress=%d data=%d hud=%d"),
			PlayerPawn != nullptr, PlayerState != nullptr, Economy != nullptr, Progress != nullptr,
			DataSubsystem != nullptr, HUD != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	AWrecktideSmithStall* Stall = nullptr;
	for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It)
	{
		Stall = *It;
		break;
	}
	if (!Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-PROBE FAIL reason=no-stall"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	SmithProbeStall = Stall;

	// The four tracks this probe drives, found by EFFECT so a renamed or reordered CSV row cannot
	// silently point the assertions at the wrong numbers.
	FName EdgeRow = NAME_None;
	FName BrutalRow = NAME_None;
	FName BalanceRow = NAME_None;
	FName BloodRow = NAME_None;
	const FUpgradeTrackTableRow* EdgeData = DataSubsystem->GetUpgradeRowForTrack(EWrecktideUpgradeTrack::Edge, &EdgeRow);
	const FUpgradeTrackTableRow* BrutalData = DataSubsystem->GetUpgradeRowForTrack(EWrecktideUpgradeTrack::Brutal, &BrutalRow);
	const FUpgradeTrackTableRow* BalanceData = DataSubsystem->GetUpgradeRowForTrack(EWrecktideUpgradeTrack::Balance, &BalanceRow);
	const FUpgradeTrackTableRow* BloodData = DataSubsystem->GetUpgradeRowForTrack(EWrecktideUpgradeTrack::Bloodletting, &BloodRow);
	const FName WeaponRow(TEXT("WornCutlass"));
	if (!EdgeData || !BrutalData || !BalanceData || !BloodData || !DataSubsystem->GetWeaponRow(WeaponRow))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-PROBE FAIL reason=no-table edge=%d brutal=%d balance=%d blood=%d"),
			EdgeData != nullptr, BrutalData != nullptr, BalanceData != nullptr, BloodData != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// A weapon he owns (the smithy only forges those) and coin enough that nothing below is ever
	// refused for gold.
	Progress->UnlockItem(WeaponRow);
	PlayerState->EquipWeapon(WeaponRow);
	Economy->ApplyGoldDelta(20000, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("SmithProbe")));

	// The measuring target: a living enemy with health enough that UStatsComponent's 40 percent
	// per-hit cap can never bind and it can never die mid-probe. Everything from here down runs in
	// ONE synchronous call, so no tick moves the target between swings.
	ACombatEnemy* Target = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (It->Stats && It->Stats->IsAlive())
		{
			const float Dist = static_cast<float>(FVector::Dist(It->GetActorLocation(), PlayerPawn->GetActorLocation()));
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Target = *It;
			}
		}
	}
	if (!Target || !Target->Stats)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-PROBE FAIL reason=no-enemy"));
		ScheduleProbeQuit(2.0f);
		return;
	}
	Target->Stats->RestoreFromSave(100000.0f, 100000.0f);

	// Stand 100 uu off the target, face it, land one combo trace (the -HitProbe recipe) and return
	// the health it actually lost.
	auto SwingAtTarget = [PlayerPawn, Target]() -> float
	{
		FVector ToEnemy = (Target->GetActorLocation() - PlayerPawn->GetActorLocation()).GetSafeNormal2D();
		if (ToEnemy.IsNearlyZero())
		{
			ToEnemy = PlayerPawn->GetActorForwardVector().GetSafeNormal2D();
		}
		PlayerPawn->SetActorLocation(Target->GetActorLocation() - ToEnemy * 100.0f, false, nullptr, ETeleportType::TeleportPhysics);
		PlayerPawn->SetActorRotation(ToEnemy.Rotation());
		const float HealthBefore = Target->Stats->GetHealth();
		PlayerPawn->DoAttackTrace(FName(TEXT("hand_r")));
		return HealthBefore - Target->Stats->GetHealth();
	};

	// Stage A, part 1: the baseline swing. The crit roll is forced OFF so a lucky 5 percent cannot
	// corrupt the measurement.
	PlayerState->SetProbeCriticalChanceOverride(0.0f);
	const float DamageBefore = SwingAtTarget();

	// Stage A, part 2: buy Edge tier 1 through the REAL menu (open, pick the weapon, confirm).
	PlayerPawn->SetActorLocation(Stall->GetActorLocation() + Stall->GetActorRightVector() * 150.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Stall->OpenSmithMenu(PlayerPawn);
	const bool bMenuOK = Stall->IsMenuOpen() && Stall->GetRowCount() > 0;
	const bool bTracksOpen = bMenuOK && Stall->OpenWeaponTracks(WeaponRow)
		&& Stall->GetPage() == ESmithMenuPage::Tracks && Stall->GetSelectedRow() == EdgeRow;
	const int64 GoldBeforeEdge = Economy->GetGoldTotal();
	if (bTracksOpen)
	{
		Stall->ConfirmMenuSelection();
	}
	const int32 EdgeTier = Progress->GetUpgradeTier(WeaponRow, EdgeRow);
	const int64 EdgePaid = GoldBeforeEdge - Economy->GetGoldTotal();

	// The other three tracks, so the whole table is proven wired, not just Edge. Brutal is needed by
	// stage B anyway.
	const bool bBoughtBrutal = Stall->BuyUpgrade(WeaponRow, BrutalRow);
	const bool bBoughtBalance = Stall->BuyUpgrade(WeaponRow, BalanceRow);
	const bool bBoughtBlood = Stall->BuyUpgrade(WeaponRow, BloodRow);
	const float SpeedMultiplier = PlayerState->GetAttackSpeedMultiplier();
	const float Lifesteal = PlayerState->GetLifestealFraction();
	const bool bTracksWired = bBoughtBrutal && bBoughtBalance && bBoughtBlood
		&& FMath::IsNearlyEqual(SpeedMultiplier, 1.0f + BalanceData->Step, 0.001f)
		&& FMath::IsNearlyEqual(Lifesteal, BloodData->Step, 0.001f);

	// Stage C: a tier above the notoriety gate is refused, and not one coin moves. Tier 2 asks for
	// notoriety 8 and this run is level 1.
	const int64 GoldBeforeGate = Economy->GetGoldTotal();
	const bool bGateRefused = !Stall->BuyUpgrade(WeaponRow, EdgeRow);
	const int32 GateLevel = EdgeData->GetTierLevel(2);
	const bool bGateOK = bGateRefused
		&& Economy->GetGoldTotal() == GoldBeforeGate
		&& Progress->GetUpgradeTier(WeaponRow, EdgeRow) == 1
		&& PlayerState->GetGatingLevel() < GateLevel;

	// Stage A, part 3: the same swing again. Edge tier 1 must raise the damage the target TAKES by
	// exactly the authored step.
	const float DamageAfter = SwingAtTarget();
	const float EdgeRatio = DamageBefore > 0.0f ? DamageAfter / DamageBefore : 0.0f;
	const bool bEdgeOK = EdgeTier == 1 && EdgePaid == EdgeData->GetTierCost(1) && DamageBefore > 0.0f
		&& FMath::IsNearlyEqual(EdgeRatio, 1.0f + EdgeData->Step, 0.01f)
		// upgrades bind to the WEAPON row: another weapon must still read zero
		&& Progress->GetUpgradeTier(FName(TEXT("BoardingAxe")), EdgeRow) == 0;

	// Stage B: force the roll and prove the CRITICAL line and the damage carry Brutal.
	PlayerState->SetProbeCriticalChanceOverride(1.0f);
	const float ExpectedCrit = PlayerState->GetCriticalMultiplier();
	const float CritDamage = SwingAtTarget();
	PlayerState->SetProbeCriticalChanceOverride(-1.0f);
	const FString FeedLine = HUD->GetRewardFeedLine(0);
	const float CritRatio = DamageAfter > 0.0f ? CritDamage / DamageAfter : 0.0f;
	const bool bCritOK = FMath::IsNearlyEqual(ExpectedCrit, PlayerState->BaseCriticalMultiplier + BrutalData->Step, 0.001f)
		&& FMath::IsNearlyEqual(CritRatio, ExpectedCrit, 0.01f)
		&& FeedLine.StartsWith(TEXT("CRITICAL"))
		&& FeedLine.Contains(FString::Printf(TEXT("x%.2f"), ExpectedCrit));

	// Stage D: the tiers survive a save and load, on a slot of the probe's OWN (the player's slot is
	// never touched, and this one is deleted again below).
	const FString ProbeSlot = TEXT("Wrecktide_SmithProbeSlot");
	const bool bWrote = Progress->SaveToSlot(ProbeSlot);
	Progress->SetUpgradeTier(WeaponRow, EdgeRow, 0);
	const bool bWiped = Progress->GetUpgradeTier(WeaponRow, EdgeRow) == 0;
	const bool bRead = Progress->LoadFromSlot(ProbeSlot);
	const int32 RestoredEdge = Progress->GetUpgradeTier(WeaponRow, EdgeRow);
	const int32 RestoredBrutal = Progress->GetUpgradeTier(WeaponRow, BrutalRow);
	const int32 SavedVersion = Progress->BuildSaveData().SaveVersion;
	UGameplayStatics::DeleteGameInSlot(ProbeSlot, 0);
	const bool bPersistOK = bWrote && bWiped && bRead && RestoredEdge == 1 && RestoredBrutal == 1
		&& SavedVersion == WRECKTIDE_SAVE_VERSION;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH stage edge tier=%d paid=%lld dmg %.2f->%.2f ratio=%.3f expected=%.3f"),
		EdgeTier, EdgePaid, DamageBefore, DamageAfter, EdgeRatio, 1.0f + EdgeData->Step);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH stage crit dmg=%.2f ratio=%.3f expected=%.3f feed=[%s]"),
		CritDamage, CritRatio, ExpectedCrit, *FeedLine);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH stage gate refused=%d level=%d needed=%d gold %lld->%lld"),
		bGateRefused ? 1 : 0, PlayerState->GetGatingLevel(), GateLevel, GoldBeforeGate, Economy->GetGoldTotal());
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH stage persist wrote=%d wiped=%d read=%d edge=%d brutal=%d version=%d"),
		bWrote ? 1 : 0, bWiped ? 1 : 0, bRead ? 1 : 0, RestoredEdge, RestoredBrutal, SavedVersion);

	const bool bPass = bMenuOK && bTracksOpen && bEdgeOK && bTracksWired && bGateOK && bCritOK && bPersistOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH-PROBE %s (menu=%d tracks=%d edge=%d ratio=%.3f wired=%d speed=%.3f steal=%.3f gate=%d crit=%d x%.2f persist=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bMenuOK ? 1 : 0, bTracksOpen ? 1 : 0, bEdgeOK ? 1 : 0, EdgeRatio, bTracksWired ? 1 : 0,
		SpeedMultiplier, Lifesteal, bGateOK ? 1 : 0, bCritOK ? 1 : 0, ExpectedCrit, bPersistOK ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -HotbarProbe (spec C17): the numbered weapon bar, end to end. Ownership through the real unlock
// path, assignment through AssignSlot, wielding through the REAL key 1 binding (injected as a key
// event, not by calling the handler), the empty-slot hint through the real key 2 binding, and the
// assignment through a save/load round trip on a slot of the probe's own.
// Two stages: the HUD draws AFTER this runs, so the drawn evidence is read a beat later.
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunHotbarProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	if (!World || !PlayerController || !PlayerState || !Economy || !Progress || !DataSubsystem || !HUD)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOTBAR-PROBE FAIL reason=wiring pc=%d playerstate=%d economy=%d progress=%d data=%d hud=%d"),
			PlayerController != nullptr, PlayerState != nullptr, Economy != nullptr, Progress != nullptr,
			DataSubsystem != nullptr, HUD != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const FName CutlassRow(TEXT("WornCutlass"));
	const FWeaponTableRow* CutlassData = DataSubsystem->GetWeaponRow(CutlassRow);
	if (!CutlassData)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOTBAR-PROBE FAIL reason=no-weapon-row row=%s"), *CutlassRow.ToString());
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Stage A: own it, then assign it. Ownership goes through the same UnlockItem the stall writes,
	// so the assignment gate is tested against the real ownership record.
	Economy->ApplyGoldDelta(200, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("HotbarProbe")));
	Progress->UnlockItem(CutlassRow);
	const bool bAssigned = PlayerState->AssignSlot(1, CutlassRow);
	const bool bSlotHolds = PlayerState->GetAssignedSlot(1) == CutlassRow;
	const bool bEquipped = PlayerState->EquippedWeaponRow == CutlassRow;
	const bool bAssignOK = bAssigned && bSlotHolds && bEquipped;

	// Stage B: an unowned row is refused, and the refusal changes nothing.
	const FName AxeRow(TEXT("BoardingAxe"));
	const bool bRefusedUnowned = !PlayerState->AssignSlot(1, AxeRow) && PlayerState->GetAssignedSlot(1) == CutlassRow;
	// Slots 2 to 5 have no system yet: they refuse and the bar stays empty there.
	const bool bRefusedSlot2 = !PlayerState->AssignSlot(2, CutlassRow) && PlayerState->GetAssignedSlot(2) == NAME_None;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR stage assign assigned=%d holds=%d equipped=%d refusedUnowned=%d refusedSlot2=%d"),
		bAssigned ? 1 : 0, bSlotHolds ? 1 : 0, bEquipped ? 1 : 0, bRefusedUnowned ? 1 : 0, bRefusedSlot2 ? 1 : 0);

	// Stage C: key 1 wields the assignment. Unequip first (the assignment must SURVIVE an empty
	// hand, which is the whole difference between "assigned" and "equipped"), then inject the REAL
	// key event so the binding itself is under test, not just WieldSlot. An injected key is queued
	// into the player input and dispatched on the NEXT input flush, so every effect below is read a
	// tick later, from a timer.
	PlayerState->EquipWeapon(NAME_None);
	const bool bKeptAssignment = PlayerState->GetAssignedSlot(1) == CutlassRow && PlayerState->EquippedWeaponRow == NAME_None;
	const bool bKeyOneSent = SendProbeKey(PlayerController, EKeys::One);

	const bool bStageAB = bAssignOK && bRefusedUnowned && bRefusedSlot2 && bKeptAssignment && bKeyOneSent;
	const FString ExpectedLabel = CutlassData->DisplayName.ToString();
	TWeakObjectPtr<APlayerController> WeakPC(PlayerController);
	TWeakObjectPtr<AWrecktidePlayerState> WeakPS(PlayerState);
	TWeakObjectPtr<UProgressComponent> WeakProgress(Progress);
	TWeakObjectPtr<AWrecktideHUD> WeakHUD(HUD);

	FTimerHandle KeyOneTimer;
	World->GetTimerManager().SetTimer(KeyOneTimer, FTimerDelegate::CreateWeakLambda(this,
		[this, WeakPC, WeakPS, WeakProgress, WeakHUD, bStageAB, CutlassRow, ExpectedLabel]()
	{
		UWorld* StageWorld = GetWorld();
		APlayerController* StagePC = WeakPC.Get();
		AWrecktidePlayerState* StagePS = WeakPS.Get();
		if (!StageWorld || !StagePC || !StagePS)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR-PROBE FAIL (stage C wiring)"));
			ScheduleProbeQuit(2.0f);
			return;
		}

		const bool bWieldedByKey = StagePS->EquippedWeaponRow == CutlassRow;
		// Stage D: an empty slot toasts its hint instead of equipping anything.
		const bool bKeyTwoSent = SendProbeKey(StagePC, EKeys::Two);
		const bool bStageC = bStageAB && bWieldedByKey && bKeyTwoSent;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR stage keys wielded=%d key2sent=%d"),
			bWieldedByKey ? 1 : 0, bKeyTwoSent ? 1 : 0);

		FTimerHandle KeyTwoTimer;
		StageWorld->GetTimerManager().SetTimer(KeyTwoTimer, FTimerDelegate::CreateWeakLambda(this,
			[this, WeakPS, WeakProgress, WeakHUD, bStageC, CutlassRow, ExpectedLabel]()
		{
			AWrecktidePlayerState* StagePS2 = WeakPS.Get();
			UProgressComponent* StageProgress = WeakProgress.Get();
			AWrecktideHUD* StageHUD = WeakHUD.Get();
			UWorld* DrawWorld = GetWorld();
			if (!StagePS2 || !StageProgress || !StageHUD || !DrawWorld)
			{
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR-PROBE FAIL (stage D wiring)"));
				ScheduleProbeQuit(2.0f);
				return;
			}

			const FString Slot2Hint = AWrecktidePlayerState::GetSlotHint(2);
			const FString Slot2Toast = StageHUD->GetToastText();
			const bool bHintOK = Slot2Toast == Slot2Hint && StagePS2->EquippedWeaponRow == CutlassRow;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR stage hint toast=[%s] expected=[%s] stillArmed=%d"),
				*Slot2Toast, *Slot2Hint, StagePS2->EquippedWeaponRow == CutlassRow ? 1 : 0);

			// Stage E: the assignment survives a save and a load, on the probe's OWN slot.
			const FString ProbeSlot = TEXT("Wrecktide_HotbarProbeSlot");
			UGameplayStatics::DeleteGameInSlot(ProbeSlot, 0);
			const bool bWrote = StageProgress->SaveToSlot(ProbeSlot);
			StagePS2->AssignSlot(1, NAME_None);
			const bool bWiped = StagePS2->GetAssignedSlot(1) == NAME_None;
			const bool bRead = StageProgress->LoadFromSlot(ProbeSlot);
			const FName RestoredSlot1 = StagePS2->GetAssignedSlot(1);
			const int32 SavedVersion = StageProgress->BuildSaveData().SaveVersion;
			UGameplayStatics::DeleteGameInSlot(ProbeSlot, 0);
			const bool bPersistOK = bWrote && bWiped && bRead && RestoredSlot1 == CutlassRow
				&& SavedVersion == WRECKTIDE_SAVE_VERSION;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR stage persist wrote=%d wiped=%d read=%d slot1=%s version=%d"),
				bWrote ? 1 : 0, bWiped ? 1 : 0, bRead ? 1 : 0, *RestoredSlot1.ToString(), SavedVersion);

			// The DRAWN evidence lands a beat later still: DrawHUD runs after this timer, so reading
			// the bar now would read the frame before the reload.
			const bool bModelOK = bStageC && bHintOK && bPersistOK;
			FTimerHandle DrawTimer;
			DrawWorld->GetTimerManager().SetTimer(DrawTimer, FTimerDelegate::CreateWeakLambda(this,
				[this, WeakHUD, bModelOK, ExpectedLabel]()
			{
				AWrecktideHUD* DrawnHUD = WeakHUD.Get();
				const bool bBarDrawn = DrawnHUD && DrawnHUD->IsWeaponBarDrawn();
				const FString DrawnLabel = DrawnHUD ? DrawnHUD->GetDrawnSlotLabel(1) : FString();
				const int32 DrawnWielded = DrawnHUD ? DrawnHUD->GetDrawnWieldedSlot() : INDEX_NONE;
				const FString DrawnSlot5 = DrawnHUD ? DrawnHUD->GetDrawnSlotLabel(5) : FString();
				const bool bDrawOK = bBarDrawn && DrawnLabel == ExpectedLabel && DrawnWielded == 1 && !DrawnSlot5.IsEmpty();
				const bool bPass = bModelOK && bDrawOK;
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR stage drawn bar=%d slot1=[%s] expected=[%s] lit=%d slot5=[%s]"),
					bBarDrawn ? 1 : 0, *DrawnLabel, *ExpectedLabel, DrawnWielded, *DrawnSlot5);
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR-PROBE %s (model=%d drawn=%d)"),
					bPass ? TEXT("PASS") : TEXT("FAIL"), bModelOK ? 1 : 0, bDrawOK ? 1 : 0);
				ScheduleProbeQuit(2.0f);
			}), 0.5f, false);
		}), 0.4f, false);
	}), 0.4f, false);
}


// ---------------------------------------------------------------------------------------------
// Chest toolbar: draw evidence, real bound callbacks, modal hand-off and a persisted once-only hint.
// ---------------------------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
void UWrecktideRewardSubsystem::RunChestButtonProbe()
{
	UWorld* World = GetWorld();
	ACombatPlayerController* PC = World ? Cast<ACombatPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	AWrecktidePlayerState* State = PC ? PC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = State ? State->GetProgress() : nullptr;
	AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
	AWrecktideShopStall* Stall = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It) { Stall = *It; break; }
	}
	if (!World || !PC || !PC->InputComponent || !Pawn || !Progress || !HUD || !Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CHESTBUTTON-PROBE FAIL (button=0 map=0 keys=0 click=0 controls=0 hint=0) reason=wiring"));
		ScheduleProbeQuit(ChestButtonProbeQuitDelaySeconds);
		return;
	}

	struct FChestButtonResult
	{
		int32 Stage = 0;
		bool bButton = false;
		bool bMap = false;
		bool bKeys = false;
		bool bClick = false;
		bool bControls = false;
		bool bHint = false;
		EWrecktideRadarState OriginalRadar = EWrecktideRadarState::Small;
	};
	TSharedRef<FChestButtonResult> Result = MakeShared<FChestButtonResult>();
	Result->OriginalRadar = PC->GetRadarState();
	int32 Width = 0;
	int32 Height = 0;
	PC->GetViewportSize(Width, Height);
	const FBox2D Chest = HUD->GetChestButtonRect();
	const FBox2D Map = HUD->GetMapButtonRect();
	const FBox2D First = HUD->GetWeaponSlotRect(1);
	const FBox2D Last = HUD->GetWeaponSlotRect(AWrecktidePlayerState::EquipSlotCount);
	auto OnScreen = [Width, Height](const FBox2D& Rect)
	{
		return Rect.bIsValid && Rect.Min.X >= 0 && Rect.Min.Y >= 0
			&& Rect.Max.X <= Width && Rect.Max.Y <= Height && Rect.GetSize().GetMin() > 0;
	};
	Result->bButton = HUD->IsWeaponBarDrawn() && OnScreen(Chest) && OnScreen(Last)
		&& Chest.Min.X > Last.Max.X && Chest.GetSize().Equals(Last.GetSize())
		&& FMath::IsNearlyEqual(Chest.Min.Y, Last.Min.Y) && !HUD->IsChestButtonLit();
	Result->bMap = OnScreen(Map) && OnScreen(First) && Map.Max.X < First.Min.X
		&& Map.GetSize().Equals(First.GetSize()) && FMath::IsNearlyEqual(Map.Min.Y, First.Min.Y)
		&& FMath::IsNearlyEqual(First.Min.X - Map.Max.X, Chest.Min.X - Last.Max.X)
		&& FMath::IsNearlyEqual((Map.Min.X + Chest.Max.X) * 0.5, Width * 0.5);
	Result->bClick = !PC->bShowMouseCursor && !PC->HandleModalClickAt(Chest.GetCenter()) && !PC->IsInventoryOpen();

	// Invoke each actual bound delegate in both directions; a binding to an unrelated action fails.
	auto ExerciseChestBinding = [PC](const FKey& Key)
	{
		int32 Count = 0;
		FInputActionUnifiedDelegate Delegate;
		bool bFlags = false;
		for (const FInputKeyBinding& Binding : PC->InputComponent->KeyBindings)
		{
			if (Binding.Chord.Key == Key && Binding.KeyEvent == IE_Pressed)
			{
				++Count;
				bFlags = Binding.bExecuteWhenPaused && !Binding.bConsumeInput
					&& Binding.KeyDelegate.IsBoundToObject(PC);
				Delegate = Binding.KeyDelegate;
			}
		}
		if (Count != 1 || !bFlags) { return false; }
		Delegate.Execute(Key);
		const bool bOpened = PC->IsInventoryOpen() && PC->bShowMouseCursor && UGameplayStatics::IsGamePaused(PC);
		Delegate.Execute(Key);
		return bOpened && !PC->IsInventoryOpen() && !PC->bShowMouseCursor && !UGameplayStatics::IsGamePaused(PC);
	};
	const bool bTab = ExerciseChestBinding(EKeys::Tab);
	const bool bI = ExerciseChestBinding(EKeys::I);
	Result->bKeys = bTab && bI;

	PC->TogglePauseMenu();
	PC->MenuOpenScreen(EWrecktideMenuScreen::Controls);
	auto HasControl = [PC](const TCHAR* Label, const TCHAR* Value)
	{
		return PC->GetMenuRows().ContainsByPredicate([Label, Value](const FWrecktideMenuRow& Row)
		{
			return Row.Label == Label && Row.Value == Value;
		});
	};
	Result->bControls = HasControl(TEXT("Captain's Chest"), TEXT("Tab or I"))
		&& HasControl(TEXT("Journal"), TEXT("J")) && HasControl(TEXT("Map cycle"), TEXT("M"))
		&& HasControl(TEXT("Weapon bar"), TEXT("1 to 5"));
	PC->MenuBack();
	PC->MenuBack();

	// The automatic timer must have left a fresh probe state alone. Force the same hint once,
	// then round-trip its existing flag map through a save slot belonging only to this probe.
	const FName HintFlag(TEXT("Hint_Chest"));
	const bool bFresh = AWrecktidePlayerState::IsProbeRun() && !Progress->QuestFlags.Contains(HintFlag)
		&& Progress->GetQuestFlag(HintFlag) == 0;
	const bool bShown = PC->ShowChestHintForProbe();
	const bool bToast = HUD->GetToastText() == TEXT("Tab opens your Captain's Chest. J for the journal, M for the map.");
	const FString HintSlot(TEXT("Wrecktide_ChestButtonProbeSlot"));
	const bool bSaved = Progress->SaveToSlot(HintSlot);
	Progress->SetQuestFlag(HintFlag, 0);
	const bool bLoaded = Progress->LoadFromSlot(HintSlot);
	UGameplayStatics::DeleteGameInSlot(HintSlot, 0);
	Result->bHint = bFresh && bShown && bToast && bSaved && bLoaded
		&& Progress->GetQuestFlag(HintFlag) == 1 && !PC->ShowChestHintForProbe()
		&& Progress->BuildSaveData().SaveVersion == WRECKTIDE_SAVE_VERSION;

	// Same stall path as CursorProbe, followed by a rendered frame before every screen-point click.
	Pawn->SetActorLocation(Stall->GetActorLocation() + Stall->GetActorRightVector() * 160.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Stall->OpenShopMenu(Pawn);
	Result->bClick &= Stall->IsShopMenuOpen();
	TWeakObjectPtr<UWrecktideRewardSubsystem> WeakThis(this);
	TWeakObjectPtr<ACombatPlayerController> WeakPC(PC);
	TWeakObjectPtr<ACombatCharacter> WeakPawn(Pawn);
	TWeakObjectPtr<AWrecktideHUD> WeakHUD(HUD);
	TWeakObjectPtr<AWrecktideShopStall> WeakStall(Stall);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakThis, WeakPC, WeakPawn, WeakHUD, WeakStall, Result](float) -> bool
	{
		UWrecktideRewardSubsystem* Self = WeakThis.Get();
		ACombatPlayerController* StagePC = WeakPC.Get();
		AWrecktideHUD* StageHUD = WeakHUD.Get();
		AWrecktideShopStall* StageStall = WeakStall.Get();
		if (!Self || !StagePC || !StageHUD || !StageStall || !WeakPawn.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CHESTBUTTON-PROBE FAIL (button=0 map=0 keys=0 click=0 controls=0 hint=0) reason=stage-wiring"));
			if (Self)
			{
				UGameplayStatics::SetGamePaused(Self, false);
				Self->ScheduleProbeQuit(ChestButtonProbeQuitDelaySeconds);
			}
			return false;
		}
		const FVector2D ChestPoint = StageHUD->GetChestButtonRect().GetCenter();
		const FVector2D MapPoint = StageHUD->GetMapButtonRect().GetCenter();
		switch (Result->Stage++)
		{
		case 0:
			Result->bClick &= StagePC->IsCanvasModalCursorOn() && StageHUD->IsWeaponBarDrawn();
			Result->bClick &= StagePC->HandleModalClickAt(ChestPoint) && StagePC->IsInventoryOpen()
				&& !StageStall->IsShopMenuOpen() && StagePC->bShowMouseCursor && UGameplayStatics::IsGamePaused(StagePC);
			break;
		case 1:
			Result->bButton &= StageHUD->IsChestButtonLit();
			Result->bClick &= StagePC->HandleModalClickAt(ChestPoint) && !StagePC->IsInventoryOpen()
				&& !StagePC->bShowMouseCursor && !UGameplayStatics::IsGamePaused(StagePC);
			StagePC->SetRadarState(EWrecktideRadarState::Small);
			StageStall->OpenShopMenu(WeakPawn.Get());
			break;
		case 2:
			Result->bMap &= StagePC->IsCanvasModalCursorOn() && StagePC->HandleModalClickAt(MapPoint)
				&& StagePC->GetRadarState() == EWrecktideRadarState::Medium && !StageStall->IsShopMenuOpen()
				&& !StagePC->bShowMouseCursor && !UGameplayStatics::IsGamePaused(StagePC);
			StagePC->ToggleInventory();
			break;
		case 3:
			Result->bMap &= StagePC->IsInventoryOpen() && StagePC->HandleModalClickAt(MapPoint)
				&& StagePC->GetRadarState() == EWrecktideRadarState::Full && !StagePC->IsInventoryOpen()
				&& UGameplayStatics::IsGamePaused(StagePC);
			break;
		default:
			Result->bMap &= !StageHUD->IsWeaponBarDrawn() && !StageHUD->GetChestButtonRect().bIsValid
				&& !StageHUD->GetMapButtonRect().bIsValid;
			StagePC->SetRadarState(Result->OriginalRadar);
			// Always release a pause before ScheduleProbeQuit's world timer, even on failure.
			if (StageStall->IsShopMenuOpen()) { StageStall->ToggleShopMenu(WeakPawn.Get()); }
			if (StagePC->IsInventoryOpen()) { StagePC->ToggleInventory(); }
			UGameplayStatics::SetGamePaused(StagePC, false);
			const bool bPass = Result->bButton && Result->bMap && Result->bKeys
				&& Result->bClick && Result->bControls && Result->bHint;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHESTBUTTON-PROBE %s (button=%d map=%d keys=%d click=%d controls=%d hint=%d)"),
				bPass ? TEXT("PASS") : TEXT("FAIL"), Result->bButton, Result->bMap, Result->bKeys,
				Result->bClick, Result->bControls, Result->bHint);
			Self->ScheduleProbeQuit(ChestButtonProbeQuitDelaySeconds);
			return false;
		}
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHESTBUTTON stage=%d button=%d map=%d keys=%d click=%d controls=%d hint=%d"),
			Result->Stage, Result->bButton, Result->bMap, Result->bKeys, Result->bClick, Result->bControls, Result->bHint);
		return true;
	}), ChestButtonProbeStepSeconds);
}
#endif

// -CursorProbe: stall hover and purchase through real mouse input, then movement after closing.
void UWrecktideRewardSubsystem::RunCursorProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatPlayerController* CombatController = Cast<ACombatPlayerController>(PlayerController);
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	AWrecktideShopStall* Stall = nullptr;
	if (World)
	{
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
		{
			Stall = *It;
			break;
		}
	}
	if (!World || !CombatController || !PlayerPawn || !PlayerState || !Economy || !HUD || !Stall)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CURSOR-PROBE FAIL reason=wiring controller=%d pawn=%d state=%d hud=%d stall=%d"),
			CombatController != nullptr, PlayerPawn != nullptr, PlayerState != nullptr, HUD != nullptr, Stall != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Enough coin that the cheapest row is affordable, and stand inside the stall trigger.
	Economy->ApplyGoldDelta(400, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("CursorProbe")));
	PlayerPawn->SetActorLocation(Stall->GetActorLocation() + Stall->GetActorRightVector() * 160.0f,
		false, nullptr, ETeleportType::TeleportPhysics);
	Stall->OpenShopMenu(PlayerPawn);
	const bool bOpened = Stall->IsShopMenuOpen();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR stage open menu=%d rows=%d"), bOpened ? 1 : 0, Stall->GetOfferCount());
	if (!bOpened)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR-PROBE FAIL (the stall menu would not open)"));
		ScheduleProbeQuit(2.0f);
		return;
	}

	TWeakObjectPtr<ACombatPlayerController> WeakPC(CombatController);
	TWeakObjectPtr<ACombatCharacter> WeakPawn(PlayerPawn);
	TWeakObjectPtr<AWrecktidePlayerState> WeakPS(PlayerState);
	TWeakObjectPtr<AWrecktideHUD> WeakHUD(HUD);
	TWeakObjectPtr<AWrecktideShopStall> WeakStall(Stall);

	// Stage 2: the HUD has now drawn the list once, so the row rectangles exist. Park the cursor on
	// row 1 and let the controller's tick hover it.
	FTimerHandle HoverTimer;
	World->GetTimerManager().SetTimer(HoverTimer, FTimerDelegate::CreateWeakLambda(this,
		[this, WeakPC, WeakPawn, WeakPS, WeakHUD, WeakStall]()
	{
		UWorld* StageWorld = GetWorld();
		ACombatPlayerController* PC = WeakPC.Get();
		AWrecktideHUD* StageHUD = WeakHUD.Get();
		if (!StageWorld || !PC || !StageHUD)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR-PROBE FAIL (hover stage wiring)"));
			ScheduleProbeQuit(2.0f);
			return;
		}

		const bool bCursorOn = PC->bShowMouseCursor && PC->IsCanvasModalCursorOn();
		const int32 RowCount = StageHUD->GetShopMenuLineCount();
		const FBox2D HoverRect = StageHUD->GetShopRowRect(1);
		const bool bRectOK = RowCount >= 2 && HoverRect.bIsValid;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR stage cursor on=%d rows=%d rect=(%.0f,%.0f)-(%.0f,%.0f)"),
			bCursorOn ? 1 : 0, RowCount,
			bRectOK ? HoverRect.Min.X : 0.0f, bRectOK ? HoverRect.Min.Y : 0.0f,
			bRectOK ? HoverRect.Max.X : 0.0f, bRectOK ? HoverRect.Max.Y : 0.0f);
		if (!bRectOK)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR-PROBE FAIL (no drawn row rectangles)"));
			ScheduleProbeQuit(2.0f);
			return;
		}

		const FVector2D HoverPoint = HoverRect.GetCenter();
		PC->SetMouseLocation(FMath::RoundToInt(HoverPoint.X), FMath::RoundToInt(HoverPoint.Y));

		FTimerHandle ClickTimer;
		StageWorld->GetTimerManager().SetTimer(ClickTimer, FTimerDelegate::CreateWeakLambda(this,
			[this, WeakPC, WeakPawn, WeakPS, WeakHUD, WeakStall, bCursorOn]()
		{
			UWorld* ClickWorld = GetWorld();
			ACombatPlayerController* ClickPC = WeakPC.Get();
			AWrecktideHUD* ClickHUD = WeakHUD.Get();
			AWrecktidePlayerState* ClickPS = WeakPS.Get();
			UEconomyComponent* ClickEconomy = ClickPS ? ClickPS->GetEconomy() : nullptr;
			if (!ClickWorld || !ClickPC || !ClickHUD || !ClickPS || !ClickEconomy)
			{
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR-PROBE FAIL (click stage wiring)"));
				ScheduleProbeQuit(2.0f);
				return;
			}

			// Hover moved the selection with no key pressed at all.
			const int32 HoveredIndex = ClickHUD->GetShopMenuSelectedIndex();
			const bool bHoverOK = HoveredIndex == 1;

			// Now click row 0, the cheapest weapon, through the REAL mouse binding.
			const FBox2D ClickRect = ClickHUD->GetShopRowRect(0);
			const FVector2D ClickPoint = ClickRect.GetCenter();
			ClickPC->SetMouseLocation(FMath::RoundToInt(ClickPoint.X), FMath::RoundToInt(ClickPoint.Y));
			const int64 GoldBefore = ClickEconomy->GetGoldTotal();
			const bool bClickSent = SendProbeKey(ClickPC, EKeys::LeftMouseButton);
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR stage hover selected=%d expected=1 clickAt=(%.0f,%.0f) sent=%d gold=%lld"),
				HoveredIndex, ClickPoint.X, ClickPoint.Y, bClickSent ? 1 : 0, GoldBefore);

			FTimerHandle CloseTimer;
			ClickWorld->GetTimerManager().SetTimer(CloseTimer, FTimerDelegate::CreateWeakLambda(this,
				[this, WeakPC, WeakPawn, WeakPS, WeakHUD, WeakStall, bCursorOn, bHoverOK, bClickSent, GoldBefore]()
			{
				UWorld* CloseWorld = GetWorld();
				ACombatPlayerController* ClosePC = WeakPC.Get();
				AWrecktideHUD* CloseHUD = WeakHUD.Get();
				AWrecktidePlayerState* ClosePS = WeakPS.Get();
				UEconomyComponent* CloseEconomy = ClosePS ? ClosePS->GetEconomy() : nullptr;
				ACombatCharacter* ClosePawn = WeakPawn.Get();
				AWrecktideShopStall* CloseStall = WeakStall.Get();
				if (!CloseWorld || !ClosePC || !CloseHUD || !ClosePS || !CloseEconomy || !ClosePawn || !CloseStall)
				{
					UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR-PROBE FAIL (close stage wiring)"));
					ScheduleProbeQuit(2.0f);
					return;
				}

				const int32 ClickedIndex = CloseHUD->GetShopMenuSelectedIndex();
				const int64 GoldAfter = CloseEconomy->GetGoldTotal();
				const bool bClickOK = bClickSent && ClickedIndex == 0 && GoldAfter < GoldBefore
					&& ClosePS->EquippedWeaponRow != NAME_None;
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR stage click selected=%d gold %lld->%lld equipped=%s"),
					ClickedIndex, GoldBefore, GoldAfter, *ClosePS->EquippedWeaponRow.ToString());

				// Close every modal and prove the cursor and the game input come back.
				CloseStall->ToggleShopMenu(ClosePawn);
				const FVector StartLocation = ClosePawn->GetActorLocation();

				FTimerHandle MoveTimer;
				CloseWorld->GetTimerManager().SetTimer(MoveTimer, FTimerDelegate::CreateWeakLambda(this,
					[this, WeakPC, WeakPawn, bCursorOn, bHoverOK, bClickOK, StartLocation]()
				{
					UWorld* MoveWorld = GetWorld();
					ACombatPlayerController* MovePC = WeakPC.Get();
					ACombatCharacter* MovePawn = WeakPawn.Get();
					if (!MoveWorld || !MovePC || !MovePawn)
					{
						UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR-PROBE FAIL (move stage wiring)"));
						ScheduleProbeQuit(2.0f);
						return;
					}

					const bool bCursorOff = !MovePC->bShowMouseCursor && !MovePC->IsCanvasModalCursorOn();
					UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR stage closed cursorOff=%d"), bCursorOff ? 1 : 0);
					// Hold W through the real input path: the trap this guards is game input dying
					// after a menu closes, and only real movement proves it did not.
					SendProbeKey(MovePC, EKeys::W, IE_Pressed);

					FTimerHandle VerdictTimer;
					MoveWorld->GetTimerManager().SetTimer(VerdictTimer, FTimerDelegate::CreateWeakLambda(this,
						[this, WeakPC, WeakPawn, bCursorOn, bHoverOK, bClickOK, bCursorOff, StartLocation]()
					{
						ACombatPlayerController* VerdictPC = WeakPC.Get();
						ACombatCharacter* VerdictPawn = WeakPawn.Get();
						float Moved = 0.0f;
						if (VerdictPawn)
						{
							Moved = FVector::Dist2D(VerdictPawn->GetActorLocation(), StartLocation);
						}
						if (VerdictPC)
						{
							SendProbeKey(VerdictPC, EKeys::W, IE_Released);
						}
						const bool bMoveOK = Moved > 20.0f;
						const bool bPass = bCursorOn && bHoverOK && bClickOK && bCursorOff && bMoveOK;
						UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR stage input moved=%.0f uu (needs > 20)"), Moved);
						UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CURSOR-PROBE %s (cursorOn=%d hover=%d click=%d cursorOff=%d move=%d)"),
							bPass ? TEXT("PASS") : TEXT("FAIL"), bCursorOn ? 1 : 0, bHoverOK ? 1 : 0,
							bClickOK ? 1 : 0, bCursorOff ? 1 : 0, bMoveOK ? 1 : 0);
						ScheduleProbeQuit(2.0f);
					}), 0.9f, false);
				}), 0.4f, false);
			}), 0.4f, false);
		}), 0.4f, false);
	}), 0.5f, false);
}


// ---------------------------------------------------------------------------------------------
// -InventoryProbe (spec C18 and C19): the chest screen. Tab opens it and pauses, the pages switch,
// the Journal page lists the ACTIVE mission with its giver and marks it followed, choosing a line
// moves what is followed, the Belt page wields an owned blade into slot 1, and Tab closes it and
// hands the world back. Driven through the model the keys and the mouse both use.
// Synchronous on purpose: nothing here waits on an input flush, and the chest pauses the world, so
// a chain of world timers would never fire (the -MenuProbe lesson).
// ---------------------------------------------------------------------------------------------
void UWrecktideRewardSubsystem::RunInventoryProbe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatPlayerController* CombatController = Cast<ACombatPlayerController>(PlayerController);
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!World || !CombatController || !PlayerState || !Progress || !Economy || !DataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CHEST-PROBE FAIL reason=wiring controller=%d state=%d progress=%d data=%d"),
			CombatController != nullptr, PlayerState != nullptr, Progress != nullptr, DataSubsystem != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	// Something to look at: an owned blade and an accepted story mission, both through the real paths.
	const FName CutlassRow(TEXT("WornCutlass"));
	Economy->ApplyGoldDelta(200, EGoldSource::Earned, EGoldReason::Adjustment, FName(TEXT("ChestProbe")));
	Progress->UnlockItem(CutlassRow);
	PlayerState->EquipWeapon(NAME_None);
	const bool bAccepted = WrecktideMissionLogic::Accept(PlayerState);
	const FName ActiveMission = WrecktideMissionLogic::GetCurrentMissionRow(Progress, DataSubsystem);
	const FMissionTableRow* MissionRow = ActiveMission.IsNone() ? nullptr : DataSubsystem->GetMissionRow(ActiveMission);

	// Stage A: Tab opens the chest and pauses the world.
	CombatController->ToggleInventory();
	const bool bOpen = CombatController->IsInventoryOpen();
	const bool bPaused = UGameplayStatics::IsGamePaused(World);
	const bool bCursor = CombatController->bShowMouseCursor;
	const bool bTitleOK = CombatController->GetMenuTitle() == TEXT("Captain's Chest");
	const bool bOpenOK = bOpen && bPaused && bCursor && bTitleOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST stage open open=%d paused=%d cursor=%d title=[%s]"),
		bOpen ? 1 : 0, bPaused ? 1 : 0, bCursor ? 1 : 0, *CombatController->GetMenuTitle());

	// Stage B: every page builds rows of its own.
	int32 PagesWithRows = 0;
	FString PageReport;
	for (int32 TabIndex = 0; TabIndex < static_cast<int32>(EWrecktideChestTab::Count); ++TabIndex)
	{
		CombatController->SetChestTab(static_cast<EWrecktideChestTab>(TabIndex));
		const int32 Rows = CombatController->GetMenuRows().Num();
		PagesWithRows += Rows > 0 ? 1 : 0;
		PageReport += FString::Printf(TEXT("[%s %d rows]"), *CombatController->GetMenuSubtitle(), Rows);
	}
	const bool bPagesOK = PagesWithRows == static_cast<int32>(EWrecktideChestTab::Count);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST stage pages %s"), *PageReport);

	// Stage C: the journal lists the active mission with its giver and marks it followed.
	CombatController->SetChestTab(EWrecktideChestTab::Journal);
	const FString ExpectedTitle = MissionRow && !MissionRow->Title.IsEmpty()
		? MissionRow->Title.ToString() : ActiveMission.ToString();
	const FString ExpectedGiver = MissionRow && !MissionRow->Giver.IsEmpty()
		? MissionRow->Giver.ToString() : FString();
	bool bJournalListed = false;
	bool bGiverListed = false;
	bool bFollowed = false;
	int32 JournalRowIndex = INDEX_NONE;
	const TArray<FWrecktideMenuRow>& JournalRows = CombatController->GetMenuRows();
	for (int32 Index = 0; Index < JournalRows.Num(); ++Index)
	{
		if (JournalRows[Index].Label.Contains(ExpectedTitle))
		{
			bJournalListed = true;
			JournalRowIndex = Index;
			bGiverListed = !ExpectedGiver.IsEmpty() && JournalRows[Index].SubLine.Contains(ExpectedGiver);
			bFollowed = JournalRows[Index].Label.StartsWith(TEXT("* "))
				&& JournalRows[Index].SubLine.Contains(TEXT("(following)"));
			break;
		}
	}
	TArray<WrecktideMissionLogic::FMissionJournalEntry> Entries;
	WrecktideMissionLogic::GetJournalEntries(PlayerState, Entries);
	int32 TrackedCount = 0;
	for (const WrecktideMissionLogic::FMissionJournalEntry& Entry : Entries)
	{
		TrackedCount += Entry.bTracked ? 1 : 0;
	}
	const bool bJournalOK = bAccepted && bJournalListed && bGiverListed && bFollowed && TrackedCount == 1;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST stage journal accepted=%d title=[%s] listed=%d giver=[%s] giverListed=%d followed=%d entries=%d tracked=%d"),
		bAccepted ? 1 : 0, *ExpectedTitle, bJournalListed ? 1 : 0, *ExpectedGiver, bGiverListed ? 1 : 0,
		bFollowed ? 1 : 0, Entries.Num(), TrackedCount);

	// Stage D: choosing a journal line moves what is followed (the same path Enter and a click take).
	bool bTrackMoved = false;
	if (JournalRowIndex != INDEX_NONE)
	{
		// Clicking the line goes through the mission logic's own tracking, which is what moves the
		// beacon: the assertion is that the tracked ROW is the one the row clicked names.
		CombatController->MenuClick(JournalRowIndex);
		bTrackMoved = WrecktideMissionLogic::GetTrackedMissionRow(Progress, DataSubsystem) == ActiveMission;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST stage follow row=%d tracked=%s moved=%d"),
		JournalRowIndex, *WrecktideMissionLogic::GetTrackedMissionRow(Progress, DataSubsystem).ToString(),
		bTrackMoved ? 1 : 0);

	// Stage E: the Belt page wields an owned blade into slot 1.
	CombatController->SetChestTab(EWrecktideChestTab::Weapons);
	int32 BeltRowIndex = INDEX_NONE;
	const FWeaponTableRow* CutlassData = DataSubsystem->GetWeaponRow(CutlassRow);
	const FString CutlassLabel = CutlassData ? CutlassData->DisplayName.ToString() : CutlassRow.ToString();
	const TArray<FWrecktideMenuRow>& BeltRows = CombatController->GetMenuRows();
	for (int32 Index = 0; Index < BeltRows.Num(); ++Index)
	{
		if (BeltRows[Index].bSelectable && BeltRows[Index].Label == CutlassLabel)
		{
			BeltRowIndex = Index;
			break;
		}
	}
	bool bWieldOK = false;
	if (BeltRowIndex != INDEX_NONE)
	{
		CombatController->MenuClick(BeltRowIndex);
		bWieldOK = PlayerState->GetAssignedSlot(1) == CutlassRow && PlayerState->EquippedWeaponRow == CutlassRow;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST stage belt row=%d label=[%s] slot1=%s equipped=%s"),
		BeltRowIndex, *CutlassLabel, *PlayerState->GetAssignedSlot(1).ToString(),
		*PlayerState->EquippedWeaponRow.ToString());

	// Stage F: Tab closes it, the world runs again and the cursor is gone.
	CombatController->ToggleInventory();
	const bool bClosed = !CombatController->IsInventoryOpen() && !UGameplayStatics::IsGamePaused(World)
		&& !CombatController->bShowMouseCursor;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST stage close closed=%d paused=%d cursor=%d"),
		CombatController->IsInventoryOpen() ? 0 : 1, UGameplayStatics::IsGamePaused(World) ? 1 : 0,
		CombatController->bShowMouseCursor ? 1 : 0);

	const bool bPass = bOpenOK && bPagesOK && bJournalOK && bTrackMoved && bWieldOK && bClosed;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST-PROBE %s (open=%d pages=%d journal=%d follow=%d belt=%d close=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bOpenOK ? 1 : 0, bPagesOK ? 1 : 0, bJournalOK ? 1 : 0,
		bTrackMoved ? 1 : 0, bWieldOK ? 1 : 0, bClosed ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}

// ---------------------------------------------------------------------------------------------
// -Island2Probe (C34.8, 2026-09-07): GALLOWS REACH is a place you can sail to, land on and fight
// on. Two stages, because the chart token needs a drawn frame after the pawn is teleported:
//   stage 1 - the tiers are under the documented spots, navigation projects onto the field, the
//             sea volume contains a point 2,000 uu off the beach, five or more posts with the new
//             rows stand on it (one of them the boss), Ada spawned, Cobb offers H04 with
//             Story_Harbourmaster set, Ada brushes off without Story_Reach and offers R01 with it.
//             Then the pawn is put on the garrison square.
//   stage 2 - the HUD drew the Gallows Reach plate and title, and the arrival flag is set.
// Judge: WRECKTIDE-ISLAND2-PROBE PASS or FAIL, verdict word first.
// ---------------------------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
bool UWrecktideRewardSubsystem::CheckIsland3Terrain(bool& bTiers, bool& bNav, bool& bSea) const
{
	UWorld* World = GetWorld();
	bTiers = World != nullptr;
	bNav = bSea = false;
	if (!World) { return false; }
	struct FMangroveTierSample { const TCHAR* Name; FVector Spot; };
	const FMangroveTierSample Samples[] = {
		{TEXT("beach"), FVector(-14000, 9000, -40)},
		{TEXT("field"), FVector(-16500, 11000, 0)},
		{TEXT("bog"), FVector(-14000, 13000, -20)},
		{TEXT("village"), FVector(-11900, 12100, 60)},
		{TEXT("altar"), FVector(-16250, 14450, 80)},
		{TEXT("village_step"), FVector(-11900, 11250, 20)},
		{TEXT("altar_step"), FVector(-16100, 13850, 40)}
	};
	const FCollisionObjectQueryParams Statics(ECC_TO_BITFIELD(ECC_WorldStatic));
	for (const FMangroveTierSample& Sample : Samples)
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByObjectType(Hit, Sample.Spot + FVector(0, 0, 3000),
			Sample.Spot - FVector(0, 0, 2000), Statics);
		const bool bOK = bHit && FMath::Abs(Hit.ImpactPoint.Z - Sample.Spot.Z) <= 5.0;
		bTiers &= bOK;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3 terrain %s actual=%.1f expected=%.1f ok=%d"),
			Sample.Name, bHit ? Hit.ImpactPoint.Z : -9999.0, Sample.Spot.Z, bOK);
	}
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation Village, Altar, Landing;
		const FVector Query(100, 100, 200);
		const bool bVillage = Nav->ProjectPointToNavigation(FVector(-11900, 12100, 60), Village, Query);
		const bool bAltar = Nav->ProjectPointToNavigation(FVector(-16250, 14450, 80), Altar, Query);
		const bool bLanding = Nav->ProjectPointToNavigation(FVector(-11200, 9900, -40), Landing, Query);
		UNavigationPath* Path = bVillage && bAltar
			? Nav->FindPathToLocationSynchronously(World, Village.Location, Altar.Location) : nullptr;
		UNavigationPath* Approach = bLanding && bVillage
			? Nav->FindPathToLocationSynchronously(World, Landing.Location, Village.Location) : nullptr;
		bNav = bVillage && bAltar && bLanding && FMath::Abs(Village.Location.Z - 60.0) < 15.0
			&& Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() >= 2
			&& Approach && Approach->IsValid() && !Approach->IsPartial();
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3 nav village=%d altar=%d landing=%d path=%d approach=%d ok=%d"),
			bVillage, bAltar, bLanding, Path ? Path->PathPoints.Num() : 0,
			Approach ? Approach->PathPoints.Num() : 0, bNav);
	}
	const FVector Offshore(-20500, 13000, -400);
	for (TActorIterator<APhysicsVolume> It(World); It; ++It)
	{
		bSea |= It->bWaterVolume && It->EncompassesPoint(Offshore);
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3 sea offshore=%s contains=%d"), *Offshore.ToString(), bSea);
	return bTiers && bNav && bSea;
}

void UWrecktideRewardSubsystem::RunIsland3TerrainProbe()
{
	bool bTiers, bNav, bSea;
	const bool bPass = CheckIsland3Terrain(bTiers, bNav, bSea);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3TERRAIN-PROBE %s (tiers=%d nav=%d sea=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), bTiers, bNav, bSea);
	ScheduleProbeQuit(2.0f);
}

bool UWrecktideRewardSubsystem::CheckIsland3Population() const
{
	UWorld* World = GetWorld();
	const UWrecktideDataSubsystem* Data = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!Data) { return false; }
	auto IsMangroveSpot = [](const FVector& Spot)
	{
		return FMath::Abs(Spot.X + 14000.0) <= 4500.0 && FMath::Abs(Spot.Y - 13000.0) <= 4500.0;
	};
	int32 Posts = 0, Chests = 0;
	for (TActorIterator<ACombatEnemySpawner> It(World); It; ++It) { Posts += IsMangroveSpot(It->GetActorLocation()); }
	TMap<FName, int32> Counts;
	bool bRows = true, bCurse = true, bSkins = true;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		if (!IsMangroveSpot(It->GetHomeLocation())) { continue; }
		++Counts.FindOrAdd(It->EnemyRowName);
		const FEnemyTableRow* Row = Data->GetEnemyRow(It->EnemyRowName);
		bRows &= Row && It->Stats && It->Stats->IsAlive() && Row->AggroRadius == 1100.0f && Row->LeashRadius == 2600.0f
			&& FMath::IsNearlyEqual(It->Stats->GetMaxHealth(), Row->MaxHealth);
		bSkins &= Row && It->GetCosmeticMesh() && It->GetCosmeticMesh()->GetMaterial(0) == Row->SkinMaterial.Get();
		if (It->EnemyRowName == FName(TEXT("HexAcolyte")))
		{
			const UPointLightComponent* Light = It->GetRangedProbeLight();
			bCurse &= It->bRangedUnarmed && It->RangedRange == 1400.0f && It->RangedDamage == 10.0f
				&& It->RangedCooldownSeconds == 3.0f && It->RangedWindupSeconds == 0.8f
				&& It->GetRangedWeaponMesh() && !It->GetRangedWeaponMesh()->GetStaticMesh()
				&& Light && Light->GetLightColor().G > Light->GetLightColor().R;
		}
	}
	for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
	{
		if (It->ChestId == FName(TEXT("AltarChest")) && IsMangroveSpot(It->GetActorLocation())
			&& It->GetPayoutGold() == 180) { ++Chests; }
	}
	const bool bCount = Posts == 7 && Counts.Num() == 4 && Counts.FindRef(TEXT("BogCultist")) == 3
		&& Counts.FindRef(TEXT("HexAcolyte")) == 2 && Counts.FindRef(TEXT("BogWraith")) == 1 && Counts.FindRef(TEXT("HexMother")) == 1;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3 population posts=%d counts=%d rows=%d curse=%d skins=%d chests=%d"),
		Posts, bCount, bRows, bCurse, bSkins, Chests);
	return bCount && bRows && bCurse && bSkins && Chests == 1;
}

void UWrecktideRewardSubsystem::RunIsland3Probe()
{
	Island3Result = FMangroveIslandProbeState();
	CheckIsland3Terrain(Island3Result.Tiers, Island3Result.Nav, Island3Result.Sea);
	Island3Result.Posts = CheckIsland3Population();
	if (!FParse::Param(FCommandLine::Get(), TEXT("Island3ChartProbe")))
	{
		CheckIsland3Story(Island3Result.Giver, Island3Result.Hook, Island3Result.Board);
	}
	ACombatPlayerController* PC = Cast<ACombatPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
	if (PC && PC->GetPawn() && HUD)
	{
		Island3Result.PreviousRadar = static_cast<int32>(PC->GetRadarState());
		PC->GetPawn()->SetActorLocation(FVector(-11900, 12100, 170), false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetRadarState(EWrecktideRadarState::Medium);
	}
	GetWorld()->GetTimerManager().SetTimer(Island3ProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::BeginIsland3Chart), 1.5f, false);
}

void UWrecktideRewardSubsystem::BeginIsland3Chart()
{
	ACombatPlayerController* PC = Cast<ACombatPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
	Island3Result.Radar = HUD && HUD->GetDrawnChartId() == TEXT("MangroveHollow") && HUD->DidDrawMapPlate()
		&& HUD->GetLoadedChartPlateCount() == WrecktideMap::GetIslandCharts().Num();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3 radar state=%d drawn=%s plate=%d plateCount=%d"),
		PC ? static_cast<int32>(PC->GetRadarState()) : -1, HUD ? *HUD->GetDrawnChartId() : TEXT("none"),
		HUD && HUD->DidDrawMapPlate(), HUD ? HUD->GetLoadedChartPlateCount() : 0);
	if (PC) { PC->SetRadarState(EWrecktideRadarState::Full); }
	// The full chart pauses world timers. A one-shot core ticker can read its rendered frame.
	TWeakObjectPtr<UWrecktideRewardSubsystem> WeakMangrove(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakMangrove](float)
	{
		if (WeakMangrove.IsValid()) { WeakMangrove->FinishIsland3Probe(); }
		return false;
	}), 1.0f);
}

void UWrecktideRewardSubsystem::FinishIsland3Probe()
{
	ACombatPlayerController* PC = Cast<ACombatPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	AWrecktideHUD* HUD = PC ? Cast<AWrecktideHUD>(PC->GetHUD()) : nullptr;
	const AWrecktidePlayerState* PS = PC ? PC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	const UProgressComponent* Progress = PS ? PS->GetProgress() : nullptr;
	const bool bArrival = Progress && Progress->GetQuestFlag(TEXT("Seen_MangroveHollow")) == 1;
	const bool bChart = Island3Result.Radar && HUD && HUD->IsMapScreenOpen() && HUD->DidDrawFullMapPlate()
		&& HUD->GetDrawnChartId() == TEXT("MangroveHollow") && HUD->GetDrawnChartTitle() == TEXT("MANGROVE HOLLOW")
		&& bArrival && PC && PC->GetPawn()
		&& FString(WrecktideMap::ChartFor(PC->GetPawn()->GetActorLocation()).Id) == TEXT("MangroveHollow")
		&& FString(WrecktideMap::ChartFor(FVector(0, -5200, 0)).Id) == TEXT("WreckCove");
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3 chart radar=%d plateCount=%d fullPlate=%d title=%s arrival=%d"),
		Island3Result.Radar, HUD ? HUD->GetLoadedChartPlateCount() : 0, HUD && HUD->DidDrawFullMapPlate(),
		HUD ? *HUD->GetDrawnChartTitle() : TEXT("none"), bArrival);
	if (PC) { PC->SetRadarState(static_cast<EWrecktideRadarState>(Island3Result.PreviousRadar)); }
	UGameplayStatics::SetGamePaused(this, false);
	if (FParse::Param(FCommandLine::Get(), TEXT("Island3ChartProbe")))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3CHART-PROBE %s (chart=%d arrival=%d)"),
			bChart ? TEXT("PASS") : TEXT("FAIL"), bChart, bArrival);
	}
	else
	{
		const bool bPass = Island3Result.Tiers && Island3Result.Nav && Island3Result.Sea && Island3Result.Posts
			&& Island3Result.Giver && Island3Result.Hook && Island3Result.Board && bChart;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3-PROBE %s (tiers=%d nav=%d sea=%d posts=%d giver=%d hook=%d board=%d chart=%d)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), Island3Result.Tiers, Island3Result.Nav, Island3Result.Sea,
			Island3Result.Posts, Island3Result.Giver, Island3Result.Hook, Island3Result.Board, bChart);
	}
	ScheduleProbeQuit(2.0f);
}

bool UWrecktideRewardSubsystem::CheckIsland3Story(bool& bGiver, bool& bHook, bool& bBoard)
{
	bGiver = bHook = bBoard = false;
	UWorld* World = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ACombatCharacter* Pawn = PC ? Cast<ACombatCharacter>(PC->GetPawn()) : nullptr;
	AWrecktidePlayerState* PS = PC ? PC->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PS ? PS->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!World || !Pawn || !Progress || !Data) { return false; }
	AWrecktideMissionGiver* Ada = nullptr;
	AWrecktideMissionGiver* Oduya = nullptr;
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->GetGiverId() == FName(TEXT("Ada"))) { Ada = *It; }
		if (It->GetGiverId() == FName(TEXT("Oduya"))) { Oduya = *It; }
	}
	AWrecktideQuestBoard* Board = nullptr;
	int32 Boards = 0;
	bool bCampFirst = false;
	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		if (Boards++ == 0) { bCampFirst = FVector::Dist2D(It->GetActorLocation(), FVector(400, -5200, 0)) < 500; }
		if (FVector::Dist2D(It->GetActorLocation(), FVector(WrecktideMap::MangroveHollow::BoardX,
			WrecktideMap::MangroveHollow::BoardY, 0)) < 50) { Board = *It; }
	}
	if (!Ada || !Oduya || !Board) { return false; }
	auto Offer = [Progress, Data](const TCHAR* Id) { return WrecktideMissionLogic::GetOfferForGiver(Progress, Data, FName(Id)); };
	auto Converse = [this](AWrecktideMissionGiver* Giver, EMissionGiverAction Action)
	{
		const bool bOpened = OpenGiverForFlow(Giver);
		const bool bAction = bOpened && Giver->SelectActionLine(Action);
		if (bAction) { Giver->ConfirmMenuSelection(); }
		Giver->CloseGiverMenu();
		return bAction;
	};
	const bool bOpenedLocked = OpenGiverForFlow(Oduya);
	const bool bBrushOff = bOpenedLocked && Offer(TEXT("Oduya")).IsNone()
		&& Oduya->GetMenuLineCount() == 2 && !Oduya->SelectActionLine(EMissionGiverAction::Accept)
		&& Oduya->GetMenuLine(0) == WrecktideMissionLogic::GetGiverBrushOff(Data, TEXT("Oduya"))
		&& !Oduya->GetMenuLine(0).IsEmpty();
	Oduya->CloseGiverMenu();
	Pawn->SetActorLocation(Board->GetActorLocation() + FVector(150, 0, 30), false, nullptr, ETeleportType::TeleportPhysics);
	Board->OpenBoardMenu(Pawn);
	const int32 LockedLines = Board->GetMenuLineCount();
	const bool bBoardLocked = Board->IsMenuOpen() && !Board->SelectQuestRow(TEXT("DrownedWatch"))
		&& !Board->SelectQuestRow(TEXT("CurseKeepers"))
		&& !WrecktideQuestLogic::CanAccept(Progress, Data, TEXT("DrownedWatch"))
		&& !WrecktideQuestLogic::CanAccept(Progress, Data, TEXT("CurseKeepers"));
	Board->ToggleBoardMenu(Pawn);
	Progress->SetQuestFlag(TEXT("Story_Reach"), 1);
	for (const TCHAR* Row : {TEXT("R01_ThinTheWatch"), TEXT("R02_Magazine"), TEXT("R03_TheLight"), TEXT("R04_Harrow")})
	{
		Progress->SetQuestFlag(WrecktideMissionLogic::DoneKey(FName(Row)), 1);
	}
	const bool bHookLocked = Offer(TEXT("Ada")).IsNone();
	Progress->SetQuestFlag(TEXT("Story_Gallows"), 1);
	const bool bR05 = Offer(TEXT("Ada")) == FName(TEXT("R05_TheHollow"));
	const bool bAccepted = Converse(Ada, EMissionGiverAction::Accept);
	WrecktideMissionLogic::NotifyLocation(PS, FVector::ZeroVector);
	const bool bStillTravelling = Progress->GetQuestFlag(TEXT("Story_Hollow")) == 0;
	const FVector Landing(WrecktideMap::MangroveHollow::LandingX, WrecktideMap::MangroveHollow::LandingY, -40);
	Pawn->SetActorLocation(Landing + FVector(0, 0, 110), false, nullptr, ETeleportType::TeleportPhysics);
	WrecktideMissionLogic::NotifyLocation(PS, Pawn->GetActorLocation());
	const bool bLanded = Progress->GetQuestFlag(TEXT("Story_Hollow")) == 1
		&& Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(TEXT("R05_TheHollow"))) == 1;
	bHook = bHookLocked && bR05 && bAccepted && bStillTravelling && bLanded;
	bGiver = bBrushOff && Offer(TEXT("Oduya")) == FName(TEXT("V01_DrownedBells"))
		&& FVector::Dist2D(Oduya->GetActorLocation(), FVector(-11500, 11700, 60)) < 20
		&& FMath::Abs(Oduya->GetActorLocation().Z - 60) < 5;
	Pawn->SetActorLocation(Board->GetActorLocation() + FVector(150, 0, 30), false, nullptr, ETeleportType::TeleportPhysics);
	Board->OpenBoardMenu(Pawn);
	bBoard = bBoardLocked && Boards == 3 && bCampFirst && Board->IsMenuOpen()
		&& Board->SelectQuestRow(TEXT("DrownedWatch")) && Board->SelectQuestRow(TEXT("CurseKeepers"))
		&& WrecktideQuestLogic::CanAccept(Progress, Data, TEXT("DrownedWatch"))
		&& WrecktideQuestLogic::CanAccept(Progress, Data, TEXT("CurseKeepers"))
		&& Board->GetMenuLineCount() == LockedLines + 4; // Story_Reach also reveals Gallows' two rows.
	Board->ToggleBoardMenu(Pawn);
	// Work all four new objectives through live deaths, the actual chest and giver menus.
	auto KillRow = [World, Pawn, PC](const TCHAR* Row)
	{
		for (TActorIterator<ACombatEnemy> It(World); It; ++It)
		{
			if (It->EnemyRowName == FName(Row) && It->Stats && It->Stats->IsAlive())
			{
				// Stats caps each hit at 40 percent of max health, so use the normal repeated-hit path.
				for (int32 Hit = 0; Hit < 4 && It->Stats->IsAlive(); ++Hit)
				{
					It->TakeDamage(10000.0f, FDamageEvent(), PC, Pawn);
				}
			}
		}
	};
	bool bChain = Converse(Oduya, EMissionGiverAction::Accept);
	KillRow(TEXT("BogCultist"));
	bChain &= Converse(Oduya, EMissionGiverAction::Complete);
	bChain &= Offer(TEXT("Oduya")) == FName(TEXT("V02_TheAltar")) && Converse(Oduya, EMissionGiverAction::Accept);
	bool bChest = false;
	for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
	{
		if (It->ChestId == FName(TEXT("AltarChest")))
		{
			Pawn->SetActorLocation(It->GetActorLocation() + FVector(150, 0, 110), false, nullptr, ETeleportType::TeleportPhysics);
			bChest = It->Open(Pawn);
		}
	}
	bChain &= bChest && Converse(Oduya, EMissionGiverAction::Complete);
	bChain &= Offer(TEXT("Oduya")) == FName(TEXT("V03_Acolytes")) && Converse(Oduya, EMissionGiverAction::Accept);
	KillRow(TEXT("HexAcolyte"));
	bChain &= Converse(Oduya, EMissionGiverAction::Complete);
	bChain &= Offer(TEXT("Oduya")) == FName(TEXT("V04_HexMother")) && Converse(Oduya, EMissionGiverAction::Accept);
	KillRow(TEXT("HexMother"));
	bChain &= Converse(Oduya, EMissionGiverAction::Complete);
	bChain &= Progress->GetQuestFlag(TEXT("Story_Hollowed")) == 1 && Offer(TEXT("Oduya")) == FName(TEXT("W11_BogSweep"));
	const FMissionTableRow* Pay = Data->GetMissionRow(TEXT("W12_LampOil"));
	bChain &= Pay && Pay->bRepeatable && Pay->Type == EMissionObjective::Pay && Pay->TargetCount == 40;
	bGiver &= bChain;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3 story brush=%d r05=%d landed=%d chain=%d giver=%d hook=%d board=%d campFirst=%d"),
		bBrushOff, bR05, bLanded, bChain, bGiver, bHook, bBoard, bCampFirst);
	return bGiver && bHook && bBoard;
}

void UWrecktideRewardSubsystem::RunIsland3StoryProbe()
{
	bool bTiers, bNav, bSea, bGiver, bHook, bBoard;
	const bool bTerrain = CheckIsland3Terrain(bTiers, bNav, bSea);
	const bool bPosts = CheckIsland3Population();
	const bool bStory = CheckIsland3Story(bGiver, bHook, bBoard);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3STORY-PROBE %s (tiers=%d nav=%d sea=%d posts=%d giver=%d hook=%d board=%d)"),
		bTerrain && bPosts && bStory ? TEXT("PASS") : TEXT("FAIL"), bTiers, bNav, bSea, bPosts, bGiver, bHook, bBoard);
	ScheduleProbeQuit(2.0f);
}

void UWrecktideRewardSubsystem::RunIsland3PopulationProbe()
{
	bool bTiers, bNav, bSea;
	const bool bTerrain = CheckIsland3Terrain(bTiers, bNav, bSea);
	const bool bPosts = CheckIsland3Population();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND3POPULATION-PROBE %s (tiers=%d nav=%d sea=%d posts=%d)"),
		bTerrain && bPosts ? TEXT("PASS") : TEXT("FAIL"), bTiers, bNav, bSea, bPosts);
	ScheduleProbeQuit(2.0f);
}
#endif

void UWrecktideRewardSubsystem::RunIsland2Probe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ACombatCharacter* PlayerPawn = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!World || !PlayerPawn || !Progress || !Data)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2-PROBE FAIL (wiring world=%d pawn=%d progress=%d data=%d)"),
			World != nullptr, PlayerPawn != nullptr, Progress != nullptr, Data != nullptr);
		ScheduleProbeQuit(2.0f);
		return;
	}

	const float Isle2X = WrecktideMap::GallowsReach::CentreX;
	const float Isle2Y = WrecktideMap::GallowsReach::CentreY;
	const float Isle2Half = WrecktideMap::GallowsReach::BeachHalf;

	// ---- tiers: trace onto WorldStatic only, so a navy marine standing on a tier cannot answer
	// for it. Every expected height comes straight from Tools\island2_build.py's header block.
	auto GroundZ = [World](float X, float Y) -> float
	{
		FHitResult Hit;
		const FCollisionObjectQueryParams Statics(ECC_TO_BITFIELD(ECC_WorldStatic));
		if (World->LineTraceSingleByObjectType(Hit, FVector(X, Y, 3000.0f), FVector(X, Y, -2000.0f), Statics))
		{
			return static_cast<float>(Hit.ImpactPoint.Z);
		}
		return -9999.0f;
	};

	// RESERVED SAMPLE SPOTS. Every one of these must stay clear of props, and Tools\island2_dress.py
	// carries the same list in a comment. The first version sampled (12000,-20000) and the tower
	// centre; the dressing pass then put a 900 uu boulder on the first and the lighthouse lantern on
	// the second, and the trace honestly reported their roofs (362 and 797) instead of the tier.
	struct FIsland2TierCheck { const TCHAR* Name; float X; float Y; float ExpectedZ; };
	const FIsland2TierCheck Tiers[] = {
		{TEXT("beach"), 13800.0f, -20000.0f, -40.0f},
		{TEXT("field"), 13000.0f, -14500.0f, 0.0f},
		{TEXT("square"), 11400.0f, -16800.0f, 40.0f},
		{TEXT("lighthouse"), 14350.0f, -13950.0f, 600.0f}
	};
	bool bTiersOK = true;
	for (const FIsland2TierCheck& Tier : Tiers)
	{
		const float TierZ = GroundZ(Tier.X, Tier.Y);
		const bool bTierOK = FMath::Abs(TierZ - Tier.ExpectedZ) <= 5.0f;
		bTiersOK &= bTierOK;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 tier %s at (%.0f, %.0f) z=%.0f want=%.0f %s"),
			Tier.Name, Tier.X, Tier.Y, TierZ, Tier.ExpectedZ, bTierOK ? TEXT("OK") : TEXT("BAD"));
	}

	// The stair has to be walkable: every tread top within one MaxStepHeight (45) of the last.
	float PreviousTread = 0.0f;
	bool bStairOK = true;
	for (int32 Step = 15; Step >= 0; --Step)
	{
		const float TreadX = 13700.0f - 200.0f * Step;
		const float TreadZ = GroundZ(TreadX, -13800.0f);
		const bool bTreadOK = TreadZ > -9000.0f && (TreadZ - PreviousTread) <= 45.0f && (TreadZ - PreviousTread) >= -5.0f;
		bStairOK &= bTreadOK;
		if (!bTreadOK)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 stair tread %d at x=%.0f z=%.0f rise=%.0f BAD"),
				Step, TreadX, TreadZ, TreadZ - PreviousTread);
		}
		PreviousTread = TreadZ;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 stair walkable=%d top tread z=%.0f"), bStairOK ? 1 : 0, PreviousTread);
	bTiersOK &= bStairOK;

	// ---- navigation on the new island, the same truth test -NavProbe uses
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	bool bNavOK = false;
	if (NavSys)
	{
		// The route that has to exist for the island to be playable: from where the boat puts you
		// ashore to the gallows where Harrow stands. Two arbitrary field coordinates were the first
		// version and they landed in a pocket the garrison props pinched shut, which said nothing
		// about whether the island can be walked.
		const FVector FieldA(WrecktideMap::GallowsReach::LandingX, WrecktideMap::GallowsReach::LandingY, -40.0f);
		const FVector FieldB(11400.0f, -16000.0f, 40.0f);
		FNavLocation ProjectedA;
		FNavLocation ProjectedB;
		const FVector Query(800.0f, 800.0f, 800.0f);
		const bool bProjectedA = NavSys->ProjectPointToNavigation(FieldA, ProjectedA, Query);
		const bool bProjectedB = NavSys->ProjectPointToNavigation(FieldB, ProjectedB, Query);
		// PROJECT BOTH ENDS FIRST, the way every gameplay path request does. A raw coordinate can
		// land inside a tent or a fort wall once the island is dressed, and the resulting PARTIAL
		// path would read as "no navmesh on the island" when the navmesh is fine.
		UNavigationPath* Path = (bProjectedA && bProjectedB)
			? NavSys->FindPathToLocationSynchronously(World, ProjectedA.Location, ProjectedB.Location)
			: nullptr;
		const bool bPathOK = Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() >= 2;
		bNavOK = bProjectedA && bProjectedB && bPathOK;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 nav projectedA=%d at %s projectedB=%d at %s path=%d points=%d"),
			bProjectedA ? 1 : 0, *ProjectedA.Location.ToString(), bProjectedB ? 1 : 0,
			*ProjectedB.Location.ToString(), bPathOK ? 1 : 0, Path ? Path->PathPoints.Num() : 0);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 nav FAIL reason=no-navigation-system"));
	}

	// ---- sea: a point 2,000 uu off the south beach has to be inside the swim volume
	const FVector OffshorePoint(Isle2X, Isle2Y - Isle2Half - 2000.0f, -400.0f);
	bool bSeaOK = false;
	for (TActorIterator<APhysicsVolume> It(World); It; ++It)
	{
		APhysicsVolume* Volume = *It;
		if (!Volume || !Volume->bWaterVolume)
		{
			continue;
		}
		if (Volume->EncompassesPoint(OffshorePoint))
		{
			bSeaOK = true;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 sea volume %s contains the offshore point %s"),
				*Volume->GetName(), *OffshorePoint.ToString());
			break;
		}
	}
	if (!bSeaOK)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 sea BAD: no water volume at %s"), *OffshorePoint.ToString());
	}

	// ---- posts and the enemies actually standing on them
	auto OnIsland2 = [Isle2X, Isle2Y, Isle2Half](const FVector& Location) -> bool
	{
		return FMath::Abs(Location.X - Isle2X) <= Isle2Half + 500.0f
			&& FMath::Abs(Location.Y - Isle2Y) <= Isle2Half + 500.0f;
	};
	int32 PostCount = 0;
	for (TActorIterator<ACombatEnemySpawner> It(World); It; ++It)
	{
		if (OnIsland2(It->GetActorLocation()))
		{
			++PostCount;
		}
	}
	// EnemyRowOverride is protected on ACombatEnemySpawner, and the LIVE enemies are the stronger
	// proof anyway: they say the post fired AND that the row it names loaded out of Enemies.csv.
	int32 LiveNavy = 0;
	TSet<FName> LiveRows;
	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		const ACombatEnemy* Enemy = *It;
		if (!Enemy || !OnIsland2(Enemy->GetActorLocation()))
		{
			continue;
		}
		LiveRows.Add(Enemy->EnemyRowName);
		++LiveNavy;
	}
	const bool bBossPost = LiveRows.Contains(FName(TEXT("LieutenantHarrow")));
	const bool bPostsOK = PostCount >= 5 && LiveNavy >= 5 && bBossPost
		&& LiveRows.Contains(FName(TEXT("NavyMarine")))
		&& Data->GetEnemyRow(FName(TEXT("NavyMarine"))) != nullptr
		&& Data->GetEnemyRow(FName(TEXT("NavyMusketeer"))) != nullptr
		&& Data->GetEnemyRow(FName(TEXT("NavyGarrisonOfficer"))) != nullptr
		&& Data->GetEnemyRow(FName(TEXT("LieutenantHarrow"))) != nullptr;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 posts=%d boss=%d liveRows=%d liveEnemies=%d"),
		PostCount, bBossPost ? 1 : 0, LiveRows.Num(), LiveNavy);

	// ---- the giver and the story hook
	AWrecktideMissionGiver* Ada = nullptr;
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		if (It->GetGiverId() == FName(TEXT("Ada")))
		{
			Ada = *It;
			break;
		}
	}
	const bool bGiverOK = Ada != nullptr && OnIsland2(Ada->GetActorLocation());
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 giver Ada=%d at %s"), Ada != nullptr,
		Ada ? *Ada->GetActorLocation().ToString() : TEXT("none"));

	// Ada is silent until Cobb's H04 sets Story_Reach; check the locked state BEFORE granting it.
	const bool bAdaLockedOK = !WrecktideMissionLogic::IsGiverUnlocked(Progress, Data, FName(TEXT("Ada")))
		&& WrecktideMissionLogic::GetOfferForGiver(Progress, Data, FName(TEXT("Ada"))).IsNone()
		&& !WrecktideMissionLogic::GetGiverBrushOff(Data, FName(TEXT("Ada"))).IsEmpty();

	// H04 is the FOURTH link of Cobb's chain, so the probe has to stand where a real player stands
	// when he hears about the reach: Cobb unlocked (Story_Harbour, from Marta's T03), his first
	// three rows done, and Story_Harbourmaster set by H03. Setting the last flag alone would leave
	// him locked and prove nothing (first run of this probe did exactly that: cobbOffer=None).
	Progress->SetQuestFlag(FName(TEXT("Story_Harbour")), 1);
	for (const TCHAR* Done : {TEXT("H01_CastOff"), TEXT("H02_FarRock"), TEXT("H03_PaidInFull")})
	{
		Progress->SetQuestFlag(WrecktideMissionLogic::DoneKey(FName(Done)), 1);
	}
	Progress->SetQuestFlag(FName(TEXT("Story_Harbourmaster")), 1);
	const FName CobbOffer = WrecktideMissionLogic::GetOfferForGiver(Progress, Data, FName(TEXT("Cobb")));
	const bool bCobbOK = CobbOffer == FName(TEXT("H04_GallowsReach"));

	Progress->SetQuestFlag(FName(TEXT("Story_Reach")), 1);
	const FName AdaOffer = WrecktideMissionLogic::GetOfferForGiver(Progress, Data, FName(TEXT("Ada")));
	const bool bAdaOK = AdaOffer == FName(TEXT("R01_ThinTheWatch"));
	const bool bHookOK = bAdaLockedOK && bCobbOK && bAdaOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 hook adaLocked=%d cobbOffer=%s adaOffer=%s"),
		bAdaLockedOK ? 1 : 0, *CobbOffer.ToString(), *AdaOffer.ToString());

	// The camp board must stay the FIRST board a TActorIterator hands back, or M05_Bounty and
	// H03_PaidInFull would send the player across the sea to turn a bounty in.
	int32 BoardCount = 0;
	bool bFirstBoardIsCamp = false;
	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		if (BoardCount == 0)
		{
			bFirstBoardIsCamp = !OnIsland2(It->GetActorLocation());
		}
		++BoardCount;
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 boards=%d firstIsCamp=%d"), BoardCount, bFirstBoardIsCamp ? 1 : 0);

	Island2TiersOK = bTiersOK;
	Island2NavOK = bNavOK;
	Island2SeaOK = bSeaOK;
	Island2PostsOK = bPostsOK && BoardCount >= 2 && bFirstBoardIsCamp;
	Island2GiverOK = bGiverOK;
	Island2HookOK = bHookOK;

	// Stand him on the garrison square and let the HUD draw a frame there.
	PlayerPawn->SetActorLocation(FVector(Isle2X, Isle2Y, 260.0f), false, nullptr, ETeleportType::TeleportPhysics);

	World->GetTimerManager().SetTimer(Island2ProbeTimer,
		FTimerDelegate::CreateUObject(this, &UWrecktideRewardSubsystem::FinishIsland2Probe), 1.5f, false);
}

void UWrecktideRewardSubsystem::FinishIsland2Probe()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;

	const FVector Standing = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	const WrecktideMap::FIslandChart& Chart = WrecktideMap::ChartFor(Standing);
	const bool bModelOK = FString(Chart.Id) == TEXT("GallowsReach");
	const bool bDrawnOK = HUD && HUD->GetDrawnChartId() == TEXT("GallowsReach");
	const bool bPlateOK = HUD && HUD->IsMapTextureLoaded()
		&& HUD->GetLoadedChartPlateCount() == WrecktideMap::GetIslandCharts().Num();
	const bool bArrivalOK = Progress && Progress->GetQuestFlag(FName(TEXT("Seen_GallowsReach"))) != 0;

	// The chart of the camp must still be Wreck Cove's, or the second island would have stolen it.
	const WrecktideMap::FIslandChart& CampChart = WrecktideMap::ChartFor(FVector(0.0f, -5200.0f, 0.0f));
	const bool bCampChartOK = FString(CampChart.Id) == TEXT("WreckCove");
	const bool bChartOK = bModelOK && bDrawnOK && bPlateOK && bCampChartOK;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2 chart standing=%s model=%s drawn=%s plate=%d camp=%s arrival=%d"),
		*Standing.ToString(), Chart.Id, HUD ? *HUD->GetDrawnChartId() : TEXT("none"),
		bPlateOK ? 1 : 0, CampChart.Id, bArrivalOK ? 1 : 0);

	const bool bPass = Island2TiersOK && Island2NavOK && Island2SeaOK && Island2PostsOK
		&& Island2GiverOK && Island2HookOK && bChartOK && bArrivalOK;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ISLAND2-PROBE %s (tiers=%d nav=%d sea=%d posts=%d giver=%d hook=%d chart=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		Island2TiersOK ? 1 : 0, Island2NavOK ? 1 : 0, Island2SeaOK ? 1 : 0, Island2PostsOK ? 1 : 0,
		Island2GiverOK ? 1 : 0, Island2HookOK ? 1 : 0, (bChartOK && bArrivalOK) ? 1 : 0);
	ScheduleProbeQuit(2.0f);
}
