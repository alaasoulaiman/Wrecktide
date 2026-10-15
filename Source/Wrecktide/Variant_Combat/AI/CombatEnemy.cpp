// Copyright Epic Games, Inc. All Rights Reserved.
// Modifications Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.


#include "CombatEnemy.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UnrealType.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CombatAIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/DamageEvents.h"
#include "CombatLifeBar.h"
#include "TimerManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideRewardSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideAudio.h"
#include "Gameplay/WrecktideBoat.h"
#include "Data/WrecktideDataRows.h"
#include "Variant_Combat/CombatCharacter.h"

namespace
{
const TCHAR* AggroStateName(EEnemyAggroState State)
{
	switch (State)
	{
	case EEnemyAggroState::Idle:
		return TEXT("Idle");
	case EEnemyAggroState::Alert:
		return TEXT("Alert");
	case EEnemyAggroState::Chasing:
		return TEXT("Chasing");
	case EEnemyAggroState::Returning:
		return TEXT("Returning");
	default:
		return TEXT("Unknown");
	}
}
}

ACombatEnemy::ACombatEnemy()
{
	PrimaryActorTick.bCanEverTick = true;

	// bind the attack montage ended delegate
	OnAttackMontageEnded.BindUObject(this, &ACombatEnemy::AttackMontageEnded);

	// set the AI Controller class by default
	AIControllerClass = ACombatAIController::StaticClass();

	// use an AI Controller regardless of whether we're placed or spawned
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// ignore the controller's yaw rotation
	bUseControllerRotationYaw = false;

	// create the life bar
	LifeBar = CreateDefaultSubobject<UWidgetComponent>(TEXT("LifeBar"));
	LifeBar->SetupAttachment(RootComponent);

	// cosmetic skin rig (mirror of ACombatCharacter's): inert until CDO-configured, so
	// unconfigured enemies pay nothing and render their driving mesh as before
	CosmeticMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CosmeticMesh"));
	CosmeticMesh->SetupAttachment(GetMesh());
	CosmeticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CosmeticMesh->PrimaryComponentTick.bStartWithTickEnabled = false;
	CosmeticMesh->SetVisibility(false);

	RangedWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RangedWeaponMesh"));
	RangedWeaponMesh->SetupAttachment(CosmeticMesh);
	RangedWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RangedWeaponMesh->SetCanEverAffectNavigation(false);
	RangedWeaponMesh->SetVisibility(false);
	RangedMuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RangedMuzzleLight"));
	RangedMuzzleLight->SetupAttachment(RangedWeaponMesh);
	RangedMuzzleLight->SetLightColor(FLinearColor(1.0f, 0.65f, 0.20f));
	RangedMuzzleLight->SetCastShadows(false);
	RangedMuzzleLight->SetVisibility(false);

	Stats = CreateDefaultSubobject<UStatsComponent>(TEXT("Stats"));

	// set the collision capsule size
	GetCapsuleComponent()->SetCapsuleSize(35.0f, 90.0f);

	// set the character movement properties
	GetCharacterMovement()->bUseControllerDesiredRotation = true;

	// reset HP to maximum
	CurrentHP = MaxHP;
}

void ACombatEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RangedMuzzleLight->SetVisibility(GetWorld() && GetWorld()->GetTimeSeconds() < RangedFlashEnds);

	if (bAlreadyDead || !Stats || !Stats->IsAlive())
	{
		return;
	}

	AggroStateElapsedSeconds += DeltaSeconds;

	// A guard knocked into the sea has no swim code, cannot path off the navmesh and floats above
	// KillZ forever, holding its spawner slot and its bounty. Put it back at its post instead.
	if (RescueFromWater())
	{
		return;
	}

	ACharacter* PlayerCharacter = FindPlayerCharacter();
	UpdatePlateVisibility(PlayerCharacter);

	switch (AggroState)
	{
	case EEnemyAggroState::Idle:
	{
		// The stock tree can retain an old move request for a frame after its target is cleared.
		// Keeping path following stopped here makes Idle a hard guarantee rather than a suggestion.
		if (AAIController* AIController = Cast<AAIController>(GetController()))
		{
			AIController->StopMovement();
		}

		const UWorld* World = GetWorld();
		if (!IsPlayerAlive(PlayerCharacter) || !World || World->GetTimeSeconds() < NextProximityAggroTime)
		{
			break;
		}

		const float PlayerDistance = FVector::Distance(GetActorLocation(), PlayerCharacter->GetActorLocation());
		if (PlayerDistance <= AggroRadius
			&& (PlayerDistance <= CloseAggroRadius || HasClearSightTo(PlayerCharacter)))
		{
			SetAggroState(EEnemyAggroState::Alert, PlayerCharacter);
		}
		break;
	}

	case EEnemyAggroState::Alert:
	{
		PlayerCharacter = AggroTargetPlayer.IsValid() ? AggroTargetPlayer.Get() : PlayerCharacter;
		if (!IsPlayerAlive(PlayerCharacter))
		{
			SetAggroState(EEnemyAggroState::Returning, PlayerCharacter);
			break;
		}

		if (AAIController* AIController = Cast<AAIController>(GetController()))
		{
			AIController->SetFocus(PlayerCharacter);
		}

		const FVector ToPlayer = PlayerCharacter->GetActorLocation() - GetActorLocation();
		if (!ToPlayer.IsNearlyZero())
		{
			const FRotator DesiredRotation(0.0f, ToPlayer.Rotation().Yaw, 0.0f);
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), DesiredRotation, DeltaSeconds, AlertTurnSpeed));
		}

		if (AggroStateElapsedSeconds >= AlertSeconds)
		{
			SetAggroState(EEnemyAggroState::Chasing, PlayerCharacter);
		}
		break;
	}

	case EEnemyAggroState::Chasing:
	{
		if (!IsPlayerAlive(PlayerCharacter))
		{
			SetAggroState(EEnemyAggroState::Returning, PlayerCharacter);
			break;
		}

		AggroTargetPlayer = PlayerCharacter;

		const float PlayerDistance = FVector::Distance(GetActorLocation(), PlayerCharacter->GetActorLocation());
		const float HomeDistance = FVector::Distance(GetActorLocation(), HomeLocation);
		PlayerFarElapsedSeconds = PlayerDistance > DeaggroRadius ? PlayerFarElapsedSeconds + DeltaSeconds : 0.0f;
		// A volley never postpones the distance-based leash checks.
		if (HomeDistance > LeashRadius || PlayerFarElapsedSeconds >= DeaggroSeconds)
		{
			SetAggroState(EEnemyAggroState::Returning, PlayerCharacter);
			break;
		}
		if (UpdateRangedCombat(DeltaSeconds, PlayerCharacter))
		{
			// A deliberate hold is not a failed chase. The stock tree remains live throughout.
			StuckElapsedSeconds = 0.0f;
			ChaseMoveElapsedSeconds = 0.0f;
			break;
		}

		// The stock tree owns the chase once it has a target, but a move request can COMPLETE at its
		// acceptance radius or be aborted, and then nothing re-issues it: the guard stands there
		// "chasing" from 500 uu away (the packaged 2026-09-06 leash run). Re-issue only when no path
		// is being followed at all, so this can never fight a live request from the tree, and only
		// on the refresh beat, so it is frame-rate independent.
		ChaseMoveElapsedSeconds += DeltaSeconds;
		if (ChaseMoveElapsedSeconds >= ChaseMoveRefreshSeconds)
		{
			ChaseMoveElapsedSeconds = 0.0f;
			if (AAIController* AIController = Cast<AAIController>(GetController()))
			{
				if (AIController->GetMoveStatus() != EPathFollowingStatus::Moving)
				{
					AIController->MoveToActor(PlayerCharacter, StuckTargetDistance);
				}
			}
		}

		StuckElapsedSeconds = PlayerDistance > StuckTargetDistance
			&& GetVelocity().Size() < StuckSpeedThreshold
			? StuckElapsedSeconds + DeltaSeconds
			: 0.0f;

		if (StuckElapsedSeconds >= StuckSeconds)
		{
			SetAggroState(EEnemyAggroState::Returning, PlayerCharacter);
		}
		break;
	}

	case EEnemyAggroState::Returning:
	{
		// Path following resolves a walking goal on the nav surface, while the deferred spawn can be
		// adjusted vertically before BeginPlay captures HomeLocation. Arrival is therefore horizontal;
		// no teleport is used and the actor keeps the ground height chosen by character movement.
		const float HomeDistance = FVector::Dist2D(GetActorLocation(), HomeLocation);
		if (HomeDistance <= ReturnArrivalRadius)
		{
			SettleAfterReturn(true);
			break;
		}

		if (AggroStateElapsedSeconds >= ReturnTimeoutSeconds)
		{
			SettleAfterReturn(false);
			break;
		}

		ReturnMoveElapsedSeconds += DeltaSeconds;
		if (ReturnMoveElapsedSeconds >= ReturnMoveRefreshSeconds)
		{
			IssueReturnMove();
		}
		break;
	}
	}
}

ACharacter* ACombatEnemy::FindPlayerCharacter() const
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (RangedRange > 0.0f)
	{
		if (const AWrecktideBoat* Boat = Cast<AWrecktideBoat>(PlayerPawn)) { return Boat->GetRider(); }
	}
	return Cast<ACharacter>(PlayerPawn);
}

bool ACombatEnemy::CanStartRangedVolley(const ACharacter* PlayerCharacter) const
{
	if (!HasAuthority() || bAlreadyDead || !Stats || !Stats->IsAlive()
		|| AggroState != EEnemyAggroState::Chasing || RangedRange <= 0.0f
		|| !IsPlayerAlive(PlayerCharacter)) { return false; }
	const float Distance = FVector::Distance(GetActorLocation(), PlayerCharacter->GetActorLocation());
	if (Distance <= CloseAggroRadius || Distance > RangedRange) { return false; }
	const UCharacterMovementComponent* PlayerMovement = PlayerCharacter->GetCharacterMovement();
	if (PlayerMovement && PlayerMovement->IsSwimming()) { return false; }
	const AActor* Parent = PlayerCharacter->GetAttachParentActor();
	const UPrimitiveComponent* Base = PlayerCharacter->GetMovementBase();
	const bool bAboard = (Parent && Parent->IsA<AWrecktideBoat>())
		|| (Base && Base->GetOwner() && Base->GetOwner()->IsA<AWrecktideBoat>());
	return (!bAboard || Distance <= RangedBoatRange) && HasClearSightTo(PlayerCharacter);
}

void ACombatEnemy::SetRangedHold(bool bHold)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bHold)
	{
		if (!bRangedHolding)
		{
			RangedSavedWalkSpeed = Movement->MaxWalkSpeed;
			if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
			{
				RangedSavedRootMotionMode = Anim->RootMotionMode;
				Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
			}
		}
		// The StateTree can issue another MoveTo later in the same frame. Zero speed blocks that
		// request too; StopMovement every tick releases its path without editing the tree asset.
		Movement->MaxWalkSpeed = 0.0f;
		Movement->StopMovementImmediately();
		if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
	}
	else if (bRangedHolding)
	{
		Movement->MaxWalkSpeed = RangedSavedWalkSpeed;
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance()) { Anim->SetRootMotionMode(RangedSavedRootMotionMode); }
		if (AAIController* AI = Cast<AAIController>(GetController())) { AI->ClearFocus(EAIFocusPriority::Gameplay); }
	}
	bRangedHolding = bHold;
}

void ACombatEnemy::CancelRangedVolley()
{
	bRangedWindingUp = false;
	if (LifeBarWidget) { LifeBarWidget->SetRangedTell(false); }
	if (RangedPoseMontage)
	{
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance()) { Anim->Montage_Stop(RangedPoseBlendSeconds, RangedPoseMontage); }
		RangedPoseMontage = nullptr;
	}
	SetRangedHold(false);
}

