// Copyright Epic Games, Inc. All Rights Reserved.
// Modifications Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatAttacker.h"
#include "CombatDamageable.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Engine/TimerHandle.h"
#include "CombatEnemy.generated.h"

class UWidgetComponent;
class UCombatLifeBar;
class UAnimMontage;
class UStatsComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimInstance;
class UAnimSequenceBase;
class UStaticMeshComponent;
class UPointLightComponent;

/** High-level awareness state. The StateTree only receives a player target while Chasing. */
UENUM(BlueprintType)
enum class EEnemyAggroState : uint8
{
	Idle,
	Alert,
	Chasing,
	Returning
};

/** Completed attack animation delegate for StateTree */
DECLARE_DELEGATE(FOnEnemyAttackCompleted);

/** Landed delegate for StateTree */
DECLARE_DELEGATE(FOnEnemyLanded);

/** Enemy died delegate */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEnemyDied);

/**
 *  An AI-controlled character with combat capabilities.
 *  Its bundled AI Controller runs logic through StateTree
 */
UCLASS(abstract)
class ACombatEnemy : public ACharacter, public ICombatAttacker, public ICombatDamageable
{
	GENERATED_BODY()

	/** Life bar widget component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UWidgetComponent* LifeBar;

	/** Cosmetic skin mesh, retarget-driven by the (hidden) main mesh. Inert until configured.
	 *  Mirrors ACombatCharacter's rig (enemies derive from ACharacter, not ACombatCharacter,
	 *  so the rig is duplicated rather than inherited - refactor to a shared component is a
	 *  polish item, not worth destabilizing the proven player path for). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* CosmeticMesh;

public:
	
	/** Constructor */
	ACombatEnemy();

protected:

	/** Max amount of HP the character will have on respawn */
	UPROPERTY(EditAnywhere, Category="Damage")
	float MaxHP = 3.0f;

public:

	/** Stats component that owns the single authoritative health path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide")
	TObjectPtr<UStatsComponent> Stats;

	/** EnemyRowName is the SINGLE authoritative data row key for enemy combat rewards and tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wrecktide|Data")
	FName EnemyRowName = FName("Cutthroat");

	/** Current amount of HP the character has */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Damage", meta = (ClampMin = 0, ClampMax = 100))
	float CurrentHP = 0.0f;

