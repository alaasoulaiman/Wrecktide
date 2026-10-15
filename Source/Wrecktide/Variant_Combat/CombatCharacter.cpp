// Copyright Epic Games, Inc. All Rights Reserved.
// Modifications Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.


#include "CombatCharacter.h"
#include "Systems/WrecktideAudio.h"
#include "Animation/AnimMontage.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"
#include "CombatLifeBar.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "Engine/LocalPlayer.h"
#include "CombatPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Systems/StatsComponent.h"
#include "Systems/EconomyComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideWardrobeSubsystem.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "Gameplay/WrecktideBoat.h"
#include "UI/WrecktideHUD.h"
#include "Variant_Combat/AI/CombatEnemy.h"

DEFINE_LOG_CATEGORY(LogCombatCharacter);

namespace
{
/** Helm grip blend and loop count (spec C28.3). Prefixed with this file's own name because a unity
 *  build folds every anonymous namespace in the module into one. A quarter second reads as hands
 *  reaching for the rim rather than snapping onto it; the loop count only has to outlast a voyage,
 *  and the helm re-arms the montage in any case. */
constexpr float CombatHelmGripBlendSeconds = 0.25f;
constexpr int32 CombatHelmGripLoopCount = 600;

FVector DamageNumberAnchor(const AActor* Actor)
{
	if (!Actor)
	{
		return FVector::ZeroVector;
	}
	// Held-weapon lights and camera visualizers can enlarge component bounds far above the head.
	// Combat numbers belong over the character's capsule, including incoming musket hits.
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			return Capsule->GetComponentLocation() + FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() + 12.0f);
		}
	}

	const FBox Bounds = Actor->GetComponentsBoundingBox(true);
	return Bounds.IsValid
		? FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + 12.0f)
		: Actor->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f);
}
}

ACombatCharacter::ACombatCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// bind the attack montage ended delegate
	OnAttackMontageEnded.BindUObject(this, &ACombatCharacter::AttackMontageEnded);

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(35.0f, 90.0f);

	// Configure character movement
	GetCharacterMovement()->MaxWalkSpeed = 400.0f;

	// create the camera boom
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);

	CameraBoom->TargetArmLength = DefaultCameraDistance;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->bEnableCameraRotationLag = true;

	// create the orbiting camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// create the life bar widget component
	LifeBar = CreateDefaultSubobject<UWidgetComponent>(TEXT("LifeBar"));
	LifeBar->SetupAttachment(RootComponent);

	// cosmetic skin rig (Chunk C): a child mesh the retarget ABP drives off the main mesh's
	// pose. It must live UNDER the main mesh (Retarget Pose From Mesh sources the attach
	// parent) and carries no gameplay: no collision, no tick, hidden - fully inert until a
	// skin asset is configured, so unconfigured pawns pay nothing for it.
	CosmeticMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CosmeticMesh"));
	CosmeticMesh->SetupAttachment(GetMesh());
	CosmeticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CosmeticMesh->PrimaryComponentTick.bStartWithTickEnabled = false;
	CosmeticMesh->SetVisibility(false);

	// held-weapon visual: snapped to the skin's hand bone when the PlayerState equips a mapped
	// row; hidden and inert otherwise
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(CosmeticMesh);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetVisibility(false);
	PistolMuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PistolMuzzleLight"));
	PistolMuzzleLight->SetupAttachment(WeaponMesh);
	PistolMuzzleLight->SetLightColor(FLinearColor(1.0f, 0.65f, 0.20f));
	PistolMuzzleLight->SetCastShadows(false);
	PistolMuzzleLight->SetVisibility(false);
	PistolVisualPaths.Add(TEXT("RustyFlintlock"), FSoftObjectPath(TEXT("/Game/PolygonPirates/Meshes/Weapons/SM_Wep_MusketPistol_01.SM_Wep_MusketPistol_01")));
	PistolVisualPaths.Add(TEXT("NavyPistol"), FSoftObjectPath(TEXT("/Game/PolygonPirates/Meshes/Weapons/SM_Wep_MusketPistol_04.SM_Wep_MusketPistol_04")));

	Stats = CreateDefaultSubobject<UStatsComponent>(TEXT("Stats"));

	// Combat defaults live on the C++ CDO so ANY child blueprint (the reparented GAS locomotion
	// CBP included) inherits working attacks and a life bar; a reparented sibling BP inherits
	// NONE of BP_CombatCharacter's class-default overrides. BP-set values still win over these.
	static ConstructorHelpers::FObjectFinder<UAnimMontage> ComboMontageFinder(TEXT("/Game/Variant_Combat/Anims/AM_ComboAttack.AM_ComboAttack"));
	if (ComboMontageFinder.Succeeded())
	{
		ComboAttackMontage = ComboMontageFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimMontage> ChargedMontageFinder(TEXT("/Game/Variant_Combat/Anims/AM_ChargedAttack.AM_ChargedAttack"));
	if (ChargedMontageFinder.Succeeded())
	{
		ChargedAttackMontage = ChargedMontageFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> ComboActionFinder(TEXT("/Game/Variant_Combat/Input/Actions/IA_ComboAttack.IA_ComboAttack"));
	if (ComboActionFinder.Succeeded())
	{
		ComboAttackAction = ComboActionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> ZoomActionFinder(TEXT("/Game/Input/IA_CameraZoom.IA_CameraZoom"));
	if (ZoomActionFinder.Succeeded())
	{
		CameraZoomAction = ZoomActionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> ChargedActionFinder(TEXT("/Game/Variant_Combat/Input/Actions/IA_ChargedAttack.IA_ChargedAttack"));
	if (ChargedActionFinder.Succeeded())
	{
		ChargedAttackAction = ChargedActionFinder.Object;
	}

	// Move/Look/MouseLook/Jump actions DELIBERATELY stay unset here: the GAS locomotion CBP
	// drives movement from IMC_Sandbox in its own graph; native pointers would double-bind it.

	static ConstructorHelpers::FClassFinder<UUserWidget> LifeBarWidgetFinder(TEXT("/Game/Variant_Combat/UI/UI_LifeBar"));
	if (LifeBarWidgetFinder.Succeeded())
	{
		LifeBar->SetWidgetClass(LifeBarWidgetFinder.Class);
		LifeBar->SetWidgetSpace(EWidgetSpace::Screen);
		LifeBar->SetDrawSize(FVector2D(300.0f, 50.0f));
	}

	ComboSectionNames = { FName(TEXT("Melee01")), FName(TEXT("Melee02")) };
	ChargeLoopSection = FName(TEXT("Charge"));
	ChargeAttackSection = FName(TEXT("Attack"));
	PelvisBoneName = FName(TEXT("pelvis"));
	LifeBarColor = FLinearColor(0.0f, 0.6f, 0.0f, 1.0f);

	// set the player tag
	Tags.Add(FName("Player"));
}

void ACombatCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void ACombatCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void ACombatCharacter::ComboAttackPressed()
{
	// route the input
	DoComboAttackStart();
}

void ACombatCharacter::ChargedAttackPressed()
{
	// route the input
	DoChargedAttackStart();
}

void ACombatCharacter::ChargedAttackReleased()
{
	// route the input
	DoChargedAttackEnd();
}

void ACombatCharacter::InteractShop()
{
	for (TActorIterator<AWrecktideShopStall> It(GetWorld()); It; ++It)
	{
		if (It->IsUserInRange(this))
		{
			It->ToggleShopMenu(this);
			return;
		}
	}

	// The bounty board shares the F key (2026-09-04). Stalls answer FIRST so the shipped shop
	// behaviour is untouched; the board only ever sees F when no stall holds this player.
	for (TActorIterator<AWrecktideQuestBoard> It(GetWorld()); It; ++It)
	{
		if (It->IsUserInRange(this))
		{
			It->ToggleBoardMenu(this);
			return;
		}
	}

	// The quest giver is LAST on the same key (missions v0, 2026-09-04): the two shipped
	// interactions keep their exact behaviour and he only sees F when neither holds this player.
	for (TActorIterator<AWrecktideMissionGiver> It(GetWorld()); It; ++It)
	{
		if (It->IsUserInRange(this))
		{
			It->ToggleGiverMenu(this);
			return;
		}
	}

	// The blacksmith is FOURTH on the same key (spec C10): the three shipped interactions keep their
	// exact behaviour and he only sees F when none of them holds this player.
	for (TActorIterator<AWrecktideSmithStall> It(GetWorld()); It; ++It)
	{
		if (It->IsUserInRange(this))
		{
			It->ToggleSmithMenu(this);
			return;
		}
	}

	AWrecktideBoat* NearestBoat = nullptr;
	float NearestBoatDistanceSq = TNumericLimits<float>::Max();
	for (TActorIterator<AWrecktideBoat> It(GetWorld()); It; ++It)
	{
		if (!It->IsOccupied() && It->IsCharacterInRange(this))
		{
			const float DistanceSq = FVector::DistSquared2D(GetActorLocation(), It->GetActorLocation());
			if (DistanceSq < NearestBoatDistanceSq)
			{
				NearestBoat = *It;
				NearestBoatDistanceSq = DistanceSq;
			}
		}
	}
	if (NearestBoat)
	{
		NearestBoat->Board(this);
		return;
	}

	if (!DecorativeShipHull.IsValid())
	{
		CacheDecorativeShipHull();
	}
	if (DecorativeShipHull.IsValid()
		&& FVector::DistSquared2D(GetActorLocation(), DecorativeShipHull->GetActorLocation()) <= FMath::Square(900.0f))
	{
		const FString Guidance = TEXT("That ship is not yours. Your boat is the small one off the west rail of the pier.");
		if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
			{
				HUD->ShowToast(Guidance);
			}
		}
		UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-BOAT decor guidance: %s"), *Guidance);
	}
}

void ACombatCharacter::UseEquipSlot1()
{
	if (IsShopMenuOpen())
	{
		return;
	}

	AWrecktidePlayerState* WrecktidePS = GetPlayerState<AWrecktidePlayerState>();
	if (!WrecktidePS)
	{
		return;
	}

	// Key 1 WIELDS slot 1 (spec C17). Cycling moved to the inventory screen, so pressing 1 twice is
	// no longer a way to end up unarmed by accident mid-fight.
	if (!WrecktidePS->WieldSlot(1))
	{
		UseReservedEquipSlot(1);
	}
}

void ACombatCharacter::UseReservedEquipSlot(int32 Slot)
{
	if (IsShopMenuOpen())
	{
		return;
	}

	UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-EQUIP slot %d empty"), Slot);
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
		{
			HUD->ShowToast(AWrecktidePlayerState::GetSlotHint(Slot));
		}
	}
}

void ACombatCharacter::UseEquipSlot2()
{
	if (IsShopMenuOpen()) { return; }
	AWrecktidePlayerState* State = GetPlayerState<AWrecktidePlayerState>();
	if (!State || !State->WieldSlot(2)) { UseReservedEquipSlot(2); }
}

void ACombatCharacter::UseEquipSlot3()
{
	UseReservedEquipSlot(3);
}

void ACombatCharacter::UseEquipSlot4()
{
	UseReservedEquipSlot(4);
}

void ACombatCharacter::UseEquipSlot5()
{
	UseReservedEquipSlot(5);
}

bool ACombatCharacter::IsShopMenuOpen() const
{
	for (TActorIterator<AWrecktideShopStall> It(GetWorld()); It; ++It)
	{
		if (It->IsUserInRange(this) && It->IsShopMenuOpen())
		{
			return true;
		}
	}

	// The bounty board is the same kind of modal, so the equip keys stay locked out while it is up.
	for (TActorIterator<AWrecktideQuestBoard> It(GetWorld()); It; ++It)
	{
		if (It->IsUserInRange(this) && It->IsMenuOpen())
		{
			return true;
		}
	}
	return false;
}

void ACombatCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void ACombatCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ACombatCharacter::DoComboAttackStart()
{
	if (IsPistolWielded()) { FirePistol(); return; }
	// no swinging a cutlass in open water (2026-09-06): the attack montages carry root motion and
	// the trace expects a standing pirate. Both attack entry points early-out here, so the mouse
	// buttons, the probes and any future AI share one rule.
	if (IsSwimming())
	{
		return;
	}

	// are we already playing an attack animation?
	if (bIsAttacking)
	{
		// cache the input time so we can check it later
		CachedAttackInputTime = GetWorld()->GetTimeSeconds();

		return;
	}

	// perform a combo attack
	ComboAttack();
}

void ACombatCharacter::DoComboAttackEnd()
{
	// stub
}

void ACombatCharacter::DoChargedAttackStart()
{
	if (IsPistolWielded()) { SetPistolAim(true); return; }
	// see DoComboAttackStart: attacks are a land move
	if (IsSwimming())
	{
		return;
	}

	if (bIsAttacking)
	{
		return;
	}

	// raise the charging attack flag
	bIsChargingAttack = true;

	ChargedAttack();
}

void ACombatCharacter::DoChargedAttackEnd()
{
	if (bPistolAiming || IsPistolWielded()) { SetPistolAim(false); return; }
	// lower the charging attack flag
	bIsChargingAttack = false;

	// if we've done the charge loop at least once, release the charged attack right away
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		if (bHasLoopedChargedAttack && AnimInstance->Montage_IsPlaying(ChargedAttackMontage))
		{
			CheckChargedAttack();
		}
	}
}

