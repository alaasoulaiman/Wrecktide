// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Data/WrecktideDataRows.h"
#include "GameFramework/PlayerState.h"
#include "Systems/EconomyComponent.h"
#include "Systems/SkillComponent.h"
#include "WrecktidePlayerState.generated.h"

class UProgressComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponEquipped, FName, Row);

/**
 * Persistent Wrecktide player state for economy, skills, progress, and equipped combat data.
 *
 * Persistence (wired 2026-09-02): ONE slot per install for now. BeginPlay loads it, every gold, XP
 * and equip change schedules a debounced autosave, EndPlay flushes whatever is still dirty. Probe
 * runs get NONE of this (IsPersistenceSuppressed) so every machine gate starts from a clean slate
 * and can never write into the player's slot; -WrecktideSaveSlot=<name> opts a run back in on a
 * slot of its own, which is how the two-process -SaveProbe proves state survives a relaunch.
 */
UCLASS()
class WRECKTIDE_API AWrecktidePlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** Constructor creates the authoritative economy, skill, and progress components. */
	AWrecktidePlayerState();

	/** GetEconomy is the SINGLE authoritative read path for the player's economy component. */
	UFUNCTION(BlueprintPure, Category="Wrecktide")
	UEconomyComponent* GetEconomy() const;

	/** GetSkill is the SINGLE authoritative read path for the player's skill component. */
	UFUNCTION(BlueprintPure, Category="Wrecktide")
	USkillComponent* GetSkill() const;

	/** GetProgress is the SINGLE authoritative read path for the player's progress component. */
	UFUNCTION(BlueprintPure, Category="Wrecktide")
	UProgressComponent* GetProgress() const;

	/** CanEquipWeapon is the SINGLE authoritative notoriety gate; NAME_None is always unarmed. */
	UFUNCTION(BlueprintPure, Category="Wrecktide")
	bool CanEquipWeapon(FName Row) const;

	/**
	 * The one "player level" the content gates read (wardrobe outfits, colourways, and the smithy
	 * when it lands). The notoriety level (SPEC C4) since 2026-09-06
	 * (spec C4, lane L1) and every gate moves with it, because they all ask here.
	 */
	UFUNCTION(BlueprintPure, Category="Wrecktide")
	int32 GetGatingLevel() const;

	/** EquipWeapon is the SINGLE authoritative path; NAME_None unequips and still broadcasts. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide")
	bool EquipWeapon(FName Row);

	// --- the numbered weapon bar (spec C17, the 07-27 slot model) ------------------------
	// Slot 1 melee, 2 gun, 3 hex doll, 4 and 5 tonics. Slots 1 and 2 wield their own class;
	// the rest carry a hint saying what will unlock them. The ASSIGNMENT is the state that
	// persists, the equipped row follows it, so the bar and the pirate can never disagree.

	/** How many numbered slots the bar shows. */
	static constexpr int32 EquipSlotCount = 5;

	/** Row assigned to a numbered slot (1..EquipSlotCount). NAME_None means the slot is empty. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Hotbar")
	FName GetAssignedSlot(int32 Slot) const;

	/** AssignSlot is the SINGLE authoritative path that puts an owned item in a numbered slot.
	 *  Slot 1 takes Melee, slot 2 Shooting; both require ownership and notoriety and wield at once.
	 *  NAME_None clears only that slot and empties the hand if its weapon was wielded. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Hotbar")
	bool AssignSlot(int32 Slot, FName Row);

	/** Number-key path: draw the assigned blade or gun. An empty blade slot finds the best owned
	 *  blade; other empty slots return false so the caller can show GetSlotHint. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Hotbar")
	bool WieldSlot(int32 Slot);

	/** What a slot with nothing in it is waiting for; shown as the hover hint and the key toast. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Hotbar")
	static FString GetSlotHint(int32 Slot);

	/** Short label under an empty slot ("Gun", "Hex", "Tonic"). */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Hotbar")
	static FString GetSlotClassName(int32 Slot);

	/** Every slot, index 0 = slot 1. Always EquipSlotCount long; the save and the HUD read it. */
	const TArray<FName>& GetAssignedSlots() const { return AssignedSlots; }

	/** Load path: replaces the whole bar without re-equipping. Short arrays keep today's values,
	 *  so a save written before the bar existed loses nothing. */
	void RestoreAssignedSlots(const TArray<FName>& InSlots);

	/** GetAttackDamage is the SINGLE authoritative read path for player melee attack damage. */
	UFUNCTION(BlueprintPure, Category="Wrecktide")
	float GetAttackDamage() const;

	/** Combat class of the equipped weapon. Unarmed or an invalid row always means Melee. */
	UFUNCTION(BlueprintPure, Category="Wrecktide")
	EWrecktideSkill GetEquippedCombatClass() const;

	/**
	 * Tier bought on one blacksmith track for the EQUIPPED weapon (spec C10). Upgrades bind to the
	 * weapon row, so an unarmed pirate and a freshly bought weapon both read 0 on every track.
	 */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Smithy")
	int32 GetEquippedUpgradeTier(EWrecktideUpgradeTrack Track) const;

	/**
	 * Attack montage play rate from Balance. Tiers past BalanceFullValueTiers pay HALF a step, the
	 * diminishing return God of War gives Cooldown, so attack speed stays a choice rather than the
	 * obvious first buy.
	 */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Smithy")
	float GetAttackSpeedMultiplier() const;

	/** Chance of a critical strike, 0..1: BaseCriticalChance plus Cruelty. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Smithy")
	float GetCriticalChance() const;

	/** Damage multiplier ON a critical: BaseCriticalMultiplier plus Brutal. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Smithy")
	float GetCriticalMultiplier() const;

	/** Fraction of damage dealt healed back by Bloodletting; 0 when the track is untouched. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Smithy")
	float GetLifestealFraction() const;

	/**
	 * Forces the crit roll for a dev probe: 1 always crits, 0 never does, negative restores the real
	 * chance. NOT a UFUNCTION and not on any command line of its own; the only caller is -SmithProbe,
	 * which lives inside the Shipping/Test guard, so nothing can reach this in a shipped build.
	 */
	void SetProbeCriticalChanceOverride(float Chance) { ProbeCriticalChanceOverride = Chance; }

	/** The slot this run loads and saves: -WrecktideSaveSlot=<name> when given, else the default. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save")
	static FString GetSaveSlot();

	/** The one real slot. Probes must never name it; RunSaveProbe refuses to run against it. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save")
	static FString GetDefaultSaveSlot();

	/**
	 * True when the command line carries any dev probe switch (exact -Name or -Name=value tokens,
	 * the same list UWrecktideRewardSubsystem registers, plus -PhotoAt). Always false in Shipping
	 * and Test builds, where the probes do not exist.
	 */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save")
	static bool IsProbeRun();

	/**
	 * True when this process must neither load nor autosave: -WrecktideNoSave, or a probe run that
	 * did not name a slot OF ITS OWN with -WrecktideSaveSlot (the default slot never counts). Keeps
	 * the probe battery deterministic and keeps probe gold out of the player's slot.
	 */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save")
	static bool IsPersistenceSuppressed();

	/** Schedules an autosave about a second out; calls inside that window ride the same save. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	void RequestSave();

	/** Writes the slot NOW through UProgressComponent::SaveToSlot. False means nothing was written. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	bool SaveNow();

	/** Loads the slot NOW through UProgressComponent::LoadFromSlot. False means no file or refused. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	bool LoadNow();

	/** New Game (2026-09-05): turns persistence OFF for the rest of this world (no EndPlay flush, no
	 *  autosave) and deletes the slot, so the level restart that follows begins from nothing. Returns
	 *  true only when the slot is verifiably gone (review 2026-09-05: a restart with the file still
	 *  there would silently continue the old game). A run whose persistence is suppressed (probes)
	 *  keeps the file untouched and reports true, there is nothing to clear. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	bool AbandonSaveForNewGame();

	/** Puts the loaded save's position on the pirate. Tries at once, then waits on the core ticker
	 *  for a pawn to exist (a load from BeginPlay can run before one is possessed, and world timers
	 *  stop under pause). Does nothing when no position is pending, so it is safe to call twice. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	void PlacePawnFromSave();

	/** How many manual Save Voyage slots the menu offers ("Slot 1" .. "Slot N"). */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save")
	static int32 GetManualSlotCount();

	/**
	 * File name of a manual slot (1..GetManualSlotCount). Under a probe run every manual slot is
	 * prefixed with the run's own slot name, so no probe can ever read or overwrite a real one.
	 */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save")
	static FString GetManualSlot(int32 Index);

	/**
	 * One summary line for the Save / Load screens: "Notoriety Lv L, G gold, D/T missions, yyyy-mm-dd hh:mm",
	 * or "Empty" when the file is not there. Reads the file EVERY time it is asked; the menu never
	 * caches a summary across a save.
	 */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save", meta=(WorldContext="WorldContextObject"))
	static FString GetSlotSummary(const UObject* WorldContextObject, const FString& SlotName);

	/** Writes the CURRENT session into a manual slot. Not gated on the autosave being healthy: a
	 *  player whose autosave failed to load must still be able to save his voyage by hand. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	bool SaveToManualSlot(int32 Index);

	/** Reads a manual slot into the live session, mirrors it into the autosave slot so Continue
	 *  reflects it, re-applies the wardrobe and puts the pirate back where he was. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	bool LoadManualSlot(int32 Index);

	/** Same, for the autosave slot itself (the Load Voyage screen's first row). */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Save")
	bool LoadAutosaveSlot();

	/** True once this PlayerState restored state from its slot in this session. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Save")
	bool WasLoadedFromSlot() const { return bLoadedFromSlot; }

	/** Stores a validated wardrobe choice and schedules it through the normal autosave path. */
	UFUNCTION(BlueprintCallable, Category="Wrecktide|Wardrobe")
	void MarkOutfitChanged(FName InOutfitId, int32 InVariant);

	/** The persisted wardrobe row, or NAME_None when the player has never chosen one. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Wardrobe")
	FName GetOutfitId() const { return OutfitId; }

	/** The persisted zero-based colourway. */
	UFUNCTION(BlueprintPure, Category="Wrecktide|Wardrobe")
	int32 GetOutfitVariant() const { return OutfitVariant; }

	/** EquippedWeaponRow is the SINGLE authoritative stored equipped weapon row. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide")
	FName EquippedWeaponRow;

	/** UnarmedDamage is the SINGLE authoritative fallback base damage when no weapon is equipped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wrecktide")
	float UnarmedDamage = 8.0f;

	/** OnWeaponEquipped is the SINGLE authoritative notification path for weapon equip changes. */
	UPROPERTY(BlueprintAssignable, Category="Wrecktide")
	FOnWeaponEquipped OnWeaponEquipped;

	/** Critical chance before Cruelty buys any. 5 points is the base the smithy tracks are tuned
	 *  against (spec C10); Cruelty adds 3 points a tier on top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wrecktide|Smithy")
	float BaseCriticalChance = 0.05f;

	/** Damage multiplier on a critical before Brutal buys any. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wrecktide|Smithy")
	float BaseCriticalMultiplier = 1.5f;

	/** Balance tiers up to and including this one pay a full step; later tiers pay half. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wrecktide|Smithy")
	int32 BalanceFullValueTiers = 3;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class UProgressComponent;

	UFUNCTION()
	void HandleGoldChangedForSave(int64 Delta, EGoldReason Reason, int64 NewTotal);

	UFUNCTION()
	void HandleSkillXPChangedForSave(EWrecktideSkill InSkill, int64 NewXP);

	UFUNCTION()
	void HandleWeaponEquippedForSave(FName Row);

	UFUNCTION()
	void HandleProgressChangedForSave();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UEconomyComponent> Economy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkillComponent> Skill;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UProgressComponent> Progress;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide|Wardrobe", meta=(AllowPrivateAccess="true"))
	FName OutfitId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide|Wardrobe", meta=(AllowPrivateAccess="true"))
	int32 OutfitVariant = 0;

	/** The weapon bar, index 0 = slot 1. Sized to EquipSlotCount by the constructor and never
	 *  resized, so every read is a plain indexed lookup. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide|Hotbar", meta=(AllowPrivateAccess="true"))
	TArray<FName> AssignedSlots;

	/** Debounce timer behind RequestSave. */
	FTimerHandle AutosaveTimer;

	/** Set by RequestSave, cleared by a successful SaveNow; EndPlay flushes when still set. */
	bool bSaveDirty = false;

	/** Set by a successful LoadNow. */
	bool bLoadedFromSlot = false;

	/** True after BeginPlay decided this run persists (authority and not suppressed). */
	bool bPersistenceActive = false;

	/** Set only by SetProbeCriticalChanceOverride; negative means the real chance applies. */
	float ProbeCriticalChanceOverride = -1.0f;

	/** Tier bought and per-tier step for one track on the equipped weapon. False when the weapon,
	 *  the table row or the progress component is missing, and then nothing is applied. */
	bool GetTrackState(EWrecktideUpgradeTrack Track, int32& OutTier, float& OutStep) const;

	/** Shared body behind LoadManualSlot / LoadAutosaveSlot. */
	bool LoadSlotInternal(const FString& Slot, bool bMirrorToAutosave);

	/** One immediate attempt at the teleport. False when there is no pawn or nothing pending. */
	bool TryPlacePawnFromSaveNow();
};
