// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktidePatrolBoat.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "UObject/UnrealType.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/WrecktideBoat.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/WrecktideAudio.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

namespace
{
/** Same Synty small hull as the player's boat, so the two read as sister ships of different navies. */
constexpr float PatrolHullHalfLength = 420.0f;
constexpr float PatrolHullHalfWidth = 145.0f;
constexpr float PatrolHullHalfHeight = 60.0f;
constexpr float PatrolHullMeshZ = -35.0f;
/** A lunge only counts as a ram once it has actually been running: at station she already sits
 *  inside RamContactDistance, and a hit with no run at it is not a ram. */
constexpr float RamContactMinLungeSeconds = 0.3f;
/** What GetSecondsSinceContact reports when she has never landed one. */
constexpr float NeverTouchedSeconds = 9999.0f;
}

AWrecktidePatrolBoat::AWrecktidePatrolBoat()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	// Nobody drives her: the state machine below is the whole crew, and an AI controller would only
	// give the stock tree something to argue with.
	AutoPossessAI = EAutoPossessAI::Disabled;

	Hull = CreateDefaultSubobject<UBoxComponent>(TEXT("Hull"));
	Hull->SetBoxExtent(FVector(PatrolHullHalfLength, PatrolHullHalfWidth, PatrolHullHalfHeight));
	Hull->SetCollisionProfileName(TEXT("Pawn"));
	Hull->SetCanEverAffectNavigation(false);
	RootComponent = Hull;

	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(Hull);
	HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HullMesh->SetCanEverAffectNavigation(false);
	HullMesh->SetRelativeLocation(FVector(0.0f, 0.0f, PatrolHullMeshZ));
	HullMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	// The PLAIN hull, not the _Pirate one the player sails: same silhouette, navy paint, no black
	// flag anywhere on her (spec S3.1, the "plain hull plus a navy flag" option).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HullAsset(TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Boat_Small_01_Hull.SM_Veh_Boat_Small_01_Hull"));
	if (HullAsset.Succeeded())
	{
		HullMesh->SetStaticMesh(HullAsset.Object);
	}

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
	// Same rig geometry as the player's boat (the offsets are hers, measured there), but the plain
	// canvas and the navy ensign rather than the pirate set.
	MastMesh = MakeRigPart(TEXT("MastMesh"), TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Boat_Medium_01_Mast.SM_Veh_Boat_Medium_01_Mast"), 0.45f, FVector(0.0f, -40.0f, 40.0f));
	SailMesh = MakeRigPart(TEXT("SailMesh"), TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Boat_Medium_01_Sails.SM_Veh_Boat_Medium_01_Sails"), 0.45f, FVector(0.0f, -40.0f, 40.0f));
	FlagMesh = MakeRigPart(TEXT("FlagMesh"), TEXT("/Game/PolygonPirates/Meshes/Ships/SM_Veh_Flag_British_01.SM_Veh_Flag_British_01"), 1.0f, FVector(0.0f, -40.0f, 40.0f + 0.45f * 1820.0f));

	// Her helm (spec C28.1). Identical construction to the player's boat: a pivot that rolls about
	// the hull's forward axis carrying a wheel mesh with the prop's own base rotation on it.
	HelmPivot = CreateDefaultSubobject<USceneComponent>(TEXT("HelmPivot"));
	HelmPivot->SetupAttachment(Hull);
	HelmWheelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HelmWheelMesh"));
	HelmWheelMesh->SetupAttachment(HelmPivot);
	HelmWheelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmWheelMesh->SetCollisionProfileName(TEXT("NoCollision"));
	HelmWheelMesh->SetGenerateOverlapEvents(false);
	HelmWheelMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PatrolWheel01(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_ShipWheel_01.SM_Prop_ShipWheel_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PatrolWheel02(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_ShipWheel_02.SM_Prop_ShipWheel_02"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PatrolWheel03(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_ShipWheel_03.SM_Prop_ShipWheel_03"));
	HelmWheelChoices.Add(PatrolWheel01.Succeeded() ? PatrolWheel01.Object : nullptr);
	HelmWheelChoices.Add(PatrolWheel02.Succeeded() ? PatrolWheel02.Object : nullptr);
	HelmWheelChoices.Add(PatrolWheel03.Succeeded() ? PatrolWheel03.Object : nullptr);
	HelmPostMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HelmPostMesh"));
	HelmPostMesh->SetupAttachment(Hull);
	HelmPostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmPostMesh->SetCollisionProfileName(TEXT("NoCollision"));
	HelmPostMesh->SetGenerateOverlapEvents(false);
	HelmPostMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PatrolPostAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Pole_01.SM_Prop_Pole_01"));
	if (PatrolPostAsset.Succeeded())
	{
		HelmPostMesh->SetStaticMesh(PatrolPostAsset.Object);
	}

	// The navy helmsman (spec C28.3): a hidden mannequin holding the pose, a Synty soldier skin
	// retargeted from it. This is AWrecktideMissionGiver's pattern, asset for asset, because it is
	// the only skin path in this project that is already proven not to T-pose.
	HelmsmanDriver = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HelmsmanDriver"));
	HelmsmanDriver->SetupAttachment(Hull);
	HelmsmanDriver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmsmanDriver->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> HelmsmanMannequin(TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (HelmsmanMannequin.Succeeded())
	{
		HelmsmanDriver->SetSkeletalMesh(HelmsmanMannequin.Object);
	}
	HelmsmanSkin = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HelmsmanSkin"));
	HelmsmanSkin->SetupAttachment(HelmsmanDriver);
	HelmsmanSkin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmsmanSkin->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SoldierSkin(TEXT("/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_English_Soldier_01.SK_Chr_English_Soldier_01"));
	if (SoldierSkin.Succeeded())
	{
		HelmsmanSkin->SetSkeletalMesh(SoldierSkin.Object);
	}
	// The grip pose is the same one the player's pirate holds, and it is authored on SK_Mannequin
	// rather than the driver's own skeleton; the two are registered compatible in this project (the
	// player's SK_Mannequin attack montages already play on a UEFN driver). If that ever stops
	// being true the idle below takes over and the log says which one landed.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> HelmsmanGrip(TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS"));
	if (HelmsmanGrip.Succeeded())
	{
		HelmsmanGripPose = HelmsmanGrip.Object;
	}
	static ConstructorHelpers::FObjectFinder<UAnimSequence> HelmsmanIdle(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop"));
	if (HelmsmanIdle.Succeeded())
	{
		HelmsmanIdlePose = HelmsmanIdle.Object;
	}
	static ConstructorHelpers::FClassFinder<UAnimInstance> HelmsmanRetargetABP(TEXT("/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget"));
	if (HelmsmanRetargetABP.Succeeded())
	{
		HelmsmanRetargetAnimClass = HelmsmanRetargetABP.Class;
	}
	static ConstructorHelpers::FObjectFinder<UObject> HelmsmanRetargeterAsset(TEXT("/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin.RTG_UEFN_to_UE4_Mannequin"));
	if (HelmsmanRetargeterAsset.Succeeded())
	{
		HelmsmanRetargeter = HelmsmanRetargeterAsset.Object;
	}

	// Kept for its UpdatedComponent and its swept move helpers ONLY, exactly as the player's boat
	// does it: the tick is off and the state machine owns the speed.
	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 1200.0f;
	Movement->Acceleration = 320.0f;
	Movement->Deceleration = 420.0f;
	Movement->TurningBoost = 0.0f;
	Movement->PrimaryComponentTick.bCanEverTick = false;
	Movement->PrimaryComponentTick.bStartWithTickEnabled = false;
}

void AWrecktidePatrolBoat::BuildRoute(TArray<FVector>& OutRoute, float BeachHalfExtent, float OffsetFromBeach, int32 CornerSteps, float RouteZ)
{
	OutRoute.Reset();
	const int32 Steps = FMath::Max(CornerSteps, 1);
	// The route is the beach SQUARE pushed outward by OffsetFromBeach: four straight legs parallel to
	// the sand joined by four quarter circles of that radius centred on the beach corners. Every
	// point of it is therefore exactly OffsetFromBeach off the beach, which a circle could never be
	// (a circle through the corners runs almost aground at the middle of each side).
	const FVector2D Corners[4] =
	{
		FVector2D(BeachHalfExtent, BeachHalfExtent),
		FVector2D(-BeachHalfExtent, BeachHalfExtent),
		FVector2D(-BeachHalfExtent, -BeachHalfExtent),
		FVector2D(BeachHalfExtent, -BeachHalfExtent)
	};
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const float StartAngle = 90.0f * Corner;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Angle = FMath::DegreesToRadians(StartAngle + 90.0f * Step / Steps);
			OutRoute.Add(FVector(
				Corners[Corner].X + OffsetFromBeach * FMath::Cos(Angle),
				Corners[Corner].Y + OffsetFromBeach * FMath::Sin(Angle),
				RouteZ));
		}
	}
}

void AWrecktidePatrolBoat::BeginPlay()
{
	Super::BeginPlay();

	// The helm goes in BEFORE the route check below: a patrol with no route still has a wheel, and
	// the probe reads the wheel, not the waypoints.
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
			UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HELM-WIRING patrol wheel mesh %d did not load; her helm will be empty"), HelmWheelChoice);
		}
	}
	if (HelmPostMesh)
	{
		const UStaticMesh* ChosenWheelAsset = HelmWheelMesh ? HelmWheelMesh->GetStaticMesh() : nullptr;
		const float WheelRadiusZ = ChosenWheelAsset ? ChosenWheelAsset->GetBoundingBox().Max.Z * HelmWheelScale : 0.0f;
		HelmPostMesh->SetRelativeLocation(FVector(HelmOffset.X, HelmOffset.Y, HelmPostBaseZ));
		const bool bPostFitted = WrecktideHelm::FitHelmPost(HelmPostMesh, HelmPostBaseZ, HelmOffset.Z - WheelRadiusZ, HelmPostWidthUU);
		HelmPostMesh->SetVisibility(bPostFitted);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM patrol post fitted=%d scale=%s"), bPostFitted ? 1 : 0,
			*HelmPostMesh->GetRelativeScale3D().ToString());
	}
	ApplyHelmsmanSkin();

	BuildRoute(Route, RouteBeachHalfExtent, RouteOffsetFromBeach, RouteCornerSteps, WaterZ);
	if (Route.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-PATROL-WIRING route is empty, the patrol will sit still"));
		return;
	}

	// Start for the leg she is nearest, so a spawn point can be chosen by the spawner without
	// having to know the waypoint order.
	int32 Nearest = 0;
	float NearestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Route.Num(); ++Index)
	{
		const float DistanceSquared = FVector::DistSquared2D(Route[Index], GetActorLocation());
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Index;
		}
	}
	// The nearest point is usually the one she is sitting on; steer for the NEXT one.
	WaypointIndex = (Nearest + 1) % Route.Num();
	SinceContactSeconds = NeverTouchedSeconds;

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL ready at X=%.0f Y=%.0f route=%d points offBeach=%.0f nextWp=%d turnRate=%.0f"),
		GetActorLocation().X, GetActorLocation().Y, Route.Num(), RouteOffsetFromBeach, WaypointIndex, PatrolTurnRate);
}

