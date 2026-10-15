// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"

class ACombatEnemy;
class AWrecktidePlayerState;

/**
 * Map model for the whole game (2026-09-06, lane L4). Free functions with no state of their own, the
 * same shape as WrecktideMissionLogic: the minimap, the full map on M, the edge arrow, the mission
 * beacon and -MapProbe all ask THESE and nothing else, so the world-to-map transform and the icon set
 * exist in exactly one place and a probe can assert the same numbers the HUD draws with.
 *
 * WORLD TO MAP. The picture is Tools\map_capture.py's output, an orthographic-equivalent top-down
 * plate of the island drawn from the level's own tier geometry. The transform is therefore known
 * exactly rather than fitted:
 *     U = (WorldY + MapWorldHalfExtent) / (2 * MapWorldHalfExtent)     map RIGHT = world +Y
 *     V = (MapWorldHalfExtent - WorldX) / (2 * MapWorldHalfExtent)     map UP    = world +X
 * Map up is world +X and map right is world +Y because UE is left handed with X forward and Y right:
 * a pirate facing +X has his right hand on +Y, so any other pairing draws a MIRRORED map and puts
 * every icon on the wrong side of the player. +X is labelled N on the rim.
 * Half extent is 10,000 uu (ortho width 20,000) and not the 16,000 the brief sketched, because the
 * pier deck runs to Y = -9,110 and the player's boat is moored at (-700, -8,300): a 16,000 wide plate
 * would cut the pier and the boat off the map edge. Keep this in step with Tools\map_capture.py.
 */
namespace WrecktideMap
{
/** Centre of the map plate in world XY. */
constexpr float MapWorldCentreX = 0.0f;
constexpr float MapWorldCentreY = 0.0f;

/** Half the width of the map plate in uu. The plate spans centre +- this on both axes. */
constexpr float MapWorldHalfExtent = 10000.0f;

/** Minimap radar range in uu: the disc shows this far in every direction. One tunable. */
constexpr float MinimapRangeUU = 3500.0f;

/**
 * GALLOWS REACH, island 2 (C34, 2026-09-07). Its own world numbers, so the chart, the arrival
 * toast, Ada's spot and the second bounty board all read the SAME centre instead of four copies of
 * the same literal. Kept in step with Tools\island2_build.py (which lays the tiers) and
 * Tools\island2_map.py (which bakes the plate).
 */
namespace GallowsReach
{
constexpr float CentreX = 12000.0f;
constexpr float CentreY = -16000.0f;

/** Half the beach ring: X 7500..16500, Y -20500..-11500. */
constexpr float BeachHalf = 4500.0f;

/** Landing beach on the side that faces Wreck Cove; H04_GallowsReach's TargetLocation. */
constexpr float LandingX = 9500.0f;
constexpr float LandingY = -12700.0f;

/** The garrison square's second bounty board. */
constexpr float BoardX = 10400.0f;
constexpr float BoardY = -15400.0f;

/** How close counts as "land ho" for the once-per-save arrival toast (C34.7). */
constexpr float ArrivalRadius = 6000.0f;
}

/** Mangrove Hollow. Keep in step with Tools/island3_build.py and island3_map.py. */
namespace MangroveHollow
{
constexpr float CentreX = -14000.0f;
constexpr float CentreY = 13000.0f;
constexpr float BeachHalf = 4500.0f;
constexpr float ChartHalf = 6000.0f;
constexpr float LandingX = -11200.0f;
constexpr float LandingY = 9900.0f;
constexpr float BoardX = -12400.0f;
constexpr float BoardY = 11550.0f;
constexpr float OduyaX = -11500.0f;
constexpr float OduyaY = 11700.0f;
constexpr float ArrivalRadius = 6000.0f;
}

/**
 * One island's chart plate: the picture, the world rectangle it covers and the name written over
 * it. The radar and the full map read the plate of the island the player stands on, or the nearest
 * one while he is at sea, so a second island is a row here rather than a second draw path.
 *
 * KEEP IN STEP with the python that bakes each plate: Tools\map_capture.py for Wreck Cove and
 * Tools\island2_map.py for Gallows Reach. Each script logs its own centre and half extent.
 */
struct FIslandChart
{
	const TCHAR* Id;
	const TCHAR* Title;
	float CentreX;
	float CentreY;
	float HalfExtent;

	/** Half the island's own beach ring in uu, which is NOT a fixed fraction of the plate: Wreck
	 *  Cove is 7,000 of 10,000 and Gallows Reach 4,500 of 6,000. The fallback art (no plate texture
	 *  yet, or a texture that failed to load) draws sand out to this, so the drawn shore matches the
	 *  land the player can actually walk on. */
	float BeachHalfExtent;

