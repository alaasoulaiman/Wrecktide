// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/SaveGame.h"
#include "Systems/SkillComponent.h"
#include "ProgressComponent.generated.h"

/**
 * Current FWrecktideSaveData layout version.
 * 1: gold, skills, quest flags, unlocked items, crew, beta veteran.
 * 2: adds EquippedWeaponRow. A version 1 save has no equipped weapon, so ApplySaveData revalidates
 *    the live value rather than forcing the player unarmed.
 * 3: adds the player's position (PlayerLocation, PlayerYaw, bHasLocation). Older saves carry
 *    bHasLocation = false and start the pirate at the PlayerStart exactly as they always did.
 * 4: adds NotorietyXP. Older saves derive it from the cumulative lifetime XP in every skill.
 * 4: adds OwnedOutfits (the earned wardrobe). Deliberately NOT gated on this number: the migration
 *    reads the DATA (an empty list can never be a real saved state, every save owns at least the
 *    starting outfit), so it still does the right thing if another lane's bump lands first or the
 *    two bumps meet in a merge.
 * 5: adds WeaponUpgrades, the blacksmith tiers per weapon row (spec C10). No migration is needed
 *    and none is gated on this number: an older file simply carries no map, which reads back as the
 *    default (every track at tier 0), exactly the state a pirate who never visited the smithy is in.
 * 6: adds AssignedSlots, the numbered weapon bar (spec C17). Not gated on this number either: the
 *    migration reads the DATA, so a file with an empty array keeps whatever slot 1 the restored
 *    equipped weapon already set, which is exactly what an old save should show on the bar.
 * 6: adds the boat (spec S2 and S4): bHasBoatState, BoatHullHealth, BoatHold and BoatUpgrades.
 *    Deliberately NOT gated on this number, for the same reason the outfit migration is not: the
 *    MIGRATION READS THE DATA. bHasBoatState is false in every file written before the boat had a
 *    hull, and false means "full hull, empty hold, no shipwright tiers", which is exactly the state
 *    a pirate who never took damage at sea is in. Other lanes bump this number in the same wave, so
 *    whichever bump lands first, these fields still read back correctly.
 */
#define WRECKTIDE_SAVE_VERSION 6

/**
 * Blacksmith tiers for ONE weapon row: track row name (Edge, Balance, ...) to tier 0..5. A struct
 * rather than a nested container because UE cannot serialise a TMap of TMaps directly. Every member
 * carries SaveGame: the save archive skips unflagged properties even inside a nested struct.
 */
USTRUCT(BlueprintType)
struct FWrecktideWeaponUpgrades
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TMap<FName, int32> Tiers;
};

USTRUCT(BlueprintType)
struct FWrecktideSaveData
{
	GENERATED_BODY()

