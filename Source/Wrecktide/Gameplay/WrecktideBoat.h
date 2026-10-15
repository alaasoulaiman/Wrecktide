// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "WrecktideBoat.generated.h"

class ACombatCharacter;
class UBoxComponent;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UFloatingPawnMovement;
class UPrimitiveComponent;
class USphereComponent;
class USpringArmComponent;
class UProgressComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Row names and per-tier steps the sea systems share (spec S2 and S4). Here rather than in a .cpp so
 * the barrel, the shipwright, the patrol and the probes all spell them the same way once.
 */
namespace WrecktideSea
{
WRECKTIDE_API extern const FName SupplyPlanks;
WRECKTIDE_API extern const FName SupplyShot;
WRECKTIDE_API extern const FName UpgradeTimbers;
WRECKTIDE_API extern const FName UpgradeCanvas;
WRECKTIDE_API extern const FName UpgradeHold;

/** What ONE shipwright tier buys. These are the FALLBACKS: Content\Data\BoatUpgrades.csv is what
 *  the shipwright prices and gates from, and its Step column overrides these when the row loads. */
constexpr int32 TimbersHullPerTier = 25;
constexpr float TimbersRamResistPerTier = 0.15f;
constexpr float CanvasSpeedPerTier = 0.08f;
constexpr int32 HoldSlotsPerTier = 2;
constexpr int32 MaxUpgradeTier = 3;
}

/** Which sail is set. W steps up, S steps down; S again at Furled backs her down on the oars. */
UENUM(BlueprintType)
enum class EWrecktideSailState : uint8
{
	Furled = 0,
	Half = 1,
	Full = 2
};

/**
 * The world wind (spec S1.4). ONE direction for the whole sea, so the boat, the patrol, the HUD line
 * and the minimap arrow can never disagree, and a pure function of world time rather than a ticking
 * actor, so nothing has to own it and a probe can pin it without waiting out a drift cycle.
 *
 * All the tunables live here, named, and nowhere else.
 */
namespace WrecktideWind
{
/** Yaw the wind BLOWS TOWARD before drift. World +X is north and +Y is east (WrecktideMapView.h's
 *  convention), so 225 degrees blows toward the south west, which is a wind out of the north east. */
constexpr float BaseToYaw = 225.0f;

/** The drift envelope, plus and minus, and the seconds one full swing takes. */
constexpr float DriftDegrees = 15.0f;
constexpr float DriftPeriodSeconds = 240.0f;

/** Speed multipliers: running with the wind, across it, dead into it. */
constexpr float RunningFactor = 1.15f;
constexpr float AcrossFactor = 1.0f;
constexpr float IntoFactor = 0.70f;

/** Rudder authority left when sailing dead into the wind. */
constexpr float IntoRudderFactor = 0.5f;

/** Yaw the wind blows TOWARD right now: drifted, or the pinned value when a probe set one. */
WRECKTIDE_API float ToYaw(const UObject* WorldContext);

/** Speed multiplier for a hull heading HeadingYaw while the wind blows toward WindToYaw. */
WRECKTIDE_API float SpeedFactor(float HeadingYaw, float WindToYaw);

/** Rudder multiplier on the same two angles: sailing into it costs steering, never speed alone. */
WRECKTIDE_API float RudderFactor(float HeadingYaw, float WindToYaw);

/** Compass point the wind comes FROM, for the HUD line ("Wind NE") and the minimap rim. */
WRECKTIDE_API FString Compass(float WindToYaw);

/** Probe hook: pin the wind, or let it drift again. Only the Shipping-guarded probe block calls these. */
WRECKTIDE_API void SetOverride(float ToYawDegrees);
WRECKTIDE_API void ClearOverride();
}

/**
 * The ship's wheel at the helm (spec C28). Shared by the player's boat and the navy patrol so the
 * two wheels can never drift apart, and a namespace rather than a base class because the two hulls
 * are deliberately unrelated types (see AWrecktidePatrolBoat's header for why).
 */
