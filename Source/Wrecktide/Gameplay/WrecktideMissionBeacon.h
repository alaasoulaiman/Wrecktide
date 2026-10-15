// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WrecktideMissionBeacon.generated.h"

class ACombatEnemy;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** What a beacon follows. One actor, two jobs, so the look is authored once. */
UENUM()
enum class EWrecktideBeaconMode : uint8
{
	/** The active story mission's target: the spot, the nearest living enemy of the row, the chest,
	 *  the boat or the bounty board. Full size, full brightness. */
	Mission,

	/** An accepted bounty whose kill count is complete: the board it turns in at. Smaller, paler. */
	BountyTurnIn
};

/**
 * The gold sky beam that says "the thing you were told to do is HERE" (2026-09-06, lane L4).
 * The classic MMO's quest indicator is the reference for the IDEA only: a billboarded additive beam
 * rising out of the target with sparkles round its foot. Every asset here is ours: two engine basic
 * shapes (cylinder and plane) wearing M_WrecktideBeacon, an unlit additive two-sided material built
 * headlessly by Tools\map_capture.py, with a vertical fade mask on the beam and a radial glow mask
 * on the disc and the sparkles.
 *
 * WHY IT DRIVES ITSELF ON A 1 Hz BEAT rather than being pushed from every mission change point:
 * a kill objective has to re-target the nearest living enemy every second anyway, and a push-only
 * beacon strands a beam over a dead target the moment one call site is forgotten. Pull is fail-safe;
 * the mission state it pulls (WrecktideMap::ResolveMissionTarget -> WrecktideMissionLogic) is still
 * the single authority, so nothing here can invent a target the mission logic does not agree with.
 *
 * The beacon hides itself when nothing is active, so "the chain ended" and "no mission accepted"
 * both read as no beam without a destroy/respawn race.
 */
UCLASS()
class WRECKTIDE_API AWrecktideMissionBeacon : public AActor
{
	GENERATED_BODY()

public:
	AWrecktideMissionBeacon();

	virtual void Tick(float DeltaSeconds) override;

	/** SpawnBeacons is the SINGLE authoritative path that puts the two beacons in the world; the
	 *  reward subsystem calls it once at world begin play, beside the camp props. */
	static void SpawnBeacons(UWorld* World);

	/** The live mission beacon, or null. Read path for -MapProbe. */
	static AWrecktideMissionBeacon* FindBeacon(const UWorld* World, EWrecktideBeaconMode Mode);

	/** True while the beam is on screen (a hidden beacon is parked, not destroyed). */
	bool IsLit() const { return bLit; }

	EWrecktideBeaconMode GetMode() const { return Mode; }

	/** Re-resolve the target and move or hide. Called on the beat and directly by the probe so a
	 *  test never has to sleep for a timer it cannot see. */
	void RefreshTarget();

	/** Which mission or bounty row the beam is currently standing on; None when unlit. */
	FName GetTargetRow() const { return TargetRow; }

	/** The anchor the last RefreshTarget resolved, lit or not. The beam parks where it went out, so
	 *  a probe cannot read the anchor off the actor once the reach rule has put it out (C33). */
	FVector GetLastAnchor() const { return LastAnchor; }

	/** True while the reach rule is holding the beam out: he stood on this anchor for this row. */
	bool HasReachedTarget() const { return bReached; }

	/** The enemy whose post the beam is standing on, or null when the objective is not a kill. */
	const ACombatEnemy* GetAnchorEnemy() const { return AnchorEnemy.Get(); }

protected:
	virtual void BeginPlay() override;

