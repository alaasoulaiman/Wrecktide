// Copyright Epic Games, Inc. All Rights Reserved.
// Modifications Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatAttacker.h"
#include "CombatDamageable.h"
#include "Animation/AnimInstance.h"
#include "CombatCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;
class UCombatLifeBar;
class UWidgetComponent;
class UStatsComponent;
class UAnimSequenceBase;
class USkeletalMesh;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogCombatCharacter, Log, All);

/**
 *  An enhanced Third Person Character with melee combat capabilities:
 *  - Combo attack string
 *  - Press and hold charged attack
 *  - Damage dealing and reaction
 *  - Death
 *  - Respawning
 */
UCLASS(abstract)
class ACombatCharacter : public ACharacter, public ICombatAttacker, public ICombatDamageable
{
	GENERATED_BODY()
	friend class UWrecktideWardrobeSubsystem;

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	/** Life bar widget component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UWidgetComponent* LifeBar;

	/** Cosmetic skin mesh, retarget-driven by the (hidden) main mesh. Inert until configured. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* CosmeticMesh;

	/** Held-weapon visual, driven by the PlayerState's equipped weapon row. Hidden when unarmed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* WeaponMesh;

	UPROPERTY(VisibleAnywhere, Category="Pistol")
	TObjectPtr<class UPointLightComponent> PistolMuzzleLight;

public:

	/** Stats component that owns the single authoritative health path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide")
	TObjectPtr<UStatsComponent> Stats;

	/** Camera-centre shot reach in Unreal units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pistol", meta=(ClampMin="1"))
	float PistolRange = 2500.0f;

	/** One shot, then this many seconds before another can fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pistol", meta=(ClampMin="0.05"))
	float PistolReloadSeconds = 1.2f;

	UPROPERTY(EditAnywhere, Category="Pistol", meta=(ClampMin="1"))
	float PistolAimArmLength = 270.0f;

	UPROPERTY(EditAnywhere, Category="Pistol", meta=(ClampMin="20", ClampMax="120"))
	float PistolAimFOV = 75.0f;

	/** Look past the right shoulder so the pirate and held pistol leave the crosshair clear. */
	UPROPERTY(EditAnywhere, Category="Pistol")
	FVector PistolAimSocketOffset = FVector(0.0f, 90.0f, 65.0f);

	UPROPERTY(EditAnywhere, Category="Pistol", meta=(ClampMin="0.01"))
	float PistolFlashSeconds = 0.08f;

	UPROPERTY(EditAnywhere, Category="Pistol", meta=(ClampMin="0"))
	float PistolFlashIntensity = 12000.0f;

	UPROPERTY(EditAnywhere, Category="Pistol", meta=(ClampMin="1"))
	float PistolFlashRadius = 180.0f;

	/** Offset from the pistol mesh pivot to its muzzle. */
	UPROPERTY(EditAnywhere, Category="Pistol")
	FVector PistolMuzzleOffset = FVector(0.0f, 35.0f, 5.0f);

	UPROPERTY(EditAnywhere, Category="Pistol")
	FVector PistolAttachLocation = FVector::ZeroVector;

	/** Align the barrel (+Y) with the hand's forward aim in the retargeted pose. */
	UPROPERTY(EditAnywhere, Category="Pistol")
	FRotator PistolAttachRotation = FRotator(0.0f, -11.0f, 28.0f);

	/** String references are kept by the weapons folder cook rule. */
	UPROPERTY(EditAnywhere, Category="Pistol")
	TMap<FName, FSoftObjectPath> PistolVisualPaths;

	/** Existing sample upper-body aim pose; the legs retain locomotion. */
	UPROPERTY(EditAnywhere, Category="Pistol")
	FString PistolPosePath = TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS");

	UPROPERTY(EditAnywhere, Category="Pistol", meta=(ClampMin="0"))
	float PistolPoseBlendSeconds = 0.15f;

	bool IsPistolWielded() const;
	bool CanUsePistol() const;
	bool TracePistol(FHitResult& OutHit) const;
	bool IsPistolTargetLivingEnemy() const;
	bool IsPistolReloading() const;
	float GetPistolReloadFraction() const;
	bool IsPistolAiming() const { return bPistolAiming; }
	/** Shared by LMB (DoComboAttackStart) and the shooting probe. */
	bool FirePistol();
	void SetPistolAim(bool bAim);
	void UseEquipSlot2();
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Combo Attack Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* ComboAttackAction;

