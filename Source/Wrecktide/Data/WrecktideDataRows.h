// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.
#pragma once

#include "Engine/DataTable.h"
#include "Systems/SkillComponent.h"
#include "WrecktideDataRows.generated.h"

class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;

/** One selectable whole-body pirate skin from Content/Data/Outfits.csv. */
USTRUCT(BlueprintType)
struct FOutfitTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Description;

	/** Whole-body Synty skin loaded only when the player selects this outfit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	TSoftObjectPtr<USkeletalMesh> SkinMesh;

	/** Wardrobe menu order, low first. Row name breaks ties deterministically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 SortOrder = 0;

	/** Player level this outfit unlocks at (the level AND gold double gate, spec C9). 1 = from the start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 RequiredLevel = 1;

	/** Gold the tailor charges once the level gate is met. 0 = free. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Price = 0;

	/** Owned from the first minute (the broke pirate's one set of clothes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	bool bStartOwned = false;
};

USTRUCT(BlueprintType)
struct FWeaponTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	EWrecktideSkill CombatClass = EWrecktideSkill::Shooting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	float BaseDamage = 10.f;

	/** Minimum player notoriety level; CombatClass still controls damage scaling and awarded kill XP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 RequiredLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Price = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	bool bPurchasableWithGold = true;
};

USTRUCT(BlueprintType)
struct FEnemyTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	float BaseDamage = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 GoldDropMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 GoldDropMax = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 XPAward = 0;

	/** Legacy authoring metadata. Kill XP uses the killer's equipped weapon CombatClass instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	EWrecktideSkill XPSkill = EWrecktideSkill::Shooting;

	/**
	 * Synty skin for this row (2026-09-02), resolved by ACombatEnemy::BeginPlay before the cosmetic
	 * retarget rig hooks up, so a Skeleton Deckhand is a skeleton and a Navy Cadet a redcoat. Empty
	 * keeps the enemy class default (BP_CombatEnemy's Deckhand). CSV cell = the object path,
	 * /Game/.../SK_Chr_X.SK_Chr_X. Packaging cooks the whole Synty character folder for these
	 * (DirectoriesToAlwaysCook in DefaultGame.ini) because nothing else references most of them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	TSoftObjectPtr<USkeletalMesh> SkinMesh;

	/** 3D distance at which this enemy can notice a living player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data", meta=(ClampMin="1.0", Units="cm"))
	float AggroRadius = 1100.0f;

	/** Maximum distance this enemy may chase from its spawn post before returning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data", meta=(ClampMin="1.0", Units="cm"))
	float LeashRadius = 2600.0f;

	/** Zero (including an absent column) keeps the existing melee behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data", meta=(ClampMin="0", Units="cm"))
	float RangedRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data", meta=(ClampMin="0"))
	float RangedDamage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data", meta=(ClampMin="0", Units="s"))
	float RangedCooldownSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data", meta=(ClampMin="0", Units="s"))
	float RangedWindupSeconds = 0.0f;

	/** Held mesh; the weapons folder is included by DefaultGame.ini's cook rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	TSoftObjectPtr<UStaticMesh> RangedWeaponMesh;

	/** Explicit opt-in for an empty-hand curse using the ranged timing and sight rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	bool bRangedUnarmed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FLinearColor RangedFlashColor = FLinearColor(1.0f, 0.65f, 0.20f);

	/** Optional whole-skin material. Mangrove materials also have level references for cooking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	TSoftObjectPtr<UMaterialInterface> SkinMaterial;
};

/** Runtime payout override for a level-placed treasure chest. */
USTRUCT(BlueprintType)
struct FChestTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Gold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Tier = 1;
};

/**
 * One bounty on the quest board (2026-09-04). The board, the HUD objective line and the kill
 * counter all read this row through WrecktideQuestLogic; nothing else interprets these fields.
 */
USTRUCT(BlueprintType)
struct FQuestTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Description;

	/** Enemies.csv row this bounty counts. None (or an empty cell) counts ANY enemy row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName TargetEnemyRow;

	/** Kills needed. Treated as at least 1 everywhere, so a 0 in the CSV cannot auto-complete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 TargetCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 RewardGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 RewardXP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	EWrecktideSkill RewardSkill = EWrecktideSkill::Melee;

	/** False rows are gone for good once turned in; true rows return to the board. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	bool bRepeatable = false;

	/** Board menu order, low first. Ties fall back to the row name so the list never shuffles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 SortOrder = 0;

	/** Progress flag that must already be set before ANY board lists or accepts this bounty. None
	 *  (or an empty cell, or no column at all) is an ungated row every board offers. Story_Reach
	 *  keeps the Gallows Reach bounties off the camp board, where a new player who took one could
	 *  never turn it in and would block the story chain behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName RequireFlag;
};

/**
 * What one story mission asks of the player (2026-09-04, missions v0). The mission logic switches on
 * this and NOTHING else does; a new objective kind means one new case there, never a new flag
 * spelling scattered through the actors.
 */
UENUM(BlueprintType)
enum class EMissionObjective : uint8
{
	/** Kill TargetCount enemies of the Enemies.csv row TargetName. */
	KillRow,

