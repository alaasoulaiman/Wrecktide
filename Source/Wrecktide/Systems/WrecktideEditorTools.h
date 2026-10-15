// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WrecktideEditorTools.generated.h"

class UAnimMontage;
class USkeleton;

/**
 * Headless authoring helpers for the python commandlet lane (Tools\*.py).
 *
 * They exist because the reflected Python surface only reaches editor-visible properties, and the
 * two values the upper-body montage layer needs to write are plain UPROPERTY members:
 * UAnimMontage::SlotAnimTracks[i].SlotName and USkeleton::SlotGroups. Every mutating body is
 * editor-only; in a cooked game it returns -1 / false and touches nothing. The read helpers work
 * everywhere and are what the probes use to prove a montage really moved to the UpperBody slot.
 */
UCLASS()
class WRECKTIDE_API UWrecktideEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Renames every slot track on Montage to NewSlotName and registers that slot on the montage's
	 * skeleton. Returns the number of tracks renamed (0 = already there), -1 on bad input or outside
	 * the editor. Marks the montage (and the skeleton, when newly registered) dirty; saving is the
	 * caller's job so a failed verify can still abort without touching disk.
	 */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|EditorTools")
	static int32 SetMontageSlotName(UAnimMontage* Montage, FName NewSlotName);

	/** Slot names of every track on Montage, in track order. Empty for a null montage. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|EditorTools")
	static TArray<FName> GetMontageSlotNames(const UAnimMontage* Montage);

	/** Registers SlotName on Skeleton (default group). True only when it was newly added. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|EditorTools")
	static bool RegisterSkeletonSlot(USkeleton* Skeleton, FName SlotName);

	/** Every slot name registered on Skeleton across all slot groups. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|EditorTools")
	static TArray<FName> GetSkeletonSlotNames(const USkeleton* Skeleton);

	/**
	 * Spawns a NavMeshBoundsVolume through the editor's own Box Volume path (2026-09-04):
	 * UCubeBuilder + UActorFactory::CreateBrushForVolumeActor + PostEditChange. The PostEditChange is
	 * the part that matters: it is what makes the editor navigation system spawn the level's
	 * RecastNavMesh actor. A volume placed by Python's spawn_actor_from_class gets a brush and valid
	 * bounds but never that notification, so Lvl_Island shipped with a bounds volume and NO nav data
	 * from 07-27 until today (a -game process does not create nav data by itself). Returns the
	 * volume, or null outside the editor / on bad input. Build the navmesh
	 * (BuildNavigationSynchronously) and save afterwards; both are the caller's job.
	 */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|EditorTools", meta=(WorldContext="WorldContextObject"))
	static AActor* CreateNavBoundsVolume(UObject* WorldContextObject, FVector Center, FVector Extent);

	/**
	 * Spawns a water APhysicsVolume through the SAME editor Box Volume path CreateNavBoundsVolume
	 * uses (UCubeBuilder + UActorFactory::CreateBrushForVolumeActor), then flips bWaterVolume on.
	 * This is the ONLY way a UCharacterMovementComponent ever enters MOVE_Swimming
	 * (CharacterMovementComponent.cpp PhysicsVolumeChanged), and it needs REAL brush geometry: a
	 * volume from python's spawn_actor_from_class carries an empty brush, so its bounds are zero and
	 * nothing ever overlaps it (the same trap that left the island with no navmesh from 07-27).
	 * The brush is taken out of navigation gathering so a sea volume can never grow a navmesh over
	 * open water. Bounds are logged on purpose: an empty brush reads size 0,0,0 and must fail the
	 * caller's check. Returns the volume, or null outside the editor / on bad input. Saving is the
	 * caller's job.
	 */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|EditorTools", meta=(WorldContext="WorldContextObject"))
	static AActor* CreateWaterVolume(UObject* WorldContextObject, FVector Center, FVector Extent, FName Label);

	/**
	 * Builds every navigation data in the world and pumps the navigation system until the build is
	 * finished (or TimeoutSeconds). A python commandlet never ticks the editor, so a nav bounds volume
	 * added headless would otherwise save an EMPTY navmesh. Returns the seconds spent, -1 outside the
	 * editor or without a navigation system.
	 */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|EditorTools", meta=(WorldContext="WorldContextObject"))
	static float BuildNavigationSynchronously(UObject* WorldContextObject, float TimeoutSeconds = 300.0f);

	/** World-space bounding box size of an actor's components (diagnostic: an empty brush reads 0,0,0). */
	UFUNCTION(BlueprintPure, Category="Wrecktide|EditorTools")
	static FVector GetActorBoundsSize(const AActor* Actor);
};