bool ACombatEnemy::IsRangedPosePlaying() const
{
	const UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	return Anim && RangedPoseMontage && Anim->Montage_IsPlaying(RangedPoseMontage);
}

bool ACombatEnemy::UpdateRangedCombat(float DeltaSeconds, ACharacter* PlayerCharacter)
{
	if (RangedRange <= 0.0f) { return false; }
	// Re-evaluate at every windup tick and again at fire time: cover or sea entry cancels the tell.
	if (!CanStartRangedVolley(PlayerCharacter)) { CancelRangedVolley(); return false; }
	const float Now = GetWorld()->GetTimeSeconds();
	const float Distance = FVector::Distance(GetActorLocation(), PlayerCharacter->GetActorLocation());
	if (!bRangedWindingUp && Now < RangedNextVolleyTime && Distance > RangedRange * RangedHoldRangeFraction)
	{
		SetRangedHold(false);
		return false;
	}
	SetRangedHold(true);
	if (AAIController* AI = Cast<AAIController>(GetController())) { AI->SetFocus(PlayerCharacter); }
	const FRotator Facing(0.0f, (PlayerCharacter->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.0f);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Facing, DeltaSeconds, AlertTurnSpeed));
	if (!bRangedWindingUp && Now >= RangedNextVolleyTime)
	{
		bRangedWindingUp = true;
		RangedWindupEnds = Now + RangedWindupSeconds;
		if (LifeBarWidget) { LifeBarWidget->SetRangedTell(true); }
		if (!bRangedUnarmed && !bRangedPoseLoadAttempted)
		{
			bRangedPoseLoadAttempted = true;
			RangedPose = LoadObject<UAnimSequenceBase>(nullptr, *RangedPosePath);
			if (!RangedPose) { UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MUSKET missing aim pose %s"), *RangedPosePath); }
		}
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Stop(RangedPoseBlendSeconds, ComboAttackMontage);
			Anim->Montage_Stop(RangedPoseBlendSeconds, ChargedAttackMontage);
			if (RangedPose)
			{
				RangedPoseMontage = Anim->PlaySlotAnimationAsDynamicMontage(RangedPose, RangedPoseSlot,
					RangedPoseBlendSeconds, RangedPoseBlendSeconds, 1.0f, MAX_int32);
			}
		}
		UE_LOG(LogTemp, Verbose, TEXT("WRECKTIDE-MUSKET windup %s distance=%.0f"), *GetName(), Distance);
	}
	if (bRangedWindingUp && Now >= RangedWindupEnds)
	{
		FireRangedShot(PlayerCharacter);
		RangedNextVolleyTime = Now + RangedCooldownSeconds;
		CancelRangedVolley();
		SetRangedHold(true); // Hold for the shot frame; the next tick may resume closing.
	}
	return true;
}

void ACombatEnemy::FireRangedShot(ACharacter* PlayerCharacter)
{
	if (!CanStartRangedVolley(PlayerCharacter)) { return; }
	const FVector Muzzle = bRangedUnarmed ? FVector::ZeroVector : RangedMuzzleOffset;
	const FVector Origin = RangedWeaponMesh->GetComponentTransform().TransformPosition(Muzzle);
	const FVector Aim = (PlayerCharacter->GetActorLocation() - Origin).GetSafeNormal();
	const FVector Direction = FMath::VRandCone(Aim, FMath::DegreesToRadians(RangedSpreadDegrees * 0.5f));
	FCollisionQueryParams Query(SCENE_QUERY_STAT(WrecktideMusketTrace), false, this);
	FCollisionObjectQueryParams Pawns;
	Pawns.AddObjectTypesToQuery(ECC_Pawn);
	FHitResult Hit;
	bool bHitPlayer = false;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Origin, Origin + Direction * RangedRange, Pawns, Query)
		&& Hit.GetActor() == PlayerCharacter)
	{
		FHitResult Cover;
		if (!GetWorld()->LineTraceSingleByChannel(Cover, Origin, Hit.ImpactPoint, ECC_Visibility, Query)
			|| Cover.GetActor() == PlayerCharacter || Cover.Distance + 1.0f >= Hit.Distance)
		{
			if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(PlayerCharacter))
			{
				Damageable->ApplyDamage(RangedDamage, this, Hit.ImpactPoint, FVector::ZeroVector);
				bHitPlayer = true;
			}
		}
	}
	RangedFlashEnds = GetWorld()->GetTimeSeconds() + (bRangedUnarmed ? UnarmedFlashSeconds : RangedFlashSeconds);
	RangedMuzzleLight->SetRelativeLocation(Muzzle);
	RangedMuzzleLight->SetIntensity(RangedFlashIntensity);
	RangedMuzzleLight->SetAttenuationRadius(RangedFlashRadius);
	RangedMuzzleLight->SetVisibility(true);
	if (!bRangedUnarmed) { UWrecktideAudio::PlaySfx(this, TEXT("pistol_shot"), 1.0f, RangedShotPitch); }
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	++RangedShotsFired;
#endif
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET fire %s hit=%d damage=%.0f distance=%.0f"),
		*GetName(), bHitPlayer, RangedDamage, FVector::Distance(GetActorLocation(), PlayerCharacter->GetActorLocation()));
}

bool ACombatEnemy::CanUseRangedMelee() const
{
	const ACharacter* Player = FindPlayerCharacter();
	return !bAlreadyDead && !bRangedHolding && AggroState == EEnemyAggroState::Chasing && IsPlayerAlive(Player)
		&& FVector::Distance(GetActorLocation(), Player->GetActorLocation()) <= CloseAggroRadius;
}