void ACombatCharacter::ResetHP()
{
	bAlreadyDead = false;

	if (Stats)
	{
		Stats->InitStats(PlayerMaxHealth);
		CurrentHP = Stats->GetHealth();
	}

	// update the life bar
	if (LifeBarWidget)
	{
		LifeBarWidget->SetLifePercentage(1.0f);
	}
}

void ACombatCharacter::CameraZoom(const FInputActionValue& Value)
{
	if (bPistolAiming) { return; }
	// wheel up = closer, wheel down = farther, hard-capped both ends (play test note: "not too zoomed
	// in or not too zoomed out")
	const float Axis = Value.Get<float>();
	if (USpringArmComponent* Boom = GetCameraBoom())
	{
		Boom->TargetArmLength = FMath::Clamp(Boom->TargetArmLength - Axis * CameraZoomStep, CameraZoomMin, CameraZoomMax);
	}
}

void ACombatCharacter::FaceControlYaw()
{
	// orient-to-movement leaves the character facing wherever it last walked; attacks must
	// strike where the camera aims, enemy or not
	if (Controller)
	{
		SetActorRotation(FRotator(0.0f, GetControlRotation().Yaw, 0.0f));
	}
}

void ACombatCharacter::ComboAttack()
{
	UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-INPUT combo-start falling=%d speed=%.0f"),
		GetCharacterMovement()->IsFalling() ? 1 : 0, GetVelocity().Size2D());

	// face the camera aim before the swing
	FaceControlYaw();

	// raise the attacking flag
	bIsAttacking = true;

	// reset the combo count
	ComboCount = 0;

	float MontageLength = 0.0f;

	// play the attack montage
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		MontageLength = AnimInstance->Montage_Play(ComboAttackMontage, GetAttackPlayRate(), EMontagePlayReturnType::MontageLength, 0.0f, true);

		// subscribe to montage completed and interrupted events
		if (MontageLength > 0.0f)
		{
			// set the end delegate for the montage
			AnimInstance->Montage_SetEndDelegate(OnAttackMontageEnded, ComboAttackMontage);
			ActiveAttackMontage = ComboAttackMontage;

			// attacks NEVER root the player (play test round-2 verdict: "i cant keep moving while
			// attacking"). A root-motion montage owns movement, so instead of zero-scaling it
			// (which froze the pawn: air probe minAirSpeed 0), ignore montage root motion
			// entirely - WASD and the jump arc flow through physics; restored on montage end.
			// Known trade-off: fast swings while sprinting show a touch of foot-slide until
			// upper-body layering lands (content chunk).
			if (!bAirAttackRootMotionOverride)
			{
				SavedRootMotionMode = AnimInstance->RootMotionMode;
			}
			AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
			bAirAttackRootMotionOverride = true;
		}
	}

	// no montage means no end delegate: clear the flag here or every later attack input is eaten
	if (MontageLength <= 0.0f)
	{
		bIsAttacking = false;
		UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-ATTACK combo montage failed to play"));
	}
}

void ACombatCharacter::ChargedAttack()
{
	UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-INPUT charged-start falling=%d speed=%.0f"),
		GetCharacterMovement()->IsFalling() ? 1 : 0, GetVelocity().Size2D());

	// face the camera aim before the charge-up
	FaceControlYaw();

	// raise the attacking flag
	bIsAttacking = true;

	// reset the charge loop flag
	bHasLoopedChargedAttack = false;

	float MontageLength = 0.0f;

	// play the charged attack montage
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		MontageLength = AnimInstance->Montage_Play(ChargedAttackMontage, GetAttackPlayRate(), EMontagePlayReturnType::MontageLength, 0.0f, true);

		// subscribe to montage completed and interrupted events
		if (MontageLength > 0.0f)
		{
			// set the end delegate for the montage
			AnimInstance->Montage_SetEndDelegate(OnAttackMontageEnded, ChargedAttackMontage);
			ActiveAttackMontage = ChargedAttackMontage;

			// attacks never root the player - same momentum-preserving override as ComboAttack
			if (!bAirAttackRootMotionOverride)
			{
				SavedRootMotionMode = AnimInstance->RootMotionMode;
			}
			AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
			bAirAttackRootMotionOverride = true;
		}
	}

	// no montage means no end delegate: clear the flag here or every later attack input is eaten
	if (MontageLength <= 0.0f)
	{
		bIsAttacking = false;
		UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-ATTACK charged montage failed to play"));
	}
}

