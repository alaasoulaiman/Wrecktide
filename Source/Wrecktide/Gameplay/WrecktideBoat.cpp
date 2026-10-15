// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideBoat.h"
#include "Systems/WrecktideAudio.h"

#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Systems/ProgressComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

namespace
{
/** The Synty small hull is 288 x 850 uu with its long axis on Y; the pawn's forward is X. */
constexpr float HullHalfLength = 420.0f;
constexpr float HullHalfWidth = 145.0f;
constexpr float HullHalfHeight = 60.0f;
/** Keel sits this far below the water line so the hull reads as floating, not hovering. */
constexpr float HullMeshZ = -35.0f;
/** Lift above the landing point so the pirate's capsule never spawns intersecting the deck. */
constexpr float CapsuleClearance = 110.0f;
/** Foam sprites in the trail. Sixteen at FoamDropSeconds 0.09 covers FoamLifeSeconds 2.2 with
 *  room to spare, so the oldest is always long dead before its slot is needed again. */
constexpr int32 FoamSpritePoolSize = 16;
/** The engine plane is 100 uu square, so a sprite's scale is its width over this. */
constexpr float FoamPlaneSizeUU = 100.0f;
/** Parameter names on M_WrecktideBeacon (see Tools\map_capture.py: unlit translucent, two sided,
 *  Emissive = BeamColour x Intensity, Opacity = Mask.R x OpacityScale). */
const FName FoamMaskParam(TEXT("Mask"));
const FName FoamColourParam(TEXT("BeamColour"));
const FName FoamIntensityParam(TEXT("Intensity"));
const FName FoamOpacityParam(TEXT("OpacityScale"));
/** Foam is white water with a touch of the sky in it, and unlit, so intensity 1 is plain white. */
const FLinearColor FoamColour(0.86f, 0.94f, 1.00f);
constexpr float FoamIntensity = 1.0f;
/** Probe pin for the wind. TOptional so "nothing pinned it" stays distinct from a real 0 degrees. */
TOptional<float> GWindToYawOverride;
}

namespace WrecktideSea
{
const FName SupplyPlanks(TEXT("Planks"));
const FName SupplyShot(TEXT("Shot"));
const FName UpgradeTimbers(TEXT("Timbers"));
const FName UpgradeCanvas(TEXT("Canvas"));
const FName UpgradeHold(TEXT("Hold"));
}

float WrecktideWind::ToYaw(const UObject* WorldContext)
{
	if (GWindToYawOverride.IsSet())
	{
		return GWindToYawOverride.GetValue();
	}
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const float Time = World ? World->GetTimeSeconds() : 0.0f;
	// One slow swing, no state: every reader gets the same answer for the same world time.
	return FRotator::ClampAxis(BaseToYaw + DriftDegrees * FMath::Sin(2.0f * PI * Time / DriftPeriodSeconds));
}

float WrecktideWind::SpeedFactor(float HeadingYaw, float WindToYaw)
{
	// Cosine of the angle between the heading and the way the wind blows: +1 running with it, 0 across
	// it, -1 dead into it. Two straight lines through the three authored points, so the numbers the
	// spec names are exactly the numbers a probe measures at the ends.
	const float Cosine = FMath::Cos(FMath::DegreesToRadians(FRotator::NormalizeAxis(WindToYaw - HeadingYaw)));
	return Cosine >= 0.0f
		? FMath::Lerp(AcrossFactor, RunningFactor, Cosine)
		: FMath::Lerp(AcrossFactor, IntoFactor, -Cosine);
}

float WrecktideWind::RudderFactor(float HeadingYaw, float WindToYaw)
{
	// Sailing into it costs steering authority more than raw speed (research B4). Never below the
	// authored floor, and never a stall.
	const float Cosine = FMath::Cos(FMath::DegreesToRadians(FRotator::NormalizeAxis(WindToYaw - HeadingYaw)));
	return Cosine >= 0.0f ? 1.0f : FMath::Lerp(1.0f, IntoRudderFactor, -Cosine);
}

FString WrecktideWind::Compass(float WindToYaw)
{
	// A wind is named for where it comes FROM, so the label is half a turn from the blowing direction.
	// World +X is north and +Y is east, the map's own convention.
	static const TCHAR* Points[] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
	const float FromYaw = FRotator::ClampAxis(WindToYaw + 180.0f);
	const int32 Index = FMath::RoundToInt(FromYaw / 45.0f) % 8;
	return Points[Index];
}

void WrecktideWind::SetOverride(float ToYawDegrees)
{
	GWindToYawOverride = FRotator::ClampAxis(ToYawDegrees);
}

void WrecktideWind::ClearOverride()
{
	GWindToYawOverride.Reset();
}

float WrecktideHelm::StepWheelAngle(float CurrentDegrees, float SteerInput, float MaxTurnDegrees, float TurnRateDegPerSecond, float DeltaSeconds)
{
	const float Target = FMath::Clamp(SteerInput, -1.0f, 1.0f) * FMath::Max(MaxTurnDegrees, 0.0f);
	// A CONSTANT rate, not an ease: a wheel is turned by hands, at the speed hands turn it, and a
	// constant rate is also the only shape a probe can assert a recentring time against.
	return FMath::FInterpConstantTo(CurrentDegrees, Target, DeltaSeconds, FMath::Max(TurnRateDegPerSecond, 1.0f));
}

bool WrecktideHelm::FitHelmPost(UStaticMeshComponent* Post, float DeckZ, float WheelBottomZ, float PostWidthUU)
{
	const UStaticMesh* PostAsset = Post ? Post->GetStaticMesh() : nullptr;
	if (!PostAsset)
	{
		return false;
	}
	const FBox PostBox = PostAsset->GetBoundingBox();
	const FVector PostSize = PostBox.GetSize();
	const float Gap = WheelBottomZ - DeckZ;
	if (Gap <= 1.0f || PostSize.Z <= 1.0f)
	{
		return false;
	}
	const float ScaleZ = Gap / PostSize.Z;
	const float Widest = FMath::Max(PostSize.X, PostSize.Y);
	const float ScaleXY = Widest > 1.0f ? FMath::Max(PostWidthUU, 1.0f) / Widest : 1.0f;
	Post->SetRelativeScale3D(FVector(ScaleXY, ScaleXY, ScaleZ));
	// Its BOTTOM sits on the deck whatever the artist put the pivot at, which is why the box minimum
	// is subtracted rather than assumed to be zero.
	const FVector Placed = Post->GetRelativeLocation();
	Post->SetRelativeLocation(FVector(Placed.X, Placed.Y, DeckZ - PostBox.Min.Z * ScaleZ));
	return true;
}

