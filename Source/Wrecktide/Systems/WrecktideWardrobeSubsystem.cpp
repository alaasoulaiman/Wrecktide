// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.
#include "Systems/WrecktideWardrobeSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "Variant_Combat/CombatCharacter.h"
#include "Variant_Combat/CombatPlayerController.h"

namespace
{
/** The broke start (Outfits.csv: Deckhand is level 1, free, StartOwned). A save that never chose an
 *  outfit wears this one. */
const FName DefaultOutfitId(TEXT("Deckhand"));
constexpr float PreviewDistance = 260.0f;
constexpr float PreviewHeight = 20.0f;
constexpr float PreviewBlendSeconds = 0.35f;
/** Sphere swept from the pirate out to the camera spot so the preview never sits inside geometry. */
constexpr float PreviewProbeRadius = 30.0f;
/** How far short of a blocking hit the camera stops. */
constexpr float PreviewProbeClearance = 40.0f;
/** Closer than this the shot is a nostril portrait: it stays here even in a tight corner. */
constexpr float PreviewMinDistance = 120.0f;
/** The menu panel is centred and opaque, so a camera aimed straight at the pirate frames him behind
 *  it. Yawing the shot puts him in the right-hand third, clear of the parchment. */
constexpr float PreviewFrameYaw = 30.0f;

/**
 * The change room. Spawned at runtime (the level belongs to one lane per wave and is never saved
 * from here), far out to sea and high up so nothing of the island can be seen over its walls.
 */
const FVector ChangeRoomOrigin(0.0f, 40000.0f, 5000.0f);
/** 1,200 x 1,200 floor, so 600 from the middle to a wall. */
constexpr float ChangeRoomHalfSize = 600.0f;
constexpr float ChangeRoomWallHeight = 400.0f;
constexpr float ChangeRoomWallThickness = 20.0f;
/** Fixed shot: far enough for the whole pirate, close enough to read the cloth. */
constexpr float ChangeRoomCameraDistance = 380.0f;
/** Chest height above the actor origin (which sits at the middle of the capsule). */
constexpr float ChangeRoomCameraHeight = 55.0f;
/** Colourway ladder (spec C9 item 3): three from level 1, one more every eight levels. */
constexpr int32 StartingVariantCount = 3;
constexpr int32 VariantLevelStep = 8;

/** The lantern is warm and local; the fill keeps the shadow side off black without flattening it. */
constexpr float ChangeRoomLanternIntensity = 12000.0f;
constexpr float ChangeRoomFillIntensity = 4000.0f;

const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
const TCHAR* BasicMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
/** Synty dressing. Missing pack = the room still works, it is just bare (fail open, never closed). */
const TCHAR* ChangeRoomChestPath = TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Chest_01.SM_Prop_Chest_01");
const TCHAR* ChangeRoomBarrelPath = TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Barrel_01.SM_Prop_Barrel_01");

/**
 * Player-facing names for the twelve atlas instances, in the SAME order they load (01_A .. 04_C).
 * "Colourway 3B" told the player nothing; these are the words the wardrobe row says. Original
 * colour words only, nothing borrowed from any film or brand.
 */
const TCHAR* VariantColourWords[] =
{
	TEXT("Weathered"), TEXT("Seafoam"),  TEXT("Ash"),
	TEXT("Crimson"),   TEXT("Indigo"),   TEXT("Bone"),
	TEXT("Rust"),      TEXT("Moss"),     TEXT("Slate"),
	TEXT("Wine"),      TEXT("Sand"),     TEXT("Midnight")
};

/**
 * The camera manager does not tick while the world is paused, so a view target set from a menu never
 * reached the screen: the wardrobe screen kept showing the gameplay (or scenic) view. One manual
 * update pushes the new target into the camera cache the renderer reads.
 */
void PushCameraUpdateWhilePaused(const UWorld& World, APlayerController& PlayerController)
{
	if (World.IsPaused() && PlayerController.PlayerCameraManager)
	{
		PlayerController.PlayerCameraManager->UpdateCamera(0.0f);
	}
}
}

void UWrecktideWardrobeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	VariantMaterials.Reset();
	VariantLabels.Reset();
	VariantColourNames.Reset();
	int32 LoadOrder = 0;
	for (int32 Palette = 1; Palette <= 4; ++Palette)
	{
		for (const TCHAR Letter : {TEXT('A'), TEXT('B'), TEXT('C')})
		{
			const FString AssetName = FString::Printf(TEXT("MI_PolygonPirates_%02d_%c"), Palette, Letter);
			const FString ObjectPath = FString::Printf(TEXT("/Game/PolygonPirates/Materials/%s.%s"), *AssetName, *AssetName);
			const int32 WordIndex = LoadOrder++;
			if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath))
			{
				VariantMaterials.Add(Material);
				VariantLabels.Add(FString::Printf(TEXT("%d%c"), Palette, Letter));
				// The colour word is chosen by the asset's place in the pack, not by the index it
				// ends up at: one missing instance must not rename all the ones after it.
				VariantColourNames.Add(WordIndex < static_cast<int32>(UE_ARRAY_COUNT(VariantColourWords))
					? FString(VariantColourWords[WordIndex]) : FString::Printf(TEXT("%d%c"), Palette, Letter));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE material failed to load: %s"), *ObjectPath);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE loaded variants=%d"), VariantMaterials.Num());
}

