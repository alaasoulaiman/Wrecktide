// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
// For WrecktideHelm: her wheel runs the SAME model the player's does, and the defaults below are
// that namespace's numbers rather than a second copy of them (spec C28.2).
#include "Gameplay/WrecktideBoat.h"
#include "WrecktidePatrolBoat.generated.h"

class ACombatCharacter;
class AWrecktideBoat;
class UAnimInstance;
class UAnimSequence;
class UBoxComponent;
class UFloatingPawnMovement;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** What the patrol is doing right now. The land guards' Idle / Chasing / Returning at sea (spec S3.2). */
UENUM(BlueprintType)
enum class EPatrolBoatState : uint8
{
	/** Sailing the loop route, watching for a pirate. */
	Patrolling = 0,
	/** Closing on the player, matching his speed alongside and lunging at him. */
	Hostile = 1,
	/** Given up, heading back to the nearest point of the route. */
	Returning = 2
};

/**
 * The navy patrol (spec S3): one hostile boat that sails a loop around the island, turns on the
 * player inside AggroRadius, runs alongside him, rams him every RamIntervalSeconds, and NEVER boards.
 *
 * It is a plain APawn and deliberately NOT an AWrecktideBoat subclass. Half the game finds the
 * player's boat with TActorIterator<AWrecktideBoat> (the supply barrel's pier, -BoatProbe, the map's
 * "Your boat" marker, the mission target for M06); a subclass would be picked up by every one of
 * those iterators and the first patrol to spawn would silently become "your boat". It therefore
 * MIRRORS the player's kinematic sail model rather than inheriting it: same swept move through a
 * UFloatingPawnMovement whose tick is off, same water-line pin, same one-shot toast path.
 *
 * The designed weakness (research B5: a patrol carries one findable flaw) is MaxTurnRate. She turns
 * at PatrolTurnRate degrees a second where the player's boat pivots at 70 x 1.6 with her sails
 * furled, so dropping sail and spinning behind the navy's stern takes you out of her arc faster than
 * she can follow; hold that blind arc for BlindArcBreakSeconds and she loses you.
 */
UCLASS()
class WRECKTIDE_API AWrecktidePatrolBoat : public APawn
{
	GENERATED_BODY()

public:
	AWrecktidePatrolBoat();

	virtual void Tick(float DeltaSeconds) override;

	/** Builds the loop route: the island's beach square pushed out by RouteOffsetFromBeach with
	 *  rounded corners, so EVERY point of it is exactly that far off the beach. Static so the spawn
	 *  code can pick a start point without an actor existing yet. */
	static void BuildRoute(TArray<FVector>& OutRoute, float BeachHalfExtent, float OffsetFromBeach, int32 CornerSteps, float RouteZ);

	/** The route this patrol is sailing, in world space. Empty before BeginPlay. */
	const TArray<FVector>& GetRoute() const { return Route; }

	/** Shortest 2D distance from a world point to the route polygon (segments, not just corners). */
	float DistanceToRoute(const FVector& World) const;

	EPatrolBoatState GetPatrolState() const { return PatrolState; }

	/** True while she is hunting the player. The minimap glyph and the probe read this and nothing else. */
	bool IsHostile() const { return PatrolState == EPatrolBoatState::Hostile; }

	/** Signed speed along her forward axis, uu/s. */
	float GetForwardSpeed() const { return ForwardSpeed; }

	// ---- Her helm (spec C28). The same wheel model the player's boat runs, through the shared
	// WrecktideHelm::StepWheelAngle, driven by how hard HER steering is over rather than by a key.

	/** Where her wheel is pointing in degrees, signed like her steering: negative is hard a port. */
	float GetHelmWheelAngle() const { return HelmWheelAngleDegrees; }

	/** How hard her wheel is over this frame, -1 to +1: the yaw step she actually took over the
	 *  largest step her turn rate allows, so full lock means turning as hard as she can. */
	float GetHelmSteerInput() const { return HelmSteerInput; }