AWrecktideBoat::AWrecktideBoat()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	// the boat never spawns a default controller; the player brings theirs when boarding
	AutoPossessAI = EAutoPossessAI::Disabled;

	Hull = CreateDefaultSubobject<UBoxComponent>(TEXT("Hull"));
	Hull->SetBoxExtent(FVector(HullHalfLength, HullHalfWidth, HullHalfHeight));
	// Pawn profile: blocks the beach tiers, the surf walls, the pier and other pawns; the water body
	// itself only overlaps, and floating movement has no gravity, so the hull never sinks
	Hull->SetCollisionProfileName(TEXT("Pawn"));
	Hull->SetCanEverAffectNavigation(false);
	RootComponent = Hull;

	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(Hull);
	HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HullMesh->SetCanEverAffectNavigation(false);
	// the mesh's bow runs along its own +Y; yaw it onto the pawn's forward
	HullMesh->SetRelativeLocation(FVector(0.0f, 0.0f, HullMeshZ));
	HullMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HullAsset(TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Boat_Small_01_Hull_Pirate.SM_Veh_Boat_Small_01_Hull_Pirate"));
	if (HullAsset.Succeeded())
	{
		HullMesh->SetStaticMesh(HullAsset.Object);
	}

	// rig: the medium ship's mast and furled-up sail at 0.45 scale (there is no small-boat mast in
	// the pack); they share the hull's pivot convention (long axis on +Y), so the same yaw applies
	auto MakeRigPart = [this](const TCHAR* Name, const TCHAR* AssetPath, float Scale, const FVector& Offset) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(HullMesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetRelativeLocation(Offset);
		Part->SetRelativeScale3D(FVector(Scale, Scale, Scale));
		ConstructorHelpers::FObjectFinder<UStaticMesh> Asset(AssetPath);
		if (Asset.Succeeded())
		{
			Part->SetStaticMesh(Asset.Object);
		}
		return Part;
	};
	MastMesh = MakeRigPart(TEXT("MastMesh"), TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Boat_Medium_01_Mast.SM_Veh_Boat_Medium_01_Mast"), 0.45f, FVector(0.0f, -40.0f, 40.0f));
	SailMesh = MakeRigPart(TEXT("SailMesh"), TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Boat_Medium_01_Sails_Pirate.SM_Veh_Boat_Medium_01_Sails_Pirate"), 0.45f, FVector(0.0f, -40.0f, 40.0f));
	FlagMesh = MakeRigPart(TEXT("FlagMesh"), TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Flag_Pirate_01.SM_Veh_Flag_Pirate_01"), 1.0f, FVector(0.0f, -40.0f, 40.0f + 0.45f * 1820.0f));

	// Stern foam (spec S1.6, re-cut 2026-09-06). A fixed pool of flat quads, laid in the water behind
	// her and left there to fade. The pool size is a construction-time constant because
	// CreateDefaultSubobject runs before any property override could be read, and a pool that never
	// grows is also what keeps the trail free of allocation while she sails.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FoamPlane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	FoamSprites.Reserve(FoamSpritePoolSize);
	for (int32 FoamIndex = 0; FoamIndex < FoamSpritePoolSize; ++FoamIndex)
	{
		UStaticMeshComponent* Sprite = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("FoamSprite%d"), FoamIndex));
		Sprite->SetupAttachment(Hull);
		Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprite->SetCollisionProfileName(TEXT("NoCollision"));
		Sprite->SetGenerateOverlapEvents(false);
		Sprite->SetCanEverAffectNavigation(false);
		Sprite->bCastDynamicShadow = false;
		Sprite->CastShadow = false;
		// ABSOLUTE transforms: a wake is water, not rigging. Attached normally it would be towed
		// along at the stern and read as a pair of skirts rather than a trail.
		Sprite->SetUsingAbsoluteLocation(true);
		Sprite->SetUsingAbsoluteRotation(true);
		Sprite->SetUsingAbsoluteScale(true);
		Sprite->SetVisibility(false);
		if (FoamPlane.Succeeded())
		{
			Sprite->SetStaticMesh(FoamPlane.Object);
		}
		FoamSprites.Add(Sprite);
	}

	// The helm (spec C28.1). The pivot carries the spin, the mesh carries the artist's own axes, so
	// the axle is the hull's forward axis whatever the prop was authored facing.
	HelmPivot = CreateDefaultSubobject<USceneComponent>(TEXT("HelmPivot"));
	HelmPivot->SetupAttachment(Hull);
	HelmWheelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HelmWheelMesh"));
	HelmWheelMesh->SetupAttachment(HelmPivot);
	HelmWheelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmWheelMesh->SetCollisionProfileName(TEXT("NoCollision"));
	HelmWheelMesh->SetGenerateOverlapEvents(false);
	HelmWheelMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WheelAsset01(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_ShipWheel_01.SM_Prop_ShipWheel_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WheelAsset02(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_ShipWheel_02.SM_Prop_ShipWheel_02"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WheelAsset03(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_ShipWheel_03.SM_Prop_ShipWheel_03"));
	HelmWheelChoices.Add(WheelAsset01.Succeeded() ? WheelAsset01.Object : nullptr);
	HelmWheelChoices.Add(WheelAsset02.Succeeded() ? WheelAsset02.Object : nullptr);
	HelmWheelChoices.Add(WheelAsset03.Succeeded() ? WheelAsset03.Object : nullptr);
	// The post is on the HULL, not the pivot: a pedestal that turned with the wheel would be a drill.
	HelmPostMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HelmPostMesh"));
	HelmPostMesh->SetupAttachment(Hull);
	HelmPostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmPostMesh->SetCollisionProfileName(TEXT("NoCollision"));
	HelmPostMesh->SetGenerateOverlapEvents(false);
	HelmPostMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HelmPostAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Pole_01.SM_Prop_Pole_01"));
	if (HelmPostAsset.Succeeded())
	{
		HelmPostMesh->SetStaticMesh(HelmPostAsset.Object);
	}

	BoardTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("BoardTrigger"));
	BoardTrigger->SetupAttachment(Hull);
	BoardTrigger->SetSphereRadius(650.0f);
	BoardTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoardTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoardTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BoardTrigger->SetCanEverAffectNavigation(false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Hull);
	CameraBoom->TargetArmLength = 950.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 320.0f);
	CameraBoom->SetRelativeRotation(FRotator(-16.0f, 0.0f, 0.0f));
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 6.0f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 5.0f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// The movement component is kept for its UpdatedComponent, its swept move helpers and for
	// APawn::GetVelocity, but its TICK is off: the sail model in TickSail owns speed, and a component
	// that also braked and moved the hull every frame would double the move. The boat stays kinematic
	// by design (research B4): a Chaos body drifts and jitters where a saved position must restore
	// exactly, and every documented Buoyancy Component failure would be ours to chase for no gain.
	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 1200.0f;
	Movement->Acceleration = 320.0f;
	Movement->Deceleration = 420.0f;
	Movement->TurningBoost = 0.0f;
	Movement->PrimaryComponentTick.bCanEverTick = false;
	Movement->PrimaryComponentTick.bStartWithTickEnabled = false;
}