void ACombatCharacter::AttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Ignore a stale callback from any montage that no longer owns the attack state.
	if (Montage != nullptr && ActiveAttackMontage != nullptr && Montage != ActiveAttackMontage)
	{
		return;
	}
	ActiveAttackMontage = nullptr;

	// restore root motion for locomotion/traversal now the swing is over. Must run BEFORE the
	// cached-input re-attack below, which re-zeros it for the chained swing.
	SetAnimRootMotionTranslationScale(1.0f);

	// undo the airborne momentum override (see ComboAttack) before anything re-attacks
	if (bAirAttackRootMotionOverride)
	{
		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			AnimInstance->SetRootMotionMode(SavedRootMotionMode);
		}
		bAirAttackRootMotionOverride = false;
	}

	// reset the attacking flag
	bIsAttacking = false;

	// reset the charge loop flag
	bHasLoopedChargedAttack = false;

	// A click buffered during Melee02 is deliberately consumed by the terminal swing. A fresh
	// string begins only from a click received after this montage has ended.
	const bool bFinishedTwoSwingCombo = Montage == ComboAttackMontage
		&& ComboCount >= ComboSectionNames.Num() - 1;
	if (bFinishedTwoSwingCombo)
	{
		CachedAttackInputTime = 0.0f;
	}

	// check if we have a non stale cached input
	if (!bFinishedTwoSwingCombo
		&& GetWorld()->GetTimeSeconds() - CachedAttackInputTime <= AttackInputCacheTimeTolerance)
	{
		// are we holding the charged attack button?
		if (bIsChargingAttack)
		{
			// do a charged attack
			ChargedAttack();
		}
		else
		{
			// do a regular attack
			ComboAttack();
		}
	}
}

void ACombatCharacter::OnStatsOutOfHealth(AActor* InstigatorActor)
{
	HandleDeath();
}

bool ACombatCharacter::IsPistolWielded() const
{
	const AWrecktidePlayerState* State = GetPlayerState<AWrecktidePlayerState>();
	return State && State->GetEquippedCombatClass() == EWrecktideSkill::Shooting;
}

bool ACombatCharacter::CanUsePistol() const
{
	if (!IsPistolWielded() || bAlreadyDead || !Stats || !Stats->IsAlive() || IsSwimming() || IsShopMenuOpen()) { return false; }
	const ACombatPlayerController* PC = Cast<ACombatPlayerController>(Controller);
	if (PC && PC->IsAnyMenuOpen()) { return false; }
	if (GetAttachParentActor() && GetAttachParentActor()->IsA<AWrecktideBoat>()) { return false; }
	if (GetMovementBase() && GetMovementBase()->GetOwner()->IsA<AWrecktideBoat>()) { return false; }
	for (TActorIterator<AWrecktideBoat> It(GetWorld()); It; ++It)
	{
		if (It->GetRider() == this) { return false; }
	}
	return true;
}

bool ACombatCharacter::TracePistol(FHitResult& OutHit) const
{
	if (!Controller || !GetWorld() || !FMath::IsFinite(PistolRange) || PistolRange <= 0.0f) { return false; }
	FVector Origin;
	FRotator Direction;
	Controller->GetPlayerViewPoint(Origin, Direction);
	const FVector End = Origin + Direction.Vector() * PistolRange;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(WrecktidePistolTrace), false, this);
	FCollisionObjectQueryParams Pawns;
	Pawns.AddObjectTypesToQuery(ECC_Pawn);
	if (!GetWorld()->LineTraceSingleByObjectType(OutHit, Origin, End, Pawns, Query)) { return false; }
	// Solid scenery may stop the shot before it reaches the pawn.
	FHitResult Cover;
	return !GetWorld()->LineTraceSingleByChannel(Cover, Origin, OutHit.ImpactPoint, ECC_Visibility, Query)
		|| Cover.GetActor() == OutHit.GetActor() || Cover.Distance + 1.0f >= OutHit.Distance;
}

bool ACombatCharacter::IsPistolTargetLivingEnemy() const
{
	FHitResult Hit;
	const ACombatEnemy* Enemy = TracePistol(Hit) ? Cast<ACombatEnemy>(Hit.GetActor()) : nullptr;
	return Enemy && Enemy->Stats && Enemy->Stats->IsAlive();
}

bool ACombatCharacter::IsPistolReloading() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < PistolReloadEnds;
}

float ACombatCharacter::GetPistolReloadFraction() const
{
	if (!IsPistolReloading()) { return 1.0f; }
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - PistolReloadStarted)
		/ FMath::Max(0.05f, PistolReloadEnds - PistolReloadStarted), 0.0f, 1.0f);
}

bool ACombatCharacter::FirePistol()
{
	if (!HasAuthority() || !CanUsePistol() || IsPistolReloading()) { return false; }
	FaceControlYaw();
	PistolReloadStarted = GetWorld()->GetTimeSeconds();
	PistolReloadEnds = PistolReloadStarted + FMath::Max(0.05f, PistolReloadSeconds);
	PistolFlashEnds = PistolReloadStarted + FMath::Max(0.01f, PistolFlashSeconds);
	bPistolReloadPending = true;
	PistolMuzzleLight->SetRelativeLocation(PistolMuzzleOffset);
	PistolMuzzleLight->SetIntensity(PistolFlashIntensity);
	PistolMuzzleLight->SetAttenuationRadius(PistolFlashRadius);
	PistolMuzzleLight->SetVisibility(true);
	UWrecktideAudio::PlaySfx(this, TEXT("pistol_shot"));
	FHitResult Hit;
	if (TracePistol(Hit))
	{
		ApplyWeaponHit(Hit, GetAttackDamage(), FVector::ZeroVector, false);
	}
	return true;
}

void ACombatCharacter::SetPistolAim(bool bAim)
{
	bAim = bAim && CanUsePistol();
	if (bAim == bPistolAiming || !CameraBoom || !FollowCamera) { return; }
	if (bAim)
	{
		PistolSavedArmLength = CameraBoom->TargetArmLength;
		PistolSavedFOV = FollowCamera->FieldOfView;
		PistolSavedSocketOffset = CameraBoom->SocketOffset;
		CameraBoom->TargetArmLength = FMath::Min(PistolSavedArmLength, PistolAimArmLength);
		FollowCamera->SetFieldOfView(FMath::Min(PistolSavedFOV, PistolAimFOV));
		CameraBoom->SocketOffset = PistolSavedSocketOffset + PistolAimSocketOffset;
	}
	else
	{
		CameraBoom->TargetArmLength = PistolSavedArmLength;
		FollowCamera->SetFieldOfView(PistolSavedFOV);
		CameraBoom->SocketOffset = PistolSavedSocketOffset;
	}
	bPistolAiming = bAim;
}

void ACombatCharacter::SetBaseCameraSocketOffset(const FVector& Offset)
{
	if (!CameraBoom) { return; }
	// Swimming owns the base lift; aiming adds its shoulder framing without losing that base.
	if (bPistolAiming) { PistolSavedSocketOffset = Offset; }
	CameraBoom->SocketOffset = Offset + (bPistolAiming ? PistolAimSocketOffset : FVector::ZeroVector);
}

void ACombatCharacter::UpdatePistolPose()
{
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim) { return; }
	if (!IsPistolWielded() || IsSwimming() || bAlreadyDead || bHelmGripRequested)
	{
		if (PistolPoseMontage) { Anim->Montage_Stop(PistolPoseBlendSeconds, PistolPoseMontage); PistolPoseMontage = nullptr; }
		return;
	}
	if (PistolPoseMontage && Anim->Montage_IsPlaying(PistolPoseMontage)) { return; }
	if (!PistolPose) { PistolPose = LoadObject<UAnimSequenceBase>(nullptr, *PistolPosePath); }
	if (PistolPose)
	{
		PistolPoseMontage = Anim->PlaySlotAnimationAsDynamicMontage(PistolPose, HelmGripSlot,
			PistolPoseBlendSeconds, PistolPoseBlendSeconds, 1.0f, CombatHelmGripLoopCount);
	}
}