	/** Charged Attack Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* ChargedAttackAction;

	/** Max amount of HP the character will have on respawn */
	UPROPERTY(EditAnywhere, Category="Damage", meta = (ClampMin = 0, ClampMax = 100))
	float MaxHP = 5.0f;

	UPROPERTY(EditAnywhere, Category="Wrecktide")
	float PlayerMaxHealth = 100.0f;

	/** Current amount of HP the character has */
	UPROPERTY(VisibleAnywhere, Category="Damage")
	float CurrentHP = 0.0f;

	/** Life bar widget fill color */
	UPROPERTY(EditAnywhere, Category="Damage")
	FLinearColor LifeBarColor;

	/** Gap between the crown of the collision capsule and the life bar's bottom edge. Mirrors the
	 *  enemy plate rule: the pirate's own bar is the only health readout on screen and it used to
	 *  draw across his chest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Life Bar", meta=(ClampMin="0.0", Units="cm"))
	float LifeBarHeadClearance = 24.0f;

	/** Point of the bar pinned to the anchor. Y=1 hangs the whole bar above it, clear of the head. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI|Life Bar")
	FVector2D LifeBarPivot = FVector2D(0.5f, 1.0f);

	/** Name of the pelvis bone, for damage ragdoll physics */
	UPROPERTY(EditAnywhere, Category="Damage")
	FName PelvisBoneName;

	/** Pointer to the life bar widget */
	UPROPERTY(EditAnywhere, Category="Damage")
	TObjectPtr<UCombatLifeBar> LifeBarWidget;

	/** Max amount of time that may elapse for a non-combo attack input to not be considered stale */
	UPROPERTY(EditAnywhere, Category="Melee Attack", meta = (ClampMin = 0, ClampMax = 5, Units = "s"))
	float AttackInputCacheTimeTolerance = 1.0f;

	/** Time at which an attack button was last pressed */
	float CachedAttackInputTime = 0.0f;

	/** If true, the character is currently playing an attack animation */
	bool bIsAttacking = false;

	/** Distance ahead of the character that melee attack sphere collision traces will extend */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Trace", meta = (ClampMin = 0, ClampMax = 500, Units="cm"))
	float MeleeTraceDistance = 75.0f;

	/** Radius of the sphere trace for melee attacks */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Trace", meta = (ClampMin = 0, ClampMax = 200, Units = "cm"))
	float MeleeTraceRadius = 75.0f;

