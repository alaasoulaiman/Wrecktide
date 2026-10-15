// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WrecktideRewardSubsystem.generated.h"

class ACombatEnemy;
class ACombatCharacter;
class APawn;

/**
 * Runtime bridge from combat kills into Wrecktide reward systems.
 */
UCLASS()
class WRECKTIDE_API UWrecktideRewardSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Get is the SINGLE authoritative lookup path for the reward subsystem. */
	static UWrecktideRewardSubsystem* Get(UWorld* World);

	/** NotifyEnemyKilled is the SINGLE authoritative path for kill rewards and enemy respawns. */
	void NotifyEnemyKilled(ACombatEnemy* Enemy, AActor* KillerActor);

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void SpawnShopStall(UWorld* World) const;

	/** North side of the Gallows Reach square, clear of Ada, the board and the gallows post. */
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	FVector GunsmithLocation = FVector(11400.0f, -15300.0f, 40.0f);

	UPROPERTY(EditAnywhere, Category="Gunsmith")
	FRotator GunsmithRotation = FRotator::ZeroRotator;

	/** Spawns the bounty board opposite the shop stall; the quest loop's one interact surface. */
	void SpawnQuestBoard(UWorld* World) const;

	/** Spawns the camp's quest giver beside the board; the story chain's one interact surface. */
	void SpawnMissionGiver(UWorld* World) const;

	/** Spawns the blacksmith out past the market (spec C10): workbench, barrel, tools and a forge
	 *  fire, far enough from the other three triggers that no two menus share a spot. */
	void SpawnSmithStall(UWorld* World) const;

	/** The supply barrel on the pier beside the mooring (spec S2.2). Runtime, like every other prop. */
	void SpawnSupplyBarrel(UWorld* World) const;

	/** The shipwright's bench beside the bounty board (spec S4). Runtime, like every other prop. */
	void SpawnShipwright(UWorld* World) const;

	/** The navy patrol on her loop route (spec S3). Runtime like every other prop, and dropped on the
	 *  route point FARTHEST from the player's mooring so she is a sail on the horizon at first light
	 *  rather than an ambush at the pier. */
	void SpawnPatrolBoat(UWorld* World) const;

	/** 2 Hz feed for ReachSpot mission objectives: the ONE place that tells the mission logic
	 *  where the player is standing. Returns immediately unless such a mission is active. */
	void TickMissionLocation();

	/** Repeating handle behind TickMissionLocation. */
	FTimerHandle MissionLocationTimer;

	/** "Land ho: Gallows Reach", once per save (C34.7). Rides the mission location timer; the flag
	 *  Seen_GallowsReach lives in the existing QuestFlags map, so the save version does not move. */
	void CheckIslandArrival(class AWrecktidePlayerState* PlayerState, const FVector& PlayerLocation) const;

	void ScheduleRespawn(ACombatEnemy* Enemy) const;

	/** Self-test for the kill->reward chain; runs only when launched with -FungateProbe. */
	void RunFungateProbe();

	/** Staged distance, name, debug text, chase, and return gate for enemy plates. */
	void RunPlateProbe();

	/** Strong references to the enemies -PlateProbe spawns, held for the probe's whole life. Nothing
	 *  else in the world owns a probe-spawned actor, and the staged checks read it through weak
	 *  pointers, so this array is what keeps those pointers resolvable. Emptied when the probe ends. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACombatEnemy>> PlateProbeEnemies;

	/** Weapon-attributed kill XP gate; runs only with -XpSkillProbe. */
	void RunXpSkillProbe();

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	void RunMusketProbe();
	void SampleMusketProbe();
	void FinishMusketProbe();
	void ArmMusketCapture();
	void SampleMusketCapture();
	ACombatEnemy* SpawnMusketProbeEnemy(FName Row, const FVector& Location);
	FTimerHandle MusketProbeTimer;
	bool bMusketCaptureHit = false;
	TSubclassOf<ACombatEnemy> MusketProbeEnemyClass;
	struct FMusketProbeState
	{
		TWeakObjectPtr<ACombatEnemy> Enemy;
		TWeakObjectPtr<AActor> Cover;
		TWeakObjectPtr<ACombatCharacter> Player;
		int32 Stage = 0;
		int32 Shots = 0;
		float Deadline = 0.0f;
		float Health = 0.0f;
		float MinDistance = 1000000.0f;
		float HoldDrift = 0.0f;
		float CadetMinDistance = 1000000.0f;
		float CadetPeakSpeed = 0.0f;
		float CaptureReadyTime = 0.0f;
		FVector HoldLocation = FVector::ZeroVector;
		FVector LandPlayerLocation = FVector::ZeroVector;
		FVector LandEnemyLocation = FVector::ZeroVector;
		bool bSawHold = false;
		bool bTell = false;
		bool bNumbers = false;
		bool bFire = false;
		bool bHeld = false;
		bool bLos = true;
		bool bRange = true;
		bool bSwim = true;
		bool bSawSwimInRange = false;
		bool bSawReturning = false;
		bool bLeash = false;
		bool bCadet = false;
		bool bFallback = false;
		bool bMeleeRequested = false;
		bool bBoat = false;
	};
	FMusketProbeState MusketProbe;
	void RunShootProbe();
	void RunGunsmithProbe();
	void ArmPistolCapture();
	void SamplePistolCapture();
	void ArmGunsmithCapture();
	FTimerHandle PistolCaptureTimer;
	int32 PistolCaptureStage = 0;
	void SampleShootProbe();
	void FinishShootProbe();
	FTimerHandle ShootProbeTimer;
	TWeakObjectPtr<ACombatEnemy> ShootProbeEnemy;
	int32 ShootProbeStage = 0;
	int32 ShootProbeShots = 0;
	float ShootProbeNextTime = 0.0f;
	float ShootProbeHealth = 0.0f;
	float ShootProbeDamage = 0.0f;
	int64 ShootProbeMeleeXP = 0;
	int64 ShootProbeShootingXP = 0;
	bool bShootProbeEquip = false;
	bool bShootProbeFire = false;
	bool bShootProbeReload = false;
	bool bShootProbeXP = false;
	bool bShootProbeBar = false;
	bool bShootProbeSawReloadArc = false;
	bool bShootProbeBack = false;