	/** Stand within TargetRadius of the spot named by TargetName (or of TargetLocation). */
	ReachSpot,

	/** Open the treasure chest whose ChestId is TargetName. */
	OpenChest,

	/** Board the sailing boat. */
	BoardBoat,

	/** Kill TargetCount of the boss row TargetName. Counts exactly like KillRow; separate so the
	 *  giver and the log can read as a boss hunt and a future health bar has a hook. */
	KillBoss,

	/** Turn in the Quests.csv bounty row TargetName at the board. */
	TurnInBounty,

	/** Speak to the NPC whose GiverId is TargetName. The hand-off: it pays out the moment his menu
	 *  opens, because there is nobody to walk back to (C22/C23, 2026-09-06). */
	TalkTo,

	/** Hand over TargetCount gold at the giver's menu. */
	Pay,

	/** Buy any tier at the smithy. */
	Upgrade,

	/** Carry something to the NPC whose GiverId is TargetName. Counts on his menu opening, but the
	 *  payout is back at the NPC who set it, which is what makes it a round trip and not a TalkTo. */
	Deliver
};

/**
 * One authored story mission (2026-09-04). The giver NPC, the HUD mission line and every progress
 * hook read this row through WrecktideMissionLogic; nothing else interprets these fields.
 * Missions run in a strict chain by Order, so exactly one row is ever the current one.
 */
USTRUCT(BlueprintType)
struct FMissionTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Title;

	/** Who hands it out; drawn on the giver's menu and in the "return to" hint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Giver;

	/** WHICH NPC hands it out, as a stable key the giver actors match on (Bell, Jerold, Marta,
	 *  Cobb). The Giver text above is what the player reads and may be reworded freely; this is what
	 *  the code matches, so the two can never drift into a mission nobody offers. Empty means Bell,
	 *  which keeps every missions v0 row correct without editing it (C22, 2026-09-06). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName GiverId;

	/** Shown when the mission is accepted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Intro;

	/** The one line that says what to do; the giver menu draws it with the live progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Objective;

	/** Shown on completion. The last mission's Outro is the finale text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Outro;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	EMissionObjective Type = EMissionObjective::KillRow;

	/** Enemy row, chest id, bounty row or spot name, depending on Type. None means "any". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName TargetName;

	/** Treated as at least 1 everywhere, so a 0 in the CSV cannot auto-complete a mission. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 TargetCount = 1;

	/** ReachSpot fallback: used only when TargetName names no live actor, so the CSV alone can
	 *  still place a destination on a map with no market stall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FVector TargetLocation = FVector::ZeroVector;

	/** ReachSpot arrival radius in uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	float TargetRadius = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 RewardGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 RewardXP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	EWrecktideSkill RewardSkill = EWrecktideSkill::Melee;

	/** Optional owned weapon granted on completion. Older mission tables default to no weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName RewardWeapon = NAME_None;

	/** CHAPTER GATE (2026-09-06, research B3/B6): this row is only offerable once that QuestFlag is
	 *  set. None means "no gate". One flag can open several chains at the same moment, which is what
	 *  stops the island feeling empty after a chapter: killing the Bone Captain sets Story_Complete
	 *  and Jerold, Marta and Cobb all wake up together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName RequireFlag;

	/** True = the objective pays out where it happens, with no NPC to return to. Kept ONLY for
	 *  M01_Wake, the tutorial walk that predates the return-visit rule. Every other row ends face to
	 *  face with its giver (research A4: the classic MMO's engine appended that step to every chain
	 *  link, which is what makes the "!" and "?" glyphs mean anything). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	bool bAutoComplete = false;

	/** Repeatable WORK (C22.3): never marked done, banks a count, cools down and comes back around,
	 *  and pays on the notoriety curve. A repeatable row is never part of the story Order chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	bool bRepeatable = false;

	/** Seconds before a repeatable row may be offered again by its giver. Ignored on story rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data", meta=(ClampMin="0.0", Units="s"))
	float CooldownSeconds = 0.0f;

	/** A QuestFlag set to 1 on completion, for later content to gate on. None sets nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName UnlockFlag;

	/** Chain position, low first. Ties fall back to the row name so the chain never shuffles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Order = 0;
};

/**
 * One quest-giving NPC, from Content/Data/Npcs.csv. The row NAME is his Missions.csv GiverId
 * (Bell, Jerold, Marta, Cobb), so a mission row and an NPC row meet on one key.
 *
 * WHY a table of its own (C30, 2026-09-06): the STRICT order means an NPC is a thing the player
 * unlocks, not just a name attached to some missions. His gate flag and the line he says while he
 * is still locked belong to HIM, not to any one mission, and both are text a writer must be able
 * to change without touching C++.
 */