float ACombatCharacter::ApplyWeaponHit(const FHitResult& Hit, float Damage, const FVector& Impulse, bool bCritical)
{
	ICombatDamageable* Damageable = Cast<ICombatDamageable>(Hit.GetActor());
	if (!Damageable) { return 0.0f; }
	ACombatEnemy* Enemy = Cast<ACombatEnemy>(Hit.GetActor());
	const float Before = Enemy && Enemy->Stats ? Enemy->Stats->GetHealth() : 0.0f;
	const FVector NumberLocation = DamageNumberAnchor(Enemy);
	Damageable->ApplyDamage(Damage, this, Hit.ImpactPoint, Impulse);
	const float Applied = Enemy && Enemy->Stats ? FMath::Max(0.0f, Before - Enemy->Stats->GetHealth()) : Damage;
	DealtDamage(Damage, Hit.ImpactPoint);
	if (IsPlayerControlled() && Enemy && Applied > 0.0f)
	{
		if (!IsPistolWielded()) { UWrecktideAudio::PlaySfx(this, TEXT("hit")); }
		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PC->GetHUD()))
			{
				HUD->FlashHitMarker(bCritical);
				HUD->PushDamageNumber(NumberLocation, Applied,
					bCritical ? EWrecktideDamageNumberKind::Critical : EWrecktideDamageNumberKind::Normal);
			}
		}
	}
	return Applied;
}

void ACombatCharacter::DoAttackTrace(FName DamageSourceBone)
{
	// A late melee montage notify must never turn a newly drawn pistol into a blade sweep.
	if (IsPistolWielded()) { return; }
	// sweep for objects in front of the character to be hit by the attack
	TArray<FHitResult> OutHits;

	// start at the provided socket location, sweep forward
	const FVector TraceStart = GetMesh()->GetSocketLocation(DamageSourceBone);
	const FVector TraceEnd = TraceStart + (GetActorForwardVector() * MeleeTraceDistance);

	// check for pawn and world dynamic collision object types
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	// combo hits stagger; only the charged strike launches (2026-07-27 playtest: a full launch
	// on combo hit 1 threw the target out of range of hits 2-3)
	UAnimInstance* TraceAnimInstance = GetMesh()->GetAnimInstance();
	const bool bChargedStrike = TraceAnimInstance && ChargedAttackMontage && TraceAnimInstance->Montage_IsPlaying(ChargedAttackMontage);
	UE_LOG(LogCombatCharacter, Verbose, TEXT("WRECKTIDE-COMBAT trace charged=%d"), bChargedStrike ? 1 : 0);

	// The critical roll (spec C10) happens ONCE per swing, not once per target: a sweep that catches
	// two pirates reads the same on both, which is what a player expects from one blade stroke.
	// Enemies never roll, GetPlayerState is null for them, so their swings are untouched.
	AWrecktidePlayerState* AttackerState = GetPlayerState<AWrecktidePlayerState>();
	float CriticalMultiplier = 1.0f;
	bool bCriticalStrike = false;
	if (AttackerState)
	{
		const float CriticalChance = AttackerState->GetCriticalChance();
		bCriticalStrike = CriticalChance > 0.0f && FMath::FRand() < CriticalChance;
		if (bCriticalStrike)
		{
			CriticalMultiplier = AttackerState->GetCriticalMultiplier();
		}
	}
	const float LifestealFraction = AttackerState ? AttackerState->GetLifestealFraction() : 0.0f;
	bool bCriticalReported = false;

	// use a sphere shape for the sweep
	FCollisionShape CollisionShape;
	CollisionShape.SetSphere(MeleeTraceRadius);

	// ignore self
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (GetWorld()->SweepMultiByObjectType(OutHits, TraceStart, TraceEnd, FQuat::Identity, ObjectParams, CollisionShape, QueryParams))
	{
		// iterate over each object hit
		for (const FHitResult& CurrentHit : OutHits)
		{
			// check if we've hit a damageable actor
			ICombatDamageable* Damageable = Cast<ICombatDamageable>(CurrentHit.GetActor());

			if (Damageable)
			{
				ACombatEnemy* HitEnemy = Cast<ACombatEnemy>(CurrentHit.GetActor());

				// the charged strike is the payoff move: bigger damage as well as the launcher
				const float AttackDamage = GetAttackDamage() * (bChargedStrike ? ChargedDamageMultiplier : 1.0f)
					* CriticalMultiplier;

				// knock upwards and away from the impact normal; combo hits use the stagger
				// pair so the target stays in range for the rest of the string
				const FVector Impulse = bChargedStrike
					? (CurrentHit.ImpactNormal * -MeleeKnockbackImpulse) + (FVector::UpVector * MeleeLaunchImpulse)
					: (CurrentHit.ImpactNormal * -ComboKnockbackImpulse) + (FVector::UpVector * ComboLaunchImpulse);

				const float DamageDealt = ApplyWeaponHit(CurrentHit, AttackDamage, Impulse, bCriticalStrike);

				// Hit marker: the ONE place the player's melee damage lands on an enemy. Gated on
				// a player controller so an enemy swing never marks the player's screen, and on
				// ACombatEnemy so training dummies and damageable props stay silent.
				if (HitEnemy)
				{
					if (APlayerController* AttackerController = Cast<APlayerController>(GetController()))
					{
						if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(AttackerController->GetHUD()))
						{
							// One CRITICAL line per swing however many pirates the blade caught.
							if (bCriticalStrike && !bCriticalReported)
							{
								bCriticalReported = true;
								HUD->PushRewardLine(FString::Printf(TEXT("CRITICAL  x%.2f  %.0f damage"),
									CriticalMultiplier, DamageDealt));
							}
						}
					}

					// Bloodletting heals from the same applied health delta shown over the enemy.
					if (LifestealFraction > 0.0f && DamageDealt > 0.0f && Stats && Stats->IsAlive())
					{
						ApplyHealing(DamageDealt * LifestealFraction, this);
					}
				}
			}
		}
	}
}

void ACombatCharacter::CheckCombo()
{
	// are we playing a non-charge attack animation?
	if (bIsAttacking && !bIsChargingAttack)
	{
		// is the last attack input not stale?
		if (GetWorld()->GetTimeSeconds() - CachedAttackInputTime <= ComboInputCacheTimeTolerance)
		{
			// consume the attack input so we don't accidentally trigger it twice
			CachedAttackInputTime = 0.0f;

			// increase the combo counter
			++ComboCount;

			// Do we still have a combo section to play? Melee02 is terminal, so an input
			// consumed by its notify never wraps or reaches a third section.
			if (ComboCount < ComboSectionNames.Num())
			{
				UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-INPUT combo-chain stage=%d"), ComboCount);

				// each combo stage re-tracks the camera aim
				FaceControlYaw();

				// jump to the next combo section
				if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
				{
					AnimInstance->Montage_JumpToSection(ComboSectionNames[ComboCount], ComboAttackMontage);
				}
			}
			else
			{
				// The source montage still contains a legacy third section. Stop explicitly at the
				// terminal notify so asset section links can never carry a third swing.
				CachedAttackInputTime = 0.0f;
				if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
				{
					AnimInstance->Montage_Stop(0.1f, ComboAttackMontage);
				}
			}
		}
	}
}

void ACombatCharacter::CheckChargedAttack()
{
	// raise the looped charged attack flag
	bHasLoopedChargedAttack = true;

	// the release strike tracks the camera aim; the charge loop itself doesn't spin the character
	if (!bIsChargingAttack)
	{
		UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-INPUT charged-release"));
		FaceControlYaw();
	}

	// jump to either the loop or the attack section depending on whether we're still holding the charge button
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_JumpToSection(bIsChargingAttack ? ChargeLoopSection : ChargeAttackSection, ChargedAttackMontage);
	}
}

void ACombatCharacter::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	// pass the damage event to the actor
	FDamageEvent DamageEvent;
	const float ActualDamage = TakeDamage(Damage, DamageEvent, nullptr, DamageCauser);

	// only process knockback and effects if we received nonzero damage
	if (ActualDamage > 0.0f)
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
			{
				HUD->PushDamageNumber(DamageNumberAnchor(this), ActualDamage,
					EWrecktideDamageNumberKind::PlayerDamage);
			}
		}

		// apply the knockback impulse
		GetCharacterMovement()->AddImpulse(DamageImpulse, true);

		// is the character ragdolling?
		if (GetMesh()->IsSimulatingPhysics())
		{
			// apply an impulse to the ragdoll
			GetMesh()->AddImpulseAtLocation(DamageImpulse * GetMesh()->GetMass(), DamageLocation);
		}

		// pass control to BP to play effects, etc.
		ReceivedDamage(ActualDamage, DamageLocation, DamageImpulse.GetSafeNormal());
	}

}