	/** Spawn post captured in BeginPlay; all leash distance and return movement use this point. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Aggro")
	FVector HomeLocation = FVector::ZeroVector;

	/** Facing captured with HomeLocation and restored after a successful return. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Aggro")
	FRotator HomeRotation = FRotator::ZeroRotator;

	/** Current awareness/return state (also exposed for the leash probe). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Aggro")
	EEnemyAggroState AggroState = EEnemyAggroState::Idle;

	/** Per-row acquisition distance loaded from Enemies.csv. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Aggro", meta=(Units="cm"))
	float AggroRadius = 1100.0f;

	/** Extra exit distance for an already visible plate, preventing edge flicker at AggroRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Enemy Plate", meta=(ClampMin="0.0", Units="cm"))
	float PlateVisibilityHysteresis = 150.0f;

	/** Gap between the crown of the collision capsule and the plate's bottom edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Enemy Plate", meta=(ClampMin="0.0", Units="cm"))
	float PlateHeadClearance = 24.0f;

	/** Point of the plate pinned to the anchor. Y=1 hangs the whole plate above it, clear of the face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Enemy Plate")
	FVector2D PlatePivot = FVector2D(0.5f, 1.0f);

	/** Per-row maximum chase distance from HomeLocation, loaded from Enemies.csv. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Aggro", meta=(Units="cm"))
	float LeashRadius = 2600.0f;

	/** Player distance that starts the continuous de-aggro timer while chasing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="1.0", Units="cm"))
	float DeaggroRadius = 2000.0f;

	/** Time the player must remain beyond DeaggroRadius before the enemy returns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.0", Units="s"))
	float DeaggroSeconds = 2.0f;

	/** Maximum time spent attempting to path home before settling calmly in place. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.1", Units="s"))
	float ReturnTimeoutSeconds = 15.0f;

	/** Low-speed time beyond melee range that counts as a stuck chase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.1", Units="s"))
	float StuckSeconds = 3.0f;

	/** Nearby players are noticed without a visibility trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.0", Units="cm"))
	float CloseAggroRadius = 350.0f;

	/** Length of the face-player anticipation beat before the StateTree may chase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.0", Units="s"))
	float AlertSeconds = 0.4f;

	/** Rotation interpolation speed used during Alert. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.0"))
	float AlertTurnSpeed = 8.0f;

	/** Path following acceptance radius for the return request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="1.0", Units="cm"))
	float ReturnAcceptanceRadius = 60.0f;

	/** Distance from HomeLocation that counts as an arrived, teleport-free return. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="1.0", Units="cm"))
	float ReturnArrivalRadius = 120.0f;

	/** Refresh interval for the nav request while Returning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.1", Units="s"))
	float ReturnMoveRefreshSeconds = 2.0f;

	/** How often a chase with no path being followed re-issues its move request. A live request is
	 *  never touched, so this only rescues a chase whose path completed short or was aborted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.1", Units="s"))
	float ChaseMoveRefreshSeconds = 1.0f;

	/** Calm period after arriving home before proximity may acquire the player again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.0", Units="s"))
	float ReaggroDelaySeconds = 1.0f;

	/** Speed below which a distant chase accumulates stuck time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="0.0", Units="cm/s"))
	float StuckSpeedThreshold = 20.0f;

	/** Conservative melee-range boundary; standing inside it never counts as stuck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(ClampMin="1.0", Units="cm"))
	float StuckTargetDistance = 250.0f;

	/** Capsule-centre height at or below which a guard counts as lost at sea and is put back at his
	 *  post. The ocean surface sits at Z -120 and the lowest walkable tier (beach, pier deck) is at
	 *  -40, so nothing he can legitimately stand on is anywhere near this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Aggro", meta=(Units="cm"))
	float WaterRescueZ = -120.0f;

	/** Loaded from Enemies.csv. Zero leaves the melee path unchanged. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Ranged")
	float RangedRange = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Ranged")
	float RangedDamage = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Ranged")
	float RangedCooldownSeconds = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Ranged")
	float RangedWindupSeconds = 0.0f;

	/** Full cone width; a stationary target can still be missed near the edge of the shot. */
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0", ClampMax="45", Units="deg"))
	float RangedSpreadDegrees = 2.0f;

	/** During cooldown, hold inside this fraction of range; otherwise resume closing. */
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0", ClampMax="1"))
	float RangedHoldRangeFraction = 0.5f;

	/** The sea is an escape; only a rider still close to the pier can receive a volley. */
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0", Units="cm"))
	float RangedBoatRange = 800.0f;

	/** Existing sample pose on the enemy's existing full-body montage slot, only while stopped. */
	UPROPERTY(EditAnywhere, Category="AI|Ranged")
	FString RangedPosePath = TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS");
	UPROPERTY(EditAnywhere, Category="AI|Ranged")
	FName RangedPoseSlot = TEXT("DefaultSlot");
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0", Units="s"))
	float RangedPoseBlendSeconds = 0.15f;