	/** Amount of damage a melee attack will deal */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 100))
	float MeleeDamage = 1.0f;

	/** Amount of knockback impulse a melee attack will apply */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm/s"))
	float MeleeKnockbackImpulse = 250.0f;

	/** Amount of upwards impulse a melee attack will apply */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Damage", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm/s"))
	float MeleeLaunchImpulse = 300.0f;

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

	/** Max amount of time that may elapse for a combo attack input to not be considered stale */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Combo", meta = (ClampMin = 0, ClampMax = 5, Units = "s"))
	float ComboInputCacheTimeTolerance = 0.45f;

	/** Index of the current stage of the melee attack combo */
	int32 ComboCount = 0;

	/** The montage whose end delegate currently owns the attack state. */
	UPROPERTY(Transient)
	UAnimMontage* ActiveAttackMontage = nullptr;

	/** AnimMontage that will play for charged attacks */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged")
	UAnimMontage* ChargedAttackMontage;

	/** Name of the AnimMontage section that corresponds to the charge loop */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged")
	FName ChargeLoopSection;

	/** Name of the AnimMontage section that corresponds to the attack */
	UPROPERTY(EditAnywhere, Category="Melee Attack|Charged")
	FName ChargeAttackSection;

	/** Flag that determines if the player is currently holding the charged attack input */
	bool bIsChargingAttack = false;
	
	/** If true, the charged attack hold check has been tested at least once */
	bool bHasLoopedChargedAttack = false;

	/** True while an AIRBORNE attack has switched the anim instance to IgnoreRootMotion so the
	 *  jump arc and forward momentum survive the swing (2026-07-27: a root-motion montage owns
	 *  movement, and with translation scale 0 that froze the pawn dead in mid-air). */
	bool bAirAttackRootMotionOverride = false;

	/** The anim instance's root motion mode before the airborne-attack override, restored on
	 *  montage end. */
	TEnumAsByte<ERootMotionMode::Type> SavedRootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;

	/** Camera boom length while the character is dead */
	UPROPERTY(EditAnywhere, Category="Camera", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float DeathCameraDistance = 400.0f;

	/** Camera boom length when the character respawns */
	UPROPERTY(EditAnywhere, Category="Camera", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float DefaultCameraDistance = 100.0f;

	/** Scroll-zoom input action (mouse wheel; bound Triggered in SetupPlayerInputComponent) */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* CameraZoomAction;

	/** Camera distance change per wheel notch */
	UPROPERTY(EditAnywhere, Category="Camera", meta = (ClampMin = 1, ClampMax = 200, Units = "cm"))
	float CameraZoomStep = 30.0f;

	/** Closest the scroll zoom may bring the camera (play test note: capped, not too close) */
	UPROPERTY(EditAnywhere, Category="Camera", meta = (ClampMin = 50, ClampMax = 1000, Units = "cm"))
	float CameraZoomMin = 150.0f;

	/** Farthest the scroll zoom may push the camera (capped, not too far) */
	UPROPERTY(EditAnywhere, Category="Camera", meta = (ClampMin = 50, ClampMax = 2000, Units = "cm"))
	float CameraZoomMax = 500.0f;

	/** Time to wait before respawning the character */
	UPROPERTY(EditAnywhere, Category="Respawn", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 3.0f;

	// ---------------------------------------------------------------------------------------------
	// Swimming (2026-09-06). The sea is a real water APhysicsVolume (Tools\island_sea_volume.py), so
	// the engine owns the movement mode; everything here is the pawn's half: the CMC numbers, the
	// rig tilt that makes a walk cycle read as a swim, and the haul-out that gets the pirate back on
	// land. These are runtime values because the CBP CDO owns the rest of the CMC (RECON trap 10).
	// ---------------------------------------------------------------------------------------------

	/** Swim speed applied to the CMC at BeginPlay (a shade under the 500 walk speed). */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm/s"))
	float SwimSpeed = 330.0f;

	/** Buoyancy applied at BeginPlay. Above 1 floats the pirate with his head clear of the surface.
	 *  The CMC balances at ImmersionDepth = 1/Buoyancy, so with an 86 uu capsule half height the
	 *  resting capsule centre is waterZ + 86 - 172/Buoyancy: 1.12 sat him at -188 (chest deep, and
	 *  the chase camera sank under the surface with him, _logs\side_swimside_t5_idle.png), 1.30 sits
	 *  him at -166 and also restores him faster after a dive, since the restoring acceleration is
	 *  gravity * (Buoyancy * depth - 1). */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 3))
	float SwimBuoyancy = 1.30f;

	/** Braking deceleration while swimming (water is thick; he should not glide like ice). */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 4000))
	float SwimBraking = 400.0f;

	/** Rig pitch while swimming forward: the body lies along the surface, face down. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = -90, ClampMax = 0, Units = "deg"))
	float SwimTiltMovingPitch = -72.0f;

	/** Rig pitch while treading water (upright-ish, head clear). */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = -90, ClampMax = 0, Units = "deg"))
	float SwimTiltIdlePitch = -28.0f;

	/** Speed above which the swim rig uses the moving tilt. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 400, Units = "cm/s"))
	float SwimTiltMoveSpeed = 60.0f;

	/** Interpolation rate for the rig tilt, in and out of the water. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0.5, ClampMax = 30))
	float SwimTiltInterpSpeed = 5.0f;

	/** Fraction of the capsule half height the rig rises by while swimming, so the prone body's
	 *  centre sits at the capsule centre instead of hanging off its feet. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 1))
	float SwimRigLift = 0.45f;

	/** Camera boom rise while swimming, so the spring arm does not sit inside the surface. Measured
	 *  by capture, not guessed: the swimmer floats with his capsule centre near -158 and the water
	 *  plane is at -120, so the boom needs well over the 30 uu first tried (_logs
un_swimfront_t3.png
	 *  from that value is shot from UNDER the sea). */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 400, Units = "cm"))
	float SwimCameraLift = 150.0f;

	/** Hard floor for the camera above the sea surface while swimming, in uu. SwimCameraLift alone
	 *  scales with the tilt, so a TREADING swimmer (tilt 0, so lift 0) kept only 41 to 43 uu of
	 *  clearance and any downward look pitch put the lens through the water plane
	 *  (probe_polish_swim.log / probe_pkg5_swim.log: socketZ=32 cameraZ=-77 and -79 with the surface
	 *  at -120). This clamp is measured against the sea APhysicsVolume's own top, so it cannot drift
	 *  away from island_sea_volume.py's Z_WATER the way a second copy of the constant would.
	 *  Raised 90 -> 180 on 09-06 round 3: at 90 uu the lens sat almost ON the surface, so the sea was
	 *  seen edge on at a grazing angle, which is the worst case for both the reflection smear and the
	 *  wave silhouette. Swim cameras in comparable games sit 1.5 to 2.5 m up looking down. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 400, Units = "cm"))
	float SwimCameraMinClearance = 180.0f;

	/** Ceiling on the clamp above, so a bad surface reading can never fly the camera off. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float SwimCameraClampMax = 260.0f;

	/** Running total the clamp has added to the boom this swim, in uu. Reset on leaving the water. */
	float SwimCameraClampAdd = 0.0f;

	/**
	 * Top of the water APhysicsVolume the pirate is standing in, in world Z. False when he is not in
	 * a water volume, or when the volume reports an empty brush (extent 0, the exact failure
	 * island_sea_volume.py fails closed on) so a clamp is never taken against a lie. Reading the
	 * level beats a second copy of Z_WATER in C++ that could drift away from the tool's.
	 */
	bool GetSwimSurfaceZ(float& OutSurfaceZ) const;

	/** Tallest ledge (measured capsule-centre to capsule-centre) the swimmer can haul himself onto.
	 *  The beach ring and the pier deck are both 80 uu above the water line; the engine's built-in
	 *  water jump cannot clear either (MaxOutOfWaterStepHeight 40, OutofWaterZ 150 = an 11 uu rise),
	 *  so the haul-out below is the shore exit. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 600, Units = "cm"))
	float SwimHaulMaxLedge = 300.0f;

	/** Fastest the pirate may still be sinking on the frame he enters the water. Measured: at 250 he
	 *  plunged to -286 and took three seconds to surface (-SwimProbe sample series, probe_swim_3.log);
	 *  the water should catch him, not swallow him. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 2000, Units = "cm/s"))
	float SwimEntrySinkSpeed = 110.0f;

	/** Horizontal speed kept on entering the water (the sea grabs a falling body). */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 1))
	float SwimEntryDrag = 0.6f;

	/** Seconds between haul-out attempts, so a swimmer pressed against a wall does not spam it. */
	UPROPERTY(EditAnywhere, Category="Swimming", meta = (ClampMin = 0, ClampMax = 5, Units = "s"))
	float SwimHaulCooldown = 0.6f;

	/** True on the frame the CMC reports swimming; drives the enter / leave transitions. */
	bool bWasSwimming = false;

	/** Current interpolated rig pitch (degrees, 0 = the pawn's normal standing rig). */
	float SwimRigPitch = 0.0f;

	/** World time the last haul-out was attempted. */
	float LastSwimHaulTime = -100.0f;

	/** Decorative medium hull cached at BeginPlay so a failed F can point to the player's small boat. */
	TWeakObjectPtr<AActor> DecorativeShipHull;

	/** Skin to show instead of the main mesh. Unset = no cosmetic pass (fallback pawns unchanged). */
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

	/** The helm grip pose (spec C28.3), loaded BY STRING the first time the wheel asks for it, so no
	 *  hard reference is added to the pawn. MF_Pistol_Idle_ADS is the Game Animation Sample's pistol
	 *  aim idle: the only pose in the shipped set that holds BOTH hands forward at chest height with
	 *  the head up and the shoulders square, which is what a helmsman's hands on a wheel look like.
	 *  It is authored on SK_Mannequin, the same skeleton the player's attack montages use, so it
	 *  plays on the same driving mesh they do. */
	UPROPERTY(EditAnywhere, Category="Helm")
	FString HelmGripPosePath = TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS");

	/** The slot the grip plays on: the SAME upper-body layer the player's attacks use (layered from
	 *  spine_01), so the legs keep the standing idle underneath and only the arms come up. */
	UPROPERTY(EditAnywhere, Category="Helm")
	FName HelmGripSlot = FName(TEXT("UpperBody"));

	/** The live dynamic montage and the sequence behind it. Transient: both are runtime only. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> HelmGripMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> HelmGripPose;

	/** What the helm last asked for, so a stop is only ever sent once. */
	bool bHelmGripRequested = false;

	/** Weapon row name -> held-weapon mesh. Rows without an entry render no weapon. */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	TMap<FName, TObjectPtr<UStaticMesh>> WeaponVisuals;

	/** Bone (or socket) on the skin mesh the weapon snaps to. */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	FName WeaponAttachBone = FName(TEXT("hand_r"));

	/** Grip offset, tunable on the CDO without a rebuild. */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	FVector WeaponAttachLocation = FVector::ZeroVector;

	/** Grip rotation, tunable on the CDO without a rebuild. */
	UPROPERTY(EditAnywhere, Category="Cosmetic")
	FRotator WeaponAttachRotation = FRotator::ZeroRotator;

	/** Attack montage ended delegate */
	FOnMontageEnded OnAttackMontageEnded;

	/** Character respawn timer */
	FTimerHandle RespawnTimer;

	bool bAlreadyDead = false;

	float PistolReloadStarted = -100.0f;
	float PistolReloadEnds = -100.0f;
	float PistolFlashEnds = -100.0f;
	float PistolSavedArmLength = 0.0f;
	float PistolSavedFOV = 0.0f;
	FVector PistolSavedSocketOffset = FVector::ZeroVector;
	bool bPistolAiming = false;
	bool bPistolReloadPending = false;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PistolPoseMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> PistolPose;
	void UpdatePistolPose();
	void SetBaseCameraSocketOffset(const FVector& Offset);
	float ApplyWeaponHit(const FHitResult& Hit, float Damage, const FVector& Impulse, bool bCritical);

	/** Copy of the mesh's transform so we can reset it after ragdoll animations */
	FTransform MeshStartingTransform;

	/** The boom's authored SocketOffset, captured beside MeshStartingTransform. The swim rig adds
	 *  its lift to this and restores it on settle, so the authored value is never lost. */
	FVector CameraBoomStartingSocketOffset = FVector::ZeroVector;