FVector AWrecktidePatrolBoat::GetHelmWheelWorldLocation() const
{
	if (HelmWheelMesh)
	{
		return HelmWheelMesh->GetComponentLocation();
	}
	return HelmPivot ? HelmPivot->GetComponentLocation() : GetActorLocation();
}

FVector AWrecktidePatrolBoat::GetHelmWheelLocalOffset() const
{
	return GetActorTransform().InverseTransformPosition(GetHelmWheelWorldLocation());
}

FVector AWrecktidePatrolBoat::GetHullBoxExtent() const
{
	return Hull ? Hull->GetUnscaledBoxExtent() : FVector::ZeroVector;
}

bool AWrecktidePatrolBoat::HasHelmWheel() const
{
	return HelmWheelMesh != nullptr && HelmWheelMesh->GetStaticMesh() != nullptr;
}

void AWrecktidePatrolBoat::TickHelm(float DeltaSeconds)
{
	// The SHARED model, so her wheel and the player's cannot drift apart: same lock, same rate, same
	// lag, only the input differs (his is a key, hers is how hard her steering is over).
	HelmWheelAngleDegrees = WrecktideHelm::StepWheelAngle(HelmWheelAngleDegrees, HelmSteerInput, HelmMaxWheelTurn, HelmWheelTurnRate, DeltaSeconds);
	if (HelmPivot)
	{
		// Negated for the same reason the player's is: a positive roll about forward carries
		// starboard up, which from behind the wheel reads as turned to port.
		HelmPivot->SetRelativeRotation(FRotator(0.0f, 0.0f, -HelmWheelAngleDegrees));
	}
}