	UPROPERTY(EditAnywhere, Category="AI|Ranged")
	FName RangedAttachBone = TEXT("hand_r");
	UPROPERTY(EditAnywhere, Category="AI|Ranged")
	FVector RangedAttachLocation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category="AI|Ranged")
	FRotator RangedAttachRotation = FRotator(-7.0f, 97.1f, -30.0f);
	/** Rifle barrel is along mesh +Y. Also used by the muzzle light. */
	UPROPERTY(EditAnywhere, Category="AI|Ranged")
	FVector RangedMuzzleOffset = FVector(0.0f, 113.0f, 2.0f);

	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0.01", Units="s"))
	float RangedFlashSeconds = 0.08f;
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0"))
	float RangedFlashIntensity = 12000.0f;
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="1", Units="cm"))
	float RangedFlashRadius = 180.0f;
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0.1", ClampMax="2"))
	float RangedShotPitch = 0.85f;

	/** A curse originates at the bare hand and has a longer light tell than a musket. */
	UPROPERTY(EditAnywhere, Category="AI|Ranged", meta=(ClampMin="0.01", Units="s"))
	float UnarmedFlashSeconds = 0.25f;
	bool bRangedUnarmed = false;

	bool IsRangedHolding() const { return bRangedHolding; }
	bool IsRangedWindingUp() const { return bRangedWindingUp; }
	bool IsRangedPosePlaying() const;
	bool CanStartRangedVolley(const ACharacter* PlayerCharacter) const;
	const UStaticMeshComponent* GetRangedWeaponMesh() const { return RangedWeaponMesh; }

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	int32 GetRangedShotsFired() const { return RangedShotsFired; }
	int32 GetMeleeTracesFired() const { return MusketMeleeTracesFired; }
	bool HasRangedProbeSight(const ACharacter* Player) const { return HasClearSightTo(Player); }
	const UPointLightComponent* GetRangedProbeLight() const { return RangedMuzzleLight; }
#endif

protected:
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> RangedWeaponMesh;
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> RangedMuzzleLight;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> RangedPose;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> RangedPoseMontage;
	bool bRangedPoseLoadAttempted = false;
	bool bRangedHolding = false;
	bool bRangedWindingUp = false;
	float RangedSavedWalkSpeed = 0.0f;
	TEnumAsByte<ERootMotionMode::Type> RangedSavedRootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
	float RangedWindupEnds = 0.0f;
	float RangedNextVolleyTime = 0.0f;
	float RangedFlashEnds = 0.0f;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	int32 RangedShotsFired = 0;
	int32 MusketMeleeTracesFired = 0;
