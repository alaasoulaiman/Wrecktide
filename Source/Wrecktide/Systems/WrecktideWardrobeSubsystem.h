// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.
// Wrecktide wardrobe: the player's outfit catalogue, selection and persistence.
// Contract shared by the wardrobe feature (implements it) and the menu screens (call it).
// Until the wardrobe feature lands every call is a safe no-op: empty catalogue, false on set.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WrecktideWardrobeSubsystem.generated.h"

class ACharacter;
class ACameraActor;
class ACombatCharacter;
class AWrecktidePlayerState;
class UMaterialInterface;
class USkeletalMesh;
struct FOutfitTableRow;

/** What happened when the player pressed Enter on a locked wardrobe row. */
UENUM(BlueprintType)
enum class EWrecktideOutfitBuyResult : uint8
{
	Bought,
	AlreadyOwned,
	LevelTooLow,
	NotEnoughGold,
	Unavailable
};

USTRUCT(BlueprintType)
struct FWrecktideOutfitInfo
{
	GENERATED_BODY()

	/** Row id from Content/Data/Outfits.csv (e.g. "Captain"). */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe")
	FName Id;

	/** Player-facing name ("Pirate Captain"). */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe")
	FText DisplayName;

	/** One line of flavour for the wardrobe screen. */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe")
	FText Description;

	/** Number of colourways available for this outfit (variant indices 0..VariantCount-1). */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe")
	int32 VariantCount = 1;

	/** Player level this outfit unlocks at (Outfits.csv RequiredLevel). */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe")
	int32 RequiredLevel = 1;

	/** Gold the tailor charges for it (Outfits.csv Price). */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe")
	int64 Price = 0;

	/** True when the player already owns it and Enter simply wears it. */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe")
	bool bOwned = false;
};