	FVector GetHelmWheelWorldLocation() const;

	/** The same point in her own frame: negative X is aft of centre, which is the stern. */
	FVector GetHelmWheelLocalOffset() const;

	/** Her hull collision box's half extents, so a caller can ask "inside the hull" without keeping
	 *  a second copy of the numbers that built it. */
	FVector GetHullBoxExtent() const;

	/** True when her helm really carries a wheel mesh, not just an empty pivot. */
	bool HasHelmWheel() const;

	float GetHelmMaxWheelTurn() const { return HelmMaxWheelTurn; }

	/** True once the navy helmsman is standing at her wheel in the grip pose. */
	bool IsHelmsmanPosed() const { return bHelmsmanPosed; }

	/** Seconds since her last landed ram, or a large number when she has never touched him. */
	float GetSecondsSinceContact() const;

	/** Degrees the current target lies off her bow, 0 dead ahead and 180 dead astern. -1 with no target. */
	float GetTargetBearing() const;

	/** Whoever she is hunting: the player's boat while he is aboard, else the swimming pirate. */
	AActor* GetTarget() const;

	/** Probe hooks. Forcing a lunge keeps -PatrolProbe off a six second wall clock, and the two
	 *  counters let it assert the ram landed through the real ApplyHullDamage path. Calling it while
	 *  a lunge is already running does NOTHING, deliberately: a caller polling it every tick used to
	 *  restart the lunge every tick, and a lunge that never gets past its first frame never touches
	 *  him (the 2026-09-07 first run: contacts=0). */
	void ForceRamLunge();
	int32 GetRamContactCount() const { return RamContactCount; }
	int32 GetRamDamageDealt() const { return RamDamageDealt; }

	// ---- Tunables (spec S3). Numbers live here with their reasons, never inline in the logic.

	/** Half extent of the island's beach ring in uu (RECON G: the beach spans -7000..+7000 on X and Y). */
	UPROPERTY(EditAnywhere, Category="Patrol|Route", meta=(ClampMin="1000.0", Units="cm"))
	float RouteBeachHalfExtent = 7000.0f;

	/** How far off that beach the loop sits. Spec S3.1 asks for 3,500 to 6,000 uu; because the route
	 *  is the beach square OFFSET (rounded corners of this radius) rather than a circle, this one
	 *  number IS the distance to the sand at every point of the loop, corners included. */
	UPROPERTY(EditAnywhere, Category="Patrol|Route", meta=(ClampMin="500.0", Units="cm"))
	float RouteOffsetFromBeach = 4500.0f;

	/** Points per rounded corner. Three keeps each turn inside her turn rate at cruise. */
	UPROPERTY(EditAnywhere, Category="Patrol|Route", meta=(ClampMin="1"))
	int32 RouteCornerSteps = 3;

	/** How near a waypoint counts as reached. */
	UPROPERTY(EditAnywhere, Category="Patrol|Route", meta=(ClampMin="100.0", Units="cm"))
	float WaypointArrivalRadius = 700.0f;

	/** Cruise speed on the route, uu/s. Slow enough that the player can choose to meet her. */
	UPROPERTY(EditAnywhere, Category="Patrol|Sail", meta=(ClampMin="0.0"))
	float CruiseSpeed = 300.0f;

	/** Her top speed while hunting, uu/s. Above the player's Full sail (780) so she can close, below
	 *  a fully canvassed hull (780 x 1.24) so the shipwright's Canvas tiers really do outrun her. */
	UPROPERTY(EditAnywhere, Category="Patrol|Sail", meta=(ClampMin="0.0"))
	float MaxChaseSpeed = 900.0f;

	/** Slowest she will run while hostile. ZERO on purpose: "matches the player's speed" has to
	 *  include matching a boat that is hove to, and a kinematic hull can sit still where a real one
	 *  would fore reach. She still turns at PatrolTurnRate with no way on. */
	UPROPERTY(EditAnywhere, Category="Patrol|Sail", meta=(ClampMin="0.0"))
	float MinChaseSpeed = 0.0f;