	/** Mission or bounty turn-in. Set at spawn, never changes. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon")
	EWrecktideBeaconMode Mode = EWrecktideBeaconMode::Mission;

	/** Beam height in uu. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="100.0", Units="cm"))
	float BeamHeight = 1500.0f;

	/** Beam radius in uu. 90 rather than 50: at 900 uu a 50 uu beam is a thread, and the whole point
	 *  is that you see it from across the camp. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="5.0", Units="cm"))
	float BeamRadius = 90.0f;

	/** Base disc diameter in uu. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="10.0", Units="cm"))
	float DiscDiameter = 420.0f;

	/** How far the sparkles orbit from the beam. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="10.0", Units="cm"))
	float SparkleOrbitRadius = 130.0f;

	/** Seconds for one full alpha pulse, and the two ends of the pulse. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.1", Units="s"))
	float PulseSeconds = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.0"))
	float PulseMin = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.0"))
	float PulseMax = 1.0f;

	/** Emissive multiplier before the pulse; the bounty beacon dims by BountyScale as well. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.1"))
	// 3.0, not 5.0: at 5 the tone mapper blew the beam core to white and the gold was only left in
	// the fringe. 3 keeps the hue and still measures 160 colour distance against a daytime sky.
	float BaseIntensity = 3.0f;

	/** Translucent opacity at the foot of the beam; the mask fades it to nothing at the top. The
	 *  material is TRANSLUCENT, not additive: additive can only add to what is behind it, so a gold
	 *  beam over a bright daytime sky moved almost nothing and read as invisible (2026-09-06). */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BeamOpacity = 0.85f;

	/** Size and brightness of the bounty turn-in beacon relative to the mission one. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.05", ClampMax="1.0"))
	float BountyScale = 0.55f;

	/** How often the target is re-resolved, in seconds. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.1", Units="s"))
	float RetargetSeconds = 1.0f;

	/** How close the player has to get before the beam has done its job and goes out (C33.3). The
	 *  disc is DiscDiameter across, so 900 puts him well inside the light before it drops. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="50.0", Units="cm"))
	float BeaconReachRadius = 900.0f;

	/** How far the anchor has to MOVE, after he has stood on one, before the beam counts it as a new
	 *  destination and lights again. Below this it is the same post and walking away relights
	 *  nothing, which is the whole point of the reach rule. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="100.0", Units="cm"))
	float BeaconRearmDistance = 2500.0f;

	/** Seconds the beam takes to fade out once it has been reached. Zero pops instead. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon", meta=(ClampMin="0.0", Units="s"))
	float BeaconFadeSeconds = 0.5f;

	/** Warm gold. */
	UPROPERTY(EditDefaultsOnly, Category="Wrecktide|Beacon")
	FLinearColor BeamColour = FLinearColor(1.0f, 0.62f, 0.12f, 1.0f);

private:
	void BuildVisuals();
	void SetLit(bool bNewLit);
	/** Forget an arrival: called on every path that has no destination, so a re-accepted errand at the
	 *  same spot (a repeatable bounty at the board) lights again instead of inheriting a dead latch. */
	void ClearReach();

	/** Push one emissive level through every dynamic material, scaled per piece. */
	void ApplyIntensity(float Multiplier);

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Beam;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Disc;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Sparkles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PulsingMaterials;

	/** Per-piece emissive multiplier, parallel to PulsingMaterials, so the pulse keeps the disc
	 *  softer and the sparkles hotter than the beam instead of flattening all three. */
	TArray<float> PulseScales;

	float RetargetCountdown = 0.0f;
	bool bLit = false;
	FName TargetRow;

	/** Reach state (C33.3). bArmed says he has been seen OUTSIDE BeaconReachRadius since this
	 *  destination came up, so a mission whose target is already under his feet still lights once
	 *  rather than never showing at all; bReached says he then walked in and the beam is spent for
	 *  ReachedRow at ReachedAnchor until the row changes or the anchor moves to another post. */
	bool bArmed = false;
	bool bReached = false;

	/** STICKY kill anchor (C33 refinement, 2026-09-07): the enemy whose post the beam is standing on,
	 *  and the row that named him. He keeps the beam until he dies, goes invalid, or the tracked row
	 *  changes; only then does the next nearest living one of the row get it. Weak on purpose, so a
	 *  destroyed enemy simply reads as null instead of keeping an actor alive. */
	TWeakObjectPtr<const ACombatEnemy> AnchorEnemy;
	FName AnchorRow;
	FName ReachedRow;
	FVector ReachedAnchor = FVector::ZeroVector;

	/** The last anchor RefreshTarget resolved, whether or not the beam is standing on it. */
	FVector LastAnchor = FVector::ZeroVector;

	/** Fade-out state: bLit drops the moment the beam is spent, the pixels take BeaconFadeSeconds. */
	bool bFadingOut = false;
	float FadeRemaining = 0.0f;
};
