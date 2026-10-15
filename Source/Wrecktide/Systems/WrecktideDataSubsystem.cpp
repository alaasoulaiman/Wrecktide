// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktideDataSubsystem.h"

#include "Data/WrecktideDataRows.h"
#include "Engine/AssetManager.h"
#include "Engine/DataTable.h"
#include "Engine/StreamableManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void UWrecktideDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	WeaponsTable = LoadCsvTable(TEXT("Weapons.csv"), FWeaponTableRow::StaticStruct());
	EnemiesTable = LoadCsvTable(TEXT("Enemies.csv"), FEnemyTableRow::StaticStruct());
	ChestsTable = LoadCsvTable(TEXT("Chests.csv"), FChestTableRow::StaticStruct());
	LootTable = LoadCsvTable(TEXT("Loot.csv"), FLootTableRow::StaticStruct());
	QuestsTable = LoadCsvTable(TEXT("Quests.csv"), FQuestTableRow::StaticStruct());
	MissionsTable = LoadCsvTable(TEXT("Missions.csv"), FMissionTableRow::StaticStruct());
	NpcsTable = LoadCsvTable(TEXT("Npcs.csv"), FNpcTableRow::StaticStruct());
	OutfitsTable = LoadCsvTable(TEXT("Outfits.csv"), FOutfitTableRow::StaticStruct());
	UpgradesTable = LoadCsvTable(TEXT("Upgrades.csv"), FUpgradeTrackTableRow::StaticStruct());
	BoatUpgradesTable = LoadCsvTable(TEXT("BoatUpgrades.csv"), FBoatUpgradeTableRow::StaticStruct());

	// Aggro/leash columns were appended in 2026-09. Older or hand-authored CSVs may omit them or
	// leave a cell empty; normalize those cases to the row struct's documented defaults.
	if (EnemiesTable)
	{
		const FEnemyTableRow Defaults;
		for (const TPair<FName, uint8*>& RowPair : EnemiesTable->GetRowMap())
		{
			FEnemyTableRow* Row = reinterpret_cast<FEnemyTableRow*>(RowPair.Value);
			if (!Row)
			{
				continue;
			}
			if (!FMath::IsFinite(Row->AggroRadius) || Row->AggroRadius <= 0.0f)
			{
				Row->AggroRadius = Defaults.AggroRadius;
			}
			if (!FMath::IsFinite(Row->LeashRadius) || Row->LeashRadius <= 0.0f)
			{
				Row->LeashRadius = Defaults.LeashRadius;
			}
			// Empty/older rows remain melee. Invalid ranged tuning must never produce rapid fire.
			if (!FMath::IsFinite(Row->RangedRange) || Row->RangedRange < 0.0f
				|| !FMath::IsFinite(Row->RangedDamage) || Row->RangedDamage < 0.0f
				|| !FMath::IsFinite(Row->RangedCooldownSeconds) || Row->RangedCooldownSeconds < 0.0f
				|| !FMath::IsFinite(Row->RangedWindupSeconds) || Row->RangedWindupSeconds < 0.0f
				|| (Row->RangedRange > 0.0f && (Row->RangedDamage <= 0.0f
					|| Row->RangedCooldownSeconds <= 0.0f || (!Row->bRangedUnarmed && Row->RangedWeaponMesh.IsNull()))))
			{
				UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MUSKET invalid ranged row %s; using melee"), *RowPair.Key.ToString());
				Row->RangedRange = Row->RangedDamage = Row->RangedCooldownSeconds = Row->RangedWindupSeconds = 0.0f;
				Row->RangedWeaponMesh.Reset();
			}
		}
	}

	// RequireFlag was appended in 2026-09 (bounty gate). An older CSV without the column, or a row
	// with an empty cell, has to read as "ungated" rather than as a flag nobody can ever set.
	if (QuestsTable)
	{
		for (const TPair<FName, uint8*>& RowPair : QuestsTable->GetRowMap())
		{
			FQuestTableRow* Row = reinterpret_cast<FQuestTableRow*>(RowPair.Value);
			if (Row && Row->RequireFlag.ToString().IsEmpty())
			{
				Row->RequireFlag = NAME_None;
			}
		}
	}

	// ONE quest-side error line, and the board then offers nothing rather than guessing at bounties
	// it cannot pay out (bounty board, 2026-09-04). LoadCsvTable already logs the file-level failure
	// under WRECKTIDE-FUNGATE-DATA; this marker is what the quest probe greps for.
	if (!QuestsTable || QuestsTable->GetRowMap().Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-DATA Quests.csv loaded 0 rows; the bounty board will offer nothing"));
	}

	// ONE mission-side error line, and the giver then offers nothing rather than pointing the
	// player at a story it cannot pay out (missions v0, 2026-09-04). LoadCsvTable already logs the
	// file-level failure under WRECKTIDE-FUNGATE-DATA; this marker is what the mission probe greps for.
	if (!MissionsTable || MissionsTable->GetRowMap().Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-DATA Missions.csv loaded 0 rows; the quest giver will offer nothing"));
	}

	// Same shape again for the NPC table (C30). WHY it fails OPEN rather than closed: an unreadable
	// Npcs.csv leaves every giver with no gate flag, so the whole story is still playable in the old
	// all-at-once order. Failing closed would brush the player off at every NPC and end the game.
	if (!NpcsTable || NpcsTable->GetRowMap().Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-NPC-DATA Npcs.csv loaded 0 rows; every quest giver stays unlocked and unnamed"));
	}

	if (!OutfitsTable || OutfitsTable->GetRowMap().Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-WARDROBE-DATA Outfits.csv loaded 0 rows; the wardrobe will offer nothing"));
	}

	// Same shape as the three above: one marker the smith probe greps for, and the smithy then
	// offers nothing rather than selling tiers at prices nobody authored (spec C10, 2026-09-06).
	if (!BoatUpgradesTable || BoatUpgradesTable->GetRowMap().Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-DATA BoatUpgrades.csv loaded 0 rows; the shipwright will offer nothing"));
	}

	if (!UpgradesTable || UpgradesTable->GetRowMap().Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-DATA Upgrades.csv loaded 0 rows; the smithy will offer nothing"));
	}

	PreloadEnemySkins();
}