void AWrecktideBoat::BeginPlay()
{
	Super::BeginPlay();
	if (BoardTrigger)
	{
		BoardTrigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideBoat::OnTriggerBeginOverlap);
		BoardTrigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideBoat::OnTriggerEndOverlap);
	}
	MooringLocation = GetActorLocation();
	MooringRotation = GetActorRotation();
	if (FollowCamera)
	{
		BaseCameraFov = FollowCamera->FieldOfView;
	}

	// The helm (spec C28.1 and C28.4). The three candidate props are measured into the log so the
	// choice can be made from bounds and a capture; the chosen one is stood upright on the pivot at
	// the stern, and the chase boom is pulled back and up to frame the pirate now standing there.
	for (int32 WheelIndex = 0; WheelIndex < HelmWheelChoices.Num(); ++WheelIndex)
	{
		const UStaticMesh* Candidate = HelmWheelChoices[WheelIndex];
		const FBox CandidateBox = Candidate ? Candidate->GetBoundingBox() : FBox(ForceInit);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM wheel candidate %d name=%s size=%s min=%s max=%s"),
			WheelIndex, Candidate ? *Candidate->GetName() : TEXT("None"),
			*CandidateBox.GetSize().ToString(), *CandidateBox.Min.ToString(), *CandidateBox.Max.ToString());
	}
	if (HelmPivot)
	{
		HelmPivot->SetRelativeLocation(HelmOffset);
		HelmPivot->SetRelativeRotation(FRotator::ZeroRotator);
	}
	if (HelmWheelMesh)
	{
		UStaticMesh* ChosenWheel = HelmWheelChoices.IsValidIndex(HelmWheelChoice) ? HelmWheelChoices[HelmWheelChoice].Get() : nullptr;
		HelmWheelMesh->SetStaticMesh(ChosenWheel);
		HelmWheelMesh->SetRelativeRotation(HelmWheelBaseRotation);
		HelmWheelMesh->SetRelativeScale3D(FVector(HelmWheelScale));
		HelmWheelMesh->SetVisibility(ChosenWheel != nullptr);
		if (!ChosenWheel)
		{
			// Fail closed and say so: an invisible helm is a missing feature, not a crash.
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HELM-WIRING wheel mesh %d did not load; the helm will be empty"), HelmWheelChoice);
		}
	}
	if (HelmPostMesh)
	{
		// The wheel's own bounds decide where the post stops, so a different wheel or a different
		// helm height needs no second number changed anywhere.
		const UStaticMesh* ChosenWheelAsset = HelmWheelMesh ? HelmWheelMesh->GetStaticMesh() : nullptr;
		const float WheelRadiusZ = ChosenWheelAsset ? ChosenWheelAsset->GetBoundingBox().Max.Z * HelmWheelScale : 0.0f;
		HelmPostMesh->SetRelativeLocation(FVector(HelmOffset.X, HelmOffset.Y, HelmPostBaseZ));
		const bool bPostFitted = WrecktideHelm::FitHelmPost(HelmPostMesh, HelmPostBaseZ, HelmOffset.Z - WheelRadiusZ, HelmPostWidthUU);
		HelmPostMesh->SetVisibility(bPostFitted);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM post fitted=%d mesh=%s deckZ=%.0f wheelBottomZ=%.0f scale=%s"),
			bPostFitted ? 1 : 0, HelmPostMesh->GetStaticMesh() ? *HelmPostMesh->GetStaticMesh()->GetName() : TEXT("None"),
			HelmPostBaseZ, HelmOffset.Z - WheelRadiusZ, *HelmPostMesh->GetRelativeScale3D().ToString());
	}
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = HelmBoomLength;
		CameraBoom->SocketOffset = FVector(0.0f, 0.0f, HelmBoomHeight);
		CameraBoom->SetRelativeRotation(FRotator(HelmBoomPitch, 0.0f, 0.0f));
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM ready wheel=%d mesh=%s helmLocal=%s riderLocal=%s gap=%.0f maxTurn=%.0f rate=%.0f boom=%.0f/%.0f/%.0f"),
		HelmWheelChoice,
		(HelmWheelMesh && HelmWheelMesh->GetStaticMesh()) ? *HelmWheelMesh->GetStaticMesh()->GetName() : TEXT("None"),
		*HelmOffset.ToString(), *RiderOffset.ToString(), FVector::Dist(HelmOffset, RiderOffset),
		HelmMaxWheelTurn, HelmWheelTurnRate, HelmBoomLength, HelmBoomHeight, HelmBoomPitch);

	// The sail starts furled at the mooring, so the rig matches the state from the first frame.
	if (SailMesh)
	{
		SailMesh->SetVisibility(false);
	}
	// Stern foam: one dynamic instance per sprite, all born expired so nothing shows until she moves.
	// Both assets are loaded BY STRING, the mission beacon's pattern, so a cook keeps them alive
	// through Config\DefaultGame.ini's /Game/UI folder rule and no hard reference is added here.
	UMaterialInterface* FoamMaterial = LoadObject<UMaterialInterface>(nullptr, *FoamMaterialPath);
	UTexture* FoamMask = LoadObject<UTexture>(nullptr, *FoamMaskPath);
	FoamAgeSeconds.Init(FoamLifeSeconds, FoamSprites.Num());
	FoamMaterials.Reset();
	for (UStaticMeshComponent* Sprite : FoamSprites)
	{
		if (!Sprite)
		{
			continue;
		}
		Sprite->SetWorldScale3D(FVector(FoamSpriteSize / FoamPlaneSizeUU));
		Sprite->SetVisibility(false);
		UMaterialInstanceDynamic* FoamInstance = FoamMaterial ? UMaterialInstanceDynamic::Create(FoamMaterial, this) : nullptr;
		if (FoamInstance)
		{
			if (FoamMask)
			{
				FoamInstance->SetTextureParameterValue(FoamMaskParam, FoamMask);
			}
			FoamInstance->SetVectorParameterValue(FoamColourParam, FoamColour);
			FoamInstance->SetScalarParameterValue(FoamIntensityParam, FoamIntensity);
			FoamInstance->SetScalarParameterValue(FoamOpacityParam, 0.0f);
			Sprite->SetMaterial(0, FoamInstance);
		}
		FoamMaterials.Add(FoamInstance);
	}
	bFoamReady = FoamMaterial != nullptr && FoamMask != nullptr && FoamSprites.Num() > 0;
	if (!bFoamReady)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAIL-WIRING foam material=%d mask=%d sprites=%d, the wake will not draw"),
			FoamMaterial != nullptr, FoamMask != nullptr, FoamSprites.Num());
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL ready foamSprites=%d size=%.0fuu alpha=%.2f life=%.1fs windToYaw=%.0f wind=%s"),
		FoamSprites.Num(), FoamSpriteSize, FoamAlpha, FoamLifeSeconds, WrecktideWind::ToYaw(this),
		*WrecktideWind::Compass(WrecktideWind::ToYaw(this)));

	// Diagnostic truth for the visual harness: where the hull actually renders, not where the actor is.
	const FBoxSphereBounds HullBounds = HullMesh ? HullMesh->Bounds : FBoxSphereBounds(ForceInit);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT ready at %s hullMesh=%s visible=%d hullWorld=%s hullBoundsOrigin=%s extent=%s collision=%s enabled=%d pawnResponse=%d physicsState=%d mast=%s"),
		*GetActorLocation().ToString(),
		(HullMesh && HullMesh->GetStaticMesh()) ? *HullMesh->GetStaticMesh()->GetName() : TEXT("None"),
		(HullMesh && HullMesh->IsVisible()) ? 1 : 0,
		HullMesh ? *HullMesh->GetComponentLocation().ToString() : TEXT("None"),
		*HullBounds.Origin.ToString(), *HullBounds.BoxExtent.ToString(),
		Hull ? *Hull->GetCollisionProfileName().ToString() : TEXT("None"),
		Hull ? static_cast<int32>(Hull->GetCollisionEnabled()) : -1,
		Hull ? static_cast<int32>(Hull->GetCollisionResponseToChannel(ECC_Pawn)) : -1,
		(Hull && Hull->IsPhysicsStateCreated()) ? 1 : 0,
		(MastMesh && MastMesh->GetStaticMesh()) ? *MastMesh->GetStaticMesh()->GetName() : TEXT("None"));
}

void AWrecktideBoat::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoardTrigger)
	{
		BoardTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideBoat::OnTriggerBeginOverlap);
		BoardTrigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideBoat::OnTriggerEndOverlap);
	}
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
		{
			HUD->ClearInteractionPrompt();
		}
	}
	Super::EndPlay(EndPlayReason);
}

ACombatCharacter* AWrecktideBoat::GetRider() const
{
	return Rider.Get();
}

bool AWrecktideBoat::IsCharacterInRange(const ACombatCharacter* Character) const
{
	if (!Character)
	{
		return false;
	}
	return FVector::DistSquared2D(Character->GetActorLocation(), GetActorLocation()) <= FMath::Square(BoardRange)
		|| IsPointWithinHullRange(Character->GetActorLocation(), 400.0f);
}

FVector AWrecktideBoat::GetHelmWheelWorldLocation() const
{
	if (HelmWheelMesh)
	{
		return HelmWheelMesh->GetComponentLocation();
	}
	return HelmPivot ? HelmPivot->GetComponentLocation() : GetActorLocation();
}