void ACombatCharacter::HandleDeath()
{
	if (bAlreadyDead)
	{
		return;
	}
	bAlreadyDead = true;
	if (IsPlayerControlled())
	{
		UWrecktideAudio::PlaySfx(this, TEXT("death"));
	}

	// a death mid-swing would otherwise leave root motion stranded at zero on the pooled pawn
	SetAnimRootMotionTranslationScale(1.0f);

	// disable movement while we're dead
	GetCharacterMovement()->DisableMovement();

	// enable full ragdoll physics - only on a physics-capable mesh (see CanRagdollMesh);
	// death still proceeds either way, the corpse just holds its last animated pose
	if (CanRagdollMesh())
	{
		GetMesh()->SetSimulatePhysics(true);
	}
	else
	{
		UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-COMBAT ragdoll-skip death %s (mesh not physics-capable)"), *GetName());
	}

	// hide the life bar
	LifeBar->SetHiddenInGame(true);

	// pull back the camera
	GetCameraBoom()->TargetArmLength = DeathCameraDistance;

	// schedule respawning
	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &ACombatCharacter::RespawnCharacter, RespawnTime, false);

	// Death overlay for the whole respawn wait; the controller clears it on the respawn that
	// lands. Nothing here touches gold or XP: those live on the PlayerState and survive death.
	// The cap is a margin past RespawnTime so a broken respawn cannot strand YOU DIED on screen.
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
		{
			HUD->SetDeathScreen(true, RespawnTime + 5.0f);
		}
	}
}

void ACombatCharacter::ApplyHealing(float Healing, AActor* Healer)
{
	if (!Stats || !Stats->IsAlive() || !FMath::IsFinite(Healing) || Healing <= 0.0f)
	{
		return;
	}

	const float HealthBefore = Stats->GetHealth();
	if (!Stats->Heal(Healing))
	{
		return;
	}

	CurrentHP = Stats->GetHealth();
	if (LifeBarWidget)
	{
		LifeBarWidget->SetLifePercentage(Stats->GetHealth() / Stats->GetMaxHealth());
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
		{
			HUD->PushDamageNumber(DamageNumberAnchor(this), Stats->GetHealth() - HealthBefore,
				EWrecktideDamageNumberKind::Heal);
		}
	}
}

void ACombatCharacter::RespawnCharacter()
{
	// destroy the character and let it be respawned by the Player Controller
	Destroy();
}

float ACombatCharacter::GetAttackPlayRate() const
{
	// Balance, the blacksmith's attack-speed track (spec C10). Clamped so a bad table can never stop
	// a montage dead (rate 0) or fire the damage notify before the swing reads as a swing.
	if (const AWrecktidePlayerState* WrecktidePlayerState = GetPlayerState<AWrecktidePlayerState>())
	{
		return FMath::Clamp(WrecktidePlayerState->GetAttackSpeedMultiplier(), 0.25f, 3.0f);
	}

	return 1.0f;
}

float ACombatCharacter::GetAttackDamage() const
{
	if (const AWrecktidePlayerState* WrecktidePlayerState = GetPlayerState<AWrecktidePlayerState>())
	{
		return WrecktidePlayerState->GetAttackDamage();
	}

	return MeleeDamage;
}

float ACombatCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// only process damage if the character is still alive
	if (!Stats || !Stats->IsAlive())
	{
		return 0.0f;
	}

	const float Applied = Stats->ApplyDamage(Damage, DamageCauser, false);
	CurrentHP = Stats->GetHealth();

	if (Applied > 0.0f && Stats->IsAlive())
	{
		// update the life bar
		if (LifeBarWidget)
		{
			LifeBarWidget->SetLifePercentage(Stats->GetHealth() / Stats->GetMaxHealth());
		}

		// enable partial ragdoll physics, but keep the pelvis vertical - ONLY on a mesh whose
		// bodies can actually simulate (2026-07-27: blending toward never-simulated bodies fed
		// the retargeted skin garbage transforms = the player "despawned" on every hit)
		if (CanRagdollMesh())
		{
			GetMesh()->SetPhysicsBlendWeight(0.5f);
			GetMesh()->SetBodySimulatePhysics(PelvisBoneName, false);
		}
		else
		{
			UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-COMBAT ragdoll-skip hit %s (mesh not physics-capable)"), *GetName());
		}
	}

	// feedback sound (2026-09-05): the player got hurt; the death blow plays death instead
	if (IsPlayerControlled() && Applied > 0.0f && !bAlreadyDead)
	{
		UWrecktideAudio::PlaySfx(this, TEXT("hurt"), 0.8f);
	}

	// return the received damage amount
	return Applied;
}

bool ACombatCharacter::CanRagdollMesh() const
{
	const USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || !MeshComp->GetPhysicsAsset())
	{
		return false;
	}
	const ECollisionEnabled::Type Collision = MeshComp->GetCollisionEnabled();
	return Collision == ECollisionEnabled::QueryAndPhysics || Collision == ECollisionEnabled::PhysicsOnly;
}

void ACombatCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// is the character still alive?
	if (Stats && Stats->IsAlive())
	{
		// disable ragdoll physics
		GetMesh()->SetPhysicsBlendWeight(0.0f);
	}
}

void ACombatCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (Stats)
	{
		Stats->OnOutOfHealth.AddUniqueDynamic(this, &ACombatCharacter::OnStatsOutOfHealth);
	}

	// NOTE: root motion is scoped per-attack now (zeroed inside ComboAttack/ChargedAttack, restored
	// in AttackMontageEnded), so GAS traversal/locomotion root motion is NOT killed globally here.

	// get the life bar from the widget component
	LifeBarWidget = Cast<UCombatLifeBar>(LifeBar->GetUserWidgetObject());
	if (!LifeBarWidget)
	{
		// a reparented pawn (e.g. the GAS locomotion CBP) may carry no life-bar widget class;
		// warn and run without a life bar rather than assert-crashing the instant it spawns
		UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-COMBAT LifeBarWidget missing; life bar disabled for %s"), *GetName());
	}

	// initialize the camera
	GetCameraBoom()->TargetArmLength = DefaultCameraDistance;

	// save the relative transform for the mesh so we can reset the ragdoll later
	MeshStartingTransform = GetMesh()->GetRelativeTransform();

	// and the boom's authored socket offset: the swim rig lifts it while the pirate is in the water
	// and must put back what the CBP shipped, not zero (which is what the first swim wrote).
	CameraBoomStartingSocketOffset = GetCameraBoom()->SocketOffset;

	// set the life bar color
	if (LifeBarWidget)
	{
		LifeBarWidget->SetBarColor(LifeBarColor);
	}

	// and lift it off the belly. Nothing else on the HUD reports the pirate's health, so this bar is
	// it, and a screen-space widget projects its component location - the capsule centre - unless the
	// anchor is moved.
	UpdateLifeBarAnchor();

	// reset HP to maximum
	ResetHP();

	UWrecktideWardrobeSubsystem* Wardrobe = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWrecktideWardrobeSubsystem>() : nullptr;
	if (Wardrobe)
	{
		Wardrobe->ApplyPersistedOutfit(this);
	}

	// Swimming (2026-09-06): the sea is a real water APhysicsVolume, so the engine flips the movement
	// mode by itself; these are the numbers it swims WITH. They are set here rather than in the
	// constructor because the CBP CDO owns the rest of the CMC and would overwrite constructor values
	// (RECON trap 10). bCanSwim is asserted explicitly: a CDO with it cleared makes the engine treat
	// the whole sea as "cannot move here" instead of swimming.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->NavAgentProps.bCanSwim = true;
		Movement->MaxSwimSpeed = SwimSpeed;
		Movement->Buoyancy = SwimBuoyancy;
		Movement->BrakingDecelerationSwimming = SwimBraking;
	}

	// The large display ship is scenery. Cache its hull by mesh name; actor labels are editor-only
	// metadata and cannot be relied on in a packaged game.
	CacheDecorativeShipHull();

	// swap in the cosmetic skin last, when every component is registered
	ApplyCosmeticMesh();

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	FString OutfitSwitch;
	if (Wardrobe && FParse::Value(FCommandLine::Get(), TEXT("Outfit="), OutfitSwitch, false))
	{
		TArray<FString> Parts;
		OutfitSwitch.ParseIntoArray(Parts, TEXT(","), false);
		const bool bValidShape = Parts.Num() == 2 && !Parts[0].IsEmpty() && Parts[1].IsNumeric();
		const bool bApplied = bValidShape && Wardrobe->SetOutfit(this, FName(*Parts[0]), FCString::Atoi(*Parts[1]));
		if (bApplied)
		{
			UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-WARDROBE-CAPTURE applied value=%s"), *OutfitSwitch);
		}
		else
		{
			UE_LOG(LogCombatCharacter, Error, TEXT("WRECKTIDE-WARDROBE-CAPTURE rejected value=%s"), *OutfitSwitch);
		}
	}
#endif
}

