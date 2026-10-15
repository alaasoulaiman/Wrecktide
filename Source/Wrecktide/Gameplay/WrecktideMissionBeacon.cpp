// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideMissionBeacon.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Systems/ProgressComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "Engine/Texture.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktideQuestLogic.h"
#include "UI/WrecktideMapView.h"
#include "Variant_Combat/AI/CombatEnemy.h"

namespace
{
/** Engine basic shapes: the cylinder is 100 uu across and 100 uu tall about its centre, the plane is
 *  100 x 100 about its centre with its normal on +Z. Both ship with the engine, so nothing new has
 *  to be authored or cooked for the geometry. */
const TCHAR* CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
const TCHAR* PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
constexpr int32 SparkleCount = 7;
}

AWrecktideMissionBeacon::AWrecktideMissionBeacon()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("BeaconRoot"));
	SetRootComponent(Root);
}

void AWrecktideMissionBeacon::BeginPlay()
{
	Super::BeginPlay();

	BuildVisuals();
	SetLit(false);
	RefreshTarget();
}

void AWrecktideMissionBeacon::BuildVisuals()
{
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, CylinderMeshPath);
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, PlaneMeshPath);
	UMaterialInterface* BeaconMaterial = LoadObject<UMaterialInterface>(nullptr, WrecktideMap::BeaconMaterialPath);
	UTexture* BeamMask = LoadObject<UTexture>(nullptr, WrecktideMap::BeamTexturePath);
	UTexture* GlowMask = LoadObject<UTexture>(nullptr, WrecktideMap::GlowTexturePath);
	UTexture* RingMask = LoadObject<UTexture>(nullptr, WrecktideMap::RingTexturePath);

	// Fail open, never closed: a missing art asset costs the LOOK, not the beacon. The probe asserts
	// position, so a beam with the default material still proves the targeting is right and the log
	// says exactly which asset is missing.
	if (!BeaconMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-BEACON material missing at %s, beam uses the default material"),
			WrecktideMap::BeaconMaterialPath);
	}

	const float Scale = (Mode == EWrecktideBeaconMode::BountyTurnIn) ? BountyScale : 1.0f;

	auto MakePiece = [this, BeaconMaterial](const TCHAR* Name, UStaticMesh* Mesh, UTexture* Mask, float Brightness, float BrightnessScale, float Opacity) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this, FName(Name));
		if (!Piece)
		{
			return nullptr;
		}
		Piece->SetupAttachment(Root);
		Piece->RegisterComponent();
		Piece->SetStaticMesh(Mesh);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(false);
		Piece->bReceivesDecals = false;
		// Never culled by distance: a beam you can only see once you are on top of it is not a beacon.
		Piece->bNeverDistanceCull = true;
		Piece->SetCullDistance(0.0f);
		Piece->SetCanEverAffectNavigation(false);

		if (BeaconMaterial)
		{
			if (UMaterialInstanceDynamic* Dynamic = Piece->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BeaconMaterial))
			{
				if (Mask)
				{
					Dynamic->SetTextureParameterValue(TEXT("Mask"), Mask);
				}
				Dynamic->SetVectorParameterValue(TEXT("BeamColour"), BeamColour);
				Dynamic->SetScalarParameterValue(TEXT("Intensity"), Brightness);
				Dynamic->SetScalarParameterValue(TEXT("OpacityScale"), Opacity);
				PulsingMaterials.Add(Dynamic);
				PulseScales.Add(BrightnessScale);
			}
		}
		return Piece;
	};

	const float Brightness = BaseIntensity * ((Mode == EWrecktideBeaconMode::BountyTurnIn) ? BountyScale : 1.0f);

	Beam = MakePiece(TEXT("Beam"), Cylinder, BeamMask, Brightness, 1.0f, BeamOpacity);
	if (Beam)
	{
		// Cylinder is 100 across and 100 tall about its centre: scale to the tunables and lift it so
		// the FOOT of the beam sits on the target, not its middle.
		const float Height = BeamHeight * Scale;
		const float Radius = BeamRadius * Scale;
		Beam->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, Height / 100.0f));
		Beam->SetRelativeLocation(FVector(0.0f, 0.0f, Height * 0.5f));
	}

	Disc = MakePiece(TEXT("BaseDisc"), Plane, RingMask ? RingMask : GlowMask, Brightness * 0.8f, 0.8f, FMath::Min(1.0f, BeamOpacity * 1.15f));
	if (Disc)
	{
		const float Diameter = DiscDiameter * Scale;
		Disc->SetRelativeScale3D(FVector(Diameter / 100.0f, Diameter / 100.0f, 1.0f));
		Disc->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
	}

	Sparkles.Reset();
	for (int32 Index = 0; Index < SparkleCount; ++Index)
	{
		const FString Name = FString::Printf(TEXT("Sparkle%d"), Index);
		if (UStaticMeshComponent* Sparkle = MakePiece(*Name, Plane, GlowMask, Brightness * 1.4f, 1.4f, FMath::Min(1.0f, BeamOpacity * 1.2f)))
		{
			Sparkle->SetRelativeScale3D(FVector(0.34f * Scale, 0.34f * Scale, 1.0f));
			Sparkles.Add(Sparkle);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON built mode=%d beam=%d disc=%d sparkles=%d material=%d mids=%d cylinder=%d plane=%d beamMask=%d ringMask=%d glowMask=%d"),
		static_cast<int32>(Mode), Beam != nullptr, Disc != nullptr, Sparkles.Num(), BeaconMaterial != nullptr,
		PulsingMaterials.Num(), Cylinder != nullptr, Plane != nullptr, BeamMask != nullptr, RingMask != nullptr, GlowMask != nullptr);
	if (Beam)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON beam registered=%d visible=%d mesh=%s mat=%s scale=%s boundsR=%.0f"),
			Beam->IsRegistered() ? 1 : 0, Beam->IsVisible() ? 1 : 0,
			Beam->GetStaticMesh() ? *Beam->GetStaticMesh()->GetName() : TEXT("NONE"),
			Beam->GetMaterial(0) ? *Beam->GetMaterial(0)->GetName() : TEXT("NONE"),
			*Beam->GetRelativeScale3D().ToString(), Beam->Bounds.SphereRadius);
	}
}