	const TCHAR* TexturePath;
};

/** Every charted island. Index 0 is Wreck Cove and stays index 0: the HUD keeps its texture in the
 *  member the map probe reads. */
const TArray<FIslandChart>& GetIslandCharts();

/** The chart of the island nearest World in 2D. Never returns a dangling reference; with an empty
 *  table it returns a Wreck Cove chart built from the constants above. */
const FIslandChart& ChartFor(const FVector& World);

/** Content paths, loaded by string at runtime (nothing else references them, so /Game/UI is cooked
 *  as a folder by Config\DefaultGame.ini:21 and /Game/UI/Map rides along). */
extern const TCHAR* IslandTexturePath;
extern const TCHAR* RimTexturePath;
extern const TCHAR* GlowTexturePath;
extern const TCHAR* RingTexturePath;
extern const TCHAR* BeamTexturePath;
extern const TCHAR* BeaconMaterialPath;

/** What a marker is, which decides its colour and its glyph. */
enum class EMapIcon : uint8
{
	Stall,
	Board,
	Giver,
	Boat,
	Chest,
	Boss,
	Enemy,
	Patrol,
	Mission
};

struct FMapMarker
{
	FVector Location = FVector::ZeroVector;
	EMapIcon Icon = EMapIcon::Chest;
	FString Label;
};

/** World XY to map UV in [0,1]. Points outside the plate return UVs outside [0,1]; callers clamp.
 *  The one-argument form is Wreck Cove's plate and is what -MapProbe pins. */
FVector2D WorldToUV(const FVector& World);
FVector2D WorldToUV(const FVector& World, const FIslandChart& Chart);

/** The inverse, for the full map's click-free readouts and for the probe. */
FVector2D UVToWorld(const FVector2D& UV);

/** CollectMarkers is the SINGLE authoritative icon set for both the minimap and the full map.
 *  Every marker is found by TActorIterator on the live world, so nothing here can go stale when an
 *  actor is spawned at runtime (the camp props are). Opened chests and dead enemies drop out. */
void CollectMarkers(const UObject* WorldContext, TArray<FMapMarker>& OutMarkers);

/** Which answer the caller wants for a target that MOVES (C33, 2026-09-07). Only KillRow and
 *  KillBoss differ between the two: everything else resolves to the same static actor. */
enum class EMissionTargetFlavour : uint8
{
	/** Where the target IS right now. The tracking cues use this: the radar diamond, the edge arrow
	 *  and the HUD metres line all want the man himself. */
	Live,

	/** Where the target BELONGS: an enemy's post (ACombatEnemy::GetHomeLocation). The gold beam uses
	 *  this so it marks a PLACE and stops being dragged across the island by a chasing enemy, which
	 *  is exactly what the play test saw on 2026-09-07. */
	Anchor
};

/** The active mission's world target, the same rule the beacon uses:
 *  ReachSpot -> the named landmark, KillRow/KillBoss -> the nearest living enemy of that row (its
 *  live position for Live, its home post for Anchor), OpenChest -> the chest, BoardBoat -> the boat,
 *  TurnInBounty -> the bounty board, TalkTo -> the named giver, Deliver -> the named giver on the way
 *  out and the giver who set the errand on the way back, Pay -> the giver of the row, Upgrade -> the
 *  smith stall. False when no mission is active or nothing resolves. */
bool ResolveMissionTarget(const UObject* WorldContext, const AWrecktidePlayerState* PlayerState, FVector& OutLocation, FString& OutLabel,
	EMissionTargetFlavour Flavour = EMissionTargetFlavour::Live);

/** WHICH man a kill objective is about, so a caller can remember one. Null when the tracked mission
 *  is not a KillRow or KillBoss, or when nothing of that row is alive. Preferred is returned
 *  unchanged while he is still alive and still of the row, which is what lets the beacon STICK to
 *  one post instead of hopping to whichever enemy the player happens to be nearest (C33, sticky
 *  anchor 2026-09-07); pass null to get the nearest living one. */
const ACombatEnemy* ResolveMissionKillTarget(const UObject* WorldContext, const AWrecktidePlayerState* PlayerState, const ACombatEnemy* Preferred);

/** Clamp a centre-relative offset to the edge of a rectangle ALONG ITS OWN DIRECTION.
 *  Never clamp X and Y independently: that leaves the centre-to-target line and points at the wrong
 *  place near the corners (RESEARCH section 4). */
FVector2D ClampToRect(const FVector2D& Offset, const FVector2D& HalfSize);

/** Clamp a centre-relative offset onto a circle of the given radius, for the minimap rim. */
FVector2D ClampToCircle(const FVector2D& Offset, float Radius);

/** Screen-space offset (x right, y DOWN) of a world point on a heading-up radar centred on the
 *  player. Scale is pixels per uu. */
FVector2D WorldToRadarOffset(const FVector& PlayerLocation, float ViewYawDegrees, const FVector& Target, float PixelsPerUU);

/** Clockwise-from-up screen angle in degrees of a radar offset, for the rim marker and the probe. */
float RadarOffsetAngle(const FVector2D& Offset);

FLinearColor IconColour(EMapIcon Icon);

/** One or two characters drawn with the engine font: no glyph atlas to author or cook. */
FString IconGlyph(EMapIcon Icon);

/** Legend wording on the full map. */
FString IconLegend(EMapIcon Icon);
}