public:
	
	/** Constructor */
	ACombatCharacter();

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** Called for combo attack input */
	void ComboAttackPressed();

	/** Called for combo attack input pressed */
	void ChargedAttackPressed();

	/** Called for combo attack input released */
	void ChargedAttackReleased();

public:

	/** InteractShop toggles the SINGLE in-range stall menu from the legacy F binding. */
	void InteractShop();

	/** UseEquipSlot1 is the SINGLE authoritative slot-1 input path used by key 1 and the probes.
	 *  It WIELDS the assigned melee (spec C17); it no longer cycles through owned weapons, that job
	 *  moved to the inventory screen. */
	void UseEquipSlot1();

	/** The montage this pawn actually plays for combo attacks (probes must not hardcode a path). */
	UAnimMontage* GetComboAttackMontage() const { return ComboAttackMontage; }

	/** The montage this pawn actually plays for charged attacks. */
	UAnimMontage* GetChargedAttackMontage() const { return ChargedAttackMontage; }

	/** Authored two swing section list for the contract probe. */
	const TArray<FName>& GetComboSectionNames() const { return ComboSectionNames; }

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles combo attack pressed from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoComboAttackStart();

	/** Handles combo attack released from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoComboAttackEnd();

	/** Handles charged attack pressed from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoChargedAttackStart();

	/** Handles charged attack released from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoChargedAttackEnd();

protected:

	void UseReservedEquipSlot(int32 Slot);
	void UseEquipSlot3();
	void UseEquipSlot4();
	void UseEquipSlot5();
	bool IsShopMenuOpen() const;

	/** Resets the character's current HP to maximum */
	void ResetHP();

	/** Applies the cosmetic skin + hides the driving mesh; safe no-op when unconfigured. */
	void ApplyCosmeticMesh();

	/** Writes the retargeter into the cosmetic ABP's exposed variable (type-checked). */
	bool PushCosmeticRetargeter();

	/** Re-pushes the retargeter whenever the cosmetic anim instance is (re)created - an ABP
	 *  recompile mid-session would otherwise leave a fresh instance with a None retargeter. */
	UFUNCTION()
	void OnCosmeticAnimInitialized();

	/** PlayerState OnWeaponEquipped handler. */
	UFUNCTION()
	void OnWeaponEquippedRow(FName Row);