void AWrecktidePatrolBoat::ApplyHelmsmanSkin()
{
	if (!HelmsmanDriver || !HelmsmanSkin)
	{
		return;
	}
	HelmsmanDriver->SetRelativeLocation(HelmsmanOffset);
	HelmsmanDriver->SetRelativeRotation(FRotator::ZeroRotator);

	// The pose first: a helmsman in the bind pose reads as broken, and the grip pose is the point of
	// him. The idle is the fallback, and the log names whichever one actually landed.
	const bool bDriverOK = HelmsmanDriver->GetSkeletalMeshAsset() != nullptr;
	if (bDriverOK && (HelmsmanGripPose || HelmsmanIdlePose))
	{
		HelmsmanDriver->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		if (HelmsmanGripPose)
		{
			HelmsmanDriver->PlayAnimation(HelmsmanGripPose, true);
		}
		const bool bGripLanded = HelmsmanGripPose != nullptr && HelmsmanDriver->GetSingleNodeInstance() != nullptr
			&& HelmsmanDriver->GetSingleNodeInstance()->GetAnimationAsset() == HelmsmanGripPose;
		if (!bGripLanded && HelmsmanIdlePose)
		{
			HelmsmanDriver->PlayAnimation(HelmsmanIdlePose, true);
		}
		bHelmsmanPosed = bGripLanded;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM helmsman pose grip=%d asset=%s"), bGripLanded ? 1 : 0,
			(HelmsmanDriver->GetSingleNodeInstance() && HelmsmanDriver->GetSingleNodeInstance()->GetAnimationAsset())
				? *HelmsmanDriver->GetSingleNodeInstance()->GetAnimationAsset()->GetName() : TEXT("None"));
	}

	const bool bSkinOK = HelmsmanSkin->GetSkeletalMeshAsset() != nullptr;
	if (!bDriverOK || !bSkinOK || !HelmsmanRetargetAnimClass || !HelmsmanRetargeter)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-HELM helmsman retarget skipped driver=%d skin=%d abp=%d rtg=%d; showing the mannequin"),
			bDriverOK ? 1 : 0, bSkinOK ? 1 : 0, HelmsmanRetargetAnimClass != nullptr, HelmsmanRetargeter != nullptr);
		HelmsmanSkin->SetVisibility(false);
		HelmsmanDriver->SetVisibility(bDriverOK);
		return;
	}

	HelmsmanSkin->ComponentTags.Reset();
	HelmsmanSkin->ComponentTags.Add(HelmsmanRetargeter->GetFName());
	HelmsmanSkin->SetAnimInstanceClass(HelmsmanRetargetAnimClass);
	if (!PushHelmsmanRetargeter())
	{
		HelmsmanSkin->SetVisibility(false);
		HelmsmanDriver->SetVisibility(true);
		return;
	}
	HelmsmanSkin->OnAnimInitialized.AddUniqueDynamic(this, &AWrecktidePatrolBoat::OnHelmsmanAnimInitialized);
	HelmsmanSkin->AddTickPrerequisiteComponent(HelmsmanDriver);
	HelmsmanSkin->SetComponentTickEnabled(true);

	HelmsmanDriver->SetVisibility(false);
	HelmsmanDriver->SetCastShadow(false);
	HelmsmanDriver->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	HelmsmanSkin->SetVisibility(true);
	HelmsmanSkin->SetCastShadow(true);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HELM helmsman live skin=%s abp=%s rtg=%s at %s"),
		*HelmsmanSkin->GetSkeletalMeshAsset()->GetName(), *HelmsmanRetargetAnimClass->GetName(),
		*HelmsmanRetargeter->GetName(), *HelmsmanOffset.ToString());
}

