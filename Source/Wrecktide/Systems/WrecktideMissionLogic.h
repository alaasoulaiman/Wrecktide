// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Systems/SkillComponent.h"

class AWrecktidePlayerState;
class UProgressComponent;
class UWrecktideDataSubsystem;
struct FMissionTableRow;

/**
 * Story mission rules for the whole game (2026-09-04, quest model v2 on 2026-09-06). Free functions
 * with no state of their own, the same shape as WrecktideQuestLogic: the giver NPCs, the HUD mission
 * line, the beacon, the journal, the kill/chest/boat/bounty/talk/pay/upgrade hooks and the probes all
 * decide through THESE and nothing else, so the flag spelling and the chain order exist in exactly
 * one place.
 *
 * MODEL v2 (C23). Missions belong to a GIVER (Missions.csv GiverId: Bell, Jerold, Marta, Cobb).
 * SEVERAL may be active at once, at most one per giver, and exactly one of them is TRACKED: the
 * tracked one owns the HUD line and the beacon. The story rows still run in a strict global Order,
 * which is what makes the hand-off read as a story: Bell's chain finishes, Bell's last mission sends
 * you to Jerold, and Jerold's first row only becomes offerable once every lower-Order story row is
 * done. Once a giver's story rows are all done he offers REPEATABLE work instead, so every NPC in
 * the camp always has something.
 *
 * All of it lives in UProgressComponent::QuestFlags (TMap<FName,int32>), which is already SaveGame
 * tagged and already autosaved by the PlayerState on OnProgressChanged. NO new save field exists and
 * none is needed: v2 adds new KEYS to the same map, so the save version is untouched and an old save
 * loads with every new key reading 0, which is the correct state for a pirate who never met Jerold.
 *   Mission_Current          : Order of the TRACKED mission. 0 = nothing tracked. (v1 spelling kept:
 *                              with one active mission this is exactly the v1 meaning.)
 *   Mission_<Row>_Active     : 1 while accepted and not yet paid out.
 *   Mission_<Row>_Progress   : objective count so far (kills, or 1 for the one-shot objectives).
 *   Mission_<Row>_Done       : 1 once paid out. NEVER written for a repeatable row.
 *   Mission_<Row>_Count      : how many times a repeatable row has been paid out.
 *   Mission_<Row>_CoolUntil  : Unix time (seconds) a repeatable row may be offered again. Unix
 *                              seconds fit int32 until 2038 and survive a save, which a world-time
 *                              value would not.
 * A mission's UnlockFlag (Story_Sailed, Story_Complete, Story_Steel...) is written into the same map
 * on completion.
 *
 * WHY the tracked pointer is an Order and not a row name: QuestFlags maps FName to int32, so a row
 * NAME cannot be stored in it. Order is unique per row across the whole table (asserted by the
 * probes), so it is a faithful key, and it keeps the v1 save exactly readable.
 */
namespace WrecktideMissionLogic
{
/** What the journal says about one entry (C19). */
enum class EMissionEntryState : uint8
{
	/** Accepted and running. */
	Active,

	/** Offered by a giver right now, not yet accepted. */
	Available,

	/** Paid out (or, for repeatable work, paid out at least once). */
	Completed
};

/** One line of the journal. The UI lane's Journal tab draws these and nothing else (C19). */
struct FMissionJournalEntry
{
	/** Missions.csv row name, so a caller can track or complete the entry it clicked. */
	FName Row;

	FString Title;

	/** Display name of the NPC who hands it out ("Jerold the blacksmith"). */
	FString Giver;

	/** The objective line with the live count, and metres for a reach-the-spot entry. */
	FString Objective;

	EMissionEntryState State = EMissionEntryState::Available;

	/** True for the ONE entry the beacon and the HUD mission line follow. */
	bool bTracked = false;