#endif

	/** Self-test for the F-interact shop menu and slot-1 equip cycle; runs only with -ShopProbe. */
	void RunShopProbe();

	/** Self-test for the whole bounty loop (board menu -> accept -> kill count -> paid turn-in ->
	 *  a non-repeatable row staying done); runs only with -QuestProbe. Every assertion reads the
	 *  persisted QuestFlags through WrecktideQuestLogic, and every kill goes through the real
	 *  NotifyEnemyKilled path, so a broken hook cannot pass it. Fully synchronous, one frame. */
	void RunQuestProbe();

	/** Delayed shop-purchase and equip-cycle verdict; always ends through ScheduleProbeQuit. */
	void CheckShopProbePurchase();

	/** Self-test for the click->swing chain; runs only when launched with -AttackProbe. */
	void RunAttackProbe();

	/** Verifies a probe swing is playing and aimed; swing 2 proves no attack lockout. */
	void CheckAttackProbeSwing(int32 SwingIndex);

	/** Self-test for GAS locomotion on the combat pawn; runs only when launched with -LocoProbe. */
	void RunLocoProbe();

	/** Phase 1 verdict: velocity/mode/ABP after 2s of forward input, then kills the pawn. */
	void CheckLocoProbeMovement();

	/** Phase 2 verdict: the respawned pawn must be the SAME class the player died as. */
	void CheckLocoProbeRespawn();

	/** Arms the REAL-input self-test (-InputProbe): a passive sampler that expects an EXTERNAL
	 *  harness (tools\input_probe.ps1) to focus the window and hold W via OS SendInput. Exists
	 *  because 2026-07-27 proved DoMove-based probes can stay green while the keyboard ->
	 *  EnhancedInput -> CharacterInputState chain is completely dead (CBP compile failure). */
	void RunInputProbe();

	/** Verdict for the real-input window: displacement + peak speed from OS-level keys only. */
	void CheckInputProbe();

	/** Self-test for the enemy pirate-skin chain; runs only when launched with -EnemySkinProbe.
	 *  Phase 1: steady-state wiring on every LIVING configured enemy. Phase 2 (review 2026-07-27:
	 *  the ragdoll path is the riskiest surface): damage one enemy (partial ragdoll blend), then
	 *  kill it, re-checking the skin wiring in BOTH states. Pose-FOLLOW quality stays eyeball-
	 *  gated via the visual harness - this machine gate proves the wiring survives physics. */
	void RunEnemySkinProbe();

	/** Phase 2a verdict: skin wiring after a partial-ragdoll hit; then kills the target. */
	void CheckEnemySkinAfterDamage();

	/** Phase 2b verdict + final: skin wiring during the death-ragdoll window. */
	void CheckEnemySkinAfterDeath();

	/** Two swing combo contract. The real click path must play Melee01, then Melee02, end the
	 *  montage after a third buffered click, and start a fresh Melee01 only after that end. */
	void RunComboProbe();

	/** Samples the active section and injects the second and third clicks at their real windows. */
	void SampleComboProbe();

	/** Final two swing verdict and quit. */
	void FinishComboProbe();

	/** Repeating section and montage sampler. */
	FTimerHandle ComboProbeSampleTimer;

	/** Ordered montage sections and the observed terminal end. */
	TArray<FName> ComboProbeSections;

	/** Total samples and state reached through the real input path. */
	int32 ComboProbeSamples = 0;
	bool bComboProbeConfigOK = false;
	bool bComboProbeSecondClickSent = false;
	bool bComboProbeThirdClickSent = false;
	bool bComboProbeSawSecond = false;
	bool bComboProbeSawEnd = false;
	bool bComboProbeFreshFirst = false;
	bool bComboProbeUnexpectedSection = false;

	/** Real trace, forced critical kind, draw registration, and expiry gate. */
	void RunDamageNumbersProbe();
	void CheckDamageNumbersProbeNormal();
	void CheckDamageNumbersProbeCritical();
	void FinishDamageNumbersProbe();
	TWeakObjectPtr<ACombatEnemy> DamageNumbersProbeEnemy;
	float DamageNumbersProbeHealthBefore = 0.0f;
	bool bDamageNumbersProbeNormalOK = false;
	bool bDamageNumbersProbeCriticalOK = false;

	/** Self-test for the 2026-07-27 hit bugs; runs only when launched with -HitProbe.
	 *  Phase 1: hit the PLAYER with a charged-style impulse - the cosmetic skin must stay
	 *  visible, live, and within sane distance of the capsule (the despawn-on-hit bug).
	 *  Phase 2: drive a player combo hit into an enemy - it must stagger, never launch. */
	void RunHitProbe();

	/** Phase 1 verdict + phase 2 arming. */
	void CheckHitProbePlayer();

	/** Phase 2 verdict + quit. */
	void CheckHitProbeEnemy();

	/** The enemy the hit probe strikes in phase 2. */
	TWeakObjectPtr<ACombatEnemy> HitProbeEnemy;

	/** Enemy health before the phase-2 combo hit, to prove the hit actually landed. */
	float HitProbeEnemyHP = 0.0f;

	/** Highest Z the phase-2 enemy reached above its pre-hit base (launch detector). */
	float HitProbeEnemyPeakZRise = 0.0f;

	/** Phase-2 enemy Z baseline. */
	float HitProbeEnemyBaseZ = 0.0f;

	/** Passive sampler for the phase-2 launch detector. */
	FTimerHandle HitProbeSampleTimer;

	/** Phase 1 (player skin survives a hit) verdict. */
	bool bHitProbePlayerOK = false;

	/** The enemy the skin probe damages and kills in phase 2. */
	TWeakObjectPtr<ACombatEnemy> EnemySkinProbeTarget;

	/** Phase 1 (steady-state) verdict, folded into the final skin-probe verdict. */
	bool bEnemySkinSteadyOK = false;

	/** Phase 2a (partial-ragdoll) verdict, folded into the final skin-probe verdict. */
	bool bEnemySkinRagdollOK = false;

	/** Schedules the probe-ending QuitGame; every probe path, success or failure, must reach one. */
	void ScheduleProbeQuit(float DelaySeconds);

	/** Control yaw the attack probe aimed at (into empty space, away from enemies). */
	float AttackProbeAimYaw = 0.0f;

	/** Result of the first probe swing, folded into the final verdict. */
	bool bAttackProbeSwing1OK = false;

	/** Stall under test during -ShopProbe. */
	TWeakObjectPtr<class AWrecktideShopStall> ShopProbeStall;

	/** Gold and equipped row captured immediately before the probe confirms its purchase. */
	int64 ShopProbeGoldBefore = 0;
	FName ShopProbeEquippedBefore = NAME_None;

	/** Shop menu phase verdict, folded into the final shop-probe verdict. */
	bool bShopProbeMenuOK = false;

	/** Every montage slot active on the ABP during probe swings; fail-closed so an INVISIBLE
	 *  swing (montage plays, notifies fire, no slot node renders it) fails the gate. */
	bool bAttackProbeSlotOK = true;

	/** True when the probed pawn is not the GameMode default pawn - a vacuous pass guard. */
	bool bAttackProbeWrongPawn = false;

	/** Repeating timer that feeds forward movement input during the loco probe. */
	FTimerHandle LocoDriveTimer;

	/** Where the loco probe pawn stood before the forward drive, for displacement. */
	FVector LocoStartLocation = FVector::ZeroVector;

	/** Highest speed seen DURING the drive; final-speed sampling lies when the pawn meets a wall. */
	float LocoPeakSpeed = 0.0f;

	/** Class name of the pawn before the probe kills it, for logging. */
	FString LocoOldClassName;

	/** The pawn the loco probe killed; it must be GONE at respawn check or the pass is vacuous. */
	TWeakObjectPtr<APawn> LocoOldPawn;

	/** Exact class of the killed pawn; the respawned pawn must be this same UClass. */
	TWeakObjectPtr<UClass> LocoOldClass;

	/** Phase 1 (movement) verdict, folded into the final loco verdict. */
	bool bLocoMovementOK = false;

	/** Cosmetic-skin chain verdict, reported separately so a skin regression never reads as a
	 *  locomotion bug (and vice versa). True when no skin is configured. */
	bool bLocoCosmeticOK = true;

	/** Repeating passive sampler during the real-input window (never drives the pawn). */
	FTimerHandle InputProbeSampleTimer;

	/** Where the pawn stood when the real-input window armed, for displacement. */
	FVector InputProbeStart = FVector::ZeroVector;

	/** Highest speed seen during the real-input window (final-speed sampling lies at walls). */
	float InputProbePeakSpeed = 0.0f;

	/** Largest displacement seen during the real-input window. */
	float InputProbeMaxDisp = 0.0f;

	/** True once the pawn was seen MOVE_Walking above 50 uu/s during the window. */
	bool bInputProbeSawWalking = false;

	/** Self-test for the 2026-08-21 ownership fixes; runs only with -OwnershipProbe. Buys the first
	 *  melee shop row through the REAL stall path, floods the gold ledger with 300 kill-sized credits
	 *  (the old ledger-scan answer died at 256), then proves the row is still owned, still reachable
	 *  on the key-1 cycle, and that Buy on an owned row equips without charging. */
	void RunOwnershipProbe();

	/** Two-process persistence gate; runs only with -SaveProbe=write or -SaveProbe=read plus an
	 *  explicit -WrecktideSaveSlot=<probe slot> (never the default slot). WRITE dirties state through
	 *  the real paths, flushes with SaveNow and records the expected values beside the .sav. READ, in
	 *  a fresh process, relies on the PlayerState's own BeginPlay load and compares. A milestone is
	 *  not proven until the state survives a relaunch, so one process can never pass this alone. */
	void RunSaveProbe();

	/** Two-process wardrobe gate. WRITE checks every outfit at default, second and last colourways,
	 *  then saves FirstMate/4. READ proves that choice was applied during the fresh pawn's BeginPlay. */
	void RunWardrobeProbe();
	void StartNextWardrobeProbeCase();
	void FinishWardrobeProbeCase();
	void FinishWardrobeProbeWrite();

	TArray<FName> WardrobeProbeOutfitIds;
	TArray<int32> WardrobeProbeVariantCounts;
	int32 WardrobeProbeOutfitIndex = 0;
	int32 WardrobeProbeVariantChoice = 0;
	int32 WardrobeProbeCases = 0;
	FTransform WardrobeProbeAnchor = FTransform::Identity;
	FName WardrobeProbeBone = NAME_None;
	FVector WardrobeProbeBoneStart = FVector::ZeroVector;
	float WardrobeProbeMaxBoneMove = 0.0f;
	bool bWardrobeProbeCaseOK = false;
	bool bWardrobeProbeAllOK = false;
	FTimerHandle WardrobeProbeDriveTimer;
	FTimerHandle WardrobeProbeFinishTimer;

	/** Numeric leg-pose gate (2026-09-04, the crooked-legs play test report): samples knee angle and leg
	 *  extension on the hidden driver mesh vs the visible skin, idle and moving, and fails when the
	 *  skin bends its knees materially more than the driver. Runs only with -PoseProbe. */
	void RunPoseProbe();

	/** One sample of both meshes; buckets by speed (idle vs moving). */
	void SamplePoseProbe();

	/** Verdict from the accumulated samples; always ends through ScheduleProbeQuit. */
	void FinishPoseProbe();

	FTimerHandle PoseProbeSampleTimer;
	FTimerHandle PoseProbeDriveTimer;
	double PoseProbeStartTime = 0.0;
	int32 PoseProbeSamples = 0;
	/** [bucket 0=idle 1=moving][mesh 0=driver 1=skin] running sums of the mean knee angle (deg). */
	double PoseProbeSumKnee[2][2] = {{0.0, 0.0}, {0.0, 0.0}};
	double PoseProbeSumExt[2][2] = {{0.0, 0.0}, {0.0, 0.0}};
	int32 PoseProbeCount[2] = {0, 0};
	double PoseProbeMaxKneeDelta = 0.0;
	/** Moving-bucket sums of |skin - driver| knee lateral deviation (cm) and thigh splay (deg). */
	double PoseProbeSumKneeLatDelta = 0.0;
	double PoseProbeSumThighAbdDelta = 0.0;
	/** Capture aid. -ComboForCapture keeps clicking through the two swing string. */
	FTimerHandle ComboCaptureClickTimer;

	/** Feel gate (2026-09-04): kill feed, pause toggle and death flow, all through the same paths a
	 *  player uses; runs only with -FeelProbe. The pause phase is deliberately synchronous because
	 *  timers do not tick while the game is paused. */
	void RunFeelProbe();

	/** Death phase verdict: the YOU DIED overlay must be up a second after the pawn dies. */
	void CheckFeelProbeDeath();

	/** Respawn poll: a NEW pawn plus a cleared death overlay ends the probe; a timeout fails it. */
	void CheckFeelProbeRespawn();

	/** The pawn the feel probe kills; the respawned one must be a different actor. */
	TWeakObjectPtr<APawn> FeelProbeOldPawn;

	/** Repeating respawn poll during the feel probe's death phase. */
	FTimerHandle FeelProbeRespawnTimer;

	/** World time the respawn poll gives up at. */
	float FeelProbeRespawnDeadline = 0.0f;

	/** Reward-feed line the probe read back, kept for the final verdict log. */
	FString FeelProbeFeedLine;

	/** Phase verdicts, folded into the final feel verdict. */
	bool bFeelProbeFeedOK = false;
	bool bFeelProbePauseOK = false;
	bool bFeelProbeDeathOK = false;

	/** Sailing loop gate (2026-09-04): board -> sail -> ashore through AWrecktideBoat's authoritative
	 *  paths; runs only with -BoatProbe. */
	void RunBoatProbe();
	void CheckBoatProbeSail();
	void CheckBoatProbeAshore();
	TWeakObjectPtr<class AWrecktideBoat> BoatProbeBoat;
	FVector BoatProbeStart = FVector::ZeroVector;
	bool bBoatProbeBoardOK = false;

	/**
	 * -SailFeelProbe (spec S1.7). ONE repeating 0.1 s ticker with a phase index rather than a dozen
	 * nested one-shot timers: every stage is a settle followed by a measurement window, and the
	 * numbers it collects are the ones the spec names.
	 */
	void RunSailFeelProbe();
	void TickSailFeelProbe();
	/** Snap back to the probe origin at every phase change so a long run can never reach the range
	 *  clamp at 22,000 uu and quietly stop the boat mid-measurement. */
	void SailProbeNextPhase();

	TWeakObjectPtr<class AWrecktideBoat> SailProbeBoat;
	FTimerHandle SailProbeTimer;
	int32 SailProbePhase = 0;
	float SailProbePhaseElapsed = 0.0f;
	FVector SailProbeOrigin = FVector::ZeroVector;
	FVector SailProbeMark = FVector::ZeroVector;
	float SailProbeMarkYaw = 0.0f;
	float SailProbeSpeedFull = 0.0f;
	float SailProbeSpeedHalf = 0.0f;
	float SailProbeSpeedAstern = 0.0f;
	float SailProbeSpeedAhead = 0.0f;
	float SailProbeTurnFull = 0.0f;
	float SailProbeTurnHalf = 0.0f;
	float SailProbePivotYaw = 0.0f;
	float SailProbePivotTravel = 0.0f;

	/**
	 * -HelmProbe (spec C28.5). One repeating 0.1 s ticker with a phase index, the -SailFeelProbe
	 * shape: put the wheel hard a port and read it, let it go and time the recentring, put it hard
	 * a starboard, then check the pirate's station and grip and the navy's own wheel, then go
	 * ashore and check the grip is gone.
	 */
	void RunHelmProbe();
	void TickHelmProbe();
	void FinishHelmProbe();
	TWeakObjectPtr<class AWrecktideBoat> HelmProbeBoat;
	TWeakObjectPtr<class AWrecktidePatrolBoat> HelmProbePatrol;
	TWeakObjectPtr<class ACombatCharacter> HelmProbeRider;
	FTimerHandle HelmProbeTimer;
	int32 HelmProbePhase = 0;
	float HelmProbePhaseElapsed = 0.0f;
	float HelmProbeAngleLeft = 0.0f;
	float HelmProbeAngleRight = 0.0f;
	float HelmProbeAngleCentre = 0.0f;
	float HelmProbeCentreSeconds = -1.0f;
	float HelmProbePatrolWheel = 0.0f;
	float HelmProbeRiderGap = 0.0f;
	float HelmProbeRiderYawError = 0.0f;
	bool bHelmProbeGripAboard = false;
	bool bHelmProbeGripAshore = true;
	bool bHelmProbeWentAshore = false;

	/** -HoldProbe (spec S2.4): buy, take damage, patch through the hold-to-use path, save round trip. */
	void RunHoldProbe();
	void FinishHoldProbe();
	TWeakObjectPtr<class AWrecktideBoat> HoldProbeBoat;
	int64 HoldProbeGoldBefore = 0;
	int64 HoldProbeGoldAfterBuy = 0;
	int32 HoldProbeSlotsAfterBuy = 0;
	int32 HoldProbePlanksAfterBuy = 0;
	int32 HoldProbeHullAfterDamage = 0;
	bool bHoldProbeBought = false;

	/** -ShipwrightProbe (spec S4.2): the gate refuses before the level, the Canvas tier raises the
	 *  MEASURED speed, and the tier survives a save round trip. Same phase-ticker shape as the sail
	 *  probe, because the only honest way to read a speed is to measure travel over a window. */
	void RunShipwrightProbe();
	void TickShipwrightProbe();
	TWeakObjectPtr<class AWrecktideBoat> ShipwrightProbeBoat;
	TWeakObjectPtr<class AWrecktideShipwright> ShipwrightProbeBench;
	FTimerHandle ShipwrightProbeTimer;
	int32 ShipwrightProbePhase = 0;
	float ShipwrightProbePhaseElapsed = 0.0f;
	FVector ShipwrightProbeOrigin = FVector::ZeroVector;
	FVector ShipwrightProbeMark = FVector::ZeroVector;
	float ShipwrightProbeSpeedBefore = 0.0f;
	float ShipwrightProbeSpeedAfter = 0.0f;
	int64 ShipwrightProbeGoldAtRefusal = 0;
	int64 ShipwrightProbeGoldAfterRefusal = 0;
	int64 ShipwrightProbeGoldAfterBuy = 0;
	int32 ShipwrightProbeLevelAtRefusal = 0;
	bool bShipwrightProbeRefused = false;
	bool bShipwrightProbeBought = false;

	/**
	 * -PatrolProbe (spec S3.4). One repeating 0.1 s ticker with a phase index, the -SailFeelProbe
	 * shape, but each phase carries a BUDGET and an early exit rather than a fixed window: every
	 * stage here is "does she do X within N seconds", which is the question the spec asks.
	 * Stages: close and go alongside, land a ram, break the leash by distance, sail back to the
	 * route, turn hostile again, then lose him in her blind arc while he pivots furled.
	 */
	void RunPatrolProbe();
	void TickPatrolProbe();
	void PatrolProbeNextPhase();
	void FinishPatrolProbe();
	TWeakObjectPtr<class AWrecktideBoat> PatrolProbeBoat;
	TWeakObjectPtr<class AWrecktidePatrolBoat> PatrolProbePatrol;
	FTimerHandle PatrolProbeTimer;
	int32 PatrolProbePhase = 0;
	float PatrolProbePhaseElapsed = 0.0f;
	float PatrolProbeHostileSeconds = -1.0f;
	float PatrolProbeAlongsideSeconds = -1.0f;
	float PatrolProbeClosestGap = 0.0f;
	int32 PatrolProbeHullBefore = 0;
	int32 PatrolProbeHullAfter = 0;
	float PatrolProbeLeashRouteDistance = 0.0f;
	float PatrolProbeReturnStartDistance = 0.0f;
	float PatrolProbeReturnEndDistance = 0.0f;
	float PatrolProbeReAggroSeconds = -1.0f;
	float PatrolProbeBreakSeconds = -1.0f;
	float PatrolProbePivotYaw = 0.0f;
	float PatrolProbePivotTravel = 0.0f;
	FVector PatrolProbeMark = FVector::ZeroVector;
	float PatrolProbeMarkYaw = 0.0f;
	bool bPatrolProbeLeashed = false;
	/** Patrol glyphs on the map the first tick she is hostile, and again once she has lost him: the
	 *  red ship is a "while hostile" marker, so proving it needs BOTH numbers. */
	int32 PatrolProbeGlyphHostile = -1;
	int32 PatrolProbeGlyphAfterBreak = -1;
	bool bBoatProbeSailOK = false;

	/** Real F-binding boat gate: decorative guidance, pier walk, go ashore, then swim boarding. */
	void RunBoatWalkProbe();
	void BeginBoatWalkProbePier();
	void DriveBoatWalkProbePier();
	void CheckBoatWalkProbeBoarded();
	void CheckBoatWalkProbeAshore();
	void CheckBoatWalkProbeSwimHold();
	void FinishBoatWalkProbe();
	TWeakObjectPtr<class AWrecktideBoat> BoatWalkProbeBoat;
	FTimerHandle BoatWalkProbeDriveTimer;
	float BoatWalkProbeDeadline = 0.0f;
	bool bBoatWalkProbeDecorOK = false;
	bool bBoatWalkProbePierOK = false;
	bool bBoatWalkProbeAshoreOK = false;
	bool bBoatWalkProbeSwimHoldOK = false;
	/** Treasure chest gate (2026-09-04): one open pays the exact gold and persists; a second open
	 *  refuses; runs only with -ChestProbe. */
	void RunChestProbe();

	/** Data-driven early economy gate; runs only with -EconomyProbe. */
	void RunEconomyProbe();

	/** One-player-level curve, award, migration and HUD gate; runs only with -NotorietyProbe. */
	void RunNotorietyProbe();
	void CheckNotorietyProbeBar();
	void FinishNotorietyProbe();
	float NotorietyProbeExpectedFill = 0.0f;
	bool bNotorietyProbeAwardOK = false;
	bool bNotorietyProbeBarOK = false;
	bool bNotorietyProbeLegacyOK = false;
	/** Title menu gate (2026-09-05): the menu owns the first frames (paused, New Game and Quit on the
	 *  panel), New Game restarts the level and the fresh world starts unpaused with no menu; runs only
	 *  with -MenuProbe, two stages across the restart. */
	void RunMenuProbe();

	/** -MenuProbe=slots : manual save slot round trip plus the Load Voyage screen. */
	void RunMenuSlotsProbe(class ACombatPlayerController& CombatController);

	/** -MenuProbe=options : an option edited through the menu model and read back out of the ini file. */
	void RunMenuOptionsProbe(class ACombatPlayerController& CombatController);
	/** Feedback sounds gate (2026-09-05): all twelve named sounds load and play through PlaySfx and a
	 *  real gameplay path (Melee level-up) reaches it; runs only with -SfxProbe. */
	void RunSfxProbe();
	/** Navmesh connectivity gate (2026-09-04): field -> beach spawn -> fort must be one nav island;
	 *  runs only with -NavProbe. */
	void RunNavProbe();
	/** Enemy pursuit gate (2026-09-04): with the island navmesh live, at least one enemy must close
	 *  300 uu on a player parked inside its sight range within 12 s; runs only with -ChaseProbe. */
	void RunChaseProbe();
	void CheckChaseProbe();
	FTimerHandle ChaseProbeSampleTimer;
	TMap<TWeakObjectPtr<ACombatEnemy>, float> ChaseProbeStartDistances;
	float ChaseProbeBestClose = 0.0f;
	float ChaseProbePeakSpeed = 0.0f;
	int32 ChaseProbeSamples = 0;

	/** Four-stage aggro/leash gate: idle at range, acquire/close, return/heal, then re-acquire. */
	void RunLeashProbe();
	void SampleLeashProbe();
	void CheckLeashProbeStage1();
	void CheckLeashProbeStage2();
	void CheckLeashProbeStage3();
	void CheckLeashProbeStage4();
	void BeginLeashProbeStage4();
	TWeakObjectPtr<ACombatEnemy> LeashProbeEnemy;
	FTimerHandle LeashProbeSampleTimer;
	FTimerHandle LeashProbeStageTimer;
	int32 LeashProbeStage = 0;
	float LeashProbeStageStartTime = 0.0f;
	float LeashProbeStage1MaxHomeDistance = 0.0f;
	float LeashProbeStage2MinPlayerDistance = 1000000.0f;
	bool bLeashProbeAllStagesOK = true;
	bool bLeashProbeStage1Idle = true;
	bool bLeashProbeStage2Chasing = false;
	bool bLeashProbeStage3State = false;
	bool bLeashProbeStage3HomeHealthy = false;
	bool bLeashProbeStage3Damaged = false;
	bool bLeashProbeStage4Chasing = false;

	/** Swimming gate (2026-09-06): the sea is a water physics volume, so a pirate dropped in it swims
	 *  at the surface instead of falling forever, can swim ashore onto the beach, and stepping off
	 *  the pier lands him in the water. Three stages; runs only with -SwimProbe. */
	void RunSwimProbe();
	void CheckSwimProbeFloat();
	void CheckSwimProbeShore();
	void RunSwimProbeStagePier();
	void FinishSwimProbe();
	/** Repeating 0.016 s drive (the loco probe's DoMove pattern) and the stage poll. */
	FTimerHandle SwimProbeDriveTimer;
	FTimerHandle SwimProbePollTimer;
	/** Unit move input the drive pushes, in world XY (the control rotation is set to match). */
	FVector2D SwimProbeDrive = FVector2D::ZeroVector;
	/** World time each stage gives up at. */
	float SwimProbeDeadline = 0.0f;
	/** Per stage verdicts and the numbers behind them, folded into the final swim verdict. */
	bool bSwimProbeFloatOK = false;
	bool bSwimProbeShoreOK = false;
	bool bSwimProbePierOK = false;
	float SwimProbeFloatZ = 0.0f;
	float SwimProbeShoreZ = 0.0f;
	float SwimProbePierZ = 0.0f;

	/** Story mission chain gate (2026-09-04): the whole seven mission spine driven through the giver
	 *  menu, the real kill / chest / boat / bounty hooks and the persisted flags; runs only with
	 *  -MissionProbe. Split into phases because the ReachSpot leg has to wait for the 2 Hz location
	 *  timer and the boat leg has to wait for a possession to settle. */
	void RunMissionProbe();

	/** -MapProbe (2026-09-06): the island plate loads, the world-to-map transform puts the pier and
	 *  the fort in their UV boxes and round trips, the icon set holds the four camp landmarks and
	 *  all five chests, the rim clamp angles are exact, and the mission beacon is dark with no
	 *  mission, stands on the M01 stall, goes dark between missions and moves to a living cutthroat
	 *  on M02. Verdict WRECKTIDE-MAP-PROBE PASS|FAIL. */
	void RunMapProbe();

	/** -BeaconProbe (2026-09-07, spec C33): the gold beam marks the PLACE. With a kill target
	 *  displaced off his post the beam stands on the POST; walking onto it puts the beam out and
	 *  walking away again does not bring it back; a target whose post is past BeaconRearmDistance
	 *  lights it again; and a TalkTo row, which had no beam at all before, stands on its NPC with
	 *  the Live resolver agreeing. Verdict WRECKTIDE-BEACON-PROBE PASS|FAIL. */
	void RunBeaconProbe();

	/** -SmithProbe (2026-09-06, spec C10): the blacksmith end to end, synchronously and through the
	 *  real paths. Buys Edge tier 1 from the stall MENU and proves the damage a target actually
	 *  takes from a melee trace rises by the authored step; buys Brutal tier 1, forces the critical
	 *  roll and proves the CRITICAL feed line and the damage carry the Brutal multiplier; proves a
	 *  tier above the notoriety gate is refused with not one coin moved; proves the tiers survive a
	 *  save/load round trip on a slot of the probe's own. Verdict WRECKTIDE-SMITH-PROBE PASS|FAIL. */
	void RunSmithProbe();

	/** -HotbarProbe (2026-09-06, spec C17): the numbered weapon bar. Buys two melee weapons through
	 *  the real economy, proves the assigned row lands in slot 1 AND is what the HUD drew, proves
	 *  key 1's path wields it, proves an unassigned slot key toasts its hint instead of equipping
	 *  anything, and proves the assignment survives a save/load round trip on a slot of the probe's
	 *  own. Verdict WRECKTIDE-HOTBAR-PROBE PASS|FAIL. */
	void RunHotbarProbe();

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	/** Toolbar rectangles, chest keys/clicks, map clicks, Controls rows and the first-voyage flag. */
	void RunChestButtonProbe();