bool AWrecktidePatrolBoat::PushHelmsmanRetargeter()
{
	UAnimInstance* SkinAnim = HelmsmanSkin ? HelmsmanSkin->GetAnimInstance() : nullptr;
	FObjectProperty* RetargeterProperty = SkinAnim
		? FindFProperty<FObjectProperty>(SkinAnim->GetClass(), FName(TEXT("IKRetargeter")))
		: nullptr;
	if (!SkinAnim || !RetargeterProperty || !HelmsmanRetargeter)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-HELM helmsman retargeter not pushed anim=%d prop=%d rtg=%d"),
			SkinAnim != nullptr, RetargeterProperty != nullptr, HelmsmanRetargeter != nullptr);
		return false;
	}
	if (!HelmsmanRetargeter->IsA(RetargeterProperty->PropertyClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-HELM helmsman retargeter type mismatch have=%s want=%s"),
			*HelmsmanRetargeter->GetClass()->GetName(), *RetargeterProperty->PropertyClass->GetName());
		return false;
	}
	RetargeterProperty->SetObjectPropertyValue_InContainer(SkinAnim, HelmsmanRetargeter);
	return true;
}

void AWrecktidePatrolBoat::OnHelmsmanAnimInitialized()
{
	// The write is per instance: re-push whenever the engine rebuilds the anim instance, or the
	// skin silently falls back to its bind pose.
	PushHelmsmanRetargeter();
}