void ACombatCharacter::CacheDecorativeShipHull()
{
	if (DecorativeShipHull.IsValid() || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* StaticMeshComponent = It->GetStaticMeshComponent();
		const UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;
		if (StaticMesh && StaticMesh->GetName().Contains(TEXT("SM_Veh_Boat_Medium_01_Hull")))
		{
			DecorativeShipHull = *It;
			break;
		}
	}
}

bool ACombatCharacter::IsSwimming() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return Movement != nullptr && Movement->IsSwimming();
}

void ACombatCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PistolMuzzleLight) { PistolMuzzleLight->SetVisibility(GetWorld()->GetTimeSeconds() < PistolFlashEnds); }
	if (bPistolReloadPending && !IsPistolReloading())
	{
		bPistolReloadPending = false;
		UWrecktideAudio::PlaySfx(this, TEXT("pistol_reload"));
	}
	if (bPistolAiming && !CanUsePistol()) { SetPistolAim(false); }
	if (bPistolAiming) { FaceControlYaw(); }
	UpdatePistolPose();

	const bool bSwimming = IsSwimming();
	if (bSwimming != bWasSwimming)
	{
		bWasSwimming = bSwimming;
		if (bSwimming)
		{
			UWrecktideAudio::PlaySfx(this, TEXT("splash"));

			// water catches a falling body. Without this a pirate who steps off the pier keeps the
			// whole fall speed and sinks a couple of metres before buoyancy (a gentle 118 uu/s^2 at
			// Buoyancy 1.12) can bring him back up, which reads as "fell through the water" all over
			// again. Clamping the entry makes the splash a splash.
			if (UCharacterMovementComponent* Movement = GetCharacterMovement())
			{
				Movement->Velocity.Z = FMath::Max(Movement->Velocity.Z, -SwimEntrySinkSpeed);
				Movement->Velocity.X *= SwimEntryDrag;
				Movement->Velocity.Y *= SwimEntryDrag;
			}

			// a swing in progress would leave a root-motion montage driving a body that is now in
			// the water: end it the same way AttackMontageEnded does when the montage is interrupted
			if (bIsAttacking)
			{
				if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
				{
					AnimInstance->Montage_Stop(0.15f);
				}

			}
			bIsChargingAttack = false;
		}
		UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-SWIM %s %s z=%.0f"),
			bSwimming ? TEXT("enter") : TEXT("leave"), *GetName(), GetActorLocation().Z);
	}

	if (bSwimming)
	{
		// face the way he is actually swimming, so the prone tilt lies along the direction of travel.
		// The CBP leaves bOrientRotationToMovement and bUseControllerRotationYaw off and only an
		// attack snaps the yaw (FaceControlYaw), so nothing else owns facing while he is in the water.
		const FVector SwimVelocity = GetVelocity();
		if (SwimVelocity.SizeSquared2D() > FMath::Square(SwimTiltMoveSpeed))
		{
			const FRotator FaceSwim(0.0f, SwimVelocity.Rotation().Yaw, 0.0f);
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), FaceSwim, DeltaSeconds, 6.0f));
		}

		TryHaulOutOfWater();
	}

	UpdateSwimRig(DeltaSeconds);
}

bool ACombatCharacter::GetSwimSurfaceZ(float& OutSurfaceZ) const
{
	const APhysicsVolume* Volume = GetPhysicsVolume();
	if (!Volume || !Volume->bWaterVolume)
	{
		return false;
	}
	// bNonColliding = true: the sea brush is QueryOnly, and a colliding-only box can come back
	// empty for it. An empty brush reads size 0 and is refused rather than clamped against.
	const FBox Bounds = Volume->GetComponentsBoundingBox(true);
	if (!Bounds.IsValid || Bounds.GetSize().Z <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutSurfaceZ = static_cast<float>(Bounds.Max.Z);
	return true;
}

void ACombatCharacter::UpdateSwimRig(float DeltaSeconds)
{
	USkeletalMeshComponent* Rig = GetMesh();
	// a ragdolling rig owns its own transform: never fight the physics solver for it
	if (!Rig || Rig->IsSimulatingPhysics())
	{
		return;
	}

	const float Speed2D = static_cast<float>(GetVelocity().Size2D());
	const float TargetPitch = IsSwimming()
		? (Speed2D > SwimTiltMoveSpeed ? SwimTiltMovingPitch : SwimTiltIdlePitch)
		: 0.0f;

	if (FMath::IsNearlyEqual(SwimRigPitch, TargetPitch, 0.05f) && FMath::IsNearlyZero(TargetPitch))
	{
		// fully out of the water and already settled: leave the rig exactly where BeginPlay found it
		// so the ragdoll reset and the cosmetic chain keep their untouched starting transform
		if (!FMath::IsNearlyZero(SwimRigPitch))
		{
			SwimRigPitch = 0.0f;
			Rig->SetRelativeTransform(MeshStartingTransform);
			// The boom settles with the rig; without this the swim lift stayed in the socket offset
			// (and the authored offset stayed lost) for the rest of the run.
			SwimCameraClampAdd = 0.0f;
			if (CameraBoom)
			{
				SetBaseCameraSocketOffset(CameraBoomStartingSocketOffset);
			}
		}
		return;
	}

	SwimRigPitch = FMath::FInterpTo(SwimRigPitch, TargetPitch, DeltaSeconds, SwimTiltInterpSpeed);

	// The tilt is a rotation about the CAPSULE's right axis, applied on top of the rig's starting
	// rotation. It must be composed as a quaternion: the rig sits at yaw -90, and adding degrees to
	// a relative FRotator's Pitch there rotates about the pre-yaw axis, which flops the pirate onto
	// his side instead of laying him face down. (That is also why this interpolates the ANGLE and
	// rebuilds the quat rather than RInterpTo-ing the composed rotator.)
	const FQuat Tilt(FRotator(SwimRigPitch, 0.0f, 0.0f));
	Rig->SetRelativeRotation(Tilt * MeshStartingTransform.GetRotation());

	// lift the rig so the prone body's middle sits at the capsule centre instead of hanging off the
	// capsule's feet; scales with the tilt so it eases in and out with it
	const float LiftAlpha = FMath::Abs(SwimRigPitch) / FMath::Max(FMath::Abs(SwimTiltMovingPitch), 1.0f);
	const float Lift = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * SwimRigLift * FMath::Clamp(LiftAlpha, 0.0f, 1.0f);
	Rig->SetRelativeLocation(MeshStartingTransform.GetLocation() + FVector(0.0f, 0.0f, Lift));

	// keep the spring arm above the surface while the pirate is in it: the lift is ADDED to the
	// authored socket offset, never written over it
	if (CameraBoom)
	{
		FVector Socket = CameraBoomStartingSocketOffset;
		Socket.Z += SwimCameraLift * FMath::Clamp(LiftAlpha, 0.0f, 1.0f);
		SetBaseCameraSocketOffset(Socket);

		// Hard floor on top of the tilt-scaled lift. The lift is zero while treading, which is the
		// thin case: the camera sat 41 uu over the water and a downward look pitch shot the frame
		// from inside the sea (complaint C5, "I can't even see the water"). Take the surface from
		// the sea volume itself rather than a second copy of Z_WATER.
		//
		// This is an INTEGRATOR, not a one-shot add, on purpose. The spring arm places the camera
		// during its own tick, so GetComponentLocation() here reads LAST frame's position, which
		// already contains last frame's correction. Recomputing a fresh "deficit" every frame from
		// that number would cancel itself and leave the camera bouncing in and out of the water at
		// frame rate. Accumulating the error instead converges in one frame and then holds, and the
		// clamp to zero means the term can only ever push the camera UP.
		float SurfaceZ = 0.0f;
		if (FollowCamera && GetSwimSurfaceZ(SurfaceZ))
		{
			const float Want = SurfaceZ + SwimCameraMinClearance;
			const float Error = Want - static_cast<float>(FollowCamera->GetComponentLocation().Z);
			SwimCameraClampAdd = FMath::Clamp(SwimCameraClampAdd + Error, 0.0f, SwimCameraClampMax);
			if (SwimCameraClampAdd > 0.0f)
			{
				Socket.Z += SwimCameraClampAdd;
				SetBaseCameraSocketOffset(Socket);
			}
		}
	}

	// NOTE (honest placeholder): the locomotion animation keeps running underneath, so the legs
	// "kick" while the whole rig lies along the surface. There are no swim animations in the project
	// yet; when there are, this tilt should shrink to a lean and the pose should come from the ABP.
}

void ACombatCharacter::ResetSwimPose()
{
	SwimRigPitch = 0.0f;
	bWasSwimming = false;
	SwimCameraClampAdd = 0.0f;
	if (USkeletalMeshComponent* Rig = GetMesh())
	{
		Rig->SetRelativeTransform(MeshStartingTransform);
	}
	if (CameraBoom)
	{
		SetBaseCameraSocketOffset(CameraBoomStartingSocketOffset);
	}
}

void ACombatCharacter::SetHelmGrip(bool bEnabled)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!bEnabled)
	{
		if (bHelmGripRequested && AnimInstance && HelmGripMontage)
		{
			AnimInstance->Montage_Stop(CombatHelmGripBlendSeconds, HelmGripMontage);
		}
		bHelmGripRequested = false;
		HelmGripMontage = nullptr;
		return;
	}

	bHelmGripRequested = true;
	if (!AnimInstance)
	{
		return;
	}
	if (HelmGripMontage && AnimInstance->Montage_IsPlaying(HelmGripMontage))
	{
		return;
	}
	if (!HelmGripPose)
	{
		HelmGripPose = LoadObject<UAnimSequenceBase>(nullptr, *HelmGripPosePath);
	}
	if (!HelmGripPose)
	{
		// Fail closed and SAY so: the pirate still steers, he just steers with his arms down.
		UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-HELM grip pose missing path=%s"), *HelmGripPosePath);
		return;
	}
	// A DYNAMIC slot montage rather than an authored montage asset: the pose is a plain sequence and
	// the upper-body slot already exists on both skeletons, so nothing new has to be imported, saved
	// or staged. The loop count has to outlive a voyage; the helm re-arms it in any case.
	HelmGripMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(HelmGripPose, HelmGripSlot,
		CombatHelmGripBlendSeconds, CombatHelmGripBlendSeconds, 1.0f, CombatHelmGripLoopCount);
	if (!HelmGripMontage)
	{
		UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-HELM grip refused pose=%s slot=%s"),
			*HelmGripPose->GetName(), *HelmGripSlot.ToString());
		return;
	}
	UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-HELM grip playing pose=%s slot=%s length=%.1fs"),
		*HelmGripPose->GetName(), *HelmGripSlot.ToString(), HelmGripMontage->GetPlayLength());
}