bool ACombatEnemy::IsPlayerAlive(const ACharacter* PlayerCharacter) const
{
	if (!IsValid(PlayerCharacter))
	{
		return false;
	}

	if (const ACombatCharacter* CombatCharacter = Cast<ACombatCharacter>(PlayerCharacter))
	{
		return CombatCharacter->Stats && CombatCharacter->Stats->IsAlive();
	}

	return !PlayerCharacter->IsActorBeingDestroyed();
}

bool ACombatEnemy::HasClearSightTo(const ACharacter* PlayerCharacter) const
{
	const UWorld* World = GetWorld();
	if (!World || !PlayerCharacter)
	{
		return false;
	}

	const FVector TraceStart = GetActorLocation() + FVector(0.0f, 0.0f, BaseEyeHeight);
	const UCapsuleComponent* PlayerCapsule = PlayerCharacter->GetCapsuleComponent();
	const FVector TraceEnd = PlayerCapsule ? PlayerCapsule->GetComponentLocation() : PlayerCharacter->GetActorLocation();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WrecktideEnemyAggroSight), false, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(PlayerCharacter);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
}

bool ACombatEnemy::IsPlayerDamageSource(const AActor* DamageCauser, const AController* EventInstigator) const
{
	auto IsPlayerActor = [](const AActor* Actor)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		return Actor && (Actor->ActorHasTag(FName(TEXT("Player"))) || (Pawn && Pawn->IsPlayerControlled()));
	};

	if (IsPlayerActor(DamageCauser) || (EventInstigator && IsPlayerActor(EventInstigator->GetPawn())))
	{
		return true;
	}

	return DamageCauser
		&& (IsPlayerActor(DamageCauser->GetInstigator()) || IsPlayerActor(DamageCauser->GetOwner()));
}

void ACombatEnemy::UpdatePlateVisibility(const ACharacter* PlayerCharacter)
{
	if (!LifeBar)
	{
		bPlateVisible = false;
		return;
	}

	bool bShouldShow = !bAlreadyDead && Stats && Stats->IsAlive()
		&& AggroState != EEnemyAggroState::Idle;
	if (!bShouldShow && !bAlreadyDead && Stats && Stats->IsAlive() && IsPlayerAlive(PlayerCharacter))
	{
		const float VisibilityRadius = AggroRadius + (bPlateVisible ? PlateVisibilityHysteresis : 0.0f);
		bShouldShow = FVector::Distance(GetActorLocation(), PlayerCharacter->GetActorLocation()) <= VisibilityRadius;
	}

	bPlateVisible = bShouldShow;
	LifeBar->SetHiddenInGame(!bShouldShow);
}

void ACombatEnemy::UpdatePlateAnchor()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!LifeBar || !Capsule)
	{
		return;
	}

	// A screen-space widget projects its COMPONENT location, and LifeBar hangs off the capsule, whose
	// origin is the belly: that is why the name drew across the chest. Lift the anchor to the crown of
	// the capsule plus PlateHeadClearance and pin the plate by its bottom edge (pivot Y=1) so the name
	// AND the bar sit above the anchor and never cover the face. A relative offset is multiplied by the
	// component's world scale, so divide that back out and a scaled row (the boss) keeps the same real
	// clearance instead of drifting up with its size.
	const float ScaleZ = FMath::Max(FMath::Abs(Capsule->GetComponentScale().Z), KINDA_SMALL_NUMBER);
	const float WorldLift = Capsule->GetScaledCapsuleHalfHeight() + PlateHeadClearance;
	LifeBar->SetRelativeLocation(FVector(0.0f, 0.0f, WorldLift / ScaleZ));
	LifeBar->SetPivot(PlatePivot);
}

bool ACombatEnemy::IsPlateAboveHead(float MinClearance, float& OutClearance) const
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

FText ACombatEnemy::GetEnemyPlateName() const
{
	return LifeBarWidget ? LifeBarWidget->GetEnemyNameText() : FText::GetEmpty();
}

void ACombatEnemy::SetAggroState(EEnemyAggroState NewState, ACharacter* PlayerCharacter)
{
	if (AggroState == NewState)
	{
		return;
	}

	const EEnemyAggroState OldState = AggroState;
	if (NewState != EEnemyAggroState::Chasing && RangedRange > 0.0f) { CancelRangedVolley(); }
	const float PlayerDistance = PlayerCharacter
		? FVector::Distance(GetActorLocation(), PlayerCharacter->GetActorLocation())
		: 1000000.0f;
	const float HomeDistance = FVector::Distance(GetActorLocation(), HomeLocation);
	AggroState = NewState;
	AggroStateElapsedSeconds = 0.0f;

	AAIController* AIController = Cast<AAIController>(GetController());
	switch (NewState)
	{
	case EEnemyAggroState::Idle:
		AggroTargetPlayer.Reset();
		PlayerFarElapsedSeconds = 0.0f;
		StuckElapsedSeconds = 0.0f;
		if (AIController)
		{
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
			AIController->StopMovement();
		}
		if (OldState == EEnemyAggroState::Returning && GetWorld())
		{
			NextProximityAggroTime = GetWorld()->GetTimeSeconds() + ReaggroDelaySeconds;
		}
		break;

	case EEnemyAggroState::Alert:
		AggroTargetPlayer = PlayerCharacter;
		if (AIController)
		{
			AIController->StopMovement();
			AIController->SetFocus(PlayerCharacter);
		}
		break;

	case EEnemyAggroState::Chasing:
		AggroTargetPlayer = PlayerCharacter;
		PlayerFarElapsedSeconds = 0.0f;
		StuckElapsedSeconds = 0.0f;
		ChaseMoveElapsedSeconds = 0.0f;
		if (AIController)
		{
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
			// The stock tree's Idling state can remain selected for a frame after the target gate opens.
			// Seed the same path-following request it would make so the chase starts immediately; tree
			// flank/melee tasks remain free to replace this request as their states activate.
			if (PlayerCharacter)
			{
				AIController->MoveToActor(PlayerCharacter, StuckTargetDistance);
			}
		}
		break;

	case EEnemyAggroState::Returning:
		AggroTargetPlayer.Reset();
		PlayerFarElapsedSeconds = 0.0f;
		StuckElapsedSeconds = 0.0f;
		if (AIController)
		{
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
			AIController->StopMovement();
		}
		IssueReturnMove();
		break;
	}

	UpdatePlateVisibility(PlayerCharacter ? PlayerCharacter : FindPlayerCharacter());

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-AGGRO %s %s -> %s (dist=%.0f home=%.0f)"),
		*GetName(), AggroStateName(OldState), AggroStateName(NewState), PlayerDistance, HomeDistance);
}