FVector AWrecktideBoat::GetHelmWheelLocalOffset() const
{
	return GetActorTransform().InverseTransformPosition(GetHelmWheelWorldLocation());
}

FVector AWrecktideBoat::GetHullBoxExtent() const
{
	return Hull ? Hull->GetUnscaledBoxExtent() : FVector::ZeroVector;
}

bool AWrecktideBoat::HasHelmWheel() const
{
	return HelmWheelMesh != nullptr && HelmWheelMesh->GetStaticMesh() != nullptr;
}

bool AWrecktideBoat::IsHelmGripPlaying() const
{
	const ACombatCharacter* CurrentRider = Rider.Get();
	return CurrentRider != nullptr && CurrentRider->IsHelmGripPlaying();
}

bool AWrecktideBoat::IsPointWithinHullRange(const FVector& Point, float Range) const
{
	if (!Hull || Range < 0.0f)
	{
		return false;
	}
	const FBox Bounds = Hull->Bounds.GetBox();
	const FVector Closest(
		FMath::Clamp(Point.X, Bounds.Min.X, Bounds.Max.X),
		FMath::Clamp(Point.Y, Bounds.Min.Y, Bounds.Max.Y),
		Point.Z);
	return FVector::DistSquared2D(Point, Closest) <= FMath::Square(Range);
}

void AWrecktideBoat::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	// the ONE player controller, never Character->GetController(): a respawned pawn overlaps before it
	// is possessed and a dying pawn is detached before EndOverlap (audit 2026-09-04)
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!Character || !PlayerController || !Character->ActorHasTag(FName("Player")) || IsOccupied())
	{
		return;
	}
	DocksideCharacter = Character;
	// dockside F lives on THIS actor's input component so the pirate's own F (shop / board) keeps
	// working everywhere else; the binding consumes so both never fire at once
	EnableInput(PlayerController);
	if (InputComponent && InputComponent->KeyBindings.Num() == 0)
	{
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideBoat::OnBoardKey).bConsumeInput = true;
	}
	ShowToast(TEXT("Press F to board the boat"));
}

void AWrecktideBoat::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor != DocksideCharacter.Get() || IsOccupied())
	{
		return;
	}
	// unconditional: a dying pawn has no controller any more, and a leaked dockside input component
	// would swallow F everywhere for the rest of the session
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}
	DocksideCharacter = nullptr;
}

void AWrecktideBoat::OnBoardKey()
{
	Board(DocksideCharacter.Get());
}

void AWrecktideBoat::OnAshoreKey()
{
	// At the pier F still goes ashore on the press, exactly as it always did. Only when there is no
	// landing spot (which is to say, at sea) does F start the hull patch, so nothing a player learned
	// on the pier changes meaning.
	FVector Unused;
	if (FindAshoreSpot(Unused))
	{
		GoAshore();
		return;
	}
	BeginRepairHold();
}

void AWrecktideBoat::OnAshoreKeyReleased()
{
	EndRepairHold();
}

void AWrecktideBoat::BeginRepairHold()
{
	if (!Rider.IsValid() || bRepairHolding)
	{
		return;
	}
	if (GetHullHealth() >= GetHullMax())
	{
		ShowToast(TEXT("The hull is sound."));
		return;
	}
	if (GetHoldUnits(WrecktideSea::SupplyPlanks) <= 0)
	{
		ShowToast(TEXT("No planks in the hold."));
		return;
	}
	bRepairHolding = true;
	RepairHeldSeconds = 0.0f;
	ShowToast(TEXT("Hold F to patch the hull"));
}

void AWrecktideBoat::EndRepairHold()
{
	bRepairHolding = false;
	RepairHeldSeconds = 0.0f;
}

void AWrecktideBoat::CompleteRepair()
{
	if (ConsumeFromHold(WrecktideSea::SupplyPlanks, 1) <= 0)
	{
		EndRepairHold();
		return;
	}
	const int32 Healed = RepairHull(PlankRepairAmount);
	EndRepairHold();
	ShowToast(FString::Printf(TEXT("Hull patched, +%d"), Healed));
	UWrecktideAudio::PlaySfx(this, TEXT("wood"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD repair healed=%d hull=%d/%d planks=%d"),
		Healed, GetHullHealth(), GetHullMax(), GetHoldUnits(WrecktideSea::SupplyPlanks));
}

bool AWrecktideBoat::Board(ACombatCharacter* NewRider)
{
	if (!NewRider || IsOccupied())
	{
		return false;
	}
	APlayerController* PlayerController = Cast<APlayerController>(NewRider->GetController());
	if (!PlayerController)
	{
		return false;
	}
	if (NewRider->Stats && !NewRider->Stats->IsAlive())
	{
		return false;
	}

	Rider = NewRider;
	UWrecktideAudio::PlaySfx(this, TEXT("wood"));
	DocksideCharacter = nullptr;

	// park the pirate: no movement, no collision, riding the hull. DisableMovement leaves the CMC in
	// MOVE_None, which is what a rider must be in - including a rider who swam out here, whose mode
	// would otherwise still be MOVE_Swimming (2026-09-06).
	if (UCharacterMovementComponent* RiderMovement = NewRider->GetCharacterMovement())
	{
		RiderMovement->StopMovementImmediately();
		RiderMovement->DisableMovement();
	}

	// a pirate boarding straight out of the water still carries the swim tilt: snap the rig upright
	// before he is parked on the deck, or he rides lying face down
	NewRider->ResetSwimPose();
	NewRider->SetActorEnableCollision(false);
	NewRider->AttachToComponent(Hull, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	NewRider->SetActorRelativeLocation(RiderOffset);
	NewRider->SetActorRelativeRotation(FRotator::ZeroRotator);

	// the dockside input component must go before possession: PawnClientRestart only builds the
	// aboard bindings (SetupPlayerInputComponent) when the pawn has NO input component yet
	DisableInput(PlayerController);
	DestroyPlayerInputComponent();

	// Read BEFORE the possession: the PlayerState follows the controller onto this pawn, so the
	// pirate answers null for it a line later (missions v0, 2026-09-04).
	AWrecktidePlayerState* RiderState = NewRider->GetPlayerState<AWrecktidePlayerState>();

	CurrentThrottle = 0.0f;
	CurrentSteer = 0.0f;
	LastThrottleRequest = 0;
	// Boarding always starts furled at whatever way she still carries, so a boat found drifting is
	// never handed over already under sail.
	SailState = EWrecktideSailState::Furled;
	bReverseCrawl = false;
	if (SailMesh)
	{
		SailMesh->SetVisibility(false);
	}
	PlayerController->Possess(this);
	// Hands on the wheel (spec C28.3). After ResetSwimPose, so the grip is never layered onto a rig
	// still carrying the swim tilt, and after the possession, so nothing in PawnClientRestart can
	// stop the montage a frame later.
	NewRider->SetHelmGrip(true);
	WrecktideMissionLogic::NotifyBoarded(RiderState);
	ShowToast(TEXT("W sets sail, S furls, A/D steer, F to go ashore"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT board rider=%s at %s"), *NewRider->GetName(), *GetActorLocation().ToString());
	return true;
}

bool AWrecktideBoat::FindAshoreSpot(FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// candidate spots: either side of the hull, then astern; the first with solid, roughly level
	// ground above the water line wins (the pier deck and the beach ring both qualify, open water
	// never does because the water body does not block visibility traces)
	const FVector Origin = GetActorLocation();
	const FVector Right = GetActorRightVector();
	const FVector Forward = GetActorForwardVector();
	// two reaches per side: moored at a rail the near reach is still water, the far one is the deck
	const FVector Candidates[] =
	{
		Origin + Right * (HullHalfWidth + 210.0f),
		Origin + Right * (HullHalfWidth + 340.0f),
		Origin - Right * (HullHalfWidth + 210.0f),
		Origin - Right * (HullHalfWidth + 340.0f),
		Origin + Forward * (HullHalfLength + 210.0f),
		Origin - Forward * (HullHalfLength + 210.0f),
	};
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoatAshore), false, this);
	if (Rider.IsValid())
	{
		Params.AddIgnoredActor(Rider.Get());
	}
	for (const FVector& Candidate : Candidates)
	{
		FHitResult Hit;
		const FVector Start = Candidate + FVector(0.0f, 0.0f, 260.0f);
		const FVector End = Candidate - FVector(0.0f, 0.0f, 120.0f);
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			continue;
		}
		if (Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.7f || Hit.ImpactPoint.Z < WaterZ + 40.0f)
		{
			continue;
		}
		// a ray finds the deck under a crate or a rail top just as happily as open planks: the
		// capsule itself must fit there, or the pirate gets collision back inside a prop (review 2026-09-04)
		const FVector Spot = Hit.ImpactPoint + FVector(0.0f, 0.0f, CapsuleClearance);
		if (!CapsuleFitsAt(Spot))
		{
			continue;
		}
		OutLocation = Spot;
		return true;
	}
	return false;
}

bool AWrecktideBoat::CapsuleFitsAt(const FVector& Location) const
{
	UWorld* World = GetWorld();
	const ACombatCharacter* CurrentRider = Rider.Get();
	if (!World || !CurrentRider || !CurrentRider->GetCapsuleComponent())
	{
		return false;
	}
	const UCapsuleComponent* Capsule = CurrentRider->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoatAshoreFit), false, this);
	Params.AddIgnoredActor(CurrentRider);
	// Pawn channel: the same blocking set the pirate walks against; the water body only overlaps it
	const bool bBlocked = World->OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
	return !bBlocked;
}