UCLASS()
class WRECKTIDE_API UWrecktideWardrobeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The catalogue in display order (Content/Data/Outfits.csv). Empty until the feature lands. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	TArray<FWrecktideOutfitInfo> GetOutfits() const;

	/** Currently persisted outfit for the local player (NAME_None = the default pirate). */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	FName GetCurrentOutfitId(const UObject* WorldContextObject) const;

	/**
	 * What the pirate is ACTUALLY wearing: the persisted id, or the default outfit when the player
	 * has never chosen one (a fresh save persists nothing but ApplyPersistedOutfit still dresses
	 * him). NAME_None only when even the default row is missing from the catalogue.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	FName GetEffectiveOutfitId(const UObject* WorldContextObject) const;

	/** Currently persisted colourway index for the local player. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	int32 GetCurrentVariant(const UObject* WorldContextObject) const;

	/** Player-facing colourway name for the wardrobe screen ("Weathered", "Crimson", ...). */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	FText GetVariantName(FName OutfitId, int32 Variant) const;

	/**
	 * What the Colourway row shows: "4 of 12: Crimson". The count is the position in the palette,
	 * one-based, so the row says where the player is as well as what he is looking at.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	FText GetVariantRowLabel(FName OutfitId, int32 Variant) const;

	/** How many colourways exist in total (variant 0 = the outfit's own colours, then the atlas). */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	int32 GetVariantCount() const { return VariantMaterials.Num(); }

	/**
	 * Dresses the pirate WITHOUT persisting anything: the live preview the wardrobe screen shows
	 * while the player moves the cursor or cycles colourways. A repeated call with the same pair is
	 * a no-op, so it is safe to call from every row rebuild. EndPreview puts the worn outfit back,
	 * so browsing away and pressing Esc changes nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool PreviewOutfit(const UObject* WorldContextObject, FName OutfitId, int32 Variant);

	/**
	 * Wears OutfitId / Variant on the local player's pirate right now and persists the choice through
	 * the save system. Returns false (and changes nothing) for an unknown id or variant.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool SetOutfit(const UObject* WorldContextObject, FName OutfitId, int32 Variant);

	/** Called by the pirate on spawn, respawn and after a save load so it wears the persisted choice. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	void ApplyPersistedOutfit(ACharacter* Character);

	/** Wardrobe camera: a front view of the pirate while the wardrobe screen is open. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	void BeginPreview(const UObject* WorldContextObject);

	/** Restores the gameplay camera. Safe to call when no preview is active. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	void EndPreview(const UObject* WorldContextObject);

	/**
	 * How many colourways the player has earned: 3 from level 1, then one more every 8 levels, so the
	 * twelfth opens at 72 and the last outfit at 75 arrives with the full palette (spec C9 item 3).
	 */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	int32 GetUnlockedVariantCount(int32 Level) const;

	/** The level at which colourway number Count + 1 opens. 0 when the palette is already complete. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	int32 GetNextVariantLevel(int32 UnlockedCount) const;

	/** The level the wardrobe gates on, straight from the player state (see GetGatingLevel). */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	int32 GetPlayerGatingLevel(const UObject* WorldContextObject) const;

	/** True when the player owns this row (a StartOwned row counts once the wardrobe has granted it). */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool IsOutfitOwned(const UObject* WorldContextObject, FName OutfitId) const;

	/**
	 * Buys OutfitId with gold through UEconomyComponent::ApplyGoldDelta (reason ShopPurchase, context
	 * the row name), after the level gate. Ownership is recorded on the progress component, which
	 * schedules the save. Wearing it is the caller's job, so a refusal changes nothing at all.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	EWrecktideOutfitBuyResult BuyOutfit(const UObject* WorldContextObject, FName OutfitId);

	/** Grants every StartOwned row the table carries. Idempotent; called when the pirate is dressed. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	void GrantStartingOutfits(const UObject* WorldContextObject);

	/** True while the pirate is standing in the change room instead of out in the world. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool IsInChangeRoom() const { return bPawnInChangeRoom; }

	/** Where the pirate walked in from; what the save must store while the room has him. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	FVector GetChangeRoomReturnLocation() const { return PawnTransformBeforeRoom.GetLocation(); }

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	AWrecktidePlayerState* FindPlayerState(const UObject* WorldContextObject) const;
	ACombatCharacter* FindPlayerCharacter(const UObject* WorldContextObject) const;
	const FOutfitTableRow* FindOutfit(FName OutfitId) const;
	int32 GetMaterialIndexForVariant(const USkeletalMesh* SkinMesh, int32 Variant) const;
	bool ApplyVariantMaterials(ACombatCharacter* Character, USkeletalMesh* SkinMesh, int32 Variant) const;
	bool ApplyOutfitToCharacter(ACombatCharacter* Character, const FOutfitTableRow& Row, int32 Variant,
		bool bDeferInitialMeshApply) const;

	/** Builds the room the first time it is needed in this world (and again after a level restart). */
	AActor* EnsureChangeRoom(UWorld& World);

	/** Moves the pirate into the room, remembering exactly where he came from. False = he never moved. */
	bool EnterChangeRoom(const UObject* WorldContextObject);

	/** Puts him back on the spot EnterChangeRoom took him from. No-op when he never left. */
	void LeaveChangeRoom(const UObject* WorldContextObject);

	/** The loadable atlas instances, in 01_A, 01_B, ... 04_C order. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> VariantMaterials;

	/** Player-facing suffix for each successfully loaded material, parallel to VariantMaterials. */
	TArray<FString> VariantLabels;

	/** Player-facing colour NAME for each loaded material, parallel to VariantMaterials. */
	TArray<FString> VariantColourNames;

	TWeakObjectPtr<ACameraActor> PreviewCamera;

	/** The pair the live preview is currently showing, so a rebuild does not re-dress the pirate. */
	FName PreviewedOutfitId;
	int32 PreviewedVariant = INDEX_NONE;

	/** True when the preview shows something the player has not worn yet: EndPreview undoes it. */
	bool bPreviewDirty = false;

	/** BeginPreview found the pawn hidden (title screen) and showed it; EndPreview hides it again. */
	bool bPawnUnhiddenForPreview = false;

	/** The spawned change room. Weak: a level restart destroys it and the next open rebuilds it. */
	TWeakObjectPtr<AActor> ChangeRoom;

	/** Exactly where the pirate stood before the room took him, restored on the way out. */
	FTransform PawnTransformBeforeRoom = FTransform::Identity;
	TEnumAsByte<EMovementMode> PawnMovementBeforeRoom = MOVE_Walking;
	bool bPawnInChangeRoom = false;
};