void UWrecktideWardrobeSubsystem::Deinitialize()
{
	if (PreviewCamera.IsValid())
	{
		PreviewCamera->Destroy();
		PreviewCamera.Reset();
	}
	VariantMaterials.Reset();
	VariantLabels.Reset();
	VariantColourNames.Reset();
	Super::Deinitialize();
}

TArray<FWrecktideOutfitInfo> UWrecktideWardrobeSubsystem::GetOutfits() const
{
	TArray<FWrecktideOutfitInfo> Result;
	const UWrecktideDataSubsystem* DataSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!DataSubsystem)
	{
		return Result;
	}
	// Ownership rides along with the catalogue so the screen never has to ask twice per row.
	const AWrecktidePlayerState* PlayerState = FindPlayerState(nullptr);
	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;

	for (const TPair<FName, const FOutfitTableRow*>& Pair : DataSubsystem->GetOutfitRows())
	{
		if (!Pair.Value)
		{
			continue;
		}

		FWrecktideOutfitInfo& Info = Result.AddDefaulted_GetRef();
		Info.Id = Pair.Key;
		Info.DisplayName = Pair.Value->DisplayName;
		Info.Description = Pair.Value->Description;
		Info.VariantCount = VariantMaterials.Num();
		Info.RequiredLevel = Pair.Value->RequiredLevel;
		Info.Price = Pair.Value->Price;
		Info.bOwned = Progress && Progress->OwnsOutfit(Pair.Key);
	}
	return Result;
}

FName UWrecktideWardrobeSubsystem::GetCurrentOutfitId(const UObject* WorldContextObject) const
{
	const AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	return PlayerState ? PlayerState->GetOutfitId() : NAME_None;
}

FName UWrecktideWardrobeSubsystem::GetEffectiveOutfitId(const UObject* WorldContextObject) const
{
	const FName Persisted = GetCurrentOutfitId(WorldContextObject);
	if (!Persisted.IsNone())
	{
		return Persisted;
	}
	// Nothing was ever chosen: ApplyPersistedOutfit dresses the pirate in the default outfit without
	// writing it, so that is what he is wearing and what the screen must act on.
	return FindOutfit(DefaultOutfitId) ? DefaultOutfitId : NAME_None;
}

int32 UWrecktideWardrobeSubsystem::GetCurrentVariant(const UObject* WorldContextObject) const
{
	const AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	return PlayerState ? PlayerState->GetOutfitVariant() : 0;
}

FText UWrecktideWardrobeSubsystem::GetVariantName(FName OutfitId, int32 Variant) const
{
	const FOutfitTableRow* Row = FindOutfit(OutfitId);
	if (!Row || Variant < 0 || Variant >= VariantMaterials.Num())
	{
		return FText::GetEmpty();
	}

	if (Variant == 0)
	{
		// Variant 0 always means "the outfit's own colours", whether or not those happen to be one
		// of the shared atlas instances. Naming it after an instance mislabelled it.
		return FText::FromString(TEXT("Default"));
	}

	USkeletalMesh* SkinMesh = Row->SkinMesh.LoadSynchronous();
	const int32 MaterialIndex = GetMaterialIndexForVariant(SkinMesh, Variant);
	return VariantLabels.IsValidIndex(MaterialIndex)
		? FText::FromString(FString::Printf(TEXT("Colourway %s"), *VariantLabels[MaterialIndex]))
		: FText::GetEmpty();
}

FText UWrecktideWardrobeSubsystem::GetVariantRowLabel(FName OutfitId, int32 Variant) const
{
	const int32 Count = VariantMaterials.Num();
	if (Count == 0 || Variant < 0 || Variant >= Count)
	{
		return FText::GetEmpty();
	}

	FString ColourName(TEXT("Default"));
	if (Variant > 0)
	{
		const FOutfitTableRow* Row = FindOutfit(OutfitId);
		USkeletalMesh* SkinMesh = Row ? Row->SkinMesh.LoadSynchronous() : nullptr;
		const int32 MaterialIndex = GetMaterialIndexForVariant(SkinMesh, Variant);
		if (VariantColourNames.IsValidIndex(MaterialIndex))
		{
			ColourName = VariantColourNames[MaterialIndex];
		}
		else if (VariantLabels.IsValidIndex(MaterialIndex))
		{
			ColourName = VariantLabels[MaterialIndex];
		}
	}

	// One-based for the player: variant 0 is the first of the twelve, not the zeroth.
	return FText::FromString(FString::Printf(TEXT("%d of %d: %s"), Variant + 1, Count, *ColourName));
}