#endif

	/** -CursorProbe (2026-09-06, spec C20): the mouse in every modal. Opens the shop's canvas list,
	 *  injects a mouse position on a known row rect plus a LeftMouseButton through the controller,
	 *  proves the selection followed the mouse and the click confirmed, then proves the cursor is
	 *  gone once every modal is closed. Verdict WRECKTIDE-CURSOR-PROBE PASS|FAIL. */
	void RunCursorProbe();

	/** -InventoryProbe (2026-09-06, spec C18 and C19): the chest screen. Tab opens and closes it,
	 *  the four tabs switch, Assign to slot 1 changes the bar and survives a save/load, and the
	 *  Journal tab lists the active mission with its giver, marked tracked.
	 *  Verdict WRECKTIDE-CHEST-PROBE PASS|FAIL. */
	void RunInventoryProbe();

	TWeakObjectPtr<class AWrecktideSmithStall> SmithProbeStall;
	void CheckMissionProbeReach();
	void RunMissionProbeMidChain();

	/** -QuestFlowProbe (C22.5): drives the whole hand-off, Bell to Jerold to Marta to Cobb, through
	 *  the real giver menus and the real notify hooks, then the repeatable work loop and its
	 *  cooldown. Verdict WRECKTIDE-QUESTFLOW-PROBE PASS|FAIL. */
	void RunQuestFlowProbe();

	/** -Island2Probe (C34.8): Gallows Reach exists, is walkable, is navigable, has its posts, its
	 *  giver, its story hook and its own chart. Two stages; the chart token needs a drawn frame
	 *  after the pawn is teleported onto the island, which is what FinishIsland2Probe waits for. */
	void RunIsland2Probe();
	void FinishIsland2Probe();

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	/** Incremental terrain gate before the island's story and population are authored. */
	void RunIsland3TerrainProbe();
	bool CheckIsland3Terrain(bool& bTiers, bool& bNav, bool& bSea) const;
	void RunIsland3PopulationProbe();
	bool CheckIsland3Population() const;
	void RunIsland3StoryProbe();
	bool CheckIsland3Story(bool& bGiver, bool& bHook, bool& bBoard);
	void RunIsland3Probe();
	void BeginIsland3Chart();
	void FinishIsland3Probe();
	struct FMangroveIslandProbeState
	{
		bool Tiers = false, Nav = false, Sea = false, Posts = false;
		bool Giver = false, Hook = false, Board = false, Radar = false;
		int32 PreviousRadar = 0;
	};
	FMangroveIslandProbeState Island3Result;
	FTimerHandle Island3ProbeTimer;