	/** Where the beacon would point. Only meaningful when bHasTarget. */
	FVector TargetLocation = FVector::ZeroVector;
	bool bHasTarget = false;
};

/** The persisted flag spellings, in one place. */
FName CurrentKey();
FName ProgressKey(FName Row);
FName DoneKey(FName Row);
FName ActiveKey(FName Row);
FName CountKey(FName Row);
FName CooldownKey(FName Row);

/** GetCurrentMissionRow is the SINGLE authoritative answer to "what mission is the player ON right
 *  now?", meaning the TRACKED one (None = nothing tracked). Driven off the TABLE, so a row deleted
 *  from the CSV stops being current instead of blocking with a flag nothing can complete. */
FName GetCurrentMissionRow(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** Same thing under the name v2 code should use. */
FName GetTrackedMissionRow(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** Point the beacon and the HUD line at Row. Refuses fail-closed unless Row is active. */
bool SetTrackedMission(AWrecktidePlayerState* PlayerState, FName Row);

/** True while Row is accepted and not yet paid out. */
bool IsMissionActive(const UProgressComponent* Progress, FName Row);

/** Every accepted, unfinished mission, in table Order. */
void GetActiveMissionRows(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, TArray<FName>& OutRows);

/** The next STORY mission anyone may offer: the first non-repeatable row, in Order, that is not done
 *  and whose every lower-Order story row IS done. None = the whole story is finished. */
FName GetNextMissionRow(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** IsGiverUnlocked is the SINGLE authoritative answer to "is it this NPC's turn yet?" (C30). True
 *  when his Npcs.csv UnlockFlag is set, when he has no flag at all (Bell), or while an errand the
 *  player is already carrying names him, which is what lets the Bell to Jerold hand-off happen
 *  before the flag it sets exists. A locked giver offers nothing, wears no "!" and stays out of the
 *  journal; F on him plays his brush-off line instead. */
bool IsGiverUnlocked(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName GiverId);

/** The one line a still-locked NPC says, straight out of Npcs.csv. Empty when the table has no row
 *  for him, and the menu then falls back to its own "nothing yet" line rather than a blank. */
FString GetGiverBrushOff(const UWrecktideDataSubsystem* Data, FName GiverId);

/** What THIS giver would hand over if the player pressed F now: his active mission when he has one,
 *  else his next story row, else a repeatable template off cooldown. None = nothing right now, and
 *  None always for a giver whose turn has not come (C30). */
FName GetOfferForGiver(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName GiverId);

/** "!" when this giver has work to hand out, "?" when he has a mission of his waiting to be turned
 *  in, empty otherwise. Drawn over his head and on the map, from ONE rule (C22.4). */
FString GetGiverSignal(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName GiverId);

/** True when ANY mission is accepted and not yet completed. */
bool IsActive(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** True when every STORY row in the table is done. */
bool IsChainComplete(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** Objective progress for the TRACKED mission: Done count and the clamped target. */
void GetProgress(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, int32& OutCount, int32& OutTarget);

/** Objective progress for any row. */
void GetRowProgress(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row, int32& OutCount, int32& OutTarget);

/** Accept the globally next offerable mission (v1 entry point; still the shape -MissionProbe uses). */
bool Accept(AWrecktidePlayerState* PlayerState);

/** AcceptFromGiver is the SINGLE authoritative path that starts a mission. Refuses fail-closed when
 *  that giver has nothing to give or already has one running. */
bool AcceptFromGiver(AWrecktidePlayerState* PlayerState, FName GiverId);

/** NotifyKill is the SINGLE authoritative path that advances KillRow and KillBoss objectives. */
void NotifyKill(AWrecktidePlayerState* PlayerState, FName EnemyRow);

/** NotifyChestOpened is the SINGLE authoritative path for OpenChest objectives. */
void NotifyChestOpened(AWrecktidePlayerState* PlayerState, FName ChestId);

/** NotifyBoarded is the SINGLE authoritative path for BoardBoat objectives. */
void NotifyBoarded(AWrecktidePlayerState* PlayerState);

/** NotifyBountyTurnedIn is the SINGLE authoritative path for TurnInBounty objectives. */
void NotifyBountyTurnedIn(AWrecktidePlayerState* PlayerState, FName BountyRow);

/** NotifyTalkedTo is the SINGLE authoritative path for TalkTo and Deliver objectives: the giver
 *  actor calls it when the player opens his menu. A TalkTo pays out on the spot (the hand-off has no
 *  turn-in gesture); a Deliver only counts, so the player walks the answer back. */
void NotifyTalkedTo(AWrecktidePlayerState* PlayerState, FName GiverId);

/** NotifyPaid is the SINGLE authoritative path for Pay objectives. The gold has already moved. */
void NotifyPaid(AWrecktidePlayerState* PlayerState, int64 Amount);

/** NotifyUpgradePurchased is the SINGLE authoritative path for Upgrade objectives (any smithy buy). */
void NotifyUpgradePurchased(AWrecktidePlayerState* PlayerState);

/** NotifyLocation is the SINGLE authoritative path for ReachSpot objectives. Cheap enough for the
 *  2 Hz timer that feeds it: it returns immediately unless a ReachSpot mission is active. Arriving
 *  COMPLETES the mission on the spot, because "go there" has no turn-in gesture to make. */
void NotifyLocation(AWrecktidePlayerState* PlayerState, const FVector& PlayerLocation);

/** True when the TRACKED mission's objective is met and its giver may pay it out. */
bool IsReadyToComplete(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** True when Row's objective is met. */
bool IsRowReadyToComplete(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row);

/** Complete the TRACKED mission (v1 entry point). */
bool Complete(AWrecktidePlayerState* PlayerState);

/** CompleteRow is the SINGLE authoritative payout path. Refuses (no gold, no XP) unless ready. */
bool CompleteRow(AWrecktidePlayerState* PlayerState, FName Row);

/** Repeatable work pays on a notoriety curve so late work is still worth walking to (C22.3):
 *  gold x (1 + RepeatableGoldPerLevel x notoriety level). Story rows pay exactly what the CSV says. */
int64 GetRewardGold(const FMissionTableRow& Row, const AWrecktidePlayerState* PlayerState);
int64 GetRewardXP(const FMissionTableRow& Row, const AWrecktidePlayerState* PlayerState);

/** The HUD mission line for the TRACKED mission, empty when nothing is tracked. */
/** WorldContext + PlayerLocation (the HUD passes the pawn's) add a distance in metres to a reach-the-spot line. */
FString LogLine(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, const UObject* WorldContext = nullptr, const FVector* PlayerLocation = nullptr);

/** The objective line for one row, with its live count and (for a spot) its distance. */
FString ObjectiveLine(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row, const UObject* WorldContext = nullptr, const FVector* PlayerLocation = nullptr);

/** GetJournalEntries is the SINGLE read API the Journal tab draws from (C19 + C23.3). Active
 *  missions first (tracked one flagged), then what every giver is offering, then what is done. */
void GetJournalEntries(const AWrecktidePlayerState* PlayerState, TArray<FMissionJournalEntry>& OutEntries);

/** The world spot a mission points at: a live actor named by TargetName when there is one, else the
 *  row's TargetLocation. Knows every camp landmark and every NPC by his GiverId. False when neither
 *  resolves. */
bool ResolveSpotLocation(const UObject* WorldContext, const FMissionTableRow& Row, FVector& OutLocation);
}
