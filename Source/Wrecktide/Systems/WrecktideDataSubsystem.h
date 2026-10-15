// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WrecktideDataSubsystem.generated.h"

class UDataTable;
struct FBoatUpgradeTableRow;
struct FChestTableRow;
struct FEnemyTableRow;
struct FMissionTableRow;
struct FNpcTableRow;
struct FOutfitTableRow;
struct FQuestTableRow;
struct FUpgradeTrackTableRow;
struct FWeaponTableRow;
enum class EWrecktideUpgradeTrack : uint8;

/**
 * Loads Wrecktide CSV tables for runtime gameplay data.
 */
UCLASS()
class WRECKTIDE_API UWrecktideDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** GetWeaponRow is the SINGLE authoritative read path for weapon table rows. */
	const FWeaponTableRow* GetWeaponRow(FName RowName) const;

	/** GetEnemyRow is the SINGLE authoritative read path for enemy table rows. */
	const FEnemyTableRow* GetEnemyRow(FName RowName) const;

	/** GetChestRow is the SINGLE authoritative read path for runtime chest payouts. */
	const FChestTableRow* GetChestRow(FName RowName) const;

	/** GetMeleeShopRows is the SINGLE authoritative read path for purchasable melee shop rows. */
	TArray<TPair<FName, const FWeaponTableRow*>> GetMeleeShopRows() const;

	/** Every weapon row, cheapest first. The smithy needs the OWNED ones whatever their combat class,
	 *  which the melee shop filter deliberately excludes. */
	TArray<TPair<FName, const FWeaponTableRow*>> GetWeaponRows() const;

	/** GetQuestRow is the SINGLE authoritative read path for bounty table rows. */
	const FQuestTableRow* GetQuestRow(FName RowName) const;

	/** GetQuestRows is the SINGLE authoritative read path for the ordered bounty board list. */
	TArray<TPair<FName, const FQuestTableRow*>> GetQuestRows() const;

	/** GetMissionRow is the SINGLE authoritative read path for story mission rows. */
	const FMissionTableRow* GetMissionRow(FName RowName) const;

	/** GetMissionRows is the SINGLE authoritative read path for the mission chain, in Order. */
	TArray<TPair<FName, const FMissionTableRow*>> GetMissionRows() const;

	/** GetNpcRow is the SINGLE authoritative read path for one quest giver by his GiverId (C30). */
	const FNpcTableRow* GetNpcRow(FName RowName) const;

	/** GetNpcRows returns every quest giver in authored Order. */
	TArray<TPair<FName, const FNpcTableRow*>> GetNpcRows() const;

	/** GetOutfitRow is the SINGLE authoritative read path for selectable player skins. */
	const FOutfitTableRow* GetOutfitRow(FName RowName) const;

	/** GetOutfitRows returns the wardrobe catalogue in authored SortOrder. */
	TArray<TPair<FName, const FOutfitTableRow*>> GetOutfitRows() const;

	/** GetUpgradeRow is the SINGLE authoritative read path for one blacksmith track by row name. */
	const FUpgradeTrackTableRow* GetUpgradeRow(FName RowName) const;

	/** GetUpgradeRows returns the smithy's five tracks in authored SortOrder. */
	TArray<TPair<FName, const FUpgradeTrackTableRow*>> GetUpgradeRows() const;

	/** GetBoatUpgradeRow is the SINGLE authoritative read path for one shipwright track (spec S4). */
	const FBoatUpgradeTableRow* GetBoatUpgradeRow(FName RowName) const;

	/** GetBoatUpgradeRows returns the shipwright's three paths in authored SortOrder. */
	TArray<TPair<FName, const FBoatUpgradeTableRow*>> GetBoatUpgradeRows() const;

	/** The row that drives one effect. Null when the table is missing it, so the damage path simply
	 *  reads no bonus rather than guessing at a number nobody authored. */
	const FUpgradeTrackTableRow* GetUpgradeRowForTrack(EWrecktideUpgradeTrack Track, FName* OutRowName = nullptr) const;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UDataTable* LoadCsvTable(const FString& FileName, UScriptStruct* RowStruct) const;

	/**
	 * Requests every Enemies.csv skin asynchronously at startup and keeps the handle, so the first
	 * spawn of a row never loads a mesh synchronously on the game thread and garbage collection
	 * never evicts one mid-session (review 2026-09-02). ACombatEnemy still calls LoadSynchronous,
	 * which returns immediately for a resident asset.
	 */
	void PreloadEnemySkins();

	TSharedPtr<struct FStreamableHandle> SkinPreloadHandle;

	// The CSVs live under Content/Data on disk and ride in the pak as UFS (DefaultGame.ini
	// DirectoriesToAlwaysStageAsUFS, proven in the 2026-09-02 package).
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> WeaponsTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> EnemiesTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ChestsTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> LootTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> QuestsTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MissionsTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> NpcsTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> OutfitsTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> UpgradesTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> BoatUpgradesTable;
};