bool ACombatEnemy::RescueFromWater()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}

	// Two ways to end up lost at sea: the movement mode says so, or the capsule centre has sunk
	// below the water line (no physics volume out past the surf walls, so the mode alone is not
	// enough). Either way there is no navmesh under him and nothing else will ever bring him back.
	const bool bInWater = Movement->IsSwimming() || GetActorLocation().Z < WaterRescueZ;
	if (!bInWater)
	{
		return false;
	}

	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		AIController->StopMovement();
	}
	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Walking);
	SetActorLocation(HomeLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(HomeRotation);
	if (Stats)
	{
		Stats->InitStats(Stats->GetMaxHealth());
		CurrentHP = Stats->GetHealth();
		if (LifeBarWidget)
		{
			LifeBarWidget->SetLifePercentage(1.0f);
		}
	}
	// Straight back to Idle, whatever it was doing. The state is forced to Returning first so the
	// transition always runs (an idle guard shoved off the pier is still Idle) and the Idle entry
	// applies its usual re-aggro delay before he can lunge at the player again.
	AggroState = EEnemyAggroState::Returning;
	SetAggroState(EEnemyAggroState::Idle, nullptr);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-AGGRO %s rescued from the sea"), *GetName());
	return true;
}

void ACombatEnemy::IssueReturnMove()
{
	ReturnMoveElapsedSeconds = 0.0f;
	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		// Do not add the agent radius to the authored 60 uu acceptance: doing so stops a standard
		// enemy roughly 135 uu away, outside the 120 uu arrival/health-reset boundary.
		AIController->MoveToLocation(HomeLocation, ReturnAcceptanceRadius, false);
	}
}

void ACombatEnemy::SettleAfterReturn(bool bReachedHome)
{
	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		AIController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();

	if (bReachedHome)
	{
		SetActorRotation(HomeRotation);
		if (Stats)
		{
			Stats->InitStats(Stats->GetMaxHealth());
			CurrentHP = Stats->GetHealth();
			if (LifeBarWidget)
			{
				LifeBarWidget->SetLifePercentage(1.0f);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-AGGRO %s return timed out %.0f uu 2D / %.0f uu 3D from home (at=%s home=%s)"),
			*GetName(), FVector::Dist2D(GetActorLocation(), HomeLocation),
			FVector::Distance(GetActorLocation(), HomeLocation), *GetActorLocation().ToString(), *HomeLocation.ToString());
	}

	SetAggroState(EEnemyAggroState::Idle, FindPlayerCharacter());
}

void ACombatEnemy::DoAIComboAttack()
{
	if (RangedRange > 0.0f && !CanUseRangedMelee()) { OnAttackCompleted.ExecuteIfBound(); return; }
	// ignore if we're already playing an attack animation
	if (bIsAttacking)
	{
		return;
	}

	// raise the attacking flag
	bIsAttacking = true;

	// choose how many times we're going to attack
	TargetComboCount = FMath::RandRange(1, ComboSectionNames.Num() - 1);

	// reset the attack counter
	CurrentComboAttack = 0;

	// play the attack montage
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		const float MontageLength = AnimInstance->Montage_Play(ComboAttackMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, true);

		// subscribe to montage completed and interrupted events
		if (MontageLength > 0.0f)
		{
			// set the end delegate for the montage
			AnimInstance->Montage_SetEndDelegate(OnAttackMontageEnded, ComboAttackMontage);
		}
	}
}

void ACombatEnemy::DoAIChargedAttack()
{
	if (RangedRange > 0.0f && !CanUseRangedMelee()) { OnAttackCompleted.ExecuteIfBound(); return; }
	// ignore if we're already playing an attack animation
	if (bIsAttacking)
	{
		return;
	}

	// raise the attacking flag
	bIsAttacking = true;

	// choose how many loops are we going to charge for
	TargetChargeLoops = FMath::RandRange(MinChargeLoops, MaxChargeLoops);

	// reset the charge loop counter
	CurrentChargeLoop = 0;

	// play the attack montage
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		const float MontageLength = AnimInstance->Montage_Play(ChargedAttackMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, true);

		// subscribe to montage completed and interrupted events
		if (MontageLength > 0.0f)
		{
			// set the end delegate for the montage
			AnimInstance->Montage_SetEndDelegate(OnAttackMontageEnded, ChargedAttackMontage);
		}
	}
}

void ACombatEnemy::AttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// reset the attacking flag
	bIsAttacking = false;

	// call the attack completed delegate so the StateTree can continue execution
	OnAttackCompleted.ExecuteIfBound();
}