public:

	/** Re-reads the PlayerState's equipped row and shows/hides the held-weapon visual. */
	void RefreshWeaponVisual();

	/** Returns the held-weapon visual component. */
	FORCEINLINE class UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

protected:

	/** True when the driving mesh can actually simulate physics (ragdoll-capable). The GAS
	 *  driving mesh ships without physics collision - blending toward its never-simulated
	 *  bodies produces garbage transforms and the retargeted skin renders into nowhere. */
	bool CanRagdollMesh() const;

	/** Scroll-zoom handler: steps the camera boom, clamped to [CameraZoomMin, CameraZoomMax] */
	void CameraZoom(const struct FInputActionValue& Value);

	/** Snaps the character's yaw to the controller's aim so attacks strike where the camera looks */
	void FaceControlYaw();

	/** Leans the rig into (or out of) the swim pose for this frame. */
	void UpdateSwimRig(float DeltaSeconds);

	/** Hauls the swimmer onto a low ledge he is pushing against. True when the launch fired. */
	bool TryHaulOutOfWater();

	/** Finds and caches the decorative medium hull by runtime mesh name. */
	void CacheDecorativeShipHull();

	/** Performs a combo attack */
	void ComboAttack();

	/** Performs a charged attack */
	void ChargedAttack();

	/** Called from a delegate when the attack montage ends */
	void AttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnStatsOutOfHealth(AActor* InstigatorActor);

	