bool ACombatCharacter::IsHelmGripPlaying() const
{
	const UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	return AnimInstance != nullptr && HelmGripMontage != nullptr && AnimInstance->Montage_IsPlaying(HelmGripMontage);
}

bool ACombatCharacter::TryHaulOutOfWater()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	UWorld* World = GetWorld();
	if (!Movement || !World || !Movement->IsSwimming())
	{
		return false;
	}
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		if (!It->IsOccupied() && It->IsPointWithinHullRange(GetActorLocation(), 500.0f))
		{
			return false;
		}
	}
	if (World->GetTimeSeconds() - LastSwimHaulTime < SwimHaulCooldown)
	{
		return false;
	}

	// only when he is actively pushing somewhere: this is "swim at the shore and climb out", never
	// an automatic teleport for a pirate floating with no input
	const FVector Push = Movement->GetCurrentAcceleration();
	if (Push.SizeSquared2D() < 1.0f)
	{
		return false;
	}
	LastSwimHaulTime = World->GetTimeSeconds();

	const FVector Direction = Push.GetSafeNormal2D();
	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Start = GetActorLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WrecktideHaulOut), false, this);

	// 1. something solid right in front of him?
	FHitResult WallHit;
	const bool bWall = World->SweepSingleByChannel(WallHit, Start, Start + Direction * (Radius * 1.6f),
		FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
	if (!bWall || WallHit.GetActor() == nullptr || WallHit.GetActor()->IsA<APawn>())
	{
		return false;
	}

	// 2. how high is its top? Trace DOWN just past the wall. Visibility is the right channel here:
	//    the water body is query-only against pawns and does not block visibility traces, so open
	//    water can never answer as a ledge (the same rule AWrecktideBoat::FindAshoreSpot relies on).
	const FVector Ahead = Start + Direction * (Radius * 2.6f);
	const FVector TraceTop(Ahead.X, Ahead.Y, Start.Z + SwimHaulMaxLedge);
	const FVector TraceBottom(Ahead.X, Ahead.Y, Start.Z - HalfHeight);
	FHitResult GroundHit;
	if (!World->LineTraceSingleByChannel(GroundHit, TraceTop, TraceBottom, ECC_Visibility, Params))
	{
		return false;
	}

	// 3. can he actually stand there?
	const FVector Standing = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 6.0f);
	if (World->OverlapBlockingTestByChannel(Standing, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
	{
		return false;
	}

	const float Rise = Standing.Z - Start.Z;
	if (Rise <= 0.0f || Rise > SwimHaulMaxLedge)
	{
		return false;
	}

	// 4. haul out: a ballistic launch that clears the lip and carries him over it. Solved from the
	//    measured rise so an 80 uu beach lip is a hop and a tall pier is a real heave, instead of a
	//    magic teleport. Gravity is read from the world so a changed GravityZ cannot silently
	//    under-throw him.
	const float Gravity = FMath::Abs(World->GetGravityZ());
	const float LaunchZ = FMath::Sqrt(2.0f * Gravity * (Rise + 40.0f));
	const float TimeToApex = LaunchZ / FMath::Max(Gravity, 1.0f);
	const float Gap = static_cast<float>(FVector::Dist2D(Standing, Start)) + Radius;
	const float LaunchXY = FMath::Clamp(Gap / FMath::Max(TimeToApex, 0.05f), 150.0f, 900.0f);

	LaunchCharacter(Direction * LaunchXY + FVector(0.0f, 0.0f, LaunchZ), true, true);
	UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-SWIM haul-out %s rise=%.0f gap=%.0f launch=(%.0f,%.0f)"),
		*GetName(), Rise, Gap, LaunchXY, LaunchZ);
	return true;
}

void ACombatCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	// With the sea volume in place and WorldSettings KillZ at -3000 this is a safety net, not the
	// normal path: anything that reaches it fell through the world. Log loudly, then take the same
	// death and respawn route a killed pirate takes (HandleDeath is idempotent through bAlreadyDead,
	// so the per-frame KillZ check below the floor cannot restart it).
	UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-FELL %s at %s"), *GetName(), *GetActorLocation().ToString());
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	HandleDeath();
}

void ACombatCharacter::ApplyCosmeticMesh()
{
	if (!CosmeticMeshAsset || !CosmeticMesh)
	{
		return;
	}

	// ABP_GenericRetarget's contract (its own graph comment): every init AND update it reads
	// ComponentTags[0] off the owning mesh and looks the retargeter up in its IKRetargeter_Map -
	// "IF THERE IS NO TAG, THE CHARACTER WILL NOT RETARGET". The tag IS the retargeter's asset
	// name, and it must be in place BEFORE the anim class initializes.
	if (CosmeticRetargeter)
	{
		CosmeticMesh->ComponentTags.Reset();
		CosmeticMesh->ComponentTags.Add(CosmeticRetargeter->GetFName());
	}

	CosmeticMesh->SetSkeletalMesh(CosmeticMeshAsset);
	if (CosmeticAnimClass)
	{
		CosmeticMesh->SetAnimInstanceClass(CosmeticAnimClass);
	}

	// Resolve the WHOLE retarget hookup before hiding anything: a failed hookup must degrade to
	// the visible driving mesh (playable), never to a hidden driver behind a T-posing skin.
	// (The push is belt-and-suspenders now - the graph re-selects from the map each frame - but
	// it still validates the property exists and the asset type is right.)
	if (!PushCosmeticRetargeter())
	{
		CosmeticMesh->SetVisibility(false);
		return;
	}

	// the write is per-instance: re-push whenever the engine re-creates the anim instance
	// (e.g. an ABP recompile mid-session), or the skin silently T-poses until respawn
	CosmeticMesh->OnAnimInitialized.AddUniqueDynamic(this, &ACombatCharacter::OnCosmeticAnimInitialized);

	// the retarget node's contract: the pose SOURCE must tick before this instance, or the
	// visible skin lags the hidden mesh (and DoAttackTrace's sockets) by a frame
	CosmeticMesh->AddTickPrerequisiteComponent(GetMesh());
	CosmeticMesh->SetComponentTickEnabled(true);

	// the driving mesh keeps animating as the retarget pose source but never renders. It must
	// refresh BONES, not just tick the pose: the retarget node reads component-space transforms
	// and DoAttackTrace reads socket locations off this hidden mesh.
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCastShadow(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	CosmeticMesh->SetVisibility(true);
	CosmeticMesh->SetCastShadow(true);

	// A weapon equipped BEFORE the skin went live (the PlayerState's save load runs in its own
	// BeginPlay, ahead of this pawn's) was parented to the hidden driver's hand and stayed there,
	// rendering off the visible skin all session (review 2026-09-02). Re-parent it now.
	RefreshWeaponVisual();

	UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-COSMETIC applied skin=%s abp=%s retargeter=%s"),
		*CosmeticMeshAsset->GetName(), *CosmeticMesh->GetAnimInstance()->GetClass()->GetName(), *CosmeticRetargeter->GetName());
}