void ACombatEnemy::DoAttackTrace(FName DamageSourceBone)
{
	if (RangedRange > 0.0f && !CanUseRangedMelee()) { return; }
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	++MusketMeleeTracesFired;
#endif
	// sweep for objects in front of the character to be hit by the attack
	TArray<FHitResult> OutHits;

	// start at the provided socket location, sweep forward
	const FVector TraceStart = GetMesh()->GetSocketLocation(DamageSourceBone);
	const FVector TraceEnd = TraceStart + (GetActorForwardVector() * MeleeTraceDistance);

	// enemies only affect Pawn collision objects; they don't knock back boxes
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// use a sphere shape for the sweep
	FCollisionShape CollisionShape;
	CollisionShape.SetSphere(MeleeTraceRadius);

	// ignore self
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// combo hits stagger; only the charged strike launches (2026-07-27 playtest: a full launch
	// every hit made getting hit feel like a catapult and broke combo exchanges)
	UAnimInstance* TraceAnimInstance = GetMesh()->GetAnimInstance();
	const bool bChargedStrike = TraceAnimInstance && ChargedAttackMontage && TraceAnimInstance->Montage_IsPlaying(ChargedAttackMontage);
	UE_LOG(LogTemp, Verbose, TEXT("WRECKTIDE-COMBAT enemy trace charged=%d"), bChargedStrike ? 1 : 0);

	if (GetWorld()->SweepMultiByObjectType(OutHits, TraceStart, TraceEnd, FQuat::Identity, ObjectParams, CollisionShape, QueryParams))
	{
		// iterate over each object hit
		for (const FHitResult& CurrentHit : OutHits)
		{
			/** does the actor have the player tag? */
			if (CurrentHit.GetActor()->ActorHasTag(FName("Player")))
			{
				// check if the actor is damageable
				ICombatDamageable* Damageable = Cast<ICombatDamageable>(CurrentHit.GetActor());

				if (Damageable)
				{
					// knock upwards and away from the impact normal; combo hits use the
					// stagger pair so the target is shoved, not launched
					const FVector Impulse = bChargedStrike
						? (CurrentHit.ImpactNormal * -MeleeKnockbackImpulse) + (FVector::UpVector * MeleeLaunchImpulse)
						: (CurrentHit.ImpactNormal * -ComboKnockbackImpulse) + (FVector::UpVector * ComboLaunchImpulse);

					// pass the damage event to the actor (charged strikes hit harder)
					Damageable->ApplyDamage(MeleeDamage * (bChargedStrike ? ChargedDamageMultiplier : 1.0f),
						this, CurrentHit.ImpactPoint, Impulse);

				}
			}
		}
	}
}

bool ACombatEnemy::CanRagdollMesh() const
{
	const USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || !MeshComp->GetPhysicsAsset())
	{
		return false;
	}
	const ECollisionEnabled::Type Collision = MeshComp->GetCollisionEnabled();
	return Collision == ECollisionEnabled::QueryAndPhysics || Collision == ECollisionEnabled::PhysicsOnly;
}

void ACombatEnemy::CheckCombo()
{
	// increase the combo counter
	++CurrentComboAttack;

	// do we still have attacks to play in this string?
	if (CurrentComboAttack < TargetComboCount)
	{
		// jump to the next attack section
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_JumpToSection(ComboSectionNames[CurrentComboAttack], ComboAttackMontage);
		}
	}
}

void ACombatEnemy::CheckChargedAttack()
{
	// increase the charge loop counter
	++CurrentChargeLoop;

	// jump to either the loop or attack section of the montage depending on whether we hit the loop target
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_JumpToSection(CurrentChargeLoop >= TargetChargeLoops ? ChargeAttackSection : ChargeLoopSection, ChargedAttackMontage);
	}
}

void ACombatEnemy::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	
	// pass the damage event to the actor
	FDamageEvent DamageEvent;
	const float ActualDamage = TakeDamage(Damage, DamageEvent, nullptr, DamageCauser);

	// only process knockback and effects if we received nonzero damage
	if (ActualDamage > 0.0f)
	{
		if (RangedRange > 0.0f) { CancelRangedVolley(); }
		// apply the knockback impulse
		GetCharacterMovement()->AddImpulse(DamageImpulse, true);

		// is the character ragdolling?
		if (GetMesh()->IsSimulatingPhysics())
		{
			// apply an impulse to the ragdoll
			GetMesh()->AddImpulseAtLocation(DamageImpulse * GetMesh()->GetMass(), DamageLocation);
		}

		// stop the attack montages to interrupt the attack
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.1f, ComboAttackMontage);
			AnimInstance->Montage_Stop(0.1f, ChargedAttackMontage);
		}

		// pass control to BP to play effects, etc.
		ReceivedDamage(ActualDamage, DamageLocation, DamageImpulse.GetSafeNormal());
	}
}

void ACombatEnemy::HandleDeath()
{
	if (bAlreadyDead)
	{
		return;
	}
	bAlreadyDead = true;
	CancelRangedVolley();
	RangedFlashEnds = 0.0f;
	RangedMuzzleLight->SetVisibility(false);

	// hide the life bar
	bPlateVisible = false;
	LifeBar->SetHiddenInGame(true);

	// disable the collision capsule to avoid being hit again while dead
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// disable character movement
	GetCharacterMovement()->DisableMovement();

	// enable full ragdoll physics - only on a physics-capable mesh (see CanRagdollMesh);
	// death still proceeds either way, the corpse just holds its last animated pose
	if (CanRagdollMesh())
	{
		GetMesh()->SetSimulatePhysics(true);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-COMBAT ragdoll-skip death %s (mesh not physics-capable)"), *GetName());
	}

	if (UWrecktideRewardSubsystem* RewardSubsystem = UWrecktideRewardSubsystem::Get(GetWorld()))
	{
		RewardSubsystem->NotifyEnemyKilled(this, LastKillerActor.Get());
	}

	// call the died delegate to notify any subscribers
	OnEnemyDied.Broadcast();

	// set up the death timer
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &ACombatEnemy::RemoveFromLevel, DeathRemovalTime);
}

void ACombatEnemy::ApplyHealing(float Healing, AActor* Healer)
{
	// stub
}

void ACombatEnemy::RemoveFromLevel()
{
	// destroy this actor
	Destroy();
}