#endif
	bool UpdateRangedCombat(float DeltaSeconds, ACharacter* PlayerCharacter);
	void SetRangedHold(bool bHold);
	void CancelRangedVolley();
	void FireRangedShot(ACharacter* PlayerCharacter);
	bool CanUseRangedMelee() const;

	/** Name of the pelvis bone, for damage ragdoll physics */
	UPROPERTY(EditAnywhere, Category="Damage")
	FName PelvisBoneName;

	/** Pointer to the life bar widget */
	UPROPERTY(EditAnywhere, Category="Damage")
	UCombatLifeBar* LifeBarWidget;

	/** If true, the character is currently playing an attack animation */
	bool bIsAttacking = false;

	/** Distance ahead of the character that melee attack sphere collision traces will extend */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Trace", meta = (ClampMin = 0, ClampMax = 500, Units = "cm"))
	float MeleeTraceDistance = 75.0f;

	/** Radius of the sphere trace for melee attacks */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Trace", meta = (ClampMin = 0, ClampMax = 500, Units = "cm"))
	float MeleeTraceRadius = 50.0f;

	/** Amount of damage a melee attack will deal */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 100))
	float MeleeDamage = 1.0f;

	/** Amount of knockback impulse a melee attack will apply */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm/s"))
	float MeleeKnockbackImpulse = 150.0f;

	/** Amount of upwards impulse a melee attack will apply */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm/s"))
	float MeleeLaunchImpulse = 350.0f;

	/** Knockback impulse for regular combo hits (staggers, must not launch) */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm/s"))
	float ComboKnockbackImpulse = 60.0f;

	/** Upwards impulse for regular combo hits (keep near zero so combos chain) */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm/s"))
	float ComboLaunchImpulse = 0.0f;

	/** Damage multiplier for the charged strike (the payoff move hits harder than a combo hit) */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 1, ClampMax = 10))
	float ChargedDamageMultiplier = 2.0f;

	/** AnimMontage that will play for combo attacks */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Combo")
	UAnimMontage* ComboAttackMontage;

	/** Names of the AnimMontage sections that correspond to each stage of the combo attack */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Combo")
	TArray<FName> ComboSectionNames;

	/** Target number of attacks in the combo attack string we're playing */
	int32 TargetComboCount = 0;

	/** Index of the current stage of the melee attack combo */
	int32 CurrentComboAttack = 0;

	/** AnimMontage that will play for charged attacks */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged")
	UAnimMontage* ChargedAttackMontage;

	/** Name of the AnimMontage section that corresponds to the charge loop */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged")
	FName ChargeLoopSection;

	/** Name of the AnimMontage section that corresponds to the attack */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged")
	FName ChargeAttackSection;

	/** Minimum number of charge animation loops that will be played by the AI */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged", meta = (ClampMin = 1, ClampMax = 20))
	int32 MinChargeLoops = 2;

	/** Maximum number of charge animation loops that will be played by the AI */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged", meta = (ClampMin = 1, ClampMax = 20))
	int32 MaxChargeLoops = 5;

	/** Target number of charge animation loops to play in this charged attack */
	int32 TargetChargeLoops = 0;

	/** Number of charge animation loop currently playing */
	int32 CurrentChargeLoop = 0;

	/** Time to wait before removing this character from the level after it dies */
	UPROPERTY(EditAnywhere, Category="Death")
	float DeathRemovalTime = 5.0f;

	/** Skin to show instead of the main mesh. Unset = no cosmetic pass (enemy renders as-is). */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	TObjectPtr<USkeletalMesh> CosmeticMeshAsset;

	/** AnimInstance class for the cosmetic mesh (the GAS generic retarget ABP). */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	TSubclassOf<UAnimInstance> CosmeticAnimClass;

	/** IK Retargeter asset pushed into the cosmetic ABP's exposed variable at BeginPlay. */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	TObjectPtr<UObject> CosmeticRetargeter;

	/** Name of the exposed retargeter variable on the cosmetic ABP. */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	FName CosmeticRetargeterProperty = FName(TEXT("IKRetargeter"));

	/** Enemy death timer */
	FTimerHandle DeathTimer;

	/** Player retained through the short Alert beat. Chasing reacquires the possessed character. */
	TWeakObjectPtr<ACharacter> AggroTargetPlayer;

	float AggroStateElapsedSeconds = 0.0f;
	float PlayerFarElapsedSeconds = 0.0f;
	float StuckElapsedSeconds = 0.0f;
	float ReturnMoveElapsedSeconds = 0.0f;
	float ChaseMoveElapsedSeconds = 0.0f;
	float NextProximityAggroTime = 0.0f;
	bool bPlateVisible = false;

	TWeakObjectPtr<AActor> LastKillerActor;

	bool bAlreadyDead = false;

	/** Attack montage ended delegate */
	FOnMontageEnded OnAttackMontageEnded;

public:
	/** Attack completed internal delegate to notify StateTree tasks */
	FOnEnemyAttackCompleted OnAttackCompleted;

	/** Landed internal delegate to notify StateTree tasks. We use this instead of the built-in Landed delegate so we can bind to a Lambda in StateTree tasks */
	FOnEnemyLanded OnEnemyLanded;

	/** Enemy died delegate. Allows external subscribers to respond to enemy death */
	UPROPERTY(BlueprintAssignable, Category="Events")
	FOnEnemyDied OnEnemyDied;

public:

	/** True only while the stock StateTree is allowed to receive the player as its target. */
	FORCEINLINE bool IsPlayerTargetActive() const { return AggroState == EEnemyAggroState::Chasing; }

	FORCEINLINE EEnemyAggroState GetAggroState() const { return AggroState; }
	FORCEINLINE const FVector& GetHomeLocation() const { return HomeLocation; }
	FORCEINLINE float GetAggroRadius() const { return AggroRadius; }
	FORCEINLINE float GetLeashRadius() const { return LeashRadius; }
	FORCEINLINE float GetDeaggroSeconds() const { return DeaggroSeconds; }
	FORCEINLINE bool IsEnemyPlateVisible() const { return bPlateVisible; }
	FText GetEnemyPlateName() const;

	/** Lifts the plate to the crown of the capsule and pins it by its bottom edge. Safe to call again. */
	void UpdatePlateAnchor();

	/** True when the plate anchor clears the capsule crown by MinClearance and hangs from its bottom
	 *  edge. OutClearance reports the measured gap in uu. The plate probe's head check. */
	bool IsPlateAboveHead(float MinClearance, float& OutClearance) const;

	/** Performs an AI-initiated combo attack. Number of hits will be decided by this character */
	void DoAIComboAttack();

	/** Performs an AI-initiated charged attack. Charge time will be decided by this character */
	void DoAIChargedAttack();

	/** Called from a delegate when the attack montage ends */
	void AttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