namespace WrecktideHelm
{
/** Degrees the wheel turns at full lock, each way. 150 is a car wheel's lock in a first person
 *  driving view: far enough that the eye reads the input, short of the two full turns a real ship's
 *  wheel takes, which would read as a spinning prop rather than as steering. */
constexpr float MaxWheelTurnDegrees = 150.0f;

/** Degrees a second the spokes chase the input. At 240 a full lock takes 0.63 s, so the wheel
 *  always LAGS the key a little and never snaps, which is the whole feel the spec asks for. */
constexpr float WheelTurnRateDegPerSecond = 240.0f;

/** One frame of the wheel. Steer is the steering input, -1 hard a port to +1 hard a starboard, and
 *  the angle carries the SAME sign, so a wheel at -150 is a wheel hard over to port. A zero steer
 *  targets zero, which is the release-recentres behaviour with no extra state. */
WRECKTIDE_API float StepWheelAngle(float CurrentDegrees, float SteerInput, float MaxTurnDegrees, float TurnRateDegPerSecond, float DeltaSeconds);

/** Stands a post under a wheel (spec C28.1: the pack's three wheel props are BARE DISCS, measured
 *  at 116 to 137 uu across and 12 to 18 thick with the pivot on the hub, so none of them brings a
 *  pedestal). The post is scaled AT RUNTIME to bridge exactly the gap between the deck and the
 *  bottom of the rim, and to PostWidthUU across, so it fits whatever wheel and whatever helm height
 *  somebody later picks instead of a hand tuned number that would quietly stop matching. It only
 *  sets scale and Z: the caller places it in X and Y. False when there is no gap worth filling or
 *  the mesh did not load, and the caller should then hide it rather than draw a slab. */
WRECKTIDE_API bool FitHelmPost(UStaticMeshComponent* Post, float DeckZ, float WheelBottomZ, float PostWidthUU);
}

/**
 * A small sailing boat the player can board and steer around the island (2026-09-04, v0 of "sea").
 *
 * Deliberately simple: no buoyancy physics. The hull is a pawn with floating movement pinned to the
 * water line; the beach tiers and the hidden surf walls are plain blocking geometry, so the shore
 * stops the boat by itself. Boarding parks the pirate on the deck (attached, collision off, movement
 * frozen) and hands the player controller to the boat; going ashore reverses that, but only where a
 * walkable surface exists beside the hull, so the pirate can never be dropped into open water.
 * Board/GoAshore are the SINGLE authoritative paths; the F key, the probe and future NPC boats all
 * go through them.
 */
UCLASS()
class WRECKTIDE_API AWrecktideBoat : public APawn
{
	GENERATED_BODY()

public:
	AWrecktideBoat();

	/** Board is the SINGLE authoritative boarding path. False when occupied or the rider is unusable. */
	bool Board(ACombatCharacter* Rider);

	/** GoAshore is the SINGLE authoritative disembark path. False when no walkable ground is beside the hull. */
	bool GoAshore();

	/** SetSail is the SINGLE authoritative steering input path: throttle -1..1, steer -1..1. Keys and probes use it. */
	void SetSail(float Throttle, float Steer);

	/** Probe entry: holds a SetSail value for Seconds while no key is down (key polling would zero it). */
	void HoldSail(float Throttle, float Steer, float Seconds);

	/** SetSailState is the SINGLE authoritative path for changing sail. Keys, probes and the capture
	 *  switch all come here, so the rig, the toast and the log can never drift from the state. */
	void SetSailState(EWrecktideSailState NewState);

	EWrecktideSailState GetSailState() const { return SailState; }

	/** Signed speed along the hull's forward axis in uu/s; negative while backing down. */
	float GetForwardSpeed() const { return ForwardSpeed; }

	/** Sideways carry in uu/s. Positive is to starboard. */
	float GetLateralSpeed() const { return LateralSpeed; }

	/** The state's max speed BEFORE the wind factor, in uu/s. Furled is 0 by definition. */
	float GetMaxSpeedForState(EWrecktideSailState State) const;

	/** Words for the HUD, the toast and the probe log. */
	static FString SailStateName(EWrecktideSailState State);

	/** True while S is backing her down on the oars with the sails furled. */
	bool IsBackingDown() const { return bReverseCrawl; }