float ACombatEnemy::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// only process damage if the character is still alive
	if (!Stats || !Stats->IsAlive())
	{
		return 0.0f;
	}

	const float Applied = Stats->ApplyDamage(Damage, DamageCauser, false);
	CurrentHP = Stats->GetHealth();
	if (Applied > 0.0f && Stats->IsAlive() && AggroState != EEnemyAggroState::Returning
		&& IsPlayerDamageSource(DamageCauser, EventInstigator)
		&& AggroState == EEnemyAggroState::Idle)
	{
		SetAggroState(EEnemyAggroState::Alert, FindPlayerCharacter());
	}

	if (Applied > 0.0f && Stats->IsAlive())
	{
		// update the life bar
		if (LifeBarWidget)
		{
			LifeBarWidget->SetLifePercentage(Stats->GetHealth() / Stats->GetMaxHealth());
		}

		// enable partial ragdoll physics, but keep the pelvis vertical - only on a mesh whose
		// bodies can actually simulate (see CanRagdollMesh)
		if (CanRagdollMesh())
		{
			GetMesh()->SetPhysicsBlendWeight(0.5f);
			GetMesh()->SetBodySimulatePhysics(PelvisBoneName, false);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-COMBAT ragdoll-skip hit %s (mesh not physics-capable)"), *GetName());
		}
	}

	// return the received damage amount
	return Applied;
}

void ACombatEnemy::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// is the character still alive?
	if (Stats && Stats->IsAlive())
	{
		// disable ragdoll physics
		GetMesh()->SetPhysicsBlendWeight(0.0f);
	}

	// call the landed Delegate for StateTree
	OnEnemyLanded.ExecuteIfBound();
}

void ACombatEnemy::FellOutOfWorld(const UDamageType& DmgType)
{
	if (Stats && Stats->IsAlive())
	{
		OnStatsOutOfHealth(nullptr);
	}

	Super::FellOutOfWorld(DmgType);
}

void ACombatEnemy::OnStatsOutOfHealth(AActor* InstigatorActor)
{
	LastKillerActor = InstigatorActor;
	HandleDeath();
}

void ACombatEnemy::BeginPlay()
{
	// reset HP to maximum
	CurrentHP = MaxHP;

	// we top the HP before BeginPlay so StateTree picks it up at the right value
	Super::BeginPlay();
	HomeLocation = GetActorLocation();
	HomeRotation = GetActorRotation();
	NextProximityAggroTime = 0.0f;

	// get the life bar widget from the widget comp
	LifeBarWidget = Cast<UCombatLifeBar>(LifeBar->GetUserWidgetObject());
	if (!LifeBarWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-COMBAT enemy life bar missing for %s"), *GetName());
	}

	if (Stats)
	{
		UGameInstance* GameInstance = GetGameInstance();
		UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
		if (const FEnemyTableRow* EnemyRow = DataSubsystem ? DataSubsystem->GetEnemyRow(EnemyRowName) : nullptr)
		{
			Stats->InitStats(EnemyRow->MaxHealth);
			MeleeDamage = EnemyRow->BaseDamage;
			AggroRadius = EnemyRow->AggroRadius;
			LeashRadius = EnemyRow->LeashRadius;
			RangedRange = EnemyRow->RangedRange;
			RangedDamage = EnemyRow->RangedDamage;
			RangedCooldownSeconds = EnemyRow->RangedCooldownSeconds;
			RangedWindupSeconds = EnemyRow->RangedWindupSeconds;
			bRangedUnarmed = EnemyRow->bRangedUnarmed;
			RangedMuzzleLight->SetLightColor(EnemyRow->RangedFlashColor);
			if (RangedRange > 0.0f && !bRangedUnarmed)
			{
				UStaticMesh* Weapon = EnemyRow->RangedWeaponMesh.LoadSynchronous();
				if (Weapon) { RangedWeaponMesh->SetStaticMesh(Weapon); }
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MUSKET missing mesh for %s; using melee"), *EnemyRowName.ToString());
					RangedRange = 0.0f;
				}
			}
			if (LifeBarWidget)
			{
				const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
				const AWrecktidePlayerState* WrecktidePlayerState = PlayerController
					? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
				const USkillComponent* PlayerSkill = WrecktidePlayerState ? WrecktidePlayerState->GetSkill() : nullptr;
				LifeBarWidget->SetEnemyName(EnemyRow->DisplayName, EnemyRow->Level,
					PlayerSkill ? PlayerSkill->GetNotorietyLevel() : 1);
			}
		}
		else
		{
			Stats->InitStats(MaxHP);
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-FUNGATE-ROWMISS %s"), *EnemyRowName.ToString());
		}

		Stats->OnOutOfHealth.AddUniqueDynamic(this, &ACombatEnemy::OnStatsOutOfHealth);
		CurrentHP = Stats->GetHealth();
		if (LifeBarWidget)
		{
			LifeBarWidget->SetLifePercentage(Stats->GetHealth() / Stats->GetMaxHealth());
		}
	}

	// Per-row skin (2026-09-02): Enemies.csv names a Synty mesh per row, so a Skeleton Deckhand is a
	// skeleton and a Navy Cadet a redcoat. Resolved BEFORE ApplyCosmeticMesh so the retarget rig hooks
	// the right asset; an empty or unloadable column keeps the class default (Deckhand).
	{
		UGameInstance* GameInstance = GetGameInstance();
		const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
		const FEnemyTableRow* EnemyRow = DataSubsystem ? DataSubsystem->GetEnemyRow(EnemyRowName) : nullptr;
		if (EnemyRow && !EnemyRow->SkinMesh.IsNull())
		{
			if (USkeletalMesh* RowSkin = EnemyRow->SkinMesh.LoadSynchronous())
			{
				CosmeticMeshAsset = RowSkin;
				UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ENEMYSKIN %s row=%s skin=%s"),
					*GetName(), *EnemyRowName.ToString(), *RowSkin->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-ENEMYSKIN %s row=%s skin load FAILED for %s, keeping the class default"),
					*GetName(), *EnemyRowName.ToString(), *EnemyRow->SkinMesh.ToString());
			}
		}
	}

	// swap in the cosmetic skin last, when every component is registered
	ApplyCosmeticMesh();
	if (RangedRange > 0.0f && (bRangedUnarmed || RangedWeaponMesh->GetStaticMesh()))
	{
		USkeletalMeshComponent* Hand = CosmeticMesh->IsVisible() ? CosmeticMesh : GetMesh();
		RangedWeaponMesh->AttachToComponent(Hand, FAttachmentTransformRules::SnapToTargetNotIncludingScale, RangedAttachBone);
		RangedWeaponMesh->SetRelativeLocationAndRotation(bRangedUnarmed ? FVector::ZeroVector : RangedAttachLocation,
			bRangedUnarmed ? FRotator::ZeroRotator : RangedAttachRotation);
		RangedWeaponMesh->SetVisibility(!bRangedUnarmed);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MUSKET equipped %s mesh=%s hand=%s"),
			*GetName(), bRangedUnarmed ? TEXT("empty") : *RangedWeaponMesh->GetStaticMesh()->GetName(), *Hand->GetName());
	}
	const UWrecktideDataSubsystem* SkinData = GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>();
	const FEnemyTableRow* SkinRow = SkinData ? SkinData->GetEnemyRow(EnemyRowName) : nullptr;
	if (SkinRow && !SkinRow->SkinMaterial.IsNull())
	{
		if (UMaterialInterface* Material = SkinRow->SkinMaterial.LoadSynchronous())
		{
			for (int32 Slot = 0; Slot < CosmeticMesh->GetNumMaterials(); ++Slot) { CosmeticMesh->SetMaterial(Slot, Material); }
		}
		else { UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-ISLAND3 missing skin material for %s"), *EnemyRowName.ToString()); }
	}

	// anchor the plate after Super::BeginPlay, so a Blueprint that resized the capsule in its own
	// BeginPlay is already accounted for
	UpdatePlateAnchor();
	UpdatePlateVisibility(FindPlayerCharacter());
}