bool UWrecktideWardrobeSubsystem::PreviewOutfit(const UObject* WorldContextObject, FName OutfitId, int32 Variant)
{
	if (OutfitId == PreviewedOutfitId && Variant == PreviewedVariant)
	{
		// Already showing it. Row rebuilds run on every keypress; re-dressing the pirate each time
		// would reload the skin and restart the retargeter for nothing.
		return true;
	}

	const FOutfitTableRow* Row = FindOutfit(OutfitId);
	ACombatCharacter* Character = FindPlayerCharacter(WorldContextObject);
	if (!Row || !Character || Variant < 0 || Variant >= VariantMaterials.Num())
	{
		return false;
	}

	if (!ApplyOutfitToCharacter(Character, *Row, Variant, false))
	{
		return false;
	}

	PreviewedOutfitId = OutfitId;
	PreviewedVariant = Variant;
	// Dirty unless the preview happens to match what is actually worn: only then is there nothing
	// for EndPreview to put back.
	const AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	bPreviewDirty = !PlayerState || PlayerState->GetOutfitId() != OutfitId || PlayerState->GetOutfitVariant() != Variant;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE colourway preview outfit=%s variant=%d name=%s dirty=%d"),
		*OutfitId.ToString(), Variant, *GetVariantRowLabel(OutfitId, Variant).ToString(), bPreviewDirty ? 1 : 0);
	return true;
}

bool UWrecktideWardrobeSubsystem::SetOutfit(const UObject* WorldContextObject, FName OutfitId, int32 Variant)
{
	const FOutfitTableRow* Row = FindOutfit(OutfitId);
	ACombatCharacter* Character = FindPlayerCharacter(WorldContextObject);
	AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	if (!Row || !Character || !PlayerState || Variant < 0 || Variant >= VariantMaterials.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE rejected outfit=%s variant=%d row=%d character=%d playerstate=%d count=%d"),
			*OutfitId.ToString(), Variant, Row != nullptr, Character != nullptr, PlayerState != nullptr, VariantMaterials.Num());
		return false;
	}

	if (!ApplyOutfitToCharacter(Character, *Row, Variant, false))
	{
		return false;
	}

	PlayerState->MarkOutfitChanged(OutfitId, Variant);
	// What is worn IS what is previewed now, so leaving the screen has nothing to undo.
	PreviewedOutfitId = OutfitId;
	PreviewedVariant = Variant;
	bPreviewDirty = false;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE selected outfit=%s variant=%d"), *OutfitId.ToString(), Variant);
	return true;
}

void UWrecktideWardrobeSubsystem::ApplyPersistedOutfit(ACharacter* Character)
{
	ACombatCharacter* CombatCharacter = Cast<ACombatCharacter>(Character);
	AWrecktidePlayerState* PlayerState = FindPlayerState(Character);
	if (!CombatCharacter || !PlayerState)
	{
		return;
	}

	FName OutfitId = PlayerState->GetOutfitId();
	int32 Variant = PlayerState->GetOutfitVariant();
	const bool bNeverSelected = OutfitId.IsNone();
	if (bNeverSelected)
	{
		OutfitId = DefaultOutfitId;
		Variant = 0;
	}

	// A broke pirate still owns the clothes he stands in: the StartOwned rows are granted here, the
	// one place every spawn, respawn and load already goes through.
	GrantStartingOutfits(Character);

	const FOutfitTableRow* Row = FindOutfit(OutfitId);
	if (!Row || Variant < 0 || Variant >= VariantMaterials.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE persisted choice invalid outfit=%s variant=%d; using the default outfit at variant 0"),
			*OutfitId.ToString(), Variant);
		OutfitId = DefaultOutfitId;
		Variant = 0;
		Row = FindOutfit(OutfitId);
		if (!bNeverSelected)
		{
			PlayerState->MarkOutfitChanged(OutfitId, Variant);
		}
	}

	if (!Row || !ApplyOutfitToCharacter(CombatCharacter, *Row, Variant, true))
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE could not apply persisted outfit=%s variant=%d"),
			*OutfitId.ToString(), Variant);
	}
}