void AWrecktideMissionBeacon::ApplyIntensity(float Multiplier)
{
	const float ModeScale = (Mode == EWrecktideBeaconMode::BountyTurnIn) ? BountyScale : 1.0f;
	for (int32 Index = 0; Index < PulsingMaterials.Num(); ++Index)
	{
		UMaterialInstanceDynamic* Dynamic = PulsingMaterials[Index];
		const float PieceScale = PulseScales.IsValidIndex(Index) ? PulseScales[Index] : 1.0f;
		if (Dynamic)
		{
			Dynamic->SetScalarParameterValue(TEXT("Intensity"), BaseIntensity * ModeScale * PieceScale * Multiplier);
		}
	}
}

void AWrecktideMissionBeacon::SetLit(bool bNewLit)
{
	// Going out is a FADE, not a pop (C33.3): the emissive falls to nothing over BeaconFadeSeconds
	// and only then does the actor hide. bLit flips immediately either way, so every probe and every
	// caller reads the logical state rather than the tail of an animation.
	if (bLit && !bNewLit && BeaconFadeSeconds > KINDA_SMALL_NUMBER)
	{
		bFadingOut = true;
		FadeRemaining = BeaconFadeSeconds;
	}
	else if (bNewLit)
	{
		bFadingOut = false;
		FadeRemaining = 0.0f;
	}

	bLit = bNewLit;
	SetActorHiddenInGame(!bNewLit && !bFadingOut);
	if (!bNewLit)
	{
		TargetRow = NAME_None;
	}
}

void AWrecktideMissionBeacon::ClearReach()
{
	// The reach latch outlives its destination otherwise (review 2026-09-07): a repeatable bounty
	// turned in at the board comes back under the SAME row name at the SAME spot, and a latch that
	// only dies on a row change or a 2,500 uu move would keep that beam dark for the whole session.
	// Every "there is no destination right now" path clears it, so a re-accepted errand starts fresh.
	bReached = false;
	ReachedRow = NAME_None;
	bArmed = false;
}