	/** uu/s^2 onto and off a speed. One rate: she is a kinematic prop, not the player's boat. */
	UPROPERTY(EditAnywhere, Category="Patrol|Sail", meta=(ClampMin="1.0"))
	float SpeedRamp = 520.0f;

	/** THE DESIGNED WEAKNESS (spec S3.2). Degrees of yaw a second, flat: she cannot turn sharply, so
	 *  a furled pivot behind her breaks the chase. The player's boat pivots at 112 deg/s furled. */
	UPROPERTY(EditAnywhere, Category="Patrol|Sail", meta=(ClampMin="1.0"))
	float PatrolTurnRate = 22.0f;

	/** Way she keeps after grinding into the shore, as a fraction (the player's boat uses the same idea). */
	UPROPERTY(EditAnywhere, Category="Patrol|Sail", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ShoreImpactKeep = 0.35f;

	/** Water line she is pinned to, and the radius from the world origin she may sail to. */
	UPROPERTY(EditAnywhere, Category="Patrol|Sail")
	float WaterZ = -120.0f;

	UPROPERTY(EditAnywhere, Category="Patrol|Sail", meta=(ClampMin="1000.0", Units="cm"))
	float RangeLimit = 22000.0f;

	/** She turns hostile when the player's boat, or the swimming pirate, is this close. */
	UPROPERTY(EditAnywhere, Category="Patrol|Aggro", meta=(ClampMin="0.0", Units="cm"))
	float AggroRadius = 2500.0f;

	/** Station keeping: how far off his beam she tries to sit. */
	UPROPERTY(EditAnywhere, Category="Patrol|Aggro", meta=(ClampMin="50.0", Units="cm"))
	float AlongsideDistance = 300.0f;

	/** Beyond this she just closes at full chase speed; inside it she matches his speed. */
	UPROPERTY(EditAnywhere, Category="Patrol|Aggro", meta=(ClampMin="100.0", Units="cm"))
	float CloseInDistance = 800.0f;

	/** Seconds between lunges, how long one lasts, and the closing speed she adds during it. */
	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="0.5", Units="s"))
	float RamIntervalSeconds = 6.0f;

	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="0.1", Units="s"))
	float RamLungeSeconds = 1.6f;

	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="0.0"))
	float RamClosingSpeed = 380.0f;

	/** Centre to centre 2D distance that counts as contact, alongside the real signal: a blocking hit
	 *  against him during the lunge. Read it with AlongsideDistance. Both hulls are 840 x 290 boxes,
	 *  so however hard she asks for a 300 uu station the collision solver holds her at roughly 560
	 *  (parallel and beam to beam) to 890 (corner to corner): the measured station gap is 564. She is
	 *  therefore ALWAYS inside this gate while alongside, and what makes a ram a ram is the lunge beat
	 *  and the run at him. The number only refuses a ram declared after he has pulled away. */
	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="50.0", Units="cm"))
	float RamContactDistance = 700.0f;

	/** Hull points one ram takes, before the shipwright's Timbers resistance. */
	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="1"))
	int32 RamHullDamage = 20;

	/** How far the hit shoves him, and how far a rammed swimmer is thrown. */
	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="0.0", Units="cm"))
	float RamKnockbackDistance = 260.0f;

	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="0.0"))
	float RamSwimmerLaunchSpeed = 900.0f;

	/** Seconds a landed ram cannot be followed by another, whatever the interval says. */
	UPROPERTY(EditAnywhere, Category="Patrol|Ram", meta=(ClampMin="0.0", Units="s"))
	float RamRecoverySeconds = 2.0f;

	/** Leash (spec S3.2): she gives up this far from her own route, or after this long without a
	 *  landed ram, and sails back to the loop. */
	UPROPERTY(EditAnywhere, Category="Patrol|Leash", meta=(ClampMin="100.0", Units="cm"))
	float LeashRouteRadius = 6000.0f;

	UPROPERTY(EditAnywhere, Category="Patrol|Leash", meta=(ClampMin="1.0", Units="s"))
	float LeashNoContactSeconds = 25.0f;

	/** The blind arc: a target more than this many degrees off her bow is behind her. */
	UPROPERTY(EditAnywhere, Category="Patrol|Leash", meta=(ClampMin="90.0", ClampMax="180.0"))
	float BlindArcDegrees = 120.0f;

	/** Hold that arc for this long and she loses him. */
	UPROPERTY(EditAnywhere, Category="Patrol|Leash", meta=(ClampMin="0.1", Units="s"))
	float BlindArcBreakSeconds = 3.0f;

	/** Seconds after any give-up before she may turn hostile again, so a break is a real escape. */
	UPROPERTY(EditAnywhere, Category="Patrol|Leash", meta=(ClampMin="0.0", Units="s"))
	float AggroCooldownSeconds = 6.0f;

	/** How near the route counts as home. */
	UPROPERTY(EditAnywhere, Category="Patrol|Leash", meta=(ClampMin="100.0", Units="cm"))
	float RouteArrivalRadius = 500.0f;

	// ---- Her helm (spec C28.1 to C28.3). Same numbers as the player's boat by default, because
	// the two wheels are the same prop on the same model; her helmsman is a prop, not a pawn.

	/** Where her wheel stands, relative to the hull: aft of centre on the centre line, at a height
	 *  where a standing man's hands meet the rim. Her hull box is the player's, 420 uu of half
	 *  length, so the same -235 puts it well inside her. */
	UPROPERTY(EditAnywhere, Category="Patrol|Helm")
	FVector HelmOffset = FVector(-235.0f, 0.0f, 95.0f);

	/** Where the navy helmsman stands: just behind the wheel, facing forward. */
	UPROPERTY(EditAnywhere, Category="Patrol|Helm")
	FVector HelmsmanOffset = FVector(-300.0f, 0.0f, -15.0f);

	/** Stands the Synty wheel prop up in the pivot's YZ plane and faces it forward. */
	UPROPERTY(EditAnywhere, Category="Patrol|Helm")
	FRotator HelmWheelBaseRotation = FRotator(0.0f, 90.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="Patrol|Helm", meta=(ClampMin="0.05"))
	float HelmWheelScale = 1.0f;

	UPROPERTY(EditAnywhere, Category="Patrol|Helm", meta=(ClampMin="1.0"))
	float HelmMaxWheelTurn = WrecktideHelm::MaxWheelTurnDegrees;

	UPROPERTY(EditAnywhere, Category="Patrol|Helm", meta=(ClampMin="1.0"))
	float HelmWheelTurnRate = WrecktideHelm::WheelTurnRateDegPerSecond;

	/** Which of the pack's three wheel props stands at her helm. */
	UPROPERTY(EditAnywhere, Category="Patrol|Helm", meta=(ClampMin="0", ClampMax="2"))
	int32 HelmWheelChoice = 0;

	/** Her deck, and how thick the post under her wheel is made. Same numbers as the player's boat
	 *  because it is the same hull with the navy's paint on it. */
	UPROPERTY(EditAnywhere, Category="Patrol|Helm", meta=(Units="cm"))
	float HelmPostBaseZ = -15.0f;

	UPROPERTY(EditAnywhere, Category="Patrol|Helm", meta=(ClampMin="1.0", Units="cm"))
	float HelmPostWidthUU = 26.0f;