float AWrecktidePatrolBoat::DistanceToRoute(const FVector& World) const
{
	if (Route.Num() == 0)
	{
		return 0.0f;
	}
	const FVector Flat(World.X, World.Y, 0.0f);
	float Best = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Route.Num(); ++Index)
	{
		const FVector A(Route[Index].X, Route[Index].Y, 0.0f);
		const FVector B(Route[(Index + 1) % Route.Num()].X, Route[(Index + 1) % Route.Num()].Y, 0.0f);
		Best = FMath::Min(Best, FMath::PointDistToSegment(Flat, A, B));
	}
	return Best;
}

float AWrecktidePatrolBoat::GetSecondsSinceContact() const
{
	return SinceContactSeconds;
}

AActor* AWrecktidePatrolBoat::GetTarget() const
{
	if (TargetBoat.IsValid())
	{
		return TargetBoat.Get();
	}
	return TargetSwimmer.IsValid() ? Cast<AActor>(TargetSwimmer.Get()) : nullptr;
}

float AWrecktidePatrolBoat::GetTargetBearing() const
{
	return LastTargetBearing;
}

void AWrecktidePatrolBoat::ForceRamLunge()
{
	// A lunge already under way is left alone: cancelling and re-arming it every tick is how a
	// polling caller stops one ever running long enough to land.
	if (PatrolState != EPatrolBoatState::Hostile || LungeRemainingSeconds > 0.0f)
	{
		return;
	}
	SinceLungeSeconds = RamIntervalSeconds;
}

void AWrecktidePatrolBoat::SetPatrolState(EPatrolBoatState NewState, const TCHAR* Reason)
{
	if (PatrolState == NewState)
	{
		return;
	}
	const EPatrolBoatState Previous = PatrolState;
	PatrolState = NewState;
	StateElapsedSeconds = 0.0f;
	BlindArcHeldSeconds = 0.0f;
	LungeRemainingSeconds = 0.0f;
	SinceLungeSeconds = 0.0f;

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	if (NewState == EPatrolBoatState::Hostile)
	{
		// The no-contact leash clock starts the moment she turns, not the moment she last touched him.
		SinceContactSeconds = 0.0f;
		ShowToast(TEXT("Navy patrol closing."));
	}
	else if (Previous == EPatrolBoatState::Hostile)
	{
		TargetBoat.Reset();
		TargetSwimmer.Reset();
		LastTargetBearing = -1.0f;
		// A break has to buy real distance, or she would re-aggro on the same frame she gave up.
		AggroBlockedUntilSeconds = Now + AggroCooldownSeconds;
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL state %d -> %d reason=%s at X=%.0f Y=%.0f routeDist=%.0f"),
		static_cast<int32>(Previous), static_cast<int32>(NewState), Reason,
		GetActorLocation().X, GetActorLocation().Y, DistanceToRoute(GetActorLocation()));
}