void UWrecktideDataSubsystem::Deinitialize()
{
	if (SkinPreloadHandle.IsValid())
	{
		SkinPreloadHandle->ReleaseHandle();
		SkinPreloadHandle.Reset();
	}

	Super::Deinitialize();
}

void UWrecktideDataSubsystem::PreloadEnemySkins()
{
	if (!EnemiesTable)
	{
		return;
	}

	TArray<FSoftObjectPath> SkinPaths;
	for (const TPair<FName, uint8*>& RowPair : EnemiesTable->GetRowMap())
	{
		const FEnemyTableRow* Row = reinterpret_cast<const FEnemyTableRow*>(RowPair.Value);
		if (Row && !Row->SkinMesh.IsNull())
		{
			SkinPaths.AddUnique(Row->SkinMesh.ToSoftObjectPath());
		}
	}

	if (SkinPaths.Num() == 0)
	{
		return;
	}

	SkinPreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		SkinPaths, FStreamableDelegate(), FStreamableManager::DefaultAsyncLoadPriority, false, false, TEXT("WrecktideEnemySkins"));
	// Own marker: WRECKTIDE-FUNGATE-DATA is the data-load ERROR marker the harness greps for.
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SKIN-PRELOAD requested %d enemy skins async"), SkinPaths.Num());
}

UDataTable* UWrecktideDataSubsystem::LoadCsvTable(const FString& FileName, UScriptStruct* RowStruct) const
{
	UDataTable* Table = NewObject<UDataTable>(const_cast<UWrecktideDataSubsystem*>(this));
	Table->RowStruct = RowStruct;
	// Mission columns added after launch, including RewardWeapon, retain struct defaults when absent.
	if (RowStruct == FMissionTableRow::StaticStruct()) { Table->bIgnoreMissingFields = true; }
	if (RowStruct == FEnemyTableRow::StaticStruct()) { Table->bIgnoreMissingFields = true; }

	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), FileName);
	FString CsvText;
	if (!FFileHelper::LoadFileToString(CsvText, *Path))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-DATA Failed to load %s"), *Path);
		return Table;
	}

	const TArray<FString> Problems = Table->CreateTableFromCSVString(CsvText);
	for (const FString& Problem : Problems)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-DATA %s: %s"), *FileName, *Problem);
	}

	return Table;
}