int32 UWrecktideWardrobeSubsystem::GetUnlockedVariantCount(int32 Level) const
{
	const int32 Total = VariantMaterials.Num();
	if (Total <= 0)
	{
		return 0;
	}
	// Three from the first minute, then one more every eight levels: the twelfth at 72.
	const int32 Earned = StartingVariantCount + FMath::Max(0, Level) / VariantLevelStep;
	return FMath::Clamp(Earned, FMath::Min(StartingVariantCount, Total), Total);
}

int32 UWrecktideWardrobeSubsystem::GetNextVariantLevel(int32 UnlockedCount) const
{
	if (UnlockedCount >= VariantMaterials.Num())
	{
		return 0;
	}
	// The inverse of the line above: colourway number N + 1 opens at (N + 1 - 3) * 8.
	return FMath::Max(1, (UnlockedCount + 1 - StartingVariantCount) * VariantLevelStep);
}

int32 UWrecktideWardrobeSubsystem::GetPlayerGatingLevel(const UObject* WorldContextObject) const
{
	const AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	return PlayerState ? PlayerState->GetGatingLevel() : 1;
}

bool UWrecktideWardrobeSubsystem::IsOutfitOwned(const UObject* WorldContextObject, FName OutfitId) const
{
	const AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	return Progress && Progress->OwnsOutfit(OutfitId);
}

void UWrecktideWardrobeSubsystem::GrantStartingOutfits(const UObject* WorldContextObject)
{
	AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* DataSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!Progress || !DataSubsystem)
	{
		return;
	}

	for (const TPair<FName, const FOutfitTableRow*>& Pair : DataSubsystem->GetOutfitRows())
	{
		if (Pair.Value && Pair.Value->bStartOwned && Progress->GrantOutfit(Pair.Key))
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE granted starting outfit %s"), *Pair.Key.ToString());
		}
	}
}

EWrecktideOutfitBuyResult UWrecktideWardrobeSubsystem::BuyOutfit(const UObject* WorldContextObject, FName OutfitId)
{
	const FOutfitTableRow* Row = FindOutfit(OutfitId);
	AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject);
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	if (!Row || !Progress || !Economy)
	{
		return EWrecktideOutfitBuyResult::Unavailable;
	}
	if (Progress->OwnsOutfit(OutfitId))
	{
		return EWrecktideOutfitBuyResult::AlreadyOwned;
	}
	// The level gate is read BEFORE the gold gate on purpose: a player who is too low must be told to
	// go and earn notoriety, not to go and earn gold he may already have.
	if (PlayerState->GetGatingLevel() < Row->RequiredLevel)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE buy refused outfit=%s reason=level level=%d need=%d"),
			*OutfitId.ToString(), PlayerState->GetGatingLevel(), Row->RequiredLevel);
		return EWrecktideOutfitBuyResult::LevelTooLow;
	}
	if (Row->Price > 0 && !Economy->ApplyGoldDelta(-Row->Price, EGoldSource::Purchased,
		EGoldReason::ShopPurchase, OutfitId))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE buy refused outfit=%s reason=gold gold=%lld price=%lld"),
			*OutfitId.ToString(), Economy->GetGoldTotal(), Row->Price);
		return EWrecktideOutfitBuyResult::NotEnoughGold;
	}

	if (!Progress->GrantOutfit(OutfitId) && Row->Price > 0)
	{
		// Ownership failed after the gold left: put it back rather than charge for nothing (the shop
		// stall's refund rule, same reasoning).
		Economy->ApplyGoldDelta(Row->Price, EGoldSource::Purchased, EGoldReason::Adjustment, OutfitId);
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE buy failed to record ownership of %s, gold refunded"),
			*OutfitId.ToString());
		return EWrecktideOutfitBuyResult::Unavailable;
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE bought outfit=%s price=%lld gold=%lld level=%d"),
		*OutfitId.ToString(), Row->Price, Economy->GetGoldTotal(), PlayerState->GetGatingLevel());
	return EWrecktideOutfitBuyResult::Bought;
}