void AWrecktideMissionBeacon::RefreshTarget()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (!PlayerState)
	{
		SetLit(false);
		return;
	}

	FVector Target = FVector::ZeroVector;
	FName NewRow;

	if (Mode == EWrecktideBeaconMode::Mission)
	{
		const UGameInstance* GameInstance = World->GetGameInstance();
		const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
		NewRow = WrecktideMissionLogic::GetCurrentMissionRow(PlayerState->GetProgress(), Data);

		// STICKY ANCHOR (C33 refinement). A kill objective names a MAN and the beam stands on his post
		// until he is dead: without this the nearest-living rule re-picks every second, so walking
		// between two posts of the same row makes the beam hop, which is the moving light again.
		const ACombatEnemy* Preferred = (NewRow == AnchorRow) ? AnchorEnemy.Get() : nullptr;
		if (const ACombatEnemy* KillTarget = WrecktideMap::ResolveMissionKillTarget(this, PlayerState, Preferred))
		{
			AnchorEnemy = KillTarget;
			AnchorRow = NewRow;
			// His POST, not the man (C33.1). A home of exactly zero means BeginPlay has not captured it
			// yet, and the world origin would be worse than pointing at him, so fall back.
			const FVector Home = KillTarget->GetHomeLocation();
			Target = Home.IsNearlyZero() ? KillTarget->GetActorLocation() : Home;
		}
		else
		{
			// Not a kill objective, or nothing of the row is alive: the resolver owns the answer.
			AnchorEnemy = nullptr;
			AnchorRow = NAME_None;
			FString Label;
			// ANCHOR, not Live: the radar diamond, the edge arrow and the HUD metres line still ask for
			// Live and still track the man.
			if (!WrecktideMap::ResolveMissionTarget(this, PlayerState, Target, Label, WrecktideMap::EMissionTargetFlavour::Anchor))
			{
				ClearReach();
				SetLit(false);
				return;
			}
		}
	}
	else
	{
		// The bounty beam only lights once the kills are in: before that the board is not the answer,
		// the enemies are, and two beams at once would say nothing.
		const UProgressComponent* Progress = PlayerState->GetProgress();
		const UGameInstance* GameInstance = World->GetGameInstance();
		const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
		const FName Active = WrecktideQuestLogic::GetActiveQuest(Progress, Data);
		if (Active.IsNone() || !WrecktideQuestLogic::IsReadyToTurnIn(Progress, Data, Active))
		{
			ClearReach();
			SetLit(false);
			return;
		}
		bool bFound = false;
		for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
		{
			Target = It->GetActorLocation();
			bFound = true;
			break;
		}
		if (!bFound)
		{
			ClearReach();
			SetLit(false);
			return;
		}
		NewRow = Active;
	}

	// Drop the beam onto the ground under the target so a beacon on a floating actor still reads as
	// standing on the island. Visibility trace only, so open water cannot answer as a floor.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BeaconGround), false, this);
	if (World->LineTraceSingleByChannel(Hit, Target + FVector(0.0f, 0.0f, 400.0f), Target - FVector(0.0f, 0.0f, 1200.0f), ECC_Visibility, Params))
	{
		Target.Z = Hit.ImpactPoint.Z;
	}

	LastAnchor = Target;

	// THE REACH RULE (C33.3, C33.4). The beam is a "go there", not a tracker: once the player has
	// stood on the anchor it goes out and STAYS out for that row at that post. Walking away never
	// relights it; only a new tracked row, or an anchor that has moved to another post further than
	// BeaconRearmDistance, counts as a new destination. Play test note, 2026-09-07: "once I get near the sky
	// light then it should go away, it doesnt need to stay on the whole time".
	if (bReached && NewRow == ReachedRow && FVector::Dist2D(Target, ReachedAnchor) <= BeaconRearmDistance)
	{
		SetLit(false);
		return;
	}
	if (bReached)
	{
		// A new destination: forget the arrival and start listening for the next one.
		bReached = false;
		ReachedRow = NAME_None;
		bArmed = false;
	}
	if (NewRow != TargetRow)
	{
		bArmed = false;
	}

	const APawn* PlayerPawn = PlayerController->GetPawn();
	if (PlayerPawn)
	{
		const float PlayerDistance = FVector::Dist2D(PlayerPawn->GetActorLocation(), Target);
		if (PlayerDistance > BeaconReachRadius)
		{
			// He has seen the beam from outside, so an arrival now means something. Without this
			// arming step a mission handed out on top of its own target would never light at all.
			bArmed = true;
		}
		else if (bArmed)
		{
			bReached = true;
			ReachedRow = NewRow;
			ReachedAnchor = Target;
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON reached row=%s at X=%.0f Y=%.0f Z=%.0f distance=%.0f"),
				*NewRow.ToString(), Target.X, Target.Y, Target.Z, PlayerDistance);
			SetLit(false);
			return;
		}
	}

	if (!bLit || NewRow != TargetRow || !GetActorLocation().Equals(Target, 1.0f))
	{
		SetActorLocation(Target);
	}
	if (!bLit || NewRow != TargetRow)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BEACON mode=%d row=%s at X=%.0f Y=%.0f Z=%.0f"),
			static_cast<int32>(Mode), *NewRow.ToString(), Target.X, Target.Y, Target.Z);
	}
	TargetRow = NewRow;
	SetLit(true);
}