bool AWrecktideBoat::GoAshore()
{
	ACombatCharacter* CurrentRider = Rider.Get();
	if (!CurrentRider)
	{
		return false;
	}
	FVector Spot;
	if (!FindAshoreSpot(Spot))
	{
		ShowToast(TEXT("Steer to the pier or the beach first."));
		return false;
	}
	APlayerController* PlayerController = Cast<APlayerController>(GetController());

	// Hands off the wheel before anything else: leaving the boat restores the normal pose, and the
	// grip is an upper-body montage that would otherwise walk ashore with him (spec C28.3).
	CurrentRider->SetHelmGrip(false);
	CurrentRider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CurrentRider->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
	CurrentRider->SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	CurrentRider->SetActorEnableCollision(true);
	if (UCharacterMovementComponent* RiderMovement = CurrentRider->GetCharacterMovement())
	{
		RiderMovement->SetMovementMode(MOVE_Walking);
	}

	Rider = nullptr;
	EndRepairHold();
	CurrentThrottle = 0.0f;
	CurrentSteer = 0.0f;
	LastThrottleRequest = 0;
	SailState = EWrecktideSailState::Furled;
	bReverseCrawl = false;
	ForwardSpeed = 0.0f;
	LateralSpeed = 0.0f;
	if (SailMesh)
	{
		SailMesh->SetVisibility(false);
	}
	if (Movement)
	{
		Movement->StopMovementImmediately();
	}
	if (PlayerController)
	{
		PlayerController->Possess(CurrentRider);
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-BOAT ashore rider=%s at %s"), *CurrentRider->GetName(), *Spot.ToString());
	UWrecktideAudio::PlaySfx(this, TEXT("wood"));

	// the pirate is still inside the trigger: re-arm the dockside F without waiting for a new overlap
	if (IsCharacterInRange(CurrentRider))
	{
		FHitResult Unused;
		OnTriggerBeginOverlap(BoardTrigger, CurrentRider, nullptr, 0, false, Unused);
	}
	return true;
}

namespace
{
/** One shipwright tier's Step from Content\Data\BoatUpgrades.csv, or Fallback when the row is
 *  missing. The table is the tuning surface; the constants in WrecktideSea are the safety net so a
 *  missing CSV never silently zeroes an upgrade the player paid for. */
float BoatUpgradeStep(const AActor* Actor, FName TrackRow, float Fallback)
{
	const UGameInstance* GameInstance = Actor ? Actor->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FBoatUpgradeTableRow* Row = DataSubsystem ? DataSubsystem->GetBoatUpgradeRow(TrackRow) : nullptr;
	return (Row && Row->Step > 0.0f) ? Row->Step : Fallback;
}
}

UProgressComponent* AWrecktideBoat::GetBoatProgress() const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	AWrecktidePlayerState* RiderState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	return RiderState ? RiderState->GetProgress() : nullptr;
}

int32 AWrecktideBoat::GetHullMax() const
{
	const UProgressComponent* Progress = GetBoatProgress();
	const int32 Tiers = Progress ? Progress->GetBoatUpgradeTier(WrecktideSea::UpgradeTimbers) : 0;
	return HullBaseMax + Tiers * FMath::RoundToInt(BoatUpgradeStep(this, WrecktideSea::UpgradeTimbers, WrecktideSea::TimbersHullPerTier));
}

int32 AWrecktideBoat::GetHullHealth() const
{
	const UProgressComponent* Progress = GetBoatProgress();
	const int32 Max = GetHullMax();
	return Progress ? FMath::Clamp(Progress->GetBoatHull(Max), 0, Max) : Max;
}

void AWrecktideBoat::SetHullHealth(int32 NewHull)
{
	UProgressComponent* Progress = GetBoatProgress();
	if (!Progress)
	{
		return;
	}
	const int32 Clamped = FMath::Clamp(NewHull, 0, GetHullMax());
	Progress->SetBoatHull(Clamped);
	if (Clamped <= 0)
	{
		Sink();
	}
}

void AWrecktideBoat::ApplyHullDamage(int32 Amount, const FString& Reason)
{
	if (Amount <= 0)
	{
		return;
	}
	const int32 Before = GetHullHealth();
	SetHullHealth(Before - Amount);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD hull damage=%d before=%d after=%d reason=%s"),
		Amount, Before, GetHullHealth(), *Reason);
}

int32 AWrecktideBoat::RepairHull(int32 Amount)
{
	if (Amount <= 0)
	{
		return 0;
	}
	const int32 Before = GetHullHealth();
	const int32 After = FMath::Min(Before + Amount, GetHullMax());
	if (After == Before)
	{
		return 0;
	}
	SetHullHealth(After);
	return After - Before;
}

int32 AWrecktideBoat::GetHoldUnits(FName SupplyRow) const
{
	const UProgressComponent* Progress = GetBoatProgress();
	return Progress ? Progress->GetBoatHoldUnits(SupplyRow) : 0;
}

int32 AWrecktideBoat::GetHoldSlots() const
{
	const UProgressComponent* Progress = GetBoatProgress();
	const int32 Tiers = Progress ? Progress->GetBoatUpgradeTier(WrecktideSea::UpgradeHold) : 0;
	return HoldBaseSlots + Tiers * FMath::RoundToInt(BoatUpgradeStep(this, WrecktideSea::UpgradeHold, WrecktideSea::HoldSlotsPerTier));
}

int32 AWrecktideBoat::GetHoldSlotsUsed() const
{
	const UProgressComponent* Progress = GetBoatProgress();
	if (!Progress)
	{
		return 0;
	}
	const int32 PerSlot = FMath::Max(HoldUnitsPerSlot, 1);
	int32 Used = 0;
	for (const TPair<FName, int32>& Entry : Progress->GetBoatHold())
	{
		// Rounded up PER ROW: a part stack of planks still occupies the slot it sits in.
		Used += FMath::DivideAndRoundUp(FMath::Max(Entry.Value, 0), PerSlot);
	}
	return Used;
}