AActor* UWrecktideWardrobeSubsystem::EnsureChangeRoom(UWorld& World)
{
	if (AActor* Existing = ChangeRoom.Get())
	{
		// A level restart destroys the room but this subsystem outlives it, so the pointer is only
		// good while it belongs to the world asking for it.
		if (Existing->GetWorld() == &World && IsValid(Existing))
		{
			return Existing;
		}
		ChangeRoom.Reset();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Room = World.SpawnActor<AActor>(AActor::StaticClass(), FTransform(ChangeRoomOrigin), SpawnParameters);
	if (!Room)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE change room actor failed to spawn"));
		return nullptr;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Room, TEXT("ChangeRoomRoot"));
	Room->SetRootComponent(Root);
	Root->RegisterComponent();
	Room->SetActorLocation(ChangeRoomOrigin);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	UMaterialInterface* BasicMaterial = LoadObject<UMaterialInterface>(nullptr, BasicMaterialPath);

	// The engine cube is 100 uu on a side about its centre, so a scale of 1 is 100 uu.
	auto MakePiece = [Room, Root, BasicMaterial](const TCHAR* Name, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Scale, const FRotator& Rotation, const FLinearColor& Colour, bool bBlocking) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Room, FName(Name));
		if (!Piece || !Mesh)
		{
			return nullptr;
		}
		Piece->SetupAttachment(Root);
		Piece->RegisterComponent();
		Piece->SetStaticMesh(Mesh);
		Piece->SetRelativeLocation(Location);
		Piece->SetRelativeScale3D(Scale);
		Piece->SetRelativeRotation(Rotation);
		Piece->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bBlocking)
		{
			Piece->SetCollisionProfileName(TEXT("BlockAll"));
		}
		// The room is 40,000 uu from the island but the navmesh still must not try to cover it.
		Piece->SetCanEverAffectNavigation(false);
		if (BasicMaterial)
		{
			if (UMaterialInstanceDynamic* Dynamic = Piece->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BasicMaterial))
			{
				Dynamic->SetVectorParameterValue(TEXT("Color"), Colour);
			}
		}
		return Piece;
	};

	if (Cube)
	{
		const float Half = ChangeRoomHalfSize;
		const float WallZ = ChangeRoomWallHeight * 0.5f;
		const FLinearColor Timber(0.20f, 0.12f, 0.07f);
		const FLinearColor Plank(0.28f, 0.18f, 0.10f);
		const FLinearColor RugColour(0.35f, 0.09f, 0.10f);
		// Floor top sits exactly on the room origin, so the pirate's feet land on z = origin.
		MakePiece(TEXT("Floor"), Cube, FVector(0.0f, 0.0f, -10.0f), FVector(Half * 0.02f, Half * 0.02f, 0.2f),
			FRotator::ZeroRotator, Plank, true);
		MakePiece(TEXT("WallNorth"), Cube, FVector(Half, 0.0f, WallZ),
			FVector(ChangeRoomWallThickness * 0.01f, Half * 0.02f, ChangeRoomWallHeight * 0.01f), FRotator::ZeroRotator, Timber, true);
		MakePiece(TEXT("WallSouth"), Cube, FVector(-Half, 0.0f, WallZ),
			FVector(ChangeRoomWallThickness * 0.01f, Half * 0.02f, ChangeRoomWallHeight * 0.01f), FRotator::ZeroRotator, Timber, true);
		MakePiece(TEXT("WallEast"), Cube, FVector(0.0f, Half, WallZ),
			FVector(Half * 0.02f, ChangeRoomWallThickness * 0.01f, ChangeRoomWallHeight * 0.01f), FRotator::ZeroRotator, Timber, true);
		MakePiece(TEXT("WallWest"), Cube, FVector(0.0f, -Half, WallZ),
			FVector(Half * 0.02f, ChangeRoomWallThickness * 0.01f, ChangeRoomWallHeight * 0.01f), FRotator::ZeroRotator, Timber, true);
		// No ceiling ON PURPOSE: the sky light reaches in and the pirate is never a silhouette.
		MakePiece(TEXT("Rug"), Cube, FVector(0.0f, 0.0f, 1.0f), FVector(3.0f, 2.2f, 0.02f),
			FRotator::ZeroRotator, RugColour, false);
		// A folding screen of planks behind him, so the shot has depth instead of a flat wall.
		for (int32 PlankIndex = 0; PlankIndex < 3; ++PlankIndex)
		{
			const FString Name = FString::Printf(TEXT("Screen%d"), PlankIndex);
			MakePiece(*Name, Cube, FVector(-260.0f, -120.0f + PlankIndex * 110.0f, 90.0f),
				FVector(0.1f, 1.0f, 1.8f), FRotator(0.0f, PlankIndex * 12.0f - 12.0f, 0.0f), Plank, false);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE change room: %s missing, room is empty"), CubeMeshPath);
	}

	if (UStaticMesh* Chest = LoadObject<UStaticMesh>(nullptr, ChangeRoomChestPath))
	{
		MakePiece(TEXT("Chest"), Chest, FVector(-150.0f, 260.0f, 0.0f), FVector::OneVector, FRotator(0.0f, -60.0f, 0.0f),
			FLinearColor::White, false);
	}
	if (UStaticMesh* Barrel = LoadObject<UStaticMesh>(nullptr, ChangeRoomBarrelPath))
	{
		MakePiece(TEXT("Barrel"), Barrel, FVector(-190.0f, -280.0f, 0.0f), FVector::OneVector, FRotator::ZeroRotator,
			FLinearColor::White, false);
	}

	// The lantern is the room's own light. The world's directional light and sky light still reach
	// in over the open top, but a room lit by one point light alone put the pirate's face in the
	// dark on the first capture, so there is a fill on the camera side as well (RECON F).
	auto MakeLight = [Room, Root](const TCHAR* Name, const FVector& Location, float Intensity, const FLinearColor& Colour)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Room, FName(Name));
		if (!Light)
		{
			return;
		}
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		Light->SetRelativeLocation(Location);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(Intensity);
		Light->SetLightColor(Colour);
		Light->SetAttenuationRadius(1600.0f);
		Light->SetCastShadows(false);
	};
	MakeLight(TEXT("Lantern"), FVector(-120.0f, 0.0f, 330.0f), ChangeRoomLanternIntensity, FLinearColor(1.0f, 0.78f, 0.48f));
	MakeLight(TEXT("Fill"), FVector(ChangeRoomCameraDistance, 0.0f, 220.0f), ChangeRoomFillIntensity, FLinearColor(0.85f, 0.90f, 1.0f));

	ChangeRoom = Room;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE change room built at %s cube=%d"),
		*ChangeRoomOrigin.ToCompactString(), Cube != nullptr ? 1 : 0);
	return Room;
}