	// ---- The helm (spec C28). The wheel is a prop on a pivot at the stern; the pivot ROLLS about
	// the hull's forward axis, which is where a ship's wheel axle runs, and the mesh's own base
	// rotation only has to stand the disc up in that plane. One float is the whole state.

	/** Where the wheel is pointing in degrees, signed like the steering: negative is hard a port. */
	float GetHelmWheelAngle() const { return HelmWheelAngleDegrees; }

	/** The rim's world position, for the rider placement check and the probe. */
	FVector GetHelmWheelWorldLocation() const;

	/** The same point in the hull's own frame: negative X is aft of centre, which is the stern. */
	FVector GetHelmWheelLocalOffset() const;

	/** The hull collision box's half extents, so a caller can ask "inside the hull" without
	 *  hardcoding a copy of the numbers that built it. */
	FVector GetHullBoxExtent() const;

	/** True when the helm really carries a wheel mesh, not just an empty pivot. */
	bool HasHelmWheel() const;

	/** Degrees of lock this wheel turns to, for a probe that must not re-derive the tunable. */
	float GetHelmMaxWheelTurn() const { return HelmMaxWheelTurn; }

	/** True while the rider is holding the two-hand grip pose on the wheel. */
	bool IsHelmGripPlaying() const;

	// ---- Hull and hold (spec S2). The NUMBERS live in the save through UProgressComponent, never in
	// a copy on this actor, so what the HUD draws and what a save writes are the same value.

	/** Hull the boat is carrying. A save that never held boat state reads the full hull. */
	int32 GetHullHealth() const;

	/** Hull ceiling: the base plus the shipwright's Timbers tiers (spec S4). */
	int32 GetHullMax() const;

	/** SetHullHealth is the SINGLE authoritative write path: clamps, records, and sinks her at zero. */
	void SetHullHealth(int32 NewHull);

	/** Takes damage through the real path. Reason is ledger wording for the log, not player text. */
	void ApplyHullDamage(int32 Amount, const FString& Reason);

	/** Repairs by Amount and returns what was actually put back. */
	int32 RepairHull(int32 Amount);

	/** Units of one supply row carried. */
	int32 GetHoldUnits(FName SupplyRow) const;

	/** Adds units if the hold has room; returns what actually went aboard. */
	int32 AddToHold(FName SupplyRow, int32 Units);

	/** Takes units out of the hold; returns what was actually taken. */
	int32 ConsumeFromHold(FName SupplyRow, int32 Units);

	/** Slots the hold has: the base plus the shipwright's Hold tiers (spec S4). */
	int32 GetHoldSlots() const;

	/** Slots in use. One slot holds HoldUnitsPerSlot units of a row, rounded up per row. */
	int32 GetHoldSlotsUsed() const;

	/** Units of SupplyRow that would still fit. */
	int32 GetHoldRoomFor(FName SupplyRow) const;

	/** Speed multiplier from the shipwright's Canvas tiers (spec S4). 1.0 with none bought. */
	float GetCanvasSpeedMultiplier() const;

	/** Ram damage multiplier from the shipwright's Timbers tiers (spec S4): 1.0 with none bought,
	 *  falling by TimbersRamResistPerTier a tier and never below zero. The navy patrol reads THIS
	 *  rather than the tier count, so the price of a tier and the damage it saves cannot disagree. */
	float GetRamResistMultiplier() const;

	/** A shove from a ram: a swept slide of Distance along WorldDirection with most of her way
	 *  knocked off. Kinematic like everything else on this hull, so a saved position still restores
	 *  exactly (spec S3.2 knockback). */
	void ApplyRamShove(const FVector& WorldDirection, float Distance);

	/** Sinks her: back to the mooring, hold lost, hull full. The SINGLE authoritative sink path. */
	void Sink();

	/** Where she was moored when the level started, for the sink respawn and the patrol's route. */
	FVector GetMooringLocation() const { return MooringLocation; }

	/** How she lay there. A caller putting her back has to restore BOTH or the ashore search looks
	 *  for the pier on the wrong beam. */
	FRotator GetMooringRotation() const { return MooringRotation; }

	/** Starts the hold-to-use repair (spec S2.2). The aboard F key and the probe both come here.
	 *  It only completes while she is aboard, still, damaged and carrying a plank. */
	void BeginRepairHold();

