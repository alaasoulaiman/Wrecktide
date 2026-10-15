// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktideEditorTools.h"

#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Components/BrushComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PhysicsVolume.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#endif

int32 UWrecktideEditorTools::SetMontageSlotName(UAnimMontage* Montage, FName NewSlotName)
{
#if WITH_EDITOR
	if (!Montage || NewSlotName.IsNone())
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-EDTOOLS SetMontageSlotName bad input montage=%d slot=%s"),
			Montage != nullptr, *NewSlotName.ToString());
		return -1;
	}

	Montage->Modify();

	int32 Renamed = 0;
	for (FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (Track.SlotName != NewSlotName)
		{
			Track.SlotName = NewSlotName;
			++Renamed;
		}
	}

	// The skeleton registry is what the montage editor validates against; register so the asset
	// opens clean in the editor instead of flagging an unknown slot. Harmless when already present.
	if (USkeleton* Skeleton = Montage->GetSkeleton())
	{
		if (Skeleton->RegisterSlotNode(NewSlotName))
		{
			Skeleton->MarkPackageDirty();
		}
	}

	if (Renamed > 0)
	{
		Montage->MarkPackageDirty();
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-EDTOOLS SetMontageSlotName %s -> %s renamed=%d tracks=%d"),
		*Montage->GetName(), *NewSlotName.ToString(), Renamed, Montage->SlotAnimTracks.Num());
	return Renamed;
#else
	return -1;
#endif
}

TArray<FName> UWrecktideEditorTools::GetMontageSlotNames(const UAnimMontage* Montage)
{
	TArray<FName> Names;
	if (!Montage)
	{
		return Names;
	}

	Names.Reserve(Montage->SlotAnimTracks.Num());
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		Names.Add(Track.SlotName);
	}
	return Names;
}

bool UWrecktideEditorTools::RegisterSkeletonSlot(USkeleton* Skeleton, FName SlotName)
{
#if WITH_EDITOR
	if (!Skeleton || SlotName.IsNone())
	{
		return false;
	}

	Skeleton->Modify();
	const bool bAdded = Skeleton->RegisterSlotNode(SlotName);
	if (bAdded)
	{
		Skeleton->MarkPackageDirty();
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-EDTOOLS RegisterSkeletonSlot %s slot=%s added=%d"),
		*Skeleton->GetName(), *SlotName.ToString(), bAdded ? 1 : 0);
	return bAdded;
#else
	return false;
#endif
}

TArray<FName> UWrecktideEditorTools::GetSkeletonSlotNames(const USkeleton* Skeleton)
{
	TArray<FName> Names;
	if (!Skeleton)
	{
		return Names;
	}

	for (const FAnimSlotGroup& Group : Skeleton->GetSlotGroups())
	{
		for (const FName& Slot : Group.SlotNames)
		{
			Names.AddUnique(Slot);
		}
	}
	return Names;
}

AActor* UWrecktideEditorTools::CreateNavBoundsVolume(UObject* WorldContextObject, FVector Center, FVector Extent)
{
#if WITH_EDITOR
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || Extent.GetMin() <= 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-EDTOOLS CreateNavBoundsVolume bad input world=%d extent=%s"),
			World != nullptr, *Extent.ToString());
		return nullptr;
	}

	ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(Center, FRotator::ZeroRotator);
	if (!Volume)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-EDTOOLS CreateNavBoundsVolume spawn failed"));
		return nullptr;
	}

	// the same steps the editor's Box Volume factory runs: a cube builder writes the brush model,
	// csgPrepMovingBrush makes it a proper moving brush, PostEditChange registers it
	UCubeBuilder* Builder = NewObject<UCubeBuilder>();
	Builder->X = Extent.X * 2.0f;
	Builder->Y = Extent.Y * 2.0f;
	Builder->Z = Extent.Z * 2.0f;
	UActorFactory::CreateBrushForVolumeActor(Volume, Builder);

	const FBox Bounds = Volume->GetComponentsBoundingBox(true);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-EDTOOLS CreateNavBoundsVolume %s at %s size=%s"),
		*Volume->GetName(), *Center.ToString(), *Bounds.GetSize().ToString());
	return Volume;
#else
	return nullptr;
#endif
}

AActor* UWrecktideEditorTools::CreateWaterVolume(UObject* WorldContextObject, FVector Center, FVector Extent, FName Label)
{
#if WITH_EDITOR
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || Extent.GetMin() <= 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-EDTOOLS CreateWaterVolume bad input world=%d extent=%s"),
			World != nullptr, *Extent.ToString());
		return nullptr;
	}

	APhysicsVolume* Volume = World->SpawnActor<APhysicsVolume>(Center, FRotator::ZeroRotator);
	if (!Volume)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-EDTOOLS CreateWaterVolume spawn failed"));
		return nullptr;
	}

	UCubeBuilder* Builder = NewObject<UCubeBuilder>();
	Builder->X = Extent.X * 2.0f;
	Builder->Y = Extent.Y * 2.0f;
	Builder->Z = Extent.Z * 2.0f;
	UActorFactory::CreateBrushForVolumeActor(Volume, Builder);

	// the water switch itself, plus the swim feel tunables that belong to the sea rather than to
	// the pawn (FluidFriction damps the swimmer, Priority beats the world's default volume)
	Volume->bWaterVolume = true;
	Volume->FluidFriction = 0.3f;
	Volume->Priority = 1;

	// the volume must never become navigable geometry: the recast build would otherwise floor the
	// whole sea and the enemies would path over open water
	if (UBrushComponent* Brush = Volume->GetBrushComponent())
	{
		Brush->SetCanEverAffectNavigation(false);
	}

	if (!Label.IsNone())
	{
		Volume->SetActorLabel(Label.ToString());
	}

	const FBox Bounds = Volume->GetComponentsBoundingBox(true);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-EDTOOLS CreateWaterVolume %s at %s size=%s water=%d priority=%d"),
		*Volume->GetName(), *Center.ToString(), *Bounds.GetSize().ToString(),
		Volume->bWaterVolume ? 1 : 0, Volume->Priority);
	return Volume;
#else
	return nullptr;
#endif
}

FVector UWrecktideEditorTools::GetActorBoundsSize(const AActor* Actor)
{
	return Actor ? Actor->GetComponentsBoundingBox(true).GetSize() : FVector::ZeroVector;
}

float UWrecktideEditorTools::BuildNavigationSynchronously(UObject* WorldContextObject, float TimeoutSeconds)
{
#if WITH_EDITOR
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-EDTOOLS BuildNavigationSynchronously: no navigation system (world=%d)"), World != nullptr);
		return -1.0f;
	}

	NavSys->Build();
	const double Start = FPlatformTime::Seconds();
	int32 Pumps = 0;
	// the generator's async tile tasks only advance inside the navigation system's Tick, which nobody
	// calls in a commandlet: pump it by hand until the build reports done
	while (NavSys->IsNavigationBuildInProgress() && FPlatformTime::Seconds() - Start < TimeoutSeconds)
	{
		NavSys->Tick(0.1f);
		FPlatformProcess::Sleep(0.02f);
		++Pumps;
	}
	const float Spent = static_cast<float>(FPlatformTime::Seconds() - Start);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-EDTOOLS BuildNavigationSynchronously done in %.1fs pumps=%d stillInProgress=%d"),
		Spent, Pumps, NavSys->IsNavigationBuildInProgress() ? 1 : 0);
	return Spent;
#else
	return -1.0f;
#endif
}