void AWrecktidePatrolBoat::TickAggroSearch()
{
	UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() < AggroBlockedUntilSeconds)
	{
		return;
	}
	// She cannot start a new fight from outside her own leash: the hostile tick would check the same
	// distance and give up on the frame after, which is a state flip-flop and a toast the player sees
	// twice for nothing (seen in the 2026-09-07 run at routeDist 6021). Back inside the patrol area
	// first, then she may pick someone up again.
	if (DistanceToRoute(GetActorLocation()) > LeashRouteRadius)
	{
		return;
	}

	// The player's boat while he is aboard her, otherwise the pirate himself once he is in the water:
	// a man swimming past a navy patrol is exactly as interesting to her as a man sailing past.
	AWrecktideBoat* PlayerBoat = nullptr;
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		PlayerBoat = *It;
		break;
	}
	if (PlayerBoat && PlayerBoat->IsOccupied()
		&& FVector::Dist2D(PlayerBoat->GetActorLocation(), GetActorLocation()) <= AggroRadius)
	{
		TargetBoat = PlayerBoat;
		TargetSwimmer.Reset();
		SetPatrolState(EPatrolBoatState::Hostile, TEXT("boat in range"));
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	ACombatCharacter* Pirate = PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
	const UCharacterMovementComponent* PirateMovement = Pirate ? Pirate->GetCharacterMovement() : nullptr;
	if (Pirate && PirateMovement && PirateMovement->IsSwimming()
		&& FVector::Dist2D(Pirate->GetActorLocation(), GetActorLocation()) <= AggroRadius)
	{
		TargetSwimmer = Pirate;
		TargetBoat.Reset();
		SetPatrolState(EPatrolBoatState::Hostile, TEXT("swimmer in range"));
	}
}

void AWrecktidePatrolBoat::DriveTowards(float DesiredYaw, float DesiredSpeed, float DeltaSeconds)
{
	// One flat turn rate is the whole weakness: no speed curve, no pivot, no bow thruster. What she
	// cannot do is come about quickly, and the player can.
	const float YawError = FRotator::NormalizeAxis(DesiredYaw - GetActorRotation().Yaw);
	const float MaxStep = PatrolTurnRate * DeltaSeconds;
	const float AppliedStep = FMath::Clamp(YawError, -MaxStep, MaxStep);
	AddActorWorldRotation(FRotator(0.0f, AppliedStep, 0.0f));
	ForwardSpeed = FMath::FInterpConstantTo(ForwardSpeed, DesiredSpeed, DeltaSeconds, SpeedRamp);
	// HER steering, in the same -1 to +1 the player's keys produce: the yaw step she actually took
	// over the largest one her turn rate allowed. Full lock therefore means "turning as hard as she
	// can", which is exactly what a helmsman's hands would be doing (spec C28.2).
	HelmSteerInput = MaxStep > KINDA_SMALL_NUMBER ? FMath::Clamp(AppliedStep / MaxStep, -1.0f, 1.0f) : 0.0f;
}