const FQuestTableRow* UWrecktideDataSubsystem::GetQuestRow(FName RowName) const
{
	return QuestsTable ? QuestsTable->FindRow<FQuestTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

TArray<TPair<FName, const FQuestTableRow*>> UWrecktideDataSubsystem::GetQuestRows() const
{
	TArray<TPair<FName, const FQuestTableRow*>> Rows;

	if (!QuestsTable)
	{
		return Rows;
	}

	for (const TPair<FName, uint8*>& RowPair : QuestsTable->GetRowMap())
	{
		if (const FQuestTableRow* Row = reinterpret_cast<const FQuestTableRow*>(RowPair.Value))
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}

	// GetRowMap iteration order is hash order, so the board would reshuffle between runs without a
	// total order here. SortOrder first, row name as the tie break.
	Rows.Sort([](const TPair<FName, const FQuestTableRow*>& Left, const TPair<FName, const FQuestTableRow*>& Right)
	{
		if (Left.Value->SortOrder != Right.Value->SortOrder)
		{
			return Left.Value->SortOrder < Right.Value->SortOrder;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	return Rows;
}

const FMissionTableRow* UWrecktideDataSubsystem::GetMissionRow(FName RowName) const
{
	return MissionsTable ? MissionsTable->FindRow<FMissionTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

TArray<TPair<FName, const FMissionTableRow*>> UWrecktideDataSubsystem::GetMissionRows() const
{
	TArray<TPair<FName, const FMissionTableRow*>> Rows;

	if (!MissionsTable)
	{
		return Rows;
	}

	for (const TPair<FName, uint8*>& RowPair : MissionsTable->GetRowMap())
	{
		if (const FMissionTableRow* Row = reinterpret_cast<const FMissionTableRow*>(RowPair.Value))
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}

	// The mission chain is decided by this order, so it must be TOTAL: GetRowMap iteration is hash
	// order, and two rows sharing an Order would otherwise swap places between runs.
	Rows.Sort([](const TPair<FName, const FMissionTableRow*>& Left, const TPair<FName, const FMissionTableRow*>& Right)
	{
		if (Left.Value->Order != Right.Value->Order)
		{
			return Left.Value->Order < Right.Value->Order;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	return Rows;
}

const FNpcTableRow* UWrecktideDataSubsystem::GetNpcRow(FName RowName) const
{
	return NpcsTable ? NpcsTable->FindRow<FNpcTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

TArray<TPair<FName, const FNpcTableRow*>> UWrecktideDataSubsystem::GetNpcRows() const
{
	TArray<TPair<FName, const FNpcTableRow*>> Rows;
	if (!NpcsTable)
	{
		return Rows;
	}

	for (const TPair<FName, uint8*>& RowPair : NpcsTable->GetRowMap())
	{
		if (const FNpcTableRow* Row = reinterpret_cast<const FNpcTableRow*>(RowPair.Value))
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}

	// Total order, the mission table's rule: hash iteration would otherwise shuffle two NPCs who
	// share an Order between runs.
	Rows.Sort([](const TPair<FName, const FNpcTableRow*>& Left, const TPair<FName, const FNpcTableRow*>& Right)
	{
		if (Left.Value->Order != Right.Value->Order)
		{
			return Left.Value->Order < Right.Value->Order;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	return Rows;
}

const FOutfitTableRow* UWrecktideDataSubsystem::GetOutfitRow(FName RowName) const
{
	return OutfitsTable ? OutfitsTable->FindRow<FOutfitTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

TArray<TPair<FName, const FOutfitTableRow*>> UWrecktideDataSubsystem::GetOutfitRows() const
{
	TArray<TPair<FName, const FOutfitTableRow*>> Rows;
	if (!OutfitsTable)
	{
		return Rows;
	}

	for (const TPair<FName, uint8*>& RowPair : OutfitsTable->GetRowMap())
	{
		if (const FOutfitTableRow* Row = reinterpret_cast<const FOutfitTableRow*>(RowPair.Value))
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}

	Rows.Sort([](const TPair<FName, const FOutfitTableRow*>& Left, const TPair<FName, const FOutfitTableRow*>& Right)
	{
		if (Left.Value->SortOrder != Right.Value->SortOrder)
		{
			return Left.Value->SortOrder < Right.Value->SortOrder;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	return Rows;
}

const FWeaponTableRow* UWrecktideDataSubsystem::GetWeaponRow(FName RowName) const
{
	return WeaponsTable ? WeaponsTable->FindRow<FWeaponTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

const FEnemyTableRow* UWrecktideDataSubsystem::GetEnemyRow(FName RowName) const
{
	return EnemiesTable ? EnemiesTable->FindRow<FEnemyTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

const FChestTableRow* UWrecktideDataSubsystem::GetChestRow(FName RowName) const
{
	return ChestsTable ? ChestsTable->FindRow<FChestTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

TArray<TPair<FName, const FWeaponTableRow*>> UWrecktideDataSubsystem::GetMeleeShopRows() const
{
	TArray<TPair<FName, const FWeaponTableRow*>> Rows;

	if (!WeaponsTable)
	{
		return Rows;
	}

	for (const TPair<FName, uint8*>& RowPair : WeaponsTable->GetRowMap())
	{
		const FWeaponTableRow* Row = reinterpret_cast<const FWeaponTableRow*>(RowPair.Value);
		if (Row && Row->CombatClass == EWrecktideSkill::Melee && Row->bPurchasableWithGold)
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}

	Rows.Sort([](const TPair<FName, const FWeaponTableRow*>& Left, const TPair<FName, const FWeaponTableRow*>& Right)
	{
		return Left.Value->Price < Right.Value->Price;
	});

	return Rows;
}

const FBoatUpgradeTableRow* UWrecktideDataSubsystem::GetBoatUpgradeRow(FName RowName) const
{
	return BoatUpgradesTable ? BoatUpgradesTable->FindRow<FBoatUpgradeTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

TArray<TPair<FName, const FBoatUpgradeTableRow*>> UWrecktideDataSubsystem::GetBoatUpgradeRows() const
{
	TArray<TPair<FName, const FBoatUpgradeTableRow*>> Rows;
	if (!BoatUpgradesTable)
	{
		return Rows;
	}
	for (const TPair<FName, uint8*>& RowPair : BoatUpgradesTable->GetRowMap())
	{
		if (const FBoatUpgradeTableRow* Row = reinterpret_cast<const FBoatUpgradeTableRow*>(RowPair.Value))
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}
	Rows.Sort([](const TPair<FName, const FBoatUpgradeTableRow*>& Left, const TPair<FName, const FBoatUpgradeTableRow*>& Right)
	{
		if (Left.Value->SortOrder != Right.Value->SortOrder)
		{
			return Left.Value->SortOrder < Right.Value->SortOrder;
		}
		return Left.Key.LexicalLess(Right.Key);
	});
	return Rows;
}

const FUpgradeTrackTableRow* UWrecktideDataSubsystem::GetUpgradeRow(FName RowName) const
{
	return UpgradesTable ? UpgradesTable->FindRow<FUpgradeTrackTableRow>(RowName, TEXT("WrecktideDataSubsystem"), false) : nullptr;
}

TArray<TPair<FName, const FUpgradeTrackTableRow*>> UWrecktideDataSubsystem::GetUpgradeRows() const
{
	TArray<TPair<FName, const FUpgradeTrackTableRow*>> Rows;
	if (!UpgradesTable)
	{
		return Rows;
	}

	for (const TPair<FName, uint8*>& RowPair : UpgradesTable->GetRowMap())
	{
		if (const FUpgradeTrackTableRow* Row = reinterpret_cast<const FUpgradeTrackTableRow*>(RowPair.Value))
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}

	Rows.Sort([](const TPair<FName, const FUpgradeTrackTableRow*>& Left, const TPair<FName, const FUpgradeTrackTableRow*>& Right)
	{
		if (Left.Value->SortOrder != Right.Value->SortOrder)
		{
			return Left.Value->SortOrder < Right.Value->SortOrder;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	return Rows;
}

const FUpgradeTrackTableRow* UWrecktideDataSubsystem::GetUpgradeRowForTrack(EWrecktideUpgradeTrack Track, FName* OutRowName) const
{
	// First row wins if the table ever carries two of the same effect; the sort makes that
	// deterministic instead of hash-order dependent.
	for (const TPair<FName, const FUpgradeTrackTableRow*>& Row : GetUpgradeRows())
	{
		if (Row.Value->Effect == Track)
		{
			if (OutRowName)
			{
				*OutRowName = Row.Key;
			}
			return Row.Value;
		}
	}

	if (OutRowName)
	{
		*OutRowName = NAME_None;
	}
	return nullptr;
}

TArray<TPair<FName, const FWeaponTableRow*>> UWrecktideDataSubsystem::GetWeaponRows() const
{
	TArray<TPair<FName, const FWeaponTableRow*>> Rows;
	if (!WeaponsTable)
	{
		return Rows;
	}

	for (const TPair<FName, uint8*>& RowPair : WeaponsTable->GetRowMap())
	{
		if (const FWeaponTableRow* Row = reinterpret_cast<const FWeaponTableRow*>(RowPair.Value))
		{
			Rows.Emplace(RowPair.Key, Row);
		}
	}

	// GetRowMap iteration is hash order, so the smithy's weapon list needs a TOTAL order or it would
	// reshuffle between runs: price first, row name as the tie break.
	Rows.Sort([](const TPair<FName, const FWeaponTableRow*>& Left, const TPair<FName, const FWeaponTableRow*>& Right)
	{
		if (Left.Value->Price != Right.Value->Price)
		{
			return Left.Value->Price < Right.Value->Price;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	return Rows;
}