int32 AWrecktideBoat::GetHoldRoomFor(FName SupplyRow) const
{
	const int32 PerSlot = FMath::Max(HoldUnitsPerSlot, 1);
	const int32 Carried = GetHoldUnits(SupplyRow);
	const int32 SlotsThisRow = FMath::DivideAndRoundUp(Carried, PerSlot);
	const int32 FreeSlots = FMath::Max(GetHoldSlots() - GetHoldSlotsUsed(), 0);
	// Room in the part-filled slot this row already owns, plus every free slot.
	const int32 RoomInOwnSlot = SlotsThisRow * PerSlot - Carried;
	return RoomInOwnSlot + FreeSlots * PerSlot;
}

int32 AWrecktideBoat::AddToHold(FName SupplyRow, int32 Units)
{
	UProgressComponent* Progress = GetBoatProgress();
	if (!Progress || SupplyRow.IsNone() || Units <= 0)
	{
		return 0;
	}
	const int32 Taken = FMath::Min(Units, GetHoldRoomFor(SupplyRow));
	if (Taken <= 0)
	{
		return 0;
	}
	Progress->SetBoatHoldUnits(SupplyRow, GetHoldUnits(SupplyRow) + Taken);
	return Taken;
}

int32 AWrecktideBoat::ConsumeFromHold(FName SupplyRow, int32 Units)
{
	UProgressComponent* Progress = GetBoatProgress();
	if (!Progress || SupplyRow.IsNone() || Units <= 0)
	{
		return 0;
	}
	const int32 Carried = GetHoldUnits(SupplyRow);
	const int32 Taken = FMath::Min(Units, Carried);
	if (Taken <= 0)
	{
		return 0;
	}
	Progress->SetBoatHoldUnits(SupplyRow, Carried - Taken);
	return Taken;
}

float AWrecktideBoat::GetCanvasSpeedMultiplier() const
{
	const UProgressComponent* Progress = GetBoatProgress();
	const int32 Tiers = Progress ? Progress->GetBoatUpgradeTier(WrecktideSea::UpgradeCanvas) : 0;
	return 1.0f + Tiers * BoatUpgradeStep(this, WrecktideSea::UpgradeCanvas, WrecktideSea::CanvasSpeedPerTier);
}

float AWrecktideBoat::GetRamResistMultiplier() const
{
	const UProgressComponent* Progress = GetBoatProgress();
	const int32 Tiers = Progress ? Progress->GetBoatUpgradeTier(WrecktideSea::UpgradeTimbers) : 0;
	return FMath::Max(1.0f - Tiers * BoatUpgradeStep(this, WrecktideSea::UpgradeTimbers, WrecktideSea::TimbersRamResistPerTier), 0.0f);
}

void AWrecktideBoat::ApplyRamShove(const FVector& WorldDirection, float Distance)
{
	const FVector Flat = FVector(WorldDirection.X, WorldDirection.Y, 0.0f).GetSafeNormal();
	if (Flat.IsNearlyZero() || Distance <= 0.0f || !Movement)
	{
		return;
	}
	// Swept, so a shove into the pier or the beach stops at the geometry instead of through it.
	FHitResult Hit;
	Movement->SafeMoveUpdatedComponent(Flat * Distance, GetActorQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		Movement->SlideAlongSurface(Flat * Distance, 1.0f - Hit.Time, Hit.Normal, Hit);
	}
	// Most of her way goes with the hit; the same fraction a shore impact leaves her.
	ForwardSpeed *= ShoreImpactKeep;
	LateralSpeed = 0.0f;
}

void AWrecktideBoat::Sink(void)
{
	UProgressComponent* Progress = GetBoatProgress();
	if (Progress)
	{
		Progress->ClearBoatHold();
		Progress->SetBoatHull(GetHullMax());
	}
	// Back to the mooring with whoever is aboard: the pirate keeps his gold and his gear, the sea
	// keeps the hold (spec S2.3). Teleporting the hull rather than dropping the rider in open water
	// is what keeps the ashore search from having to succeed in the middle of the bay.
	ForwardSpeed = 0.0f;
	LateralSpeed = 0.0f;
	SailState = EWrecktideSailState::Furled;
	bReverseCrawl = false;
	if (SailMesh)
	{
		SailMesh->SetVisibility(false);
	}
	if (Movement)
	{
		Movement->StopMovementImmediately();
	}
	SetActorLocationAndRotation(MooringLocation, MooringRotation, false, nullptr, ETeleportType::TeleportPhysics);
	ShowToast(TEXT("The sea took the hold."));
	UWrecktideAudio::PlaySfx(this, TEXT("wood"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD sunk respawn=%s hull=%d holdSlots=%d"),
		*MooringLocation.ToString(), GetHullHealth(), GetHoldSlotsUsed());
}

float AWrecktideBoat::GetMaxSpeedForState(EWrecktideSailState State) const
{
	switch (State)
	{
	// The shipwright's Canvas tiers are a multiplier on the authored speeds (spec S4), so one number
	// changes and every reader (the model, the HUD and the probe) sees the same answer.
	case EWrecktideSailState::Half: return HalfSailSpeed * GetCanvasSpeedMultiplier();
	case EWrecktideSailState::Full: return FullSailSpeed * GetCanvasSpeedMultiplier();
	default: return 0.0f;
	}
}

FString AWrecktideBoat::SailStateName(EWrecktideSailState State)
{
	switch (State)
	{
	case EWrecktideSailState::Half: return TEXT("Half sail");
	case EWrecktideSailState::Full: return TEXT("Full sail");
	default: return TEXT("Sails furled");
	}
}

void AWrecktideBoat::SetSailState(EWrecktideSailState NewState)
{
	if (SailState == NewState)
	{
		return;
	}
	SailState = NewState;
	bReverseCrawl = false;
	// The rig reads the state: the canvas is off the yard only when there is sail set.
	if (SailMesh)
	{
		SailMesh->SetVisibility(SailState != EWrecktideSailState::Furled);
	}
	UWrecktideAudio::PlaySfx(this, TEXT("wood"));
	ShowToast(SailStateName(SailState));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL state=%s max=%.0f speed=%.0f"),
		*SailStateName(SailState), GetMaxSpeedForState(SailState), ForwardSpeed);
}

void AWrecktideBoat::StepSail(int32 Direction)
{
	const int32 Current = static_cast<int32>(SailState);
	if (Direction < 0 && Current == 0)
	{
		// Already furled: S again backs her down on the oars rather than doing nothing.
		if (!bReverseCrawl)
		{
			bReverseCrawl = true;
			ShowToast(TEXT("Backing down"));
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAIL state=backing max=%.0f"), ReverseCrawlSpeed);
		}
		return;
	}
	if (Direction > 0 && bReverseCrawl)
	{
		// W out of the reverse crawl stops her before it sets sail, so one tap never does two things.
		bReverseCrawl = false;
		return;
	}
	const int32 Next = FMath::Clamp(Current + Direction, 0, static_cast<int32>(EWrecktideSailState::Full));
	if (Next != Current)
	{
		SetSailState(static_cast<EWrecktideSailState>(Next));
	}
}

void AWrecktideBoat::SetSail(float Throttle, float Steer)
{
	const float ClampedThrottle = FMath::Clamp(Throttle, -1.0f, 1.0f);
	CurrentSteer = FMath::Clamp(Steer, -1.0f, 1.0f);

	// W and S are TAPS that step through the three sail states, not a throttle held down (research
	// B4: sail state, not rigging). The edge is detected here so the key polling in Tick and a
	// probe's HoldSail both step exactly once however long the value is held.
	const int32 Request = ClampedThrottle > 0.5f ? 1 : (ClampedThrottle < -0.5f ? -1 : 0);
	if (Request != 0 && Request != LastThrottleRequest)
	{
		StepSail(Request);
	}
	LastThrottleRequest = Request;
	CurrentThrottle = ClampedThrottle;
}