void ACombatEnemy::ApplyCosmeticMesh()
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
	if (!PushCosmeticRetargeter())
	{
		CosmeticMesh->SetVisibility(false);
		return;
	}

	// the write is per-instance: re-push whenever the engine re-creates the anim instance
	// (e.g. an ABP recompile mid-session), or the skin silently T-poses until this enemy dies
	CosmeticMesh->OnAnimInitialized.AddUniqueDynamic(this, &ACombatEnemy::OnCosmeticAnimInitialized);

	// the retarget node's contract: the pose SOURCE must tick before this instance so the skin
	// follows the ANIMATED pose same-frame. Known limit (review 2026-07-27): the ragdoll blend
	// runs in TG_EndPhysics, after both meshes' TG_PrePhysics ticks, so hit/death PHYSICS motion
	// reaches the skin one frame late - accepted (cosmetically invisible at playable framerates).
	CosmeticMesh->AddTickPrerequisiteComponent(GetMesh());
	CosmeticMesh->SetComponentTickEnabled(true);

	// the driving mesh keeps animating as the retarget pose source (incl. the hit/death ragdoll
	// physics blends - the retarget node reads the blended component-space pose) but never
	// renders. It must refresh BONES, not just tick the pose: the retarget node reads
	// component-space transforms and DoAttackTrace reads socket locations off this hidden mesh.
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCastShadow(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	CosmeticMesh->SetVisibility(true);
	CosmeticMesh->SetCastShadow(true);

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-ECOSMETIC applied enemy=%s skin=%s abp=%s retargeter=%s"),
		*GetName(), *CosmeticMeshAsset->GetName(),
		*CosmeticMesh->GetAnimInstance()->GetClass()->GetName(), *CosmeticRetargeter->GetName());
}

bool ACombatEnemy::PushCosmeticRetargeter()
{
	UAnimInstance* CosmeticAnim = CosmeticMesh ? CosmeticMesh->GetAnimInstance() : nullptr;
	FObjectProperty* RetargeterProperty = CosmeticAnim
		? FindFProperty<FObjectProperty>(CosmeticAnim->GetClass(), CosmeticRetargeterProperty)
		: nullptr;
	if (!CosmeticAnim || !RetargeterProperty || !CosmeticRetargeter)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("WRECKTIDE-ECOSMETIC %s hookup failed (anim=%d prop=%d rtg=%d); keeping the driving mesh visible"),
			*GetName(), CosmeticAnim != nullptr, RetargeterProperty != nullptr, CosmeticRetargeter != nullptr);
		return false;
	}

	// the destination property stores a TYPED pointer; reflection does zero IsA validation, so a
	// mistyped asset (e.g. the IK Rig sitting next to the retargeter) would be undefined behavior
	if (!CosmeticRetargeter->IsA(RetargeterProperty->PropertyClass))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("WRECKTIDE-ECOSMETIC %s retargeter %s is not a %s; keeping the driving mesh visible"),
			*GetName(), *CosmeticRetargeter->GetClass()->GetName(), *RetargeterProperty->PropertyClass->GetName());
		return false;
	}

	RetargeterProperty->SetObjectPropertyValue_InContainer(CosmeticAnim, CosmeticRetargeter);
	return true;
}

void ACombatEnemy::OnCosmeticAnimInitialized()
{
	PushCosmeticRetargeter();
}

bool ACombatEnemy::IsCosmeticRetargeterLive() const
{
	const UAnimInstance* CosmeticAnim = CosmeticMesh ? CosmeticMesh->GetAnimInstance() : nullptr;
	const FObjectProperty* RetargeterProperty = CosmeticAnim
		? FindFProperty<FObjectProperty>(CosmeticAnim->GetClass(), CosmeticRetargeterProperty)
		: nullptr;
	return RetargeterProperty != nullptr && CosmeticRetargeter != nullptr
		&& RetargeterProperty->GetObjectPropertyValue_InContainer(CosmeticAnim) == CosmeticRetargeter;
}

void ACombatEnemy::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the death timer
	GetWorld()->GetTimerManager().ClearTimer(DeathTimer);
}
