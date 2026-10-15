// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WrecktideSeaSurface.generated.h"

class UStaticMeshComponent;

/**
 * The visible sea: one flat plane wearing the Synty stylised water material, spawned at runtime.
 *
 * Complaint C5 was "I can't even see the water when I am swimming in it". Three rounds of captures
 * found two causes stacked on top of each other. The single layer water reflection smeared the
 * beach and the rocks into vertical streaks across the near surface (fixed by
 * r.Water.SingleLayer.Reflection=0), and underneath that the Water plugin's own mesh tears into a
 * picket fence at a grazing angle: the near-camera tiles are stretched into near vertical slivers,
 * and Gerstner vertex displacement turns each sliver's near vertex into a picket. Raising
 * TessellationFactor from 6 to 10 only made the pickets narrower and more numerous
 * (_logs\look_sea_r3mesh_view1.png). A FLAT plane has no vertex displacement, so it cannot produce
 * that silhouette at all, and the Synty material carries the whole look in its shading
 * (_logs\look_sea_r3style_view1.png).
 *
 * Why this is a runtime actor and not a level actor: the same plane placed by headless python kept
 * its collision through three different clearing attempts and the pirate STOOD on the sea
 * (WRECKTIDE-SWIM-PROBE float BAD swimming=0 z=-31, band -260..-20). A constructor-built component
 * cannot be overridden by a stale serialised BodyInstance, and it leaves Lvl_Island untouched.
 *
 * What this actor does NOT do: swimming. That is the sea APhysicsVolume from
 * Tools\island_sea_volume.py, which is what the character movement component reads to enter
 * MOVE_Swimming. This plane is scenery with no collision of any kind, and the Water plugin's ocean
 * actor stays in the level so the underwater post process and the water info texture still exist.
 */
UCLASS()
class WRECKTIDE_API AWrecktideSeaSurface : public AActor
{
	GENERATED_BODY()

public:
	AWrecktideSeaSurface();

	/** Spawns the sea surface into World if one is not already there. Safe to call more than once. */
	static AWrecktideSeaSurface* SpawnFor(UWorld* World);

protected:
	virtual void BeginPlay() override;

	/** The plane. Collision is off in every way the engine offers; see the constructor. */
	UPROPERTY(VisibleAnywhere, Category = "Sea")
	TObjectPtr<UStaticMeshComponent> SurfaceMesh;

	/** World Z of the visible surface. One uu above the sea volume's top (Z_WATER = -120) so the
	 *  plane reads as the waterline without ever poking through it. */
	UPROPERTY(EditAnywhere, Category = "Sea", meta = (Units = "cm"))
	float SurfaceZ = -119.0f;

	/** Uniform XY scale of the engine plane, which is 100 uu square. 512 covers the 51,200 uu water
	 *  zone in a single draw. */
	UPROPERTY(EditAnywhere, Category = "Sea", meta = (ClampMin = 1))
	float SurfaceScale = 512.0f;

	/** Off means the actor spawns but hides itself, so the Water plugin surface can be compared
	 *  against this one without a rebuild. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	bool bEnabled = true;

	/** Object path of the stylised water material. It lives in /Game/PolygonPirates/Materials, which
	 *  DefaultGame.ini already cooks as a folder for the twelve wardrobe colourways, so loading it
	 *  by path here needs no new staging rule. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	FString WaterMaterialPath = TEXT("/Game/PolygonPirates/Materials/MI_PolygonPirates_Water.MI_PolygonPirates_Water");
};