bool ACombatCharacter::PushCosmeticRetargeter()
{
	UAnimInstance* CosmeticAnim = CosmeticMesh ? CosmeticMesh->GetAnimInstance() : nullptr;
	FObjectProperty* RetargeterProperty = CosmeticAnim
		? FindFProperty<FObjectProperty>(CosmeticAnim->GetClass(), CosmeticRetargeterProperty)
		: nullptr;
	if (!CosmeticAnim || !RetargeterProperty || !CosmeticRetargeter)
	{
		UE_LOG(LogCombatCharacter, Warning,
			TEXT("WRECKTIDE-COSMETIC %s hookup failed (anim=%d prop=%d rtg=%d); keeping the driving mesh visible"),
			*GetName(), CosmeticAnim != nullptr, RetargeterProperty != nullptr, CosmeticRetargeter != nullptr);
		return false;
	}

	// the destination property stores a TYPED pointer; reflection does zero IsA validation, so a
	// mistyped asset (e.g. the IK Rig sitting next to the retargeter) would be undefined behavior
	if (!CosmeticRetargeter->IsA(RetargeterProperty->PropertyClass))
	{
		UE_LOG(LogCombatCharacter, Warning,
			TEXT("WRECKTIDE-COSMETIC %s retargeter %s is not a %s; keeping the driving mesh visible"),
			*GetName(), *CosmeticRetargeter->GetClass()->GetName(), *RetargeterProperty->PropertyClass->GetName());
		return false;
	}

	RetargeterProperty->SetObjectPropertyValue_InContainer(CosmeticAnim, CosmeticRetargeter);
	return true;
}

void ACombatCharacter::OnCosmeticAnimInitialized()
{
	PushCosmeticRetargeter();
}

void ACombatCharacter::UpdateLifeBarAnchor()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!LifeBar || !Capsule)
	{
		return;
	}

	// Same rule as the enemy plate: anchor at the crown of the capsule, pivot on the bar's own bottom
	// edge so it hangs above the head. A relative offset is multiplied by the component's world scale,
	// so divide that back out and the real clearance holds whatever the pawn is scaled to.
	const float ScaleZ = FMath::Max(FMath::Abs(Capsule->GetComponentScale().Z), KINDA_SMALL_NUMBER);
	const float WorldLift = Capsule->GetScaledCapsuleHalfHeight() + LifeBarHeadClearance;
	LifeBar->SetRelativeLocation(FVector(0.0f, 0.0f, WorldLift / ScaleZ));
	LifeBar->SetPivot(LifeBarPivot);
}

bool ACombatCharacter::IsLifeBarAboveHead(float MinClearance, float& OutClearance) const
{
	OutClearance = 0.0f;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!LifeBar || !Capsule)
	{
		return false;
	}

	const float CapsuleTopZ = Capsule->GetComponentLocation().Z + Capsule->GetScaledCapsuleHalfHeight();
	OutClearance = LifeBar->GetComponentLocation().Z - CapsuleTopZ;
	return OutClearance >= MinClearance && FMath::IsNearlyEqual(LifeBar->GetPivot().Y, 1.0f, 0.01f);
}

bool ACombatCharacter::IsCosmeticRetargeterLive() const
{
	const UAnimInstance* CosmeticAnim = CosmeticMesh ? CosmeticMesh->GetAnimInstance() : nullptr;
	const FObjectProperty* RetargeterProperty = CosmeticAnim
		? FindFProperty<FObjectProperty>(CosmeticAnim->GetClass(), CosmeticRetargeterProperty)
		: nullptr;
	return RetargeterProperty != nullptr && CosmeticRetargeter != nullptr
		&& RetargeterProperty->GetObjectPropertyValue_InContainer(CosmeticAnim) == CosmeticRetargeter;
}

void ACombatCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void ACombatCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACombatCharacter::InteractShop);
	PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ACombatCharacter::UseEquipSlot1);
	PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ACombatCharacter::UseEquipSlot2);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ACombatCharacter::UseEquipSlot3);
	PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ACombatCharacter::UseEquipSlot4);
	PlayerInputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ACombatCharacter::UseEquipSlot5);

	// Set up action bindings. A child BP may deliberately leave the movement actions unset
	// (the GAS locomotion CBP handles move/look/jump in its own graph off IMC_Sandbox), so
	// bind only the actions this pawn actually has.
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Moving
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACombatCharacter::Move);
		}

		// Looking
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACombatCharacter::Look);
		}
		if (MouseLookAction)
		{
			EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ACombatCharacter::Look);
		}

		// Combo Attack
		if (ComboAttackAction)
		{
			EnhancedInputComponent->BindAction(ComboAttackAction, ETriggerEvent::Started, this, &ACombatCharacter::ComboAttackPressed);
		}
		else
		{
			UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-INPUT %s has no ComboAttackAction; combo attacks unbound"), *GetName());
		}

		// Camera zoom (mouse wheel, clamped)
		if (CameraZoomAction)
		{
			EnhancedInputComponent->BindAction(CameraZoomAction, ETriggerEvent::Triggered, this, &ACombatCharacter::CameraZoom);
		}

		// Charged Attack
		if (ChargedAttackAction)
		{
			EnhancedInputComponent->BindAction(ChargedAttackAction, ETriggerEvent::Started, this, &ACombatCharacter::ChargedAttackPressed);
			EnhancedInputComponent->BindAction(ChargedAttackAction, ETriggerEvent::Completed, this, &ACombatCharacter::ChargedAttackReleased);
		}
		else
		{
			UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-INPUT %s has no ChargedAttackAction; charged attacks unbound"), *GetName());
		}
	}
}

void ACombatCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// update the respawn transform on the Player Controller
	if (ACombatPlayerController* PC = Cast<ACombatPlayerController>(GetController()))
	{
		PC->SetRespawnTransform(GetActorTransform());
	}

	// the PlayerState exists only after possession (never at BeginPlay): hook the authoritative
	// equip notification here and sync immediately so a respawned pawn keeps its weapon visual
	if (AWrecktidePlayerState* WrecktidePlayerState = GetPlayerState<AWrecktidePlayerState>())
	{
		WrecktidePlayerState->OnWeaponEquipped.AddUniqueDynamic(this, &ACombatCharacter::OnWeaponEquippedRow);
		RefreshWeaponVisual();
	}
}

void ACombatCharacter::OnWeaponEquippedRow(FName Row)
{
	SetPistolAim(false);
	if (IsPistolWielded() && bIsAttacking)
	{
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance()) { Anim->Montage_Stop(PistolPoseBlendSeconds); }
		bIsAttacking = false;
		bIsChargingAttack = false;
	}
	RefreshWeaponVisual();
	UpdatePistolPose();
}

void ACombatCharacter::RefreshWeaponVisual()
{
	if (!WeaponMesh)
	{
		return;
	}

	const AWrecktidePlayerState* WrecktidePlayerState = GetPlayerState<AWrecktidePlayerState>();
	UStaticMesh* Visual = WrecktidePlayerState ? WeaponVisuals.FindRef(WrecktidePlayerState->EquippedWeaponRow).Get() : nullptr;
	if (!Visual && WrecktidePlayerState)
	{
		if (const FSoftObjectPath* Path = PistolVisualPaths.Find(WrecktidePlayerState->EquippedWeaponRow))
		{
			Visual = Cast<UStaticMesh>(Path->TryLoad());
			if (Visual) { WeaponVisuals.Add(WrecktidePlayerState->EquippedWeaponRow, Visual); }
			else { UE_LOG(LogCombatCharacter, Warning, TEXT("WRECKTIDE-PISTOL missing mesh %s"), *Path->ToString()); }
		}
	}
	if (!Visual)
	{
		WeaponMesh->SetVisibility(false);
		return;
	}

	// hold in the SKIN's hand when a skin is live, else in the base mesh's hand
	USkeletalMeshComponent* Hand = (CosmeticMesh && CosmeticMesh->GetSkeletalMeshAsset()) ? CosmeticMesh : GetMesh();
	WeaponMesh->SetStaticMesh(Visual);
	WeaponMesh->AttachToComponent(Hand, FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponAttachBone);
	WeaponMesh->SetRelativeLocationAndRotation(IsPistolWielded() ? PistolAttachLocation : WeaponAttachLocation,
		IsPistolWielded() ? PistolAttachRotation : WeaponAttachRotation);
	WeaponMesh->SetVisibility(true);

	UE_LOG(LogCombatCharacter, Log, TEXT("WRECKTIDE-WEAPON %s holds %s on %s:%s"),
		*GetName(), *Visual->GetName(), *Hand->GetName(), *WeaponAttachBone.ToString());
}