bool UWrecktideWardrobeSubsystem::EnterChangeRoom(const UObject* WorldContextObject)
{
	if (bPawnInChangeRoom)
	{
		return true;
	}

	ACombatCharacter* Character = FindPlayerCharacter(WorldContextObject);
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || !EnsureChangeRoom(*World))
	{
		// Fail open: no room means the old in-place preview, not a stranded pirate.
		return false;
	}

	PawnTransformBeforeRoom = Character->GetActorTransform();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	PawnMovementBeforeRoom = Movement ? Movement->MovementMode : TEnumAsByte<EMovementMode>(MOVE_Walking);

	// The autosave must keep saying he is out in the world for as long as the room has him.
	if (AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject))
	{
		if (UProgressComponent* Progress = PlayerState->FindComponentByClass<UProgressComponent>())
		{
			Progress->SetSavedLocationOverride(PawnTransformBeforeRoom.GetLocation(),
				PawnTransformBeforeRoom.Rotator().Yaw);
		}
	}

	const float CapsuleHalfHeight = Character->GetCapsuleComponent()
		? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.0f;
	// Facing +X, which is where the camera stands: he looks at the player, not at a wall.
	const FVector RoomSpot = ChangeRoomOrigin + FVector(-60.0f, 0.0f, CapsuleHalfHeight + 2.0f);
	Character->SetActorLocationAndRotation(RoomSpot, FRotator(0.0f, 0.0f, 0.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
	}
	bPawnInChangeRoom = true;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE change room enter from=%s to=%s"),
		*PawnTransformBeforeRoom.GetLocation().ToCompactString(), *RoomSpot.ToCompactString());
	return true;
}

void UWrecktideWardrobeSubsystem::LeaveChangeRoom(const UObject* WorldContextObject)
{
	if (!bPawnInChangeRoom)
	{
		return;
	}
	bPawnInChangeRoom = false;

	if (AWrecktidePlayerState* PlayerState = FindPlayerState(WorldContextObject))
	{
		if (UProgressComponent* Progress = PlayerState->FindComponentByClass<UProgressComponent>())
		{
			Progress->ClearSavedLocationOverride();
		}
	}

	ACombatCharacter* Character = FindPlayerCharacter(WorldContextObject);
	if (!Character)
	{
		// He died or the level restarted while the screen was up: nothing to put back.
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE change room leave: no pawn to return"));
		return;
	}

	Character->SetActorTransform(PawnTransformBeforeRoom, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(PawnMovementBeforeRoom);
	}
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE change room leave back=%s"),
		*Character->GetActorLocation().ToCompactString());
}