void AWrecktideMissionBeacon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	RetargetCountdown -= DeltaSeconds;
	if (RetargetCountdown <= 0.0f)
	{
		RetargetCountdown = FMath::Max(0.1f, RetargetSeconds);
		RefreshTarget();
	}

	// The half second after the beam is spent: still on screen, emissive on its way to nothing.
	if (bFadingOut)
	{
		FadeRemaining -= DeltaSeconds;
		const float FadeAlpha = (BeaconFadeSeconds > KINDA_SMALL_NUMBER)
			? FMath::Clamp(FadeRemaining / BeaconFadeSeconds, 0.0f, 1.0f) : 0.0f;
		ApplyIntensity(FadeAlpha * PulseMax);
		if (FadeRemaining <= 0.0f)
		{
			bFadingOut = false;
			FadeRemaining = 0.0f;
			SetActorHiddenInGame(true);
		}
	}

	if (!bLit)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// Alpha pulse: PulseMin to PulseMax over PulseSeconds, a slow breath rather than a blink.
	const float Phase = (PulseSeconds > KINDA_SMALL_NUMBER) ? (Now / PulseSeconds) * 2.0f * PI : 0.0f;
	const float Pulse = FMath::Lerp(PulseMin, PulseMax, 0.5f + 0.5f * FMath::Sin(Phase));
	const float Scale = (Mode == EWrecktideBeaconMode::BountyTurnIn) ? BountyScale : 1.0f;
	ApplyIntensity(Pulse);

	// Sparkles orbit the foot of the beam and bob, and each one turns its face to the camera so a
	// flat plane never disappears edge on. Cheap: seven SetRelative calls on a 1 to 2 actor scene.
	FVector CameraLocation = GetActorLocation() + FVector(0.0f, 0.0f, 500.0f);
	if (World)
	{
		if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(const_cast<UWorld*>(World), 0))
		{
			FRotator CameraRotation;
			PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
		}
	}

	for (int32 Index = 0; Index < Sparkles.Num(); ++Index)
	{
		UStaticMeshComponent* Sparkle = Sparkles[Index];
		if (!Sparkle)
		{
			continue;
		}
		const float Offset = (2.0f * PI * Index) / FMath::Max(1, Sparkles.Num());
		const float Angle = Now * 0.9f + Offset;
		const float Height = 30.0f + 150.0f * (0.5f + 0.5f * FMath::Sin(Now * 1.3f + Offset * 1.7f));
		const FVector Local(
			FMath::Cos(Angle) * SparkleOrbitRadius * Scale,
			FMath::Sin(Angle) * SparkleOrbitRadius * Scale,
			Height * Scale);
		Sparkle->SetRelativeLocation(Local);

		const FVector ToCamera = (CameraLocation - (GetActorLocation() + Local)).GetSafeNormal();
		if (!ToCamera.IsNearlyZero())
		{
			Sparkle->SetWorldRotation(FRotationMatrix::MakeFromZ(ToCamera).Rotator());
		}
	}
}

void AWrecktideMissionBeacon::SpawnBeacons(UWorld* World)
{
	if (!World || !World->GetAuthGameMode())
	{
		return;
	}

	// Idempotent: a second call (a reloaded level, a probe re-arming) must not stack beams.
	int32 Existing = 0;
	for (TActorIterator<AWrecktideMissionBeacon> It(World); It; ++It)
	{
		++Existing;
	}
	if (Existing > 0)
	{
		return;
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Deferred on purpose: SpawnActor runs BeginPlay immediately in a world that has already begun,
	// and BeginPlay builds the visuals off Mode, so a plain spawn would build both beacons full size.
	const FTransform Origin = FTransform::Identity;
	auto SpawnOne = [World, &Parameters, &Origin](EWrecktideBeaconMode NewMode)
	{
		AWrecktideMissionBeacon* Beacon = World->SpawnActorDeferred<AWrecktideMissionBeacon>(
			AWrecktideMissionBeacon::StaticClass(), Origin, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Beacon)
		{
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-BEACON spawn failed mode=%d"), static_cast<int32>(NewMode));
			return;
		}
		Beacon->Mode = NewMode;
		Beacon->FinishSpawning(Origin);
	};

	SpawnOne(EWrecktideBeaconMode::Mission);
	SpawnOne(EWrecktideBeaconMode::BountyTurnIn);
}

AWrecktideMissionBeacon* AWrecktideMissionBeacon::FindBeacon(const UWorld* World, EWrecktideBeaconMode InMode)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AWrecktideMissionBeacon> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->GetMode() == InMode)
		{
			return *It;
		}
	}
	return nullptr;
}