void AWrecktidePatrolBoat::MoveHull(float DeltaSeconds)
{
	if (!Movement)
	{
		return;
	}
	const FVector Delta = GetActorForwardVector() * ForwardSpeed * DeltaSeconds;
	if (!Delta.IsNearlyZero())
	{
		FHitResult Hit;
		Movement->SafeMoveUpdatedComponent(Delta, GetActorQuat(), true, Hit);
		LastBlockingActor = Hit.IsValidBlockingHit() ? Hit.GetActor() : nullptr;
		if (Hit.IsValidBlockingHit())
		{
			// The island stops her the same way it stops the player: slide, do not grind.
			Movement->SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit);
			ForwardSpeed *= ShoreImpactKeep;
		}
		Movement->Velocity = Delta / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	}
	else
	{
		Movement->Velocity = FVector::ZeroVector;
	}

	FVector Location = GetActorLocation();
	bool bRepin = false;
	if (!FMath::IsNearlyEqual(Location.Z, WaterZ, 0.5f))
	{
		Location.Z = WaterZ;
		bRepin = true;
	}
	const float Range2D = FVector2D(Location.X, Location.Y).Size();
	if (Range2D > RangeLimit)
	{
		const FVector2D Direction = FVector2D(Location.X, Location.Y) / Range2D;
		Location.X = Direction.X * RangeLimit;
		Location.Y = Direction.Y * RangeLimit;
		ForwardSpeed = 0.0f;
		bRepin = true;
	}
	if (bRepin)
	{
		SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AWrecktidePatrolBoat::AdvanceWaypoint()
{
	if (Route.Num() > 0)
	{
		WaypointIndex = (WaypointIndex + 1) % Route.Num();
	}
}

void AWrecktidePatrolBoat::TickHostile(float DeltaSeconds)
{
	// Re-resolve every frame: he can step off the boat, drown, or be put ashore by a probe.
	AWrecktideBoat* Boat = TargetBoat.Get();
	if (Boat && !Boat->IsOccupied())
	{
		Boat = nullptr;
		TargetBoat.Reset();
	}
	AActor* Target = Boat ? Cast<AActor>(Boat) : Cast<AActor>(TargetSwimmer.Get());
	if (!Target)
	{
		SetPatrolState(EPatrolBoatState::Returning, TEXT("target lost"));
		return;
	}

	const FVector Here = GetActorLocation();
	const FVector There = Target->GetActorLocation();
	const FVector ToTarget = FVector(There.X - Here.X, There.Y - Here.Y, 0.0f);
	const float Gap = ToTarget.Size();
	const float BearingYaw = ToTarget.IsNearlyZero() ? GetActorRotation().Yaw : ToTarget.Rotation().Yaw;
	LastTargetBearing = FMath::Abs(FRotator::NormalizeAxis(BearingYaw - GetActorRotation().Yaw));

	// The blind arc (spec S3.2). Hold his boat behind her beam and she loses him: she cannot turn
	// fast enough to keep him in sight, which is exactly what a furled pivot buys.
	BlindArcHeldSeconds = LastTargetBearing >= BlindArcDegrees ? BlindArcHeldSeconds + DeltaSeconds : 0.0f;
	if (BlindArcHeldSeconds >= BlindArcBreakSeconds)
	{
		SetPatrolState(EPatrolBoatState::Returning, TEXT("lost him astern"));
		return;
	}

	// Leash: too far from her own route, or too long without landing one.
	if (DistanceToRoute(Here) > LeashRouteRadius)
	{
		SetPatrolState(EPatrolBoatState::Returning, TEXT("off station"));
		return;
	}
	if (SinceContactSeconds >= LeashNoContactSeconds)
	{
		SetPatrolState(EPatrolBoatState::Returning, TEXT("no contact"));
		return;
	}

	// The lunge clock. She rams on a beat rather than whenever she happens to be close, so the ram
	// reads as a decision from the deck of the player's boat.
	SinceLungeSeconds += DeltaSeconds;
	if (LungeRemainingSeconds > 0.0f)
	{
		LungeRemainingSeconds = FMath::Max(LungeRemainingSeconds - DeltaSeconds, 0.0f);
	}
	else if (SinceLungeSeconds >= RamIntervalSeconds && Gap <= CloseInDistance * 2.0f)
	{
		LungeRemainingSeconds = RamLungeSeconds;
		SinceLungeSeconds = 0.0f;
	}

	const float TargetSpeed = Boat ? FMath::Abs(Boat->GetForwardSpeed()) : Target->GetVelocity().Size2D();
	float DesiredYaw = BearingYaw;
	float DesiredSpeed = MaxChaseSpeed;

	if (LungeRemainingSeconds > 0.0f)
	{
		// Straight at him, with enough on to close the gap inside the lunge.
		DesiredSpeed = FMath::Clamp(TargetSpeed + RamClosingSpeed, MinChaseSpeed, MaxChaseSpeed);
		const float LungeElapsed = RamLungeSeconds - LungeRemainingSeconds;
		// Either signal counts: her planks on his (the swept move of the previous frame ran into him),
		// or a lunge that closed to inside the gate.
		const bool bTouched = LastBlockingActor.Get() == Target || Gap <= RamContactDistance;
		if (LungeElapsed >= RamContactMinLungeSeconds && bTouched)
		{
			LandRam(Target);
			LungeRemainingSeconds = 0.0f;
			// She pulls off after a hit rather than sawing at the same plank.
			SinceLungeSeconds = -RamRecoverySeconds;
		}
	}
	else
	{
		// Station keeping: sit off whichever beam she is already nearer and MATCH his speed, so the
		// player sails with a navy hull on his shoulder instead of being run down from astern.
		const FVector TargetRight = Target->GetActorRightVector();
		const float Side = FVector::DotProduct(ToTarget, TargetRight) > 0.0f ? -1.0f : 1.0f;
		const FVector Station = There + TargetRight * (Side * AlongsideDistance);
		const FVector ToStation = FVector(Station.X - Here.X, Station.Y - Here.Y, 0.0f);
		const float StationGap = ToStation.Size();
		if (StationGap > CloseInDistance)
		{
			DesiredYaw = ToStation.Rotation().Yaw;
			DesiredSpeed = MaxChaseSpeed;
		}
		else
		{
			// On station: run his course, and trim the speed by how far off station she is, which is
			// what makes "alongside" hold instead of oscillating past him.
			DesiredYaw = Target->GetActorRotation().Yaw;
			DesiredSpeed = FMath::Clamp(TargetSpeed + (StationGap - AlongsideDistance * 0.5f) * 0.8f,
				MinChaseSpeed, MaxChaseSpeed);
		}
	}

	DriveTowards(DesiredYaw, DesiredSpeed, DeltaSeconds);
}

void AWrecktidePatrolBoat::LandRam(AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	const FVector Shove = (FVector(TargetActor->GetActorLocation().X - GetActorLocation().X,
		TargetActor->GetActorLocation().Y - GetActorLocation().Y, 0.0f)).GetSafeNormal();

	int32 Dealt = 0;
	if (AWrecktideBoat* Boat = Cast<AWrecktideBoat>(TargetActor))
	{
		// The shipwright's Timbers tiers are the answer to this (spec S4): the multiplier is the
		// boat's own, so the price of a tier and the damage it saves can never disagree.
		Dealt = FMath::Max(FMath::RoundToInt(RamHullDamage * Boat->GetRamResistMultiplier()), 1);
		Boat->ApplyHullDamage(Dealt, TEXT("NavyPatrolRam"));
		Boat->ApplyRamShove(Shove, RamKnockbackDistance);
	}
	else if (ACombatCharacter* Swimmer = Cast<ACombatCharacter>(TargetActor))
	{
		// A swimmer has no hull to hole: she throws him aside instead. She still never boards.
		Swimmer->LaunchCharacter(Shove * RamSwimmerLaunchSpeed + FVector(0.0f, 0.0f, 150.0f), true, false);
	}

	++RamContactCount;
	RamDamageDealt += Dealt;
	SinceContactSeconds = 0.0f;
	ShowToast(TEXT("Navy patrol!"));
	UWrecktideAudio::PlaySfx(this, TEXT("wood"));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-PATROL ram contact=%d damage=%d target=%s gap=%.0f"),
		RamContactCount, Dealt, *TargetActor->GetName(),
		FVector::Dist2D(TargetActor->GetActorLocation(), GetActorLocation()));
}