public:

	// ~begin ICombatAttacker interface

	/** Performs an attack's collision check */
	virtual void DoAttackTrace(FName DamageSourceBone) override;

	/** Performs a combo attack's check to continue the string */
	UFUNCTION(BlueprintCallable, Category="Attacker")
	virtual void CheckCombo() override;

	/** Performs a charged attack's check to loop the charge animation */
	UFUNCTION(BlueprintCallable, Category="Attacker")
	virtual void CheckChargedAttack() override;

	// ~end ICombatAttacker interface

	// ~begin ICombatDamageable interface

	/** Handles damage and knockback events */
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;

	/** Handles death events */
	virtual void HandleDeath() override;

	/** Handles healing events */
	virtual void ApplyHealing(float Healing, AActor* Healer) override;

	// ~end ICombatDamageable interface

protected:

	/** Removes this character from the level after it dies */
	void RemoveFromLevel();

public:

	/** Overrides the default TakeDamage functionality */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Overrides landing to reset damage ragdoll physics */
	virtual void Landed(const FHitResult& Hit) override;

	/** Overrides KillZ death to use the single authoritative enemy death path. */
	virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

protected:

	/** Four-state awareness/leash update. */
	virtual void Tick(float DeltaSeconds) override;

	ACharacter* FindPlayerCharacter() const;
	bool IsPlayerAlive(const ACharacter* PlayerCharacter) const;
	bool HasClearSightTo(const ACharacter* PlayerCharacter) const;
	bool IsPlayerDamageSource(const AActor* DamageCauser, const AController* EventInstigator) const;
	void SetAggroState(EEnemyAggroState NewState, ACharacter* PlayerCharacter);
	void UpdatePlateVisibility(const ACharacter* PlayerCharacter);

	/** Puts a guard who ended up in the sea back at his post at full health. True when it fired. */
	bool RescueFromWater();
	void IssueReturnMove();
	void SettleAfterReturn(bool bReachedHome);

	/** Blueprint handler to play damage received effects */
	UFUNCTION(BlueprintImplementableEvent, Category="Combat")
	void ReceivedDamage(float Damage, const FVector& ImpactPoint, const FVector& DamageDirection);

	UFUNCTION()
	void OnStatsOutOfHealth(AActor* InstigatorActor);

	/** True when the driving mesh can actually simulate physics (ragdoll-capable). The GAS
	 *  driving mesh ships without physics collision - blending toward its never-simulated
	 *  bodies produces garbage transforms and the retargeted skin renders into nowhere. */
	bool CanRagdollMesh() const;

	/** Swaps the visible mesh for the configured cosmetic skin (fail-open to the driving mesh). */
	void ApplyCosmeticMesh();

	/** Pushes the retargeter asset into the cosmetic ABP's exposed variable. */
	bool PushCosmeticRetargeter();

	/** Re-pushes the retargeter when the engine re-creates the cosmetic anim instance. */
	UFUNCTION()
	void OnCosmeticAnimInitialized();

public:

	/** True when the cosmetic ABP currently holds the configured retargeter (probe hook). */
	bool IsCosmeticRetargeterLive() const;

	/** Returns the cosmetic mesh component (probe hook). */
	FORCEINLINE USkeletalMeshComponent* GetCosmeticMesh() const { return CosmeticMesh; }

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** EndPlay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
};
