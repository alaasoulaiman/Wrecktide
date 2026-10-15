// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideSeaSurface.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AWrecktideSeaSurface::AWrecktideSeaSurface()
{
	PrimaryActorTick.bCanEverTick = false;

	SurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SurfaceMesh"));
	RootComponent = SurfaceMesh;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		SurfaceMesh->SetStaticMesh(PlaneMesh.Object);
	}

	// Collision off through every door the engine has. The python-placed version of this plane kept
	// its collision across a headless save no matter which of these was called, and the pirate stood
	// on the sea; set in a constructor there is no serialised BodyInstance left to win the argument.
	SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SurfaceMesh->SetCollisionProfileName(TEXT("NoCollision"));
	SurfaceMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	SurfaceMesh->SetGenerateOverlapEvents(false);
	SurfaceMesh->SetCanEverAffectNavigation(false);
	SurfaceMesh->bCastDynamicShadow = false;
	SurfaceMesh->CastShadow = false;
	// The sea is 51,200 uu across; without this it would flicker out whenever its origin left the
	// frustum bounds the renderer computed for a 100 uu plane.
	SurfaceMesh->SetBoundsScale(100.0f);
	SetActorEnableCollision(false);
}

void AWrecktideSeaSurface::BeginPlay()
{
	Super::BeginPlay();

	SetActorLocation(FVector(0.0f, 0.0f, SurfaceZ));
	SetActorScale3D(FVector(SurfaceScale, SurfaceScale, 1.0f));

	if (!bEnabled)
	{
		SurfaceMesh->SetVisibility(false);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SEASURFACE disabled by bEnabled"));
		return;
	}

	// Loaded by path, not a hard reference, so the material is only pulled in when this actor runs.
	// The folder is in DirectoriesToAlwaysCook, which is what makes that safe in a packaged build;
	// the wardrobe colourways learned that lesson for us (DefaultGame.ini:16-18).
	UMaterialInterface* WaterMaterial = LoadObject<UMaterialInterface>(nullptr, *WaterMaterialPath);
	if (!WaterMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SEASURFACE FAIL material not found at %s"), *WaterMaterialPath);
		return;
	}
	SurfaceMesh->SetMaterial(0, WaterMaterial);

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SEASURFACE z=%.0f scale=%.0f material=%s collision=%d"),
		SurfaceZ, SurfaceScale, *WaterMaterial->GetName(),
		static_cast<int32>(SurfaceMesh->GetCollisionEnabled()));
}

AWrecktideSeaSurface* AWrecktideSeaSurface::SpawnFor(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AWrecktideSeaSurface> It(World); It; ++It)
	{
		// already present (a re-entrant BeginPlay, or someone placed one by hand)
		return *It;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWrecktideSeaSurface* Surface = World->SpawnActor<AWrecktideSeaSurface>(
		AWrecktideSeaSurface::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (!Surface)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SEASURFACE FAIL spawn returned null"));
	}
	return Surface;
}