	/** Ends it, whether it completed or not. */
	void EndRepairHold();

	/** True while the repair hold is counting. */
	bool IsRepairHolding() const { return bRepairHolding; }

	/** Hull one plank puts back. Read by the barrel's list so the price and the effect agree. */
	UPROPERTY(EditAnywhere, Category="Boat|Hull", meta=(ClampMin="1"))
	int32 PlankRepairAmount = 25;

	/** Seconds F must be held to patch, and the speed above which she is not "still". */
	UPROPERTY(EditAnywhere, Category="Boat|Hull", meta=(ClampMin="0.1"))
	float RepairHoldSeconds = 1.5f;

	UPROPERTY(EditAnywhere, Category="Boat|Hull", meta=(ClampMin="0.0"))
	float RepairStillSpeed = 25.0f;

	/** True while a rider is aboard. */
	bool IsOccupied() const { return Rider.IsValid(); }

	/** The pirate currently aboard, if any. */
	ACombatCharacter* GetRider() const;

	/** True when Character stands close enough to board (the trigger sphere). */
	bool IsCharacterInRange(const ACombatCharacter* Character) const;

	/** True when a world point is no farther than Range from the hull's 2D bounds. */
	bool IsPointWithinHullRange(const FVector& Point, float Range) const;

	/** 2D centre range used by the pirate's own F binding and the persistent HUD prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boat", meta=(ClampMin="1.0", Units="cm"))
	float BoardRange = 1000.0f;

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** F while standing beside the boat (dockside input component). */
	UFUNCTION()
	void OnBoardKey();

	/** F while aboard (the pawn's own input component). */
	UFUNCTION()
	void OnAshoreKey();

	/** F released while aboard: ends a repair hold that never completed. */
	UFUNCTION()
	void OnAshoreKeyReleased();

	/** Consumes one plank and patches the hull. Called only by the repair hold when it completes. */
	void CompleteRepair();

	/** The player's progress component, or null when nothing is possessed yet. The single place the
	 *  hull and the hold are read from and written to. */
	UProgressComponent* GetBoatProgress() const;

	/** Steps the sail one state, or backs her down when she is already furled. */
	void StepSail(int32 Direction);

	/** The whole kinematic sail model for one frame: speed, rudder, slide, the swept move. */
	void TickSail(float DeltaSeconds);

	/** Bob, wake, splash and the camera widening. Runs whether or not anyone is aboard. */
	void TickPresentation(float DeltaSeconds);

	/** The wheel chasing the steering input, and the rider's grip kept alive. Runs every frame,
	 *  occupied or not, so an abandoned wheel centres itself instead of freezing hard over. */
	void TickHelm(float DeltaSeconds);

	/** Closed-form stylised sea height at a world point. A SUM OF SINES, not Gerstner: height stays a
	 *  function of XY and time, so four hull points can be sampled with no iterative inverse solve
	 *  (research B4). The visible plane is flat, so this drives the hull rig only. */
	float SampleSeaHeight(const FVector& World, float Time) const;

	/** Finds a walkable spot beside the hull for the rider, or false. */
	bool FindAshoreSpot(FVector& OutLocation) const;

	/** True when the rider's capsule can stand at Location without overlapping anything but water. */
	bool CapsuleFitsAt(const FVector& Location) const;

	void ShowToast(const FString& Message) const;
	void RefreshInteractionPrompt() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBoxComponent> Hull;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> HullMesh;