public:

	// ~begin CombatAttacker interface

	/** Performs the collision check for an attack */
	virtual void DoAttackTrace(FName DamageSourceBone) override;

	/** Performs the combo string check */
	virtual void CheckCombo() override;

	/** Performs the charged attack hold check */
	virtual void CheckChargedAttack() override;

	// ~end CombatAttacker interface

	// ~begin CombatDamageable interface

	/** Handles damage and knockback events */
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;

	/** Handles death events */
	virtual void HandleDeath() override;

	/** Handles healing events */
	virtual void ApplyHealing(float Healing, AActor* Healer) override;

	// ~end CombatDamageable interface

	/** Called from the respawn timer to destroy and re-create the character */
	void RespawnCharacter();

	/** GetAttackDamage is the SINGLE authoritative read path for combat character melee damage. */
	float GetAttackDamage() const;

	/** Play rate every attack montage starts at: the blacksmith's Balance track (spec C10) for the
	 *  player, and always 1 for anyone without a Wrecktide player state. */
	float GetAttackPlayRate() const;

public:

	/** Overrides the default TakeDamage functionality */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Overrides landing to reset damage ragdoll physics */
	virtual void Landed(const FHitResult& Hit) override;

	/** Drives the swim rig and the shore haul-out. */
	virtual void Tick(float DeltaSeconds) override;

	/** Safety net under the sea volume (WorldSettings KillZ): log and take the normal death path. */
	virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

	/** True while the engine has this pawn in MOVE_Swimming. */
	UFUNCTION(BlueprintPure, Category="Swimming")
	bool IsSwimming() const;

	/** Snaps the rig straight out of the swim pose (boarding the boat, respawning, cutscenes). */
	void ResetSwimPose();

	/** The two-hand grip on a ship's wheel (spec C28.3). IDEMPOTENT: calling it with true while the
	 *  pose is already playing does nothing, so the helm can simply re-arm it every frame and no
	 *  caller has to track whether the montage is still alive. */
	void SetHelmGrip(bool bEnabled);

	/** True while that grip pose is playing on the upper-body slot. */
	bool IsHelmGripPlaying() const;

protected:

	/** Blueprint handler to play damage dealt effects */
	UFUNCTION(BlueprintImplementableEvent, Category="Combat")
	void DealtDamage(float Damage, const FVector& ImpactPoint);

	/** Blueprint handler to play damage received effects */
	UFUNCTION(BlueprintImplementableEvent, Category="Combat")
	void ReceivedDamage(float Damage, const FVector& ImpactPoint, const FVector& DamageDirection);

protected:

	/** Initialization */
	virtual void BeginPlay() override;

	/** Cleanup */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Handles input bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** Handles possessed initialization */
	virtual void NotifyControllerChanged() override;

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** Returns the cosmetic skin component (may hold no mesh when unconfigured) **/
	FORCEINLINE class USkeletalMeshComponent* GetCosmeticMesh() const { return CosmeticMesh; }

	/** True only when the retargeter asset is actually SET on the live cosmetic anim instance;
	 *  probes assert this so a failed reflection hookup can never pass as a working skin. */
	bool IsCosmeticRetargeterLive() const;

	/** Lifts the life bar to the crown of the capsule and pins it by its bottom edge. Safe to call again. */
	void UpdateLifeBarAnchor();

	/** True when the life bar anchor clears the capsule crown by MinClearance and hangs from its
	 *  bottom edge. OutClearance reports the measured gap in uu. The plate probe's player check. */
	bool IsLifeBarAboveHead(float MinClearance, float& OutClearance) const;
};