USTRUCT(BlueprintType)
struct FNpcTableRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Nameplate wording drawn over his head and on the map, e.g. "Jerold the Blacksmith". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	/** The same person named inside a sentence, e.g. "Jerold the blacksmith". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText PromptName;

	/** The QuestFlag that opens him. None = open from the first minute (Bell). Until it is set he
	 *  brushes the player off: no offer, no "!", no journal entry. Set by the previous NPC's last
	 *  mission, which is what makes the chain strictly one NPC at a time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FName UnlockFlag;

	/** The one line he says while he is still locked. In his own voice, never a system message. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText BrushOff;

	/** Introduction order, low first. Display only; the gate flags decide the real sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Order = 0;
};

USTRUCT(BlueprintType)
struct FLootTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Tier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Value = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	float DropWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 RequiredLevel = 1;
};

/**
 * The five blacksmith tracks (spec C10). The CSV owns the numbers; this names what a number DRIVES,
 * so a reordered or renamed Upgrades.csv can never silently swap crit chance for lifesteal.
 */
UENUM(BlueprintType)
enum class EWrecktideUpgradeTrack : uint8
{
	/** Attack damage, as a fraction of the weapon's base damage. */
	Edge,
	/** Attack montage play rate, with a deliberate diminishing return past tier 3. */
	Balance,
	/** Critical chance, in flat points on top of the base chance. */
	Cruelty,
	/** Critical damage, as a fraction added to the base critical multiplier. */
	Brutal,
	/** Lifesteal, as a fraction of the damage dealt. */
	Bloodletting
};

/** Tiers every track has. Costs and level gates below are indexed 1..this. */
#define WRECKTIDE_UPGRADE_MAX_TIER 5

/**
 * One blacksmith upgrade track from Content/Data/Upgrades.csv (spec C10). Five tiers, one gold cost
 * and one notoriety gate per tier, and a per-tier step whose meaning comes from Effect. Costs and
 * gates are flat columns rather than an array column: the CSV importer reads plain scalars reliably
 * and the table stays readable to the designer, who tunes these by taste.
 *
 * Tiers 4 and 5 are gold and level only for now. The research brief wants a rare part from elites
 * and skull chests on those two tiers; no loot item exists yet, so that gate lands with the loot
 * pass (Loot.csv is loaded but has no accessor at all today).
 */
USTRUCT(BlueprintType)
struct FUpgradeTrackTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Description;

	/** What this row's Step drives in the damage path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	EWrecktideUpgradeTrack Effect = EWrecktideUpgradeTrack::Edge;

	/** What ONE tier adds. Read against Effect: +8% damage, +4% speed, +3 points of crit chance
	 *  (0.03), +20% crit damage, +1% lifesteal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	float Step = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost1 = 120;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost2 = 260;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost3 = 580;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost4 = 1280;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost5 = 2800;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level1 = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level2 = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level3 = 18;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level4 = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level5 = 50;

	/** Smithy menu order, low first. Row name breaks ties deterministically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 SortOrder = 0;

	/** Gold for tier 1..WRECKTIDE_UPGRADE_MAX_TIER; 0 for any tier outside that range. */
	int64 GetTierCost(int32 Tier) const
	{
		switch (Tier)
		{
		case 1: return Cost1;
		case 2: return Cost2;
		case 3: return Cost3;
		case 4: return Cost4;
		case 5: return Cost5;
		default: return 0;
		}
	}

	/** Notoriety level for tier 1..WRECKTIDE_UPGRADE_MAX_TIER; MAX_int32 for anything outside it, so
	 *  an out-of-range tier fails the gate closed instead of opening it. */
	int32 GetTierLevel(int32 Tier) const
	{
		switch (Tier)
		{
		case 1: return Level1;
		case 2: return Level2;
		case 3: return Level3;
		case 4: return Level4;
		case 5: return Level5;
		default: return MAX_int32;
		}
	}
};

/**
 * One shipwright trading path (spec S4): Timbers, Canvas or Hold, three tiers each. A separate row
 * type from the blacksmith's because the boat has three tiers, not five, and gates on NOTORIETY
 * rather than a weapon skill; sharing the struct would have meant two unused columns and a comment
 * explaining which half of it applies.
 */
USTRUCT(BlueprintType)
struct FBoatUpgradeTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	FText Description;

	/** What ONE tier adds, read against the row name: 25 hull for Timbers, 0.08 (8 per cent) of max
	 *  speed for Canvas, 2 slots for Hold. The BOAT reads this, so the table is the tuning surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	float Step = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost1 = 150;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost2 = 400;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int64 Cost3 = 900;

	/** Notoriety level each tier is gated behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level1 = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level2 = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 Level3 = 20;

	/** Shipwright menu order, low first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
	int32 SortOrder = 0;

	/** Gold for tier 1..3; 0 for any tier outside that range. */
	int64 GetTierCost(int32 Tier) const
	{
		switch (Tier)
		{
		case 1: return Cost1;
		case 2: return Cost2;
		case 3: return Cost3;
		default: return 0;
		}
	}

	/** Notoriety needed for tier 1..3; unreachable for any tier outside that range. */
	int32 GetTierLevel(int32 Tier) const
	{
		switch (Tier)
		{
		case 1: return Level1;
		case 2: return Level2;
		case 3: return Level3;
		default: return MAX_int32;
		}
	}
};