	/**
	 * Defaults to 0 ON PURPOSE and is stamped by BuildSaveData. The save archive skips properties
	 * that still equal their default, so a member defaulting to the current version was never
	 * written and every old file read back as current (review 2026-09-02). A file with no tag now
	 * reads as 0, which the version gates treat as "older than everything".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	int32 SaveVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	int64 EarnedGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	int64 PurchasedGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TMap<EWrecktideSkill, FSkillState> Skills;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TMap<FName, int32> QuestFlags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TArray<FName> UnlockedItems;

	/** Added in save version 2. NAME_None means unarmed. Revalidated on load, never trusted as read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	FName EquippedWeaponRow;

	/** Zero GUID means no crew. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	FGuid CrewId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	bool bBetaVeteran = false;

	/** Wardrobe row selected by the player. NAME_None preserves the original default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	FName OutfitId;

	/** Zero-based material colourway for OutfitId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	int32 OutfitVariant = 0;

	/**
	 * Added in save version 3, at the END of the struct so an older file's property list still maps.
	 * Written only when the pirate is on foot: aboard the boat or in the water the position would
	 * reload the player somewhere he cannot stand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	float PlayerYaw = 0.0f;

	/** False on every version 1 and 2 save, and on any save taken while afloat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	bool bHasLocation = false;

	/** Added in save version 4, at the END so the tagged property layout remains backward compatible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	int64 NotorietyXP = 0;
	/**
	 * Outfits the player owns (save version 4), at the END of the struct so an older file's property
	 * list still maps. EMPTY means "this save predates earned outfits": ApplySaveData then grants the
	 * outfit it was wearing plus every StartOwned row, so nobody is stripped of the clothes he has.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TArray<FName> OwnedOutfits;

	/**
	 * Blacksmith tiers, keyed by WEAPON row (save version 5), at the END of the struct so an older
	 * file's property list still maps. Upgrades bind to the weapon, not the pirate, so buying a
	 * better cutlass starts its tracks at zero and the shop and the smithy stop cannibalising each
	 * other (research brief section 6).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TMap<FName, FWrecktideWeaponUpgrades> WeaponUpgrades;

	/**
	 * The numbered weapon bar (save version 6), at the END of the struct so an older file's tagged
	 * property list still maps. Index 0 is slot 1. EMPTY means "this save predates the bar", and
	 * ApplySaveData then leaves slot 1 on whatever the restored equipped weapon set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TArray<FName> AssignedSlots;

	/**
	 * The boat (save version 6), at the END of the struct so an older file's property list still maps.
	 * bHasBoatState is the migration signal, not the version number: false means the file predates the
	 * hull, and the load then takes the full hull, the empty hold and no shipwright tiers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	bool bHasBoatState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	int32 BoatHullHealth = 0;

	/** Supply row name (Planks, Shot) to units carried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TMap<FName, int32> BoatHold;

	/** Shipwright track row name (Timbers, Canvas, Hold) to tier 0..3. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	TMap<FName, int32> BoatUpgrades;
};

UCLASS()
class WRECKTIDE_API UWrecktideSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
	FWrecktideSaveData Data;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProgressChanged);

UCLASS(ClassGroup=(Wrecktide), meta=(BlueprintSpawnableComponent))
class WRECKTIDE_API UProgressComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProgressComponent();

	/**
	 * Fires after every successful progression write made through this component (quest flag, item
	 * unlock). The PlayerState autosave listens here; ApplySaveData deliberately does NOT fire it,
	 * restoring state is not a change worth saving again.
	 */
	UPROPERTY(BlueprintAssignable, Category="Progress")
	FOnProgressChanged OnProgressChanged;

	UFUNCTION(BlueprintCallable, Category="Progress")
	void SetQuestFlag(FName Flag, int32 Value);

	UFUNCTION(BlueprintPure, Category="Progress")
	int32 GetQuestFlag(FName Flag) const;

	UFUNCTION(BlueprintCallable, Category="Progress")
	bool UnlockItem(FName ItemId);

	UFUNCTION(BlueprintPure, Category="Progress")
	bool IsItemUnlocked(FName ItemId) const;

	/** True when the player owns this wardrobe row and may wear it. */
	UFUNCTION(BlueprintPure, Category="Progress")
	bool OwnsOutfit(FName OutfitId) const;

	/** Records an outfit as owned and schedules the save. False when it was already owned. */
	UFUNCTION(BlueprintCallable, Category="Progress")
	bool GrantOutfit(FName OutfitId);

	/** Every owned row, for the wardrobe screen and the probe. */
	UFUNCTION(BlueprintPure, Category="Progress")
	const TArray<FName>& GetOwnedOutfits() const { return OwnedOutfits; }

	/** Tier owned on one blacksmith track of one weapon row, 0 when nothing was ever bought. */
	UFUNCTION(BlueprintPure, Category="Progress")
	int32 GetUpgradeTier(FName WeaponRow, FName TrackRow) const;

	/**
	 * Records a bought tier and schedules the save. Refuses NAME_None on either key and any tier
	 * outside 0..WRECKTIDE_UPGRADE_MAX_TIER, so a bad caller can never write a state the smithy
	 * cannot price. The GOLD is the stall's business; this only records what was bought.
	 */
	UFUNCTION(BlueprintCallable, Category="Progress")
	bool SetUpgradeTier(FName WeaponRow, FName TrackRow, int32 Tier);

	/** Hull the boat carries, or DefaultHull when this save has never RECORDED a hull. A stored 0
	 *  counts as never recorded: the boat resets to a full hull the moment it sinks. */
	UFUNCTION(BlueprintPure, Category="Progress")
	int32 GetBoatHull(int32 DefaultHull) const;

	/** Records the hull and schedules the save. The BOAT owns the clamp; this only records. */
	UFUNCTION(BlueprintCallable, Category="Progress")
	void SetBoatHull(int32 Hull);

	/** Units of one supply row in the hold, 0 when none. */
	UFUNCTION(BlueprintPure, Category="Progress")
	int32 GetBoatHoldUnits(FName SupplyRow) const;

	/** Records a supply count and schedules the save. A count of 0 or less drops the row. */
	UFUNCTION(BlueprintCallable, Category="Progress")
	void SetBoatHoldUnits(FName SupplyRow, int32 Units);