#endif

	/** Stage-1 results, held for the verdict line FinishIsland2Probe writes. */
	bool Island2TiersOK = false;
	bool Island2NavOK = false;
	bool Island2SeaOK = false;
	bool Island2PostsOK = false;
	bool Island2GiverOK = false;
	bool Island2HookOK = false;
	FTimerHandle Island2ProbeTimer;

	/** Teleports the pawn to that NPC and opens his menu the way F does. False when he refuses. */
	bool OpenGiverForFlow(class AWrecktideMissionGiver* Giver);
	void CheckMissionProbeBoat();
	void FinishMissionProbe();

	/** Spawns and kills one enemy of EnemyRow through the REAL NotifyEnemyKilled path. */
	bool FireMissionProbeKill(FName EnemyRow);

	/** Puts the pawn inside the giver's trigger and opens his menu; false when it would not open. */
	bool OpenMissionProbeGiver();

	TWeakObjectPtr<class AWrecktideMissionGiver> MissionProbeGiver;

	/** A live enemy's class and transform, the only spawn recipe that survives the life-bar check. */
	TWeakObjectPtr<UClass> MissionProbeEnemyClass;
	FTransform MissionProbeSpawnTransform = FTransform::Identity;

	/** Repeating poll for the ReachSpot leg, and the world time it gives up at. */
	FTimerHandle MissionProbeReachTimer;
	float MissionProbeReachDeadline = 0.0f;

	/** Gold and cumulative reward-skill XP sampled the instant before a leg's payout, so a delta
	 *  is the mission reward exactly and nothing else. */
	int64 MissionProbeGoldBefore = 0;
	int64 MissionProbeXPBefore = 0;

	/** Per leg verdicts, folded into the final mission verdict. */
	bool bMissionProbeReachOK = false;
	bool bMissionProbeMidChainOK = false;
	bool bMissionProbeBoardOK = false;
};
