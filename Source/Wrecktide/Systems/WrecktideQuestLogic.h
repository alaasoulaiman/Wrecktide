// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Systems/SkillComponent.h"

class AWrecktidePlayerState;
class UProgressComponent;
class UWrecktideDataSubsystem;

/**
 * Bounty rules for the whole game. Free functions with no state of their own: the board actor, the
 * HUD objective line, the kill path and the probe all decide through THESE and nothing else, so the
 * flag spelling and the accept/turn-in rules exist in exactly one place.
 *
 * All bounty state lives in UProgressComponent::QuestFlags (TMap<FName,int32>), which is already
 * SaveGame tagged and already autosaved by the PlayerState on OnProgressChanged. No new save field
 * exists and none is needed.
 *   Q_<Row>_State : EQuestState below.
 *   Q_<Row>_Kills : kills counted while that row is active.
 * At most ONE row is ever Active; every accept path checks that before writing.
 */
namespace WrecktideQuestLogic
{
/** Values stored in Q_<Row>_State. Plain ints because QuestFlags is a TMap<FName,int32>. */
enum EQuestState : int32
{
	/** Never taken, or taken and turned in on a repeatable row. Offered by the board. */
	QuestState_Available = 0,

	/** Accepted and counting kills. Only one row may hold this at a time. */
	QuestState_Active = 1,

	/** Turned in on a non-repeatable row. Never offered again. */
	QuestState_Completed = 2
};

/** StateKey/KillsKey are the SINGLE authoritative spelling of the two persisted flags. */
FName StateKey(FName Row);
FName KillsKey(FName Row);

/** GetActiveQuest is the SINGLE authoritative answer to "what is the player hunting?" (None = nothing). */
FName GetActiveQuest(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** IsQuestOffered is the SINGLE authoritative answer to "may this player see this bounty at all?".
 *  True when the row's RequireFlag is None or that flag is already set in his progress. The board's
 *  listing loop and CanAccept both decide through THIS, so a gated row is neither shown nor taken.
 *  A row that is already Active stays offered whatever its gate says, so a bounty accepted before
 *  its gate existed can still be turned in (bounties are one at a time, with no abandon). */
bool IsQuestOffered(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row);

/** True only when Row is a real table row, is offered, is Available, and nothing else is Active. */
bool CanAccept(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row);

/** Accept is the SINGLE authoritative path that makes a bounty active. Refuses fail-closed. */
bool Accept(AWrecktidePlayerState* PlayerState, FName Row);

/** NotifyKill is the SINGLE authoritative path that advances bounty progress on an enemy kill. */
void NotifyKill(AWrecktidePlayerState* PlayerState, FName EnemyRow);

/** True when Row is Active and its kill count has reached its target. */
bool IsReadyToTurnIn(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row);

/** TurnIn is the SINGLE authoritative payout path. Refuses (no gold, no XP) unless ready. */
bool TurnIn(AWrecktidePlayerState* PlayerState, FName Row);

/** The HUD objective string, empty when no bounty is active. */
FString ObjectiveLine(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data);

/** Display name for a reward skill, used by the board menu and its toasts. */
FString SkillDisplayName(EWrecktideSkill Skill);
}