void UWrecktideWardrobeSubsystem::BeginPreview(const UObject* WorldContextObject)
{
	if (PreviewCamera.IsValid())
	{
		return;
	}

	ACombatCharacter* Character = FindPlayerCharacter(WorldContextObject);
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!World || !Character || !PlayerController)
	{
		return;
	}

	// The title screen hides the pirate (his life bar sat in the middle of the scenic shot); a
	// wardrobe preview of an invisible pirate is useless, so he comes back for as long as the
	// screen is up and EndPreview hides him again if the title still owns the view.
	bPawnUnhiddenForPreview = false;
	if (Character->IsHidden())
	{
		Character->SetActorHiddenInGame(false);
		bPawnUnhiddenForPreview = true;
	}

	// The live preview starts on whatever he is actually wearing, so the first row rebuild is a
	// no-op and nothing is pending an undo.
	PreviewedOutfitId = GetEffectiveOutfitId(WorldContextObject);
	PreviewedVariant = GetCurrentVariant(WorldContextObject);
	bPreviewDirty = false;

	// Into the change room first: the shot below is framed on where he ENDS UP, not where he was.
	const bool bInRoom = EnterChangeRoom(WorldContextObject);

	const FVector LookAt = Character->GetActorLocation()
		+ FVector(0.0f, 0.0f, bInRoom ? ChangeRoomCameraHeight : PreviewHeight);
	FVector CameraLocation = LookAt + Character->GetActorForwardVector()
		* (bInRoom ? ChangeRoomCameraDistance : PreviewDistance);

	// The pirate can be standing against a wall, a stall or the pier rail: sweep out to the spot and
	// stop short of the first blocker, so the preview is never framed from inside geometry. The
	// change room is built around the shot, so there the fixed distance is the right one.
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WrecktideWardrobePreviewCamera), false, Character);
	QueryParams.AddIgnoredActor(Character);
	if (!bInRoom && World->SweepSingleByChannel(Hit, LookAt, CameraLocation, FQuat::Identity, ECC_Camera,
		FCollisionShape::MakeSphere(PreviewProbeRadius), QueryParams))
	{
		const float Pulled = FMath::Clamp(Hit.Distance - PreviewProbeClearance, PreviewMinDistance, PreviewDistance);
		CameraLocation = LookAt + Character->GetActorForwardVector() * Pulled;
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-WARDROBE preview camera pulled in to %.0f uu (blocked by %s)"),
			Pulled, *GetNameSafe(Hit.GetActor()));
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FRotator LookRotation = (LookAt - CameraLocation).Rotation();
	LookRotation.Yaw += PreviewFrameYaw;
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(CameraLocation, LookRotation, SpawnParameters);
	if (!Camera)
	{
		return;
	}

	PreviewCamera = Camera;
	PlayerController->SetViewTargetWithBlend(Camera, World->IsPaused() ? 0.0f : PreviewBlendSeconds);
	PushCameraUpdateWhilePaused(*World, *PlayerController);
}

void UWrecktideWardrobeSubsystem::EndPreview(const UObject* WorldContextObject)
{
	// A browsed-but-not-worn look is undone here: the pirate leaves the screen wearing what the save
	// says he wears, never the row the cursor happened to rest on.
	if (bPreviewDirty)
	{
		bPreviewDirty = false;
		ApplyPersistedOutfit(FindPlayerCharacter(WorldContextObject));
	}
	PreviewedOutfitId = NAME_None;
	PreviewedVariant = INDEX_NONE;

	// Out of the change room BEFORE the view goes back to the pawn, or the first frame after the
	// screen closes looks at him from 40,000 uu away.
	LeaveChangeRoom(WorldContextObject);

	// Re-hide OUTSIDE the camera guard: a preview whose camera failed to spawn still showed the
	// pirate, and only the title screen wants him hidden - after Continue he must stay visible.
	if (bPawnUnhiddenForPreview)
	{
		bPawnUnhiddenForPreview = false;
		ACombatCharacter* Character = FindPlayerCharacter(WorldContextObject);
		UWorld* CharacterWorld = Character ? Character->GetWorld() : nullptr;
		const ACombatPlayerController* Controller = CharacterWorld
			? Cast<ACombatPlayerController>(UGameplayStatics::GetPlayerController(CharacterWorld, 0)) : nullptr;
		if (Character && Controller && Controller->IsPawnHiddenForTitle())
		{
			Character->SetActorHiddenInGame(true);
		}
	}

	ACameraActor* Camera = PreviewCamera.Get();
	if (!Camera)
	{
		PreviewCamera.Reset();
		return;
	}

	UWorld* World = Camera->GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (PlayerController && PlayerPawn)
	{
		PlayerController->SetViewTargetWithBlend(PlayerPawn, World->IsPaused() ? 0.0f : PreviewBlendSeconds);
		PushCameraUpdateWhilePaused(*World, *PlayerController);
	}
	Camera->Destroy();
	PreviewCamera.Reset();
}

AWrecktidePlayerState* UWrecktideWardrobeSubsystem::FindPlayerState(const UObject* WorldContextObject) const
{
	const UObject* Context = WorldContextObject ? WorldContextObject : static_cast<const UObject*>(this);
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	return PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
}

ACombatCharacter* UWrecktideWardrobeSubsystem::FindPlayerCharacter(const UObject* WorldContextObject) const
{
	const UObject* Context = WorldContextObject ? WorldContextObject : static_cast<const UObject*>(this);
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	return PlayerController ? Cast<ACombatCharacter>(PlayerController->GetPawn()) : nullptr;
}