	/** A short mast, sail and flag so the boat reads as a boat from across the bay; visuals only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> MastMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> SailMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> FlagMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> BoardTrigger;

	/** The wheel's axle (spec C28.1). It ROLLS: a ship's wheel spins about the fore and aft axis,
	 *  which is this component's local X because it inherits the hull's frame. Keeping the spin on
	 *  a pivot rather than on the mesh means the prop's own base rotation can be whatever the
	 *  artist shipped and the spin axis still cannot be got wrong. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> HelmPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> HelmWheelMesh;

	/** The post the wheel stands on. Attached to the HULL, not the pivot: a pedestal that span with
	 *  the wheel would be a drill, not a helm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> HelmPostMesh;

	/** The pack's three wheel props, all found in the constructor because ConstructorHelpers only
	 *  resolves there. BeginPlay logs each one's bounds and stands HelmWheelChoice at the helm, so
	 *  the pick is made from measurements and a capture rather than from an asset name. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> HelmWheelChoices;

	/** Stern foam (spec S1.6, re-cut 2026-09-06). The Synty NS_FX_Wake system drew metre wide white
	 *  cards around the whole hull at chase-camera distance (_logs\sea_wake3_view2.png) and exposes
	 *  NO user parameter to shrink them, so the wake is ours now: a pool of small soft sprites
	 *  DROPPED IN THE WATER at the stern and left there to fade, which is also why they carry
	 *  absolute transforms (the boat sails away from her own wake instead of towing it). Speed gated
	 *  on WakeSpeedThreshold exactly as the emitters were. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TArray<TObjectPtr<UStaticMeshComponent>> FoamSprites;

	/** One dynamic instance per sprite; the only thing that changes is OpacityScale. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FoamMaterials;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UFloatingPawnMovement> Movement;

	/** Max speed with the sail at Half, in uu/s. Half is the control state (research B4). */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.0"))
	float HalfSailSpeed = 420.0f;

