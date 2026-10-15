// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktideQuestLogic.h"

#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"

namespace
{
/** The bounty table for a given player. Null whenever the game instance or the table is missing. */
const UWrecktideDataSubsystem* QuestDataFor(const AWrecktidePlayerState* PlayerState)
{
	const UGameInstance* GameInstance = PlayerState ? PlayerState->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
}

/** Same toast route the shop stall uses, so bounty messages land in the one HUD line list. */
void ShowQuestToast(const AWrecktidePlayerState* PlayerState, const FString& Message)
{
	APlayerController* PlayerController = PlayerState ? UGameplayStatics::GetPlayerController(PlayerState, 0) : nullptr;
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}

/** A CSV row asking for 0 kills would be complete before it was accepted; clamp it fail-closed. */
int32 SafeTargetCount(const FQuestTableRow* Row)
{
	return Row ? FMath::Max(Row->TargetCount, 1) : 1;
}
}

namespace WrecktideQuestLogic
{

FName StateKey(FName Row)
{
	return FName(*FString::Printf(TEXT("Q_%s_State"), *Row.ToString()));
}

FName KillsKey(FName Row)
{
	return FName(*FString::Printf(TEXT("Q_%s_Kills"), *Row.ToString()));
}

FName GetActiveQuest(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	if (!Progress || !Data)
	{
		return NAME_None;
	}

	// Driven off the TABLE, not off QuestFlags: a row deleted from the CSV then stops being active
	// instead of blocking every future bounty with a flag nothing can turn in.
	for (const TPair<FName, const FQuestTableRow*>& Row : Data->GetQuestRows())
	{
		if (Progress->GetQuestFlag(StateKey(Row.Key)) == QuestState_Active)
		{
			return Row.Key;
		}
	}

	return NAME_None;
}

bool IsQuestOffered(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row)
{
	const FQuestTableRow* QuestRow = (Progress && Data && !Row.IsNone()) ? Data->GetQuestRow(Row) : nullptr;
	if (!QuestRow)
	{
		return false;
	}

	// An empty RequireFlag is an ungated bounty every board offers; anything else must already be
	// set (Story_Reach, which Cobb's H04 grants, for the two Gallows Reach rows).
	if (QuestRow->RequireFlag.IsNone() || Progress->GetQuestFlag(QuestRow->RequireFlag) != 0)
	{
		return true;
	}

	// Fail OPEN for a bounty the player is already hunting: hiding it would leave him holding the
	// one active row forever with no way to turn it in and no way to abandon it.
	return Progress->GetQuestFlag(StateKey(Row)) == QuestState_Active;
}

bool CanAccept(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row)
{
	if (!Progress || !Data || Row.IsNone() || !Data->GetQuestRow(Row))
	{
		return false;
	}

	if (!IsQuestOffered(Progress, Data, Row))
	{
		return false;
	}

	if (Progress->GetQuestFlag(StateKey(Row)) != QuestState_Available)
	{
		return false;
	}

	return GetActiveQuest(Progress, Data).IsNone();
}

bool Accept(AWrecktidePlayerState* PlayerState, FName Row)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = QuestDataFor(PlayerState);
	if (!Progress || !Data || !CanAccept(Progress, Data, Row))
	{
		return false;
	}

	// Kills first: if the second write ever failed, the row would be available with a stale count
	// rather than active with one.
	Progress->SetQuestFlag(KillsKey(Row), 0);
	Progress->SetQuestFlag(StateKey(Row), QuestState_Active);

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-ACCEPT row=%s"), *Row.ToString());
	return true;
}