const FOutfitTableRow* UWrecktideWardrobeSubsystem::FindOutfit(FName OutfitId) const
{
	const UWrecktideDataSubsystem* DataSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	return DataSubsystem ? DataSubsystem->GetOutfitRow(OutfitId) : nullptr;
}

int32 UWrecktideWardrobeSubsystem::GetMaterialIndexForVariant(const USkeletalMesh* SkinMesh, int32 Variant) const
{
	if (!SkinMesh || Variant < 0 || Variant >= VariantMaterials.Num())
	{
		return INDEX_NONE;
	}

	const TArray<FSkeletalMaterial>& DefaultMaterials = SkinMesh->GetMaterials();
	const UMaterialInterface* DefaultMaterial = DefaultMaterials.Num() > 0 ? DefaultMaterials[0].MaterialInterface : nullptr;
	const int32 DefaultIndex = VariantMaterials.IndexOfByPredicate([DefaultMaterial](const TObjectPtr<UMaterialInterface>& Material)
	{
		return Material.Get() == DefaultMaterial;
	});
	if (Variant == 0)
	{
		// INDEX_NONE is the honest answer for a mesh whose default is not an atlas instance: the
		// callers treat variant 0 as "restore the mesh's own materials" anyway.
		return DefaultIndex;
	}

	int32 LogicalVariant = 1;
	for (int32 MaterialIndex = 0; MaterialIndex < VariantMaterials.Num(); ++MaterialIndex)
	{
		// The mesh's own material is variant 0, so it is skipped here - but ONLY when it really is
		// one of the shared instances. Skipping index 0 unconditionally when the mesh uses some
		// other material made variant 1 a duplicate of variant 0.
		if (DefaultIndex != INDEX_NONE && MaterialIndex == DefaultIndex)
		{
			continue;
		}
		if (LogicalVariant++ == Variant)
		{
			return MaterialIndex;
		}
	}
	return INDEX_NONE;
}

bool UWrecktideWardrobeSubsystem::ApplyVariantMaterials(ACombatCharacter* Character, USkeletalMesh* SkinMesh, int32 Variant) const
{
	USkeletalMeshComponent* CosmeticMesh = Character ? Character->GetCosmeticMesh() : nullptr;
	if (!CosmeticMesh || !SkinMesh || Variant < 0 || Variant >= VariantMaterials.Num())
	{
		return false;
	}

	const TArray<FSkeletalMaterial>& Defaults = SkinMesh->GetMaterials();
	if (Defaults.Num() == 0)
	{
		return false;
	}

	if (Variant == 0)
	{
		for (int32 Slot = 0; Slot < Defaults.Num(); ++Slot)
		{
			CosmeticMesh->SetMaterial(Slot, Defaults[Slot].MaterialInterface);
		}
		return true;
	}

	const int32 MaterialIndex = GetMaterialIndexForVariant(SkinMesh, Variant);
	UMaterialInterface* Material = VariantMaterials.IsValidIndex(MaterialIndex) ? VariantMaterials[MaterialIndex].Get() : nullptr;
	if (!Material)
	{
		return false;
	}
	for (int32 Slot = 0; Slot < Defaults.Num(); ++Slot)
	{
		CosmeticMesh->SetMaterial(Slot, Material);
	}
	return true;
}

bool UWrecktideWardrobeSubsystem::ApplyOutfitToCharacter(ACombatCharacter* Character, const FOutfitTableRow& Row,
	int32 Variant, bool bDeferInitialMeshApply) const
{
	USkeletalMesh* SkinMesh = Row.SkinMesh.LoadSynchronous();
	USkeletalMeshComponent* CosmeticMesh = Character ? Character->GetCosmeticMesh() : nullptr;
	if (!Character || !CosmeticMesh || !SkinMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-WARDROBE skin failed to load: %s"), *Row.SkinMesh.ToString());
		return false;
	}

	const bool bInitialApplyPending = bDeferInitialMeshApply && CosmeticMesh->GetSkeletalMeshAsset() == nullptr;
	Character->CosmeticMeshAsset = SkinMesh;
	if (!bInitialApplyPending && (CosmeticMesh->GetSkeletalMeshAsset() != SkinMesh || !Character->IsCosmeticRetargeterLive()))
	{
		Character->ApplyCosmeticMesh();
	}

	if (!ApplyVariantMaterials(Character, SkinMesh, Variant))
	{
		return false;
	}

	if (!bInitialApplyPending)
	{
		Character->RefreshWeaponVisual();
	}
	return bInitialApplyPending || (CosmeticMesh->GetSkeletalMeshAsset() == SkinMesh && Character->IsCosmeticRetargeterLive());
}