	/** Max speed with the sail at Full, in uu/s. Also the reference speed both ramps are measured on. */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="1.0"))
	float FullSailSpeed = 780.0f;

	/** S at Furled backs her down at this speed, in uu/s. */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.0"))
	float ReverseCrawlSpeed = 150.0f;

	/** Asymmetric ramps: seconds to cross the FULL sail speed gaining way, and losing it. Both are
	 *  measured on the same reference speed, so a coast down from Half is not slower than one from Full. */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.05"))
	float SailRampUpSeconds = 1.5f;

	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.05"))
	float SailCoastDownSeconds = 4.0f;

	/** Degrees of yaw per second at full rudder and a standstill. */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="1.0"))
	float MaxTurnRate = 70.0f;

	/** How much of the rudder the full sail speed steals: rate = MaxTurnRate * (1 - this * speedFraction). */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.0", ClampMax="0.95"))
	float TurnSpeedPenalty = 0.6f;

	/** Rudder multiplier with the sails furled: drop sails to pivot (research B4). */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="1.0"))
	float FurledPivotBoost = 1.6f;

	/** Sideways carry at full rudder as a fraction of forward speed, so the hull carves rather than
	 *  rails, and the seconds it takes to bleed off. Zero here is the "on rails" feel. */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.0"))
	float LateralSlideFraction = 0.15f;

	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.05"))
	float LateralSlideDecaySeconds = 1.2f;

	/** Way the hull keeps after grinding into the shore, as a fraction. */
	UPROPERTY(EditAnywhere, Category="Boat|Sail", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ShoreImpactKeep = 0.35f;

	/** Bob: peak height of the four-point sine sample in uu, its period in seconds, how much taller it
	 *  gets at the full sail speed, and how much the fitted tilt is exaggerated so it reads on camera. */
	UPROPERTY(EditAnywhere, Category="Boat|Bob", meta=(ClampMin="0.0", Units="cm"))
	float BobAmplitude = 6.0f;

	UPROPERTY(EditAnywhere, Category="Boat|Bob", meta=(ClampMin="0.1"))
	float BobPeriodSeconds = 3.2f;

	UPROPERTY(EditAnywhere, Category="Boat|Bob", meta=(ClampMin="0.0"))
	float BobSpeedScale = 0.30f;

	UPROPERTY(EditAnywhere, Category="Boat|Bob", meta=(ClampMin="0.0"))
	float BobTiltGain = 2.0f;

	/** Degrees of camera field of view added at the full sail speed. */
	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.0"))
	float FullSailFovBoost = 6.0f;

	/** Forward speed the wake switches on at, in uu/s. */
	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.0"))
	float WakeSpeedThreshold = 90.0f;

	/** Foam sprite width in uu at birth. Sized for the chase camera, which is the only place anyone
	 *  ever sees it: the sprites lie FLAT on the water and that camera looks along the sea at about
	 *  18 degrees, so a quad reads at roughly a third of its width. 55 uu is a patch a third the
	 *  hull's beam; the Synty cards it replaces were metre-wide and hard edged. */
	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="1.0", Units="cm"))
	float FoamSpriteSize = 55.0f;

	/** Where the trail is laid, as a fraction of the hull half length aft of centre, and how far off
	 *  the centre line. Both matter for the same reason: the chase camera's lower edge crosses the
	 *  water about 389 uu astern of centre and the transom is at 420, so foam laid BEHIND her is
	 *  never in shot. It goes on the quarters instead, just outside the 145 uu planking, and streams
	 *  aft out of frame from there, which is what a wake does anyway. */
	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.0", ClampMax="1.2"))
	float FoamSternFraction = 0.6f;

	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.0", Units="cm"))
	float FoamSternSpread = 190.0f;

	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.0", Units="cm"))
	float FoamSurfaceLift = 8.0f;

	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.0"))
	float FoamGrowth = 1.0f;

	/** Peak opacity of one sprite, the seconds it takes to fade to nothing, and the interval the
	 *  stern lays them at. The pool is 16, so life must stay under 16 x the drop interval or the
	 *  oldest sprite is recycled while it is still visible and the trail pops. */
	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FoamAlpha = 0.35f;

	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.1", Units="s"))
	float FoamLifeSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category="Boat|Feel", meta=(ClampMin="0.02", Units="s"))
	float FoamDropSeconds = 0.07f;

	/** Unlit translucent material and its soft radial mask, both loaded BY STRING at BeginPlay. Both
	 *  live in /Game/UI/Map, which Config\DefaultGame.ini already cooks as a folder, so the foam
	 *  needs no new staging rule (the mission beacon proved the same path). */
	UPROPERTY(EditAnywhere, Category="Boat|Feel")
	FString FoamMaterialPath = TEXT("/Game/UI/Map/M_WrecktideBeacon.M_WrecktideBeacon");

	UPROPERTY(EditAnywhere, Category="Boat|Feel")
	FString FoamMaskPath = TEXT("/Game/UI/Map/T_MapGlow.T_MapGlow");

	/** Hull points a fresh boat carries before any shipwright work. */
	UPROPERTY(EditAnywhere, Category="Boat|Hull", meta=(ClampMin="1"))
	int32 HullBaseMax = 100;

	/** Slots the hold starts with, and how many units of one supply share a slot. */
	UPROPERTY(EditAnywhere, Category="Boat|Hull", meta=(ClampMin="1"))
	int32 HoldBaseSlots = 4;

	UPROPERTY(EditAnywhere, Category="Boat|Hull", meta=(ClampMin="1"))
	int32 HoldUnitsPerSlot = 5;

	/** Water line the hull is pinned to (the island's WaterBodyOcean sits at -120). */
	UPROPERTY(EditAnywhere, Category="Boat")
	float WaterZ = -120.0f;



	/** Radius from the world origin the boat may sail to (uu). The water zone half width is 25600; the far islands end at 19764. */

	UPROPERTY(EditAnywhere, Category="Boat")

	float BoatRangeLimit = 22000.0f;

	/** Where the rider stands, relative to the hull. Moved aft to the helm (spec C28.3): he stands
	 *  just BEHIND the wheel on the centre line, facing forward, so the boat is steered by somebody
	 *  rather than sailed by nobody. The Z is unchanged from the amidships spot, which is what puts
	 *  his feet on the deck. */
	UPROPERTY(EditAnywhere, Category="Boat")
	FVector RiderOffset = FVector(-300.0f, 0.0f, 75.0f);

	/** Where the wheel stands, relative to the hull: aft of centre on the centre line, at a height
	 *  where a standing pirate's hands meet the rim. The hull box is 420 uu of half length, so
	 *  -235 is well inside her and 65 uu ahead of the rider's chest. */
	UPROPERTY(EditAnywhere, Category="Boat|Helm")
	FVector HelmOffset = FVector(-235.0f, 0.0f, 95.0f);

	/** Stands the Synty wheel prop up in the pivot's YZ plane and faces it forward. It is a tunable
	 *  and not a constant because the prop's own axes are the artist's, not ours. */
	UPROPERTY(EditAnywhere, Category="Boat|Helm")
	FRotator HelmWheelBaseRotation = FRotator(0.0f, 90.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(ClampMin="0.05"))
	float HelmWheelScale = 1.0f;

	/** Degrees of lock, and the rate the spokes chase it. Defaults are WrecktideHelm's shared
	 *  numbers, so the player's wheel and the navy's start from the same feel. */
	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(ClampMin="1.0"))
	float HelmMaxWheelTurn = WrecktideHelm::MaxWheelTurnDegrees;

	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(ClampMin="1.0"))
	float HelmWheelTurnRate = WrecktideHelm::WheelTurnRateDegPerSecond;

	/** Which of the pack's three wheel props stands at the helm. */
	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(ClampMin="0", ClampMax="2"))
	int32 HelmWheelChoice = 0;

	/** The deck the post stands on, relative to the hull. The rider's capsule is 90 uu of half
	 *  height and parks at RiderOffset.Z, so the planks under his boots are exactly there. */
	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(Units="cm"))
	float HelmPostBaseZ = -15.0f;

	/** How thick the post is made, across. A helm post is a stout thing, not a broom handle. */
	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(ClampMin="1.0", Units="cm"))
	float HelmPostWidthUU = 26.0f;

	/** The chase camera while aboard (spec C28.4). This boom only ever frames the boat, so these ARE
	 *  the aboard values. They are solved, not guessed: the spring arm puts the camera at
	 *  (-0.94 x Length + 0.342 x Height, 0.342 x Length + 0.94 x Height) for a -20 degree pitch, and
	 *  1150 / 105 lands it 745 uu astern of the pirate and 327 uu over his head, which is a GTA chase
	 *  view of a man at a wheel. The first cut (1250 / 430) put the camera 832 uu up and photographed
	 *  the deck from above (_logs\wheel_eyes_view2.png, before). */
	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(ClampMin="100.0", Units="cm"))
	float HelmBoomLength = 1150.0f;

	UPROPERTY(EditAnywhere, Category="Boat|Helm", meta=(Units="cm"))
	float HelmBoomHeight = 105.0f;

	UPROPERTY(EditAnywhere, Category="Boat|Helm")
	float HelmBoomPitch = -20.0f;

	TWeakObjectPtr<ACombatCharacter> Rider;
	TWeakObjectPtr<ACombatCharacter> DocksideCharacter;
	float CurrentThrottle = 0.0f;
	float CurrentSteer = 0.0f;
	float BobTime = 0.0f;
	/** Live sail model state. ForwardSpeed is signed; LateralSpeed is positive to starboard. */
	EWrecktideSailState SailState = EWrecktideSailState::Furled;
	bool bReverseCrawl = false;
	float ForwardSpeed = 0.0f;
	float LateralSpeed = 0.0f;
	/** Edge detector for the W/S taps: -1, 0 or +1 as of the last SetSail call. */
	int32 LastThrottleRequest = 0;
	/** The camera's authored field of view, captured once so the Full-sail widening can be undone. */
	float BaseCameraFov = 90.0f;
	/** Where the wheel is pointing, signed like the steering. The single piece of helm state. */
	float HelmWheelAngleDegrees = 0.0f;
	/** Where she was moored at BeginPlay. The sink respawn and the patrol route both read it. */
	FVector MooringLocation = FVector::ZeroVector;
	FRotator MooringRotation = FRotator::ZeroRotator;
	/** Hold-to-use repair state. */
	bool bRepairHolding = false;
	float RepairHeldSeconds = 0.0f;
	float PromptRefreshElapsed = 0.0f;
	/** World time until which a HoldSail value survives key polling. */
	double SailHoldUntil = 0.0;
	/** Foam trail bookkeeping: seconds since each sprite was dropped, which one is next out of the
	 *  pool, and how long since the last drop. */
	TArray<float> FoamAgeSeconds;
	int32 FoamNextIndex = 0;
	float FoamDropElapsed = 0.0f;
	/** False when the material or its mask did not load: the trail then stays hidden rather than
	 *  laying untextured grey slabs across the bay. */
	bool bFoamReady = false;
};