	/** Every supply row in the hold, for the barrel list and the probe. */
	const TMap<FName, int32>& GetBoatHold() const { return BoatHold; }

	/** Empties the hold. The sea takes it when the hull goes (spec S2.3). */
	UFUNCTION(BlueprintCallable, Category="Progress")
	void ClearBoatHold();

	/** Shipwright tier on one track, 0 when nothing was bought. */
	UFUNCTION(BlueprintPure, Category="Progress")
	int32 GetBoatUpgradeTier(FName TrackRow) const;

	/** Records a bought shipwright tier and schedules the save. Refuses NAME_None and a negative tier. */
	UFUNCTION(BlueprintCallable, Category="Progress")
	bool SetBoatUpgradeTier(FName TrackRow, int32 Tier);

	/**
	 * BuildSaveData/ApplySaveData are the ONLY bridge between live components and saved state.
	 * No PROGRESSION state exists outside FWrecktideSaveData; this format must work on a dedicated
	 * server later. The one deliberate exclusion is UEconomyComponent::Ledger, a rolling 256 entry
	 * provenance buffer that is diagnostics only and is never read to decide anything.
	 */
	UFUNCTION(BlueprintCallable, Category="Progress")
	FWrecktideSaveData BuildSaveData() const;

	/**
	 * BuildSaveData/ApplySaveData are the ONLY bridge between live components and saved state.
	 * No PROGRESSION state exists outside FWrecktideSaveData; this format must work on a dedicated
	 * server later. The one deliberate exclusion is UEconomyComponent::Ledger, a rolling 256 entry
	 * provenance buffer that is diagnostics only and is never read to decide anything.
	 */
	UFUNCTION(BlueprintCallable, Category="Progress")
	void ApplySaveData(const FWrecktideSaveData& Data);

	UFUNCTION(BlueprintCallable, Category="Progress")
	bool SaveToSlot(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category="Progress")
	bool LoadFromSlot(const FString& SlotName);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	TMap<FName, int32> QuestFlags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	TArray<FName> UnlockedItems;

	/** Earned wardrobe rows. Separate from UnlockedItems so an outfit id can never be mistaken for a
	 *  weapon row (and back), which is what the shop's ownership check reads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	TArray<FName> OwnedOutfits;

	/** Live blacksmith tiers, keyed by weapon row. Mirrored into FWrecktideSaveData by BuildSaveData. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	TMap<FName, FWrecktideWeaponUpgrades> WeaponUpgrades;

	/** Live boat state (spec S2, S4). The BOAT reads and writes these through the accessors below and
	 *  keeps no copy of its own, so there is exactly one truth and a save can never disagree with the
	 *  hull the player is looking at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	bool bHasBoatState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	int32 BoatHullHealth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	TMap<FName, int32> BoatHold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	TMap<FName, int32> BoatUpgrades;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	FGuid CrewId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Progress")
	bool bBetaVeteran = false;

	/** True when the last ApplySaveData carried a position waiting to be put on the pawn. */
	bool HasPendingPlayerLocation() const { return bHasPendingLocation; }

	FVector GetPendingPlayerLocation() const { return PendingPlayerLocation; }
	float GetPendingPlayerYaw() const { return PendingPlayerYaw; }

	/** Cleared by whoever places the pawn, so a second load cannot teleport him twice. */
	void ClearPendingPlayerLocation() { bHasPendingLocation = false; }

	/**
	 * While the wardrobe has the pirate off in its change room, THIS is where he really stands: any
	 * save written with that screen open (an autosave riding a gold change, a manual Save Voyage)
	 * must carry the world spot, never the room. Cleared when he walks back out.
	 */
	void SetSavedLocationOverride(const FVector& Location, float Yaw);
	void ClearSavedLocationOverride();
	bool HasSavedLocationOverride() const { return bHasSavedLocationOverride; }

private:
	/** Where the loaded save says the pirate was standing. Applied by AWrecktidePlayerState, which is
	 *  the only thing that knows when a pawn exists to move (the load can run before one is possessed). */
	FVector PendingPlayerLocation = FVector::ZeroVector;
	float PendingPlayerYaw = 0.0f;
	bool bHasPendingLocation = false;

	/** Set by the wardrobe change room; see SetSavedLocationOverride. */
	FVector SavedLocationOverride = FVector::ZeroVector;
	float SavedYawOverride = 0.0f;
	bool bHasSavedLocationOverride = false;
};