void AWrecktidePatrolBoat::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}

void AWrecktidePatrolBoat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}
	// Her wheel ticks BEFORE the route check: a patrol with no route is already an error state that
	// logs, and a wheel frozen hard over on top of it would only hide it.
	TickHelm(DeltaSeconds);
	if (Route.Num() == 0)
	{
		return;
	}

	StateElapsedSeconds += DeltaSeconds;
	if (SinceContactSeconds < NeverTouchedSeconds)
	{
		SinceContactSeconds += DeltaSeconds;
	}

	switch (PatrolState)
	{
	case EPatrolBoatState::Patrolling:
	{
		const FVector Waypoint = Route[WaypointIndex];
		const FVector ToWaypoint = FVector(Waypoint.X - GetActorLocation().X, Waypoint.Y - GetActorLocation().Y, 0.0f);
		if (ToWaypoint.Size() <= WaypointArrivalRadius)
		{
			AdvanceWaypoint();
		}
		DriveTowards(ToWaypoint.IsNearlyZero() ? GetActorRotation().Yaw : ToWaypoint.Rotation().Yaw, CruiseSpeed, DeltaSeconds);
		TickAggroSearch();
		break;
	}

	case EPatrolBoatState::Hostile:
		TickHostile(DeltaSeconds);
		break;

	case EPatrolBoatState::Returning:
	{
		// Home is the nearest point of the loop, not the point she left it at: a chase that ended on
		// the far side of the island should rejoin the route where she is, not sail back around it.
		int32 Nearest = WaypointIndex;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Route.Num(); ++Index)
		{
			const float DistanceSquared = FVector::DistSquared2D(Route[Index], GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				Nearest = Index;
			}
		}
		WaypointIndex = Nearest;
		const FVector ToWaypoint = FVector(Route[Nearest].X - GetActorLocation().X, Route[Nearest].Y - GetActorLocation().Y, 0.0f);
		DriveTowards(ToWaypoint.IsNearlyZero() ? GetActorRotation().Yaw : ToWaypoint.Rotation().Yaw, CruiseSpeed, DeltaSeconds);
		if (DistanceToRoute(GetActorLocation()) <= RouteArrivalRadius)
		{
			AdvanceWaypoint();
			SetPatrolState(EPatrolBoatState::Patrolling, TEXT("back on station"));
		}
		else
		{
			TickAggroSearch();
		}
		break;
	}
	}

	MoveHull(DeltaSeconds);
}