float AWrecktideBoat::SampleSeaHeight(const FVector& World, float Time) const
{
	// Three sines, not Gerstner: the height stays a closed-form function of XY and time, so four hull
	// points can be sampled with no iterative inverse solve (research B4).
	const float Omega = 2.0f * PI / FMath::Max(BobPeriodSeconds, 0.1f);
	return 0.55f * FMath::Sin(Omega * Time + World.X * 0.0021f)
		+ 0.30f * FMath::Sin(Omega * 1.37f * Time + World.Y * 0.0017f)
		+ 0.15f * FMath::Sin(Omega * 0.61f * Time + (World.X + World.Y) * 0.0009f);
}

void AWrecktideBoat::TickSail(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float Heading = GetActorRotation().Yaw;
	const float WindToYaw = WrecktideWind::ToYaw(this);

	// Target speed for the state, with the wind as a soft multiplier and never a stall.
	float TargetSpeed = 0.0f;
	if (SailState == EWrecktideSailState::Furled)
	{
		TargetSpeed = bReverseCrawl ? -ReverseCrawlSpeed : 0.0f;
	}
	else
	{
		TargetSpeed = GetMaxSpeedForState(SailState) * WrecktideWind::SpeedFactor(Heading, WindToYaw);
	}

	// Asymmetric ramp: fast onto the speed, slow off it. Both rates are measured against the FULL
	// sail speed so a coast down from Half is not slower than a coast down from Full.
	const float RampUp = FullSailSpeed / FMath::Max(SailRampUpSeconds, 0.05f);
	const float CoastDown = FullSailSpeed / FMath::Max(SailCoastDownSeconds, 0.05f);
	const bool bGainingWay = FMath::Abs(TargetSpeed) > FMath::Abs(ForwardSpeed);
	ForwardSpeed = FMath::FInterpConstantTo(ForwardSpeed, TargetSpeed, DeltaSeconds, bGainingWay ? RampUp : CoastDown);

	// Rudder authority falls with speed, and furling the sails unlocks the pivot (research B4).
	const float SpeedFraction = FMath::Clamp(FMath::Abs(ForwardSpeed) / FMath::Max(FullSailSpeed, 1.0f), 0.0f, 1.0f);
	float TurnRateNow = MaxTurnRate * (1.0f - TurnSpeedPenalty * SpeedFraction);
	if (SailState == EWrecktideSailState::Furled)
	{
		TurnRateNow *= FurledPivotBoost;
	}
	TurnRateNow *= WrecktideWind::RudderFactor(Heading, WindToYaw);
	if (!FMath::IsNearlyZero(CurrentSteer))
	{
		AddActorWorldRotation(FRotator(0.0f, CurrentSteer * TurnRateNow * DeltaSeconds, 0.0f));
	}

	// Lateral slide: the hull carves instead of railing, and the carry bleeds off on its own.
	const float TargetLateral = ForwardSpeed * LateralSlideFraction * CurrentSteer;
	LateralSpeed = FMath::FInterpTo(LateralSpeed, TargetLateral, DeltaSeconds, 1.0f / FMath::Max(LateralSlideDecaySeconds, 0.05f));

	// The move itself is a SWEEP through the movement component, so the pier, the beach tiers and any
	// other pawn still stop the hull exactly as they did before the model was added.
	const FVector Delta = (GetActorForwardVector() * ForwardSpeed + GetActorRightVector() * LateralSpeed) * DeltaSeconds;
	if (!Movement)
	{
		return;
	}
	if (Delta.IsNearlyZero())
	{
		Movement->Velocity = FVector::ZeroVector;
		return;
	}
	FHitResult Hit;
	Movement->SafeMoveUpdatedComponent(Delta, GetActorQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		// The shore stops her: bleed most of the way off and slide along rather than grinding.
		Movement->SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit);
		ForwardSpeed *= ShoreImpactKeep;
		LateralSpeed = 0.0f;
	}
	Movement->Velocity = Delta / DeltaSeconds;
}

void AWrecktideBoat::TickPresentation(float DeltaSeconds)
{
	const float SpeedFraction = FMath::Clamp(FMath::Abs(ForwardSpeed) / FMath::Max(FullSailSpeed, 1.0f), 0.0f, 1.0f);

	// Bob: four points on the stylised sea (bow, stern, port, starboard) fitted to a pitch and a roll.
	// The hull COLLISION box stays level; only the mesh tilts, so the sweep above is unaffected.
	BobTime += DeltaSeconds * (1.0f + BobSpeedScale * SpeedFraction);
	if (HullMesh)
	{
		const float Amplitude = BobAmplitude * (1.0f + BobSpeedScale * SpeedFraction);
		const FVector Origin = GetActorLocation();
		const FVector Forward = GetActorForwardVector();
		const FVector Right = GetActorRightVector();
		const float Bow = Amplitude * SampleSeaHeight(Origin + Forward * HullHalfLength, BobTime);
		const float Stern = Amplitude * SampleSeaHeight(Origin - Forward * HullHalfLength, BobTime);
		const float Port = Amplitude * SampleSeaHeight(Origin - Right * HullHalfWidth, BobTime);
		const float Starboard = Amplitude * SampleSeaHeight(Origin + Right * HullHalfWidth, BobTime);

		const float Heave = 0.25f * (Bow + Stern + Port + Starboard);
		const float BowUpDeg = BobTiltGain * FMath::RadiansToDegrees(FMath::Atan2(Bow - Stern, 2.0f * HullHalfLength));
		const float PortDownDeg = BobTiltGain * FMath::RadiansToDegrees(FMath::Atan2(Starboard - Port, 2.0f * HullHalfWidth));

		// The mesh carries a -90 yaw (its bow runs along its own +Y), and a rotator applies roll and
		// pitch in the PARENT frame before that yaw: the roll slot is therefore the boat's pitch and
		// the pitch slot is the boat's roll. Checked against the shipped bob, which used the same slots.
		HullMesh->SetRelativeLocation(FVector(0.0f, 0.0f, HullMeshZ + Heave));
		HullMesh->SetRelativeRotation(FRotator(PortDownDeg, -90.0f, BowUpDeg));
	}

	// The wake, on only when there is way on her (spec S1.6, re-cut 2026-09-06). One small sprite laid
	// in the water at the stern every FoamDropSeconds, then left behind to spread and fade. Nothing is
	// emitted from the bow or the beam: what a small hull leaves is a trail, and the old emitters
	// wrapped the whole boat in white cards.
	const bool bMoving = FMath::Abs(ForwardSpeed) >= WakeSpeedThreshold;
	FoamDropElapsed += DeltaSeconds;
	if (bFoamReady && bMoving && FoamDropElapsed >= FoamDropSeconds)
	{
		FoamDropElapsed = 0.0f;
		if (UStaticMeshComponent* Sprite = FoamSprites[FoamNextIndex])
		{
			// Alternating quarters off the centre line, so the trail reads as two shoulders of foam
			// closing behind her rather than one dotted line.
			const float Side = (FoamNextIndex % 2 == 0) ? -1.0f : 1.0f;
			const FVector Drop = GetActorLocation()
				- GetActorForwardVector() * (HullHalfLength * FoamSternFraction)
				+ GetActorRightVector() * (Side * FoamSternSpread * FMath::FRandRange(0.8f, 1.0f))
				+ FVector(0.0f, 0.0f, (WaterZ + FoamSurfaceLift) - GetActorLocation().Z);
			Sprite->SetWorldLocation(Drop);
			Sprite->SetWorldRotation(FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f));
			FoamAgeSeconds[FoamNextIndex] = 0.0f;
		}
		FoamNextIndex = (FoamNextIndex + 1) % FoamSprites.Num();
	}
	for (int32 FoamIndex = 0; bFoamReady && FoamIndex < FoamSprites.Num(); ++FoamIndex)
	{
		UStaticMeshComponent* Sprite = FoamSprites[FoamIndex];
		if (!Sprite || !FoamAgeSeconds.IsValidIndex(FoamIndex))
		{
			continue;
		}
		if (FoamAgeSeconds[FoamIndex] >= FoamLifeSeconds)
		{
			if (Sprite->IsVisible())
			{
				Sprite->SetVisibility(false);
			}
			continue;
		}
		FoamAgeSeconds[FoamIndex] += DeltaSeconds;
		const float Life = FMath::Clamp(FoamAgeSeconds[FoamIndex] / FMath::Max(FoamLifeSeconds, 0.01f), 0.0f, 1.0f);
		// Squared fade: bright where she just passed, gone by the time the eye reaches the end of it.
		const float Alpha = FoamAlpha * FMath::Square(1.0f - Life);
		Sprite->SetWorldScale3D(FVector(FoamSpriteSize * (1.0f + FoamGrowth * Life) / FoamPlaneSizeUU));
		if (FoamMaterials.IsValidIndex(FoamIndex) && FoamMaterials[FoamIndex])
		{
			FoamMaterials[FoamIndex]->SetScalarParameterValue(FoamOpacityParam, Alpha);
		}
		if (!Sprite->IsVisible())
		{
			Sprite->SetVisibility(true);
		}
	}

	// A touch of field of view at speed: half the feel is presentation (research B4).
	if (FollowCamera)
	{
		const float TargetFov = BaseCameraFov + FullSailFovBoost * SpeedFraction;
		FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, TargetFov, DeltaSeconds, 3.0f));
	}
}