protected:
	virtual void BeginPlay() override;

private:
	/** SetPatrolState is the SINGLE authoritative state change: it logs, toasts and resets the timers. */
	void SetPatrolState(EPatrolBoatState NewState, const TCHAR* Reason);

	/** Aggro test, run only from Patrolling and Returning. */
	void TickAggroSearch();

	/** Steering and speed for one frame, given a heading to hold and a speed to make. */
	void DriveTowards(float DesiredYaw, float DesiredSpeed, float DeltaSeconds);

	/** The swept move plus the water-line pin and the range clamp. */
	void MoveHull(float DeltaSeconds);

	/** Hostile behaviour: station keeping, the lunge clock and the contact test. */
	void TickHostile(float DeltaSeconds);

	/** Contact: hull damage through AWrecktideBoat::ApplyHullDamage, a shove and the toast. */
	void LandRam(AActor* TargetActor);

	/** Index of the route point she should be steering for next. */
	void AdvanceWaypoint();

	/** Her wheel chasing her own steering, through the shared WrecktideHelm model. */
	void TickHelm(float DeltaSeconds);

	/** Stands the navy helmsman at the wheel: a hidden mannequin holding the grip pose with the
	 *  Synty soldier skin retargeted onto it. The mission giver's proven pattern, and it degrades
	 *  the same way (to the visible mannequin, never to a T-posed skin). */
	void ApplyHelmsmanSkin();

	/** Pushes the retargeter into the skin ABP's exposed variable by reflection. */
	bool PushHelmsmanRetargeter();

	UFUNCTION()
	void OnHelmsmanAnimInitialized();

	void ShowToast(const FString& Message) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBoxComponent> Hull;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> HullMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> MastMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> SailMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> FlagMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UFloatingPawnMovement> Movement;

	/** Her helm: the pivot rolls about the hull's forward axis (the axle), the mesh carries the
	 *  prop's own base rotation, exactly as on the player's boat. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> HelmPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> HelmWheelMesh;

	/** The post her wheel stands on, on the HULL rather than the pivot so it does not spin with it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> HelmPostMesh;

	/** The hidden mannequin that holds the pose, and the navy skin retargeted from it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> HelmsmanDriver;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> HelmsmanSkin;

	/** The pack's three wheel props, found in the constructor. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> HelmWheelChoices;

	/** The grip pose she holds, and the neutral stand idle she falls back to when the grip pose
	 *  will not set on the driver's skeleton. */
	UPROPERTY(EditAnywhere, Category="Patrol|Helm")
	TObjectPtr<UAnimSequence> HelmsmanGripPose;

	UPROPERTY(EditAnywhere, Category="Patrol|Helm")
	TObjectPtr<UAnimSequence> HelmsmanIdlePose;

	/** ABP_GenericRetarget and RTG_UEFN_to_UE4_Mannequin, the mission giver's pair. */
	UPROPERTY(EditAnywhere, Category="Patrol|Helm")
	TSubclassOf<UAnimInstance> HelmsmanRetargetAnimClass;

	UPROPERTY(EditAnywhere, Category="Patrol|Helm")
	TObjectPtr<UObject> HelmsmanRetargeter;

	TArray<FVector> Route;
	int32 WaypointIndex = 0;
	EPatrolBoatState PatrolState = EPatrolBoatState::Patrolling;
	TWeakObjectPtr<AWrecktideBoat> TargetBoat;
	TWeakObjectPtr<ACombatCharacter> TargetSwimmer;
	float ForwardSpeed = 0.0f;
	float StateElapsedSeconds = 0.0f;
	float SinceContactSeconds = 0.0f;
	float SinceLungeSeconds = 0.0f;
	float LungeRemainingSeconds = 0.0f;
	float BlindArcHeldSeconds = 0.0f;
	float AggroBlockedUntilSeconds = 0.0f;
	float LastTargetBearing = -1.0f;
	/** Whatever her swept move last ran into, so a lunge that actually grinds his planks counts as a
	 *  ram whatever the centre distance says. Cleared on any frame she moves freely. */
	TWeakObjectPtr<AActor> LastBlockingActor;
	int32 RamContactCount = 0;
	int32 RamDamageDealt = 0;
	/** Helm state: where the wheel points, and how hard her steering was over last frame. */
	float HelmWheelAngleDegrees = 0.0f;
	float HelmSteerInput = 0.0f;
	bool bHelmsmanPosed = false;
};