void NotifyKill(AWrecktidePlayerState* PlayerState, FName EnemyRow)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = QuestDataFor(PlayerState);
	if (!Progress || !Data)
	{
		return;
	}

	const FName Active = GetActiveQuest(Progress, Data);
	const FQuestTableRow* QuestRow = Active.IsNone() ? nullptr : Data->GetQuestRow(Active);
	if (!QuestRow)
	{
		return;
	}

	// An empty TargetEnemyRow counts every enemy row; anything else must match exactly.
	if (!QuestRow->TargetEnemyRow.IsNone() && QuestRow->TargetEnemyRow != EnemyRow)
	{
		return;
	}

	const int32 Target = SafeTargetCount(QuestRow);
	const int32 Kills = Progress->GetQuestFlag(KillsKey(Active));
	if (Kills >= Target)
	{
		// Already complete and waiting at the board. Capped here, so the count can never run past
		// the target and the completion toast fires exactly once.
		return;
	}

	const int32 NewKills = FMath::Min(Kills + 1, Target);
	Progress->SetQuestFlag(KillsKey(Active), NewKills);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-KILL row=%s kills=%d/%d"), *Active.ToString(), NewKills, Target);

	if (NewKills >= Target)
	{
		ShowQuestToast(PlayerState, TEXT("Bounty complete! Return to the board."));
	}
}

bool IsReadyToTurnIn(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row)
{
	if (!Progress || !Data || Row.IsNone())
	{
		return false;
	}

	const FQuestTableRow* QuestRow = Data->GetQuestRow(Row);
	if (!QuestRow || Progress->GetQuestFlag(StateKey(Row)) != QuestState_Active)
	{
		return false;
	}

	return Progress->GetQuestFlag(KillsKey(Row)) >= SafeTargetCount(QuestRow);
}

bool TurnIn(AWrecktidePlayerState* PlayerState, FName Row)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = QuestDataFor(PlayerState);
	if (!Progress || !Data || !IsReadyToTurnIn(Progress, Data, Row))
	{
		return false;
	}

	const FQuestTableRow* QuestRow = Data->GetQuestRow(Row);
	UEconomyComponent* Economy = PlayerState->GetEconomy();
	USkillComponent* Skill = PlayerState->GetSkill();
	if (!QuestRow || !Economy || !Skill)
	{
		// Checked BEFORE any payout: half a reward (gold paid, XP lost) would be worse than none,
		// and the bounty stays turn-inable so nothing is destroyed by the refusal.
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING turn-in refused row=%s economy=%d skill=%d"),
			*Row.ToString(), Economy != nullptr, Skill != nullptr);
		return false;
	}

	if (QuestRow->RewardGold > 0)
	{
		Economy->ApplyGoldDelta(QuestRow->RewardGold, EGoldSource::Earned, EGoldReason::MissionReward, Row);
	}

	if (QuestRow->RewardXP > 0)
	{
		Skill->AwardXP(QuestRow->RewardSkill, QuestRow->RewardXP, Row);
	}

	Progress->SetQuestFlag(KillsKey(Row), 0);
	Progress->SetQuestFlag(StateKey(Row), QuestRow->bRepeatable ? QuestState_Available : QuestState_Completed);

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-QUEST-TURNIN row=%s gold=%lld xp=%lld"),
		*Row.ToString(), QuestRow->RewardGold, QuestRow->RewardXP);

	// A story mission may ask for a bounty to be turned in (missions v0, 2026-09-04). After the
	// payout, so a mission can never advance on a turn-in that paid nothing.
	WrecktideMissionLogic::NotifyBountyTurnedIn(PlayerState, Row);
	return true;
}

FString ObjectiveLine(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	const FName Active = GetActiveQuest(Progress, Data);
	const FQuestTableRow* QuestRow = Active.IsNone() ? nullptr : Data->GetQuestRow(Active);
	if (!QuestRow)
	{
		return FString();
	}

	const int32 Target = SafeTargetCount(QuestRow);
	const int32 Kills = FMath::Min(Progress->GetQuestFlag(KillsKey(Active)), Target);
	FString Line = FString::Printf(TEXT("Bounty: %s %d/%d"), *QuestRow->DisplayName.ToString(), Kills, Target);
	if (Kills >= Target)
	{
		Line += TEXT(" (return to the board)");
	}

	return Line;
}

FString SkillDisplayName(EWrecktideSkill Skill)
{
	// The UENUM is the one place these names are spelled; a hand written switch would drift the
	// moment a skill is added.
	if (const UEnum* SkillEnum = StaticEnum<EWrecktideSkill>())
	{
		return SkillEnum->GetNameStringByValue(static_cast<int64>(Skill));
	}

	return FString(TEXT("Skill"));
}

}