void AWrecktideBoat::TickHelm(float DeltaSeconds)
{
	// The wheel follows the STEERING INPUT, not the yaw rate. That is the first person driving
	// wheel the spec asks for (it answers the key, lags it a little, centres itself when the key is
	// let go), and it is also the only reading that still turns while the rudder itself is stalled
	// by speed or by a headwind.
	HelmWheelAngleDegrees = WrecktideHelm::StepWheelAngle(HelmWheelAngleDegrees, CurrentSteer, HelmMaxWheelTurn, HelmWheelTurnRate, DeltaSeconds);
	if (HelmPivot)
	{
		// Roll about the pivot's local X, which is the hull's forward axis and so the wheel's axle.
		// NEGATED because a positive roll about forward carries starboard UP, which from behind the
		// wheel reads as turned to port, and the stored angle is signed like the steering.
		HelmPivot->SetRelativeRotation(FRotator(0.0f, 0.0f, -HelmWheelAngleDegrees));
	}

	// The grip is re-armed every frame rather than once on boarding: it is a real slot montage, and
	// anything that ends it (its own length running out, a montage stop from elsewhere) would leave
	// the pirate at the wheel with his arms down for the rest of the voyage. SetHelmGrip is
	// idempotent, so this costs one Montage_IsPlaying while somebody is aboard.
	if (ACombatCharacter* CurrentRider = Rider.Get())
	{
		CurrentRider->SetHelmGrip(true);
	}
}

void AWrecktideBoat::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (PlayerInputComponent)
	{
		PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideBoat::OnAshoreKey).bConsumeInput = true;
		PlayerInputComponent->BindKey(EKeys::F, IE_Released, this, &AWrecktideBoat::OnAshoreKeyReleased).bConsumeInput = true;
	}
}

void AWrecktideBoat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PromptRefreshElapsed += DeltaSeconds;
	if (PromptRefreshElapsed >= 0.25f)
	{
		PromptRefreshElapsed = FMath::Fmod(PromptRefreshElapsed, 0.25f);
		RefreshInteractionPrompt();
	}

	if (Rider.IsValid())
	{
		// keys are polled (not action-bound) so the pirate's Enhanced Input contexts stay untouched;
		// a probe drives SetSail directly and simply is not polled over while no key is down
		if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			const float KeyThrottle = (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
			const float KeySteer = (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
			if (!FMath::IsNearlyZero(KeyThrottle) || !FMath::IsNearlyZero(KeySteer) || SailHoldUntil < GetWorld()->GetTimeSeconds())
			{
				SetSail(KeyThrottle, KeySteer);
			}
		}
		TickSail(DeltaSeconds);
	}
	else if (!FMath::IsNearlyZero(ForwardSpeed) || !FMath::IsNearlyZero(LateralSpeed))
	{
		// Nobody at the tiller: she still carries her way off rather than stopping dead.
		CurrentSteer = 0.0f;
		bReverseCrawl = false;
		SailState = EWrecktideSailState::Furled;
		TickSail(DeltaSeconds);
	}

	// The hold-to-use patch. She must be aboard, damaged and STILL; way on her cancels it, which is
	// what keeps the repair a decision rather than a button pressed while running from the navy.
	if (bRepairHolding)
	{
		if (!Rider.IsValid() || FMath::Abs(ForwardSpeed) > RepairStillSpeed)
		{
			ShowToast(TEXT("Too much way on to patch."));
			EndRepairHold();
		}
		else
		{
			RepairHeldSeconds += DeltaSeconds;
			if (RepairHeldSeconds >= RepairHoldSeconds)
			{
				CompleteRepair();
			}
		}
	}

	// The helm before the presentation, and outside the rider branch: an abandoned wheel centres
	// itself instead of freezing hard over.
	TickHelm(DeltaSeconds);

	// A little swell so a moored boat is never a statue, plus the wake and the camera.
	TickPresentation(DeltaSeconds);

	// pinned to the water line, whatever the collision solver did this frame
	FVector Location = GetActorLocation();
	bool bRepin = false;
	if (!FMath::IsNearlyEqual(Location.Z, WaterZ, 0.5f))
	{
		Location.Z = WaterZ;
		bRepin = true;
	}
	// and kept inside the sea (2026-09-06): the water zone is 51200 wide and the far islands sit 16250 to 19764 uu
	// out; past BoatRangeLimit there is no land to go ashore on, so the boat stops at the edge instead of sailing
	// off the map for good
	const float Range2D = FVector2D(Location.X, Location.Y).Size();
	if (Range2D > BoatRangeLimit)
	{
		const FVector2D Dir = FVector2D(Location.X, Location.Y) / Range2D;
		Location.X = Dir.X * BoatRangeLimit;
		Location.Y = Dir.Y * BoatRangeLimit;
		if (Movement)
		{
			Movement->StopMovementImmediately();
		}
		ForwardSpeed = 0.0f;
		LateralSpeed = 0.0f;
		bRepin = true;
	}
	if (bRepin)
	{
		SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AWrecktideBoat::HoldSail(float Throttle, float Steer, float Seconds)
{
	SetSail(Throttle, Steer);
	SailHoldUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + Seconds : 0.0f;
}

void AWrecktideBoat::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}

void AWrecktideBoat::RefreshInteractionPrompt() const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr;
	if (!HUD)
	{
		return;
	}

	if (Rider.IsValid())
	{
		FVector LandingSpot;
		if (FindAshoreSpot(LandingSpot))
		{
			HUD->SetInteractionPrompt(TEXT("F: go ashore"));
		}
		else
		{
			HUD->ClearInteractionPrompt();
		}
		return;
	}

	ACombatCharacter* Character = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (Character && IsCharacterInRange(Character))
	{
		HUD->SetInteractionPrompt(TEXT("Press F to board the boat"));
	}
	else
	{
		HUD->ClearInteractionPrompt();
	}
}
