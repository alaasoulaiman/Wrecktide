// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktideAudio.h"

#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/WrecktideBoat.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideQuestLogic.h"
#include "UI/WrecktideHUD.h"

namespace
{
/** Accepting a mission points the beacon at it. WHY on by default: a player who just took a job
 *  wants the arrow on THAT job, and the journal can move it back in one click (C23.1). */
constexpr bool bAutoTrackNewestMission = true;

/** Repeatable work pays gold x (1 + this x notoriety level). 0.1 = double pay at level 10. */
constexpr float RepeatableGoldPerLevel = 0.1f;

/** The mission table for a given player. Null whenever the game instance or the table is missing. */
const UWrecktideDataSubsystem* MissionDataFor(const AWrecktidePlayerState* PlayerState)
{
	const UGameInstance* GameInstance = PlayerState ? PlayerState->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
}

/** Same toast route the stall and the board use, so mission messages land in the one HUD line list. */
void ShowMissionToast(const AWrecktidePlayerState* PlayerState, const FString& Message)
{
	APlayerController* PlayerController = PlayerState ? UGameplayStatics::GetPlayerController(PlayerState, 0) : nullptr;
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}

/** How many times the objective must happen. A CSV row asking for 0 would be complete before it was
 *  accepted, so it is clamped fail-closed.
 *  A Pay row is the exception: its TargetCount is the PRICE, not a repeat count, and one payment of
 *  that price finishes it. Read the price straight off TargetCount, never through here. */
int32 SafeTargetCount(const FMissionTableRow* Row)
{
	if (Row && Row->Type == EMissionObjective::Pay)
	{
		return 1;
	}
	return Row ? FMath::Max(Row->TargetCount, 1) : 1;
}

/** The highest Order in the table; MaxOrder + 1 is the "chain finished" value of Mission_Current. */
int32 MaxMissionOrder(const UWrecktideDataSubsystem* Data)
{
	int32 Max = 0;
	if (Data)
	{
		for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
		{
			Max = FMath::Max(Max, Entry.Value->Order);
		}
	}
	return Max;
}

/** Wall clock in whole seconds. Unix time so a cooldown survives quitting the game, which a world
 *  time value would not; it fits int32 until 2038 and QuestFlags stores int32. */
int32 NowSeconds()
{
	return static_cast<int32>(FDateTime::UtcNow().ToUnixTimestamp());
}

/** Who a row belongs to. An empty GiverId means Bell, so every missions v0 row keeps its owner
 *  without the CSV having to spell it out twice. */
FName GiverOf(const FMissionTableRow* Row)
{
	return (Row && !Row->GiverId.IsNone()) ? Row->GiverId : FName(TEXT("Bell"));
}

/** Is this STORY row offerable right now? Three rules and nothing else decides it:
 *  its chapter gate is open, it is not already paid out, and every earlier row OF THE SAME GIVER is
 *  done. Per giver rather than globally, so one gate flag can open three chains at once (research
 *  B3: one mission that opens several things is what keeps the island from going quiet). */
bool IsStoryRowOfferable(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row, const FMissionTableRow* MissionRow);

/** Counting is identical for every objective kind, so exactly one function writes the count and
 *  exactly one place decides when the "ready" toast fires. */
void AdvanceObjective(AWrecktidePlayerState* PlayerState, FName Row, const FMissionTableRow* MissionRow)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	if (!Progress || !MissionRow)
	{
		return;
	}

	const int32 Target = SafeTargetCount(MissionRow);
	const int32 Count = Progress->GetQuestFlag(WrecktideMissionLogic::ProgressKey(Row));
	if (Count >= Target)
	{
		// Already met and waiting at the giver. Capped here, so the count can never run past the
		// target and the ready toast fires exactly once.
		return;
	}

	const int32 NewCount = FMath::Min(Count + 1, Target);
	Progress->SetQuestFlag(WrecktideMissionLogic::ProgressKey(Row), NewCount);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PROGRESS row=%s %d/%d"), *Row.ToString(), NewCount, Target);

	const bool bSelfCompleting = MissionRow->bAutoComplete
		|| MissionRow->Type == EMissionObjective::TalkTo;
	if (NewCount >= Target && !bSelfCompleting)
	{
		// ReachSpot and TalkTo complete where they happen and toast through CompleteRow, so they
		// must not toast twice.
		const FString Giver = MissionRow->Giver.IsEmpty() ? FString(TEXT("the quartermaster")) : MissionRow->Giver.ToString();
		ShowMissionToast(PlayerState, FString::Printf(TEXT("Objective done. Report to %s."), *Giver));
	}
}

bool IsStoryRowOfferable(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row, const FMissionTableRow* MissionRow)
{
	if (!Progress || !Data || !MissionRow || MissionRow->bRepeatable)
	{
		return false;
	}
	if (Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(Row)) != 0)
	{
		return false;
	}
	if (!MissionRow->RequireFlag.IsNone() && Progress->GetQuestFlag(MissionRow->RequireFlag) == 0)
	{
		return false;
	}

	const FName Giver = GiverOf(MissionRow);
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (Entry.Value->bRepeatable || Entry.Value->Order >= MissionRow->Order || GiverOf(Entry.Value) != Giver)
		{
			continue;
		}
		if (Progress->GetQuestFlag(WrecktideMissionLogic::DoneKey(Entry.Key)) == 0)
		{
			// An earlier link of his own chain is unfinished, so this one is not his next word.
			return false;
		}
	}

	return true;
}

/** Advance every ACTIVE mission whose type and target match, then pay out the ones that finish
 *  where they stand. ONE loop, so a player carrying three jobs never has two of them silently
 *  ignored (the v1 code only ever looked at the single current mission).
 *  Returns nothing: every caller is a notify hook with nothing to decide. */
template <typename TPredicate>
void AdvanceMatchingMissions(AWrecktidePlayerState* PlayerState, EMissionObjective Type, TPredicate&& Matches)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	if (!Progress || !Data)
	{
		return;
	}

	TArray<FName> Rows;
	WrecktideMissionLogic::GetActiveMissionRows(Progress, Data, Rows);
	for (const FName Row : Rows)
	{
		const FMissionTableRow* MissionRow = Data->GetMissionRow(Row);
		if (!MissionRow || MissionRow->Type != Type || !Matches(*MissionRow))
		{
			continue;
		}

		AdvanceObjective(PlayerState, Row, MissionRow);

		// TalkTo is a hand-off: there is nobody to return to, the NPC you are looking at IS the
		// turn-in, so it pays out here through the one authoritative path.
		if (Type == EMissionObjective::TalkTo && WrecktideMissionLogic::IsRowReadyToComplete(Progress, Data, Row))
		{
			WrecktideMissionLogic::CompleteRow(PlayerState, Row);
		}
	}
}
}

namespace WrecktideMissionLogic
{

FName CurrentKey()
{
	return FName(TEXT("Mission_Current"));
}

FName ProgressKey(FName Row)
{
	return FName(*FString::Printf(TEXT("Mission_%s_Progress"), *Row.ToString()));
}

FName DoneKey(FName Row)
{
	return FName(*FString::Printf(TEXT("Mission_%s_Done"), *Row.ToString()));
}

FName ActiveKey(FName Row)
{
	return FName(*FString::Printf(TEXT("Mission_%s_Active"), *Row.ToString()));
}

FName CountKey(FName Row)
{
	return FName(*FString::Printf(TEXT("Mission_%s_Count"), *Row.ToString()));
}

FName CooldownKey(FName Row)
{
	return FName(*FString::Printf(TEXT("Mission_%s_CoolUntil"), *Row.ToString()));
}

bool IsMissionActive(const UProgressComponent* Progress, FName Row)
{
	return Progress && !Row.IsNone() && Progress->GetQuestFlag(ActiveKey(Row)) == 1;
}

void GetActiveMissionRows(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, TArray<FName>& OutRows)
{
	OutRows.Reset();
	if (!Progress || !Data)
	{
		return;
	}

	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (IsMissionActive(Progress, Entry.Key))
		{
			OutRows.Add(Entry.Key);
		}
	}
}

FName GetCurrentMissionRow(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	if (!Progress || !Data)
	{
		return NAME_None;
	}

	const int32 TrackedOrder = Progress->GetQuestFlag(CurrentKey());
	TArray<FName> ActiveRows;
	GetActiveMissionRows(Progress, Data, ActiveRows);
	if (ActiveRows.Num() == 0)
	{
		return NAME_None;
	}

	for (const FName Row : ActiveRows)
	{
		const FMissionTableRow* MissionRow = Data->GetMissionRow(Row);
		if (MissionRow && MissionRow->Order == TrackedOrder)
		{
			return Row;
		}
	}

	// Self healing: the tracked pointer names nothing active (a row was deleted from the CSV, or an
	// old save carries a stale Order), so the first active mission takes the beacon rather than the
	// player being left with a job and no arrow.
	return ActiveRows[0];
}

FName GetTrackedMissionRow(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	return GetCurrentMissionRow(Progress, Data);
}

bool SetTrackedMission(AWrecktidePlayerState* PlayerState, FName Row)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	const FMissionTableRow* MissionRow = (Progress && Data && !Row.IsNone()) ? Data->GetMissionRow(Row) : nullptr;
	if (!MissionRow || !IsMissionActive(Progress, Row))
	{
		// Fail closed: tracking a mission the player has not accepted would put the beacon on a job
		// he cannot complete.
		return false;
	}

	Progress->SetQuestFlag(CurrentKey(), MissionRow->Order);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-TRACK row=%s order=%d"), *Row.ToString(), MissionRow->Order);
	return true;
}

FName GetNextMissionRow(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	if (!Progress || !Data)
	{
		return NAME_None;
	}

	// The first story row anyone would hand over, in global Order. Several may be offerable at once
	// after a chapter gate opens; this returns the earliest, which is the one the story means next.
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (IsStoryRowOfferable(Progress, Data, Entry.Key, Entry.Value))
		{
			return Entry.Key;
		}
	}

	return NAME_None;
}

bool IsGiverUnlocked(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName GiverId)
{
	if (!Progress || !Data || GiverId.IsNone())
	{
		return false;
	}

	const FNpcTableRow* Npc = Data->GetNpcRow(GiverId);
	if (!Npc || Npc->UnlockFlag.IsNone())
	{
		// No row, or a row with no gate: open. Bell is the authored case; a missing table is the
		// fail-OPEN case, because an island where nobody will talk to you is worse than one where
		// everybody will (the data subsystem logs WRECKTIDE-NPC-DATA when the table is empty).
		return true;
	}
	if (Progress->GetQuestFlag(Npc->UnlockFlag) != 0)
	{
		return true;
	}

	// An errand the player is ALREADY carrying that names him opens his door for that conversation.
	// WHY: M08 is "speak to Jerold" and completing it is what sets Story_Steel, so gating Jerold on
	// the flag alone would make the mission that unlocks him impossible to finish.
	TArray<FName> ActiveRows;
	GetActiveMissionRows(Progress, Data, ActiveRows);
	for (const FName Row : ActiveRows)
	{
		const FMissionTableRow* MissionRow = Data->GetMissionRow(Row);
		if (MissionRow && MissionRow->TargetName == GiverId
			&& (MissionRow->Type == EMissionObjective::TalkTo || MissionRow->Type == EMissionObjective::Deliver))
		{
			return true;
		}
	}

	return false;
}

FString GetGiverBrushOff(const UWrecktideDataSubsystem* Data, FName GiverId)
{
	const FNpcTableRow* Npc = (Data && !GiverId.IsNone()) ? Data->GetNpcRow(GiverId) : nullptr;
	return Npc ? Npc->BrushOff.ToString() : FString();
}

FName GetOfferForGiver(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName GiverId)
{
	if (!Progress || !Data || GiverId.IsNone())
	{
		return NAME_None;
	}

	// 0. It is not his turn yet (C30): he has nothing, wears no glyph and stays out of the journal.
	//    Checked FIRST so one rule covers the menu, the head glyph, the map mark and the journal.
	if (!IsGiverUnlocked(Progress, Data, GiverId))
	{
		return NAME_None;
	}

	// 1. A job of his already running is what he talks about.
	TArray<FName> ActiveRows;
	GetActiveMissionRows(Progress, Data, ActiveRows);
	for (const FName Row : ActiveRows)
	{
		if (GiverOf(Data->GetMissionRow(Row)) == GiverId)
		{
			return Row;
		}
	}

	// 2. His own next story row: the earliest offerable row of his, gate open, chain in order.
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (GiverOf(Entry.Value) == GiverId && IsStoryRowOfferable(Progress, Data, Entry.Key, Entry.Value))
		{
			return Entry.Key;
		}
	}

	// 3. Repeatable work, but ONLY once his own story is finished (C22.3). WHY: a blacksmith who
	//    offers errands before the story has introduced him steals the hand-off, and the chapter
	//    gate stops meaning anything because he was already talking.
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (!Entry.Value->bRepeatable && GiverOf(Entry.Value) == GiverId
			&& Progress->GetQuestFlag(DoneKey(Entry.Key)) == 0)
		{
			return NAME_None;
		}
	}

	//    The template he has run least, tie broken by Order, skipping anything still cooling down.
	//    WHY least-run first: two templates then alternate, so the same NPC never hands out the same
	//    errand twice in a row.
	const int32 Now = NowSeconds();
	FName Best = NAME_None;
	int32 BestCount = MAX_int32;
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (!Entry.Value->bRepeatable || GiverOf(Entry.Value) != GiverId)
		{
			continue;
		}
		if (Progress->GetQuestFlag(CooldownKey(Entry.Key)) > Now)
		{
			continue;
		}
		const int32 Count = Progress->GetQuestFlag(CountKey(Entry.Key));
		if (Count < BestCount)
		{
			BestCount = Count;
			Best = Entry.Key;
		}
	}

	return Best;
}

FString GetGiverSignal(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName GiverId)
{
	const FName Offer = GetOfferForGiver(Progress, Data, GiverId);
	if (Offer.IsNone())
	{
		return FString();
	}

	if (IsMissionActive(Progress, Offer))
	{
		// His own job, finished, waiting to be handed back.
		return IsRowReadyToComplete(Progress, Data, Offer) ? FString(TEXT("?")) : FString();
	}

	return FString(TEXT("!"));
}

bool IsActive(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	TArray<FName> ActiveRows;
	GetActiveMissionRows(Progress, Data, ActiveRows);
	return ActiveRows.Num() > 0;
}

bool IsChainComplete(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	// An empty table is NOT a finished chain: the giver must say "unavailable", never "well done".
	// Every story row DONE, not merely "nothing offerable": with chapter gates a row can be waiting
	// on a flag rather than finished, and that is not a finished story.
	if (!Progress || !Data || Data->GetMissionRows().Num() == 0)
	{
		return false;
	}
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		if (!Entry.Value->bRepeatable && Progress->GetQuestFlag(DoneKey(Entry.Key)) == 0)
		{
			return false;
		}
	}
	return true;
}

void GetRowProgress(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row, int32& OutCount, int32& OutTarget)
{
	OutCount = 0;
	OutTarget = 1;

	const FMissionTableRow* MissionRow = (Progress && Data && !Row.IsNone()) ? Data->GetMissionRow(Row) : nullptr;
	if (!MissionRow)
	{
		return;
	}

	OutTarget = SafeTargetCount(MissionRow);
	OutCount = FMath::Min(Progress->GetQuestFlag(ProgressKey(Row)), OutTarget);
}

void GetProgress(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, int32& OutCount, int32& OutTarget)
{
	GetRowProgress(Progress, Data, GetCurrentMissionRow(Progress, Data), OutCount, OutTarget);
}

bool AcceptFromGiver(AWrecktidePlayerState* PlayerState, FName GiverId)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	if (!Progress || !Data)
	{
		return false;
	}

	const FName Offer = GetOfferForGiver(Progress, Data, GiverId);
	const FMissionTableRow* MissionRow = Offer.IsNone() ? nullptr : Data->GetMissionRow(Offer);
	if (!MissionRow || IsMissionActive(Progress, Offer))
	{
		// Nothing to give, or his job is already running: fail closed, no flags touched.
		return false;
	}

	// Count first: if the second write ever failed, the mission would be un-accepted with a zeroed
	// count rather than active with a stale one.
	Progress->SetQuestFlag(ProgressKey(Offer), 0);
	Progress->SetQuestFlag(ActiveKey(Offer), 1);
	if (bAutoTrackNewestMission)
	{
		Progress->SetQuestFlag(CurrentKey(), MissionRow->Order);
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-ACCEPT row=%s order=%d giver=%s"),
		*Offer.ToString(), MissionRow->Order, *GiverOf(MissionRow).ToString());
	return true;
}

bool Accept(AWrecktidePlayerState* PlayerState)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	const FName Next = GetNextMissionRow(Progress, Data);
	if (Next.IsNone())
	{
		return false;
	}

	return AcceptFromGiver(PlayerState, GiverOf(Data->GetMissionRow(Next)));
}

void NotifyKill(AWrecktidePlayerState* PlayerState, FName EnemyRow)
{
	// An empty TargetName counts every enemy row; anything else must match exactly.
	auto Matches = [EnemyRow](const FMissionTableRow& Row)
	{
		return Row.TargetName.IsNone() || Row.TargetName == EnemyRow;
	};
	AdvanceMatchingMissions(PlayerState, EMissionObjective::KillRow, Matches);
	AdvanceMatchingMissions(PlayerState, EMissionObjective::KillBoss, Matches);
}

void NotifyChestOpened(AWrecktidePlayerState* PlayerState, FName ChestId)
{
	AdvanceMatchingMissions(PlayerState, EMissionObjective::OpenChest, [ChestId](const FMissionTableRow& Row)
	{
		return Row.TargetName.IsNone() || Row.TargetName == ChestId;
	});
}

void NotifyBoarded(AWrecktidePlayerState* PlayerState)
{
	AdvanceMatchingMissions(PlayerState, EMissionObjective::BoardBoat, [](const FMissionTableRow&) { return true; });
}

void NotifyBountyTurnedIn(AWrecktidePlayerState* PlayerState, FName BountyRow)
{
	AdvanceMatchingMissions(PlayerState, EMissionObjective::TurnInBounty, [BountyRow](const FMissionTableRow& Row)
	{
		return Row.TargetName.IsNone() || Row.TargetName == BountyRow;
	});
}

void NotifyTalkedTo(AWrecktidePlayerState* PlayerState, FName GiverId)
{
	auto Matches = [GiverId](const FMissionTableRow& Row) { return Row.TargetName == GiverId; };
	AdvanceMatchingMissions(PlayerState, EMissionObjective::TalkTo, Matches);
	// Deliver counts here too but does NOT pay out: the errand ends back at the NPC who set it.
	AdvanceMatchingMissions(PlayerState, EMissionObjective::Deliver, Matches);
}

void NotifyPaid(AWrecktidePlayerState* PlayerState, int64 Amount)
{
	AdvanceMatchingMissions(PlayerState, EMissionObjective::Pay, [Amount](const FMissionTableRow& Row)
	{
		// TargetCount is the price, and it is met by one payment of at least that much.
		return Amount >= static_cast<int64>(FMath::Max(Row.TargetCount, 1));
	});
}

void NotifyUpgradePurchased(AWrecktidePlayerState* PlayerState)
{
	AdvanceMatchingMissions(PlayerState, EMissionObjective::Upgrade, [](const FMissionTableRow&) { return true; });
}

bool ResolveSpotLocation(const UObject* WorldContext, const FMissionTableRow& Row, FVector& OutLocation)
{
	UWorld* World = WorldContext ? const_cast<UWorld*>(WorldContext->GetWorld()) : nullptr;
	const FName Target = Row.TargetName;

	// Named landmarks resolve to the LIVE actor, because the camp props and the NPCs are spawned at
	// runtime off the PlayerStart and have no fixed coordinates to put in a CSV.
	if (World && Target == FName(TEXT("MarketStall")))
	{
		for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
	}
	else if (World && Target == FName(TEXT("QuestBoard")))
	{
		for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
	}
	else if (World && Target == FName(TEXT("Boat")))
	{
		for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
	}
	else if (World && Target == FName(TEXT("Smithy")))
	{
		for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
	}
	else if (World && !Target.IsNone())
	{
		// Any NPC by his GiverId (Bell, Jerold, Marta, Cobb): a TalkTo, a Deliver or a ReachSpot may
		// all name a person, and the beacon has to be able to point at one (C23.2).
		for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
		{
			if (It->GetGiverId() == Target)
			{
				OutLocation = It->GetActorLocation();
				return true;
			}
		}
	}

	// CSV fallback for a spot with no actor behind it. A zero vector means the row named nothing
	// reachable, and the mission then stays active rather than completing at the world origin.
	if (!Row.TargetLocation.IsNearlyZero())
	{
		OutLocation = Row.TargetLocation;
		return true;
	}

	return false;
}

void NotifyLocation(AWrecktidePlayerState* PlayerState, const FVector& PlayerLocation)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	if (!Progress || !Data)
	{
		return;
	}

	TArray<FName> ActiveRows;
	GetActiveMissionRows(Progress, Data, ActiveRows);
	for (const FName Row : ActiveRows)
	{
		const FMissionTableRow* MissionRow = Data->GetMissionRow(Row);
		if (!MissionRow || MissionRow->Type != EMissionObjective::ReachSpot)
		{
			continue;
		}

		FVector Spot = FVector::ZeroVector;
		if (!ResolveSpotLocation(PlayerState, *MissionRow, Spot))
		{
			// Once per process: the 2 Hz timer would otherwise bury the log, and a silent fail-closed
			// mission is exactly the kind of soft lock that has to be visible.
			static bool bLoggedSpotFailure = false;
			if (!bLoggedSpotFailure)
			{
				bLoggedSpotFailure = true;
				UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING row=%s ReachSpot target %s resolves to nothing; the mission cannot complete"),
					*Row.ToString(), *MissionRow->TargetName.ToString());
			}
			continue;
		}

		const float Radius = FMath::Max(MissionRow->TargetRadius, 100.0f);
		if (FVector::DistSquared(PlayerLocation, Spot) > FMath::Square(Radius))
		{
			continue;
		}

		AdvanceObjective(PlayerState, Row, MissionRow);

		// The RETURN VISIT rule (research A4): arriving counts the objective, but the reward is paid
		// face to face with the giver, which is what puts a "?" over his head and lets him hand over
		// the next link in the same conversation. bAutoComplete opts a row out of that, and only the
		// tutorial walk M01_Wake does, because it predates the rule and the shipped probe asserts it.
		if (MissionRow->bAutoComplete && IsRowReadyToComplete(Progress, Data, Row))
		{
			CompleteRow(PlayerState, Row);
		}
	}
}

bool IsRowReadyToComplete(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row)
{
	const FMissionTableRow* MissionRow = (Progress && Data && !Row.IsNone()) ? Data->GetMissionRow(Row) : nullptr;
	if (!MissionRow || !IsMissionActive(Progress, Row))
	{
		return false;
	}

	return Progress->GetQuestFlag(ProgressKey(Row)) >= SafeTargetCount(MissionRow);
}

bool IsReadyToComplete(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data)
{
	return IsRowReadyToComplete(Progress, Data, GetCurrentMissionRow(Progress, Data));
}

int64 GetRewardGold(const FMissionTableRow& Row, const AWrecktidePlayerState* PlayerState)
{
	if (!Row.bRepeatable)
	{
		return Row.RewardGold;
	}

	const USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	const int32 Level = Skill ? FMath::Max(Skill->GetNotorietyLevel(), 1) : 1;
	return FMath::RoundToInt64(static_cast<double>(Row.RewardGold) * (1.0 + RepeatableGoldPerLevel * static_cast<double>(Level)));
}

int64 GetRewardXP(const FMissionTableRow& Row, const AWrecktidePlayerState* PlayerState)
{
	if (!Row.bRepeatable)
	{
		return Row.RewardXP;
	}

	const USkillComponent* Skill = PlayerState ? PlayerState->GetSkill() : nullptr;
	const int32 Level = Skill ? FMath::Max(Skill->GetNotorietyLevel(), 1) : 1;
	return FMath::RoundToInt64(static_cast<double>(Row.RewardXP) * (1.0 + RepeatableGoldPerLevel * static_cast<double>(Level)));
}

bool CompleteRow(AWrecktidePlayerState* PlayerState, FName Row)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	if (!Progress || !Data || !IsRowReadyToComplete(Progress, Data, Row))
	{
		return false;
	}

	const FMissionTableRow* MissionRow = Data->GetMissionRow(Row);
	UEconomyComponent* Economy = PlayerState->GetEconomy();
	USkillComponent* Skill = PlayerState->GetSkill();
	if (!MissionRow || !Economy || !Skill)
	{
		// Checked BEFORE any write: half a reward (gold paid, XP lost) would be worse than none, and
		// the mission stays completable so nothing is destroyed by the refusal.
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING complete refused row=%s economy=%d skill=%d"),
			*Row.ToString(), Economy != nullptr, Skill != nullptr);
		return false;
	}

	const FWeaponTableRow* RewardWeapon = MissionRow->RewardWeapon.IsNone() ? nullptr : Data->GetWeaponRow(MissionRow->RewardWeapon);
	if (!MissionRow->RewardWeapon.IsNone() && (!RewardWeapon
		|| (RewardWeapon->CombatClass != EWrecktideSkill::Melee && RewardWeapon->CombatClass != EWrecktideSkill::Shooting)))
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MISSION-WIRING reward weapon unavailable row=%s weapon=%s"),
			*Row.ToString(), *MissionRow->RewardWeapon.ToString());
		return false;
	}

	// Closed first, then pay: a mission is a ONE TIME payout, so a crash between the two writes must
	// cost one reward rather than let the chain pay it twice (the treasure chest's rule).
	Progress->SetQuestFlag(ActiveKey(Row), 0);
	if (MissionRow->bRepeatable)
	{
		// Repeatable work is never "done": it banks a count and starts cooling down, so the same
		// template can come back around. Cooldown 0 in the CSV means "offer it again at once".
		Progress->SetQuestFlag(ProgressKey(Row), 0);
		Progress->SetQuestFlag(CountKey(Row), Progress->GetQuestFlag(CountKey(Row)) + 1);
		Progress->SetQuestFlag(CooldownKey(Row), NowSeconds() + FMath::Max(0, FMath::RoundToInt(MissionRow->CooldownSeconds)));
	}
	else
	{
		Progress->SetQuestFlag(DoneKey(Row), 1);
	}
	if (!MissionRow->UnlockFlag.IsNone())
	{
		Progress->SetQuestFlag(MissionRow->UnlockFlag, 1);
	}

	// The tracked pointer: hand the beacon to whatever is still running, else the "all done" marker
	// when the story is over, else nothing. Derived from the table, never a hard coded 8.
	TArray<FName> StillActive;
	GetActiveMissionRows(Progress, Data, StillActive);
	if (StillActive.Num() > 0)
	{
		const FMissionTableRow* NextActive = Data->GetMissionRow(StillActive[0]);
		Progress->SetQuestFlag(CurrentKey(), NextActive ? NextActive->Order : 0);
	}
	else
	{
		Progress->SetQuestFlag(CurrentKey(), GetNextMissionRow(Progress, Data).IsNone() ? MaxMissionOrder(Data) + 1 : 0);
	}

	const int64 RewardGold = GetRewardGold(*MissionRow, PlayerState);
	const int64 RewardXP = GetRewardXP(*MissionRow, PlayerState);
	if (RewardGold > 0)
	{
		Economy->ApplyGoldDelta(RewardGold, EGoldSource::Earned, EGoldReason::MissionReward, Row);
	}

	if (RewardXP > 0)
	{
		Skill->AwardXP(MissionRow->RewardSkill, RewardXP, Row);
	}

	// Toast and kill-feed line live HERE, not in the giver: a ReachSpot mission completes with no
	// NPC in sight and must still tell the player what just happened.
	const FString Title = MissionRow->Title.IsEmpty() ? Row.ToString() : MissionRow->Title.ToString();
	ShowMissionToast(PlayerState, FString::Printf(TEXT("Mission complete: %s"), *Title));
	if (RewardWeapon)
	{
		Progress->UnlockItem(MissionRow->RewardWeapon);
		const int32 WeaponSlot = RewardWeapon->CombatClass == EWrecktideSkill::Shooting ? 2 : 1;
		const FName HeldRow = PlayerState->EquippedWeaponRow;
		if (PlayerState->GetAssignedSlot(WeaponSlot).IsNone())
		{
			PlayerState->AssignSlot(WeaponSlot, MissionRow->RewardWeapon);
			PlayerState->EquipWeapon(HeldRow);
		}
		const FString WeaponName = RewardWeapon->DisplayName.IsEmpty()
			? MissionRow->RewardWeapon.ToString() : RewardWeapon->DisplayName.ToString();
		ShowMissionToast(PlayerState, PlayerState->GetAssignedSlot(WeaponSlot) == MissionRow->RewardWeapon
			? FString::Printf(TEXT("%s: press %d to draw it"), *WeaponName, WeaponSlot)
			: FString::Printf(TEXT("%s: choose it on the Belt"), *WeaponName));
	}
	UWrecktideAudio::PlaySfx(PlayerState, TEXT("fanfare"));

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(PlayerState, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->PushRewardLine(FString::Printf(TEXT("+%lld gold   +%lld %s XP  (%s)"),
			RewardGold, RewardXP,
			*WrecktideQuestLogic::SkillDisplayName(MissionRow->RewardSkill), *Title));
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-COMPLETE row=%s gold=%lld xp=%lld unlock=%s repeat=%d current=%d"),
		*Row.ToString(), RewardGold, RewardXP, *MissionRow->UnlockFlag.ToString(),
		MissionRow->bRepeatable ? 1 : 0, Progress->GetQuestFlag(CurrentKey()));
	return true;
}

bool Complete(AWrecktidePlayerState* PlayerState)
{
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	return CompleteRow(PlayerState, GetCurrentMissionRow(Progress, Data));
}

FString ObjectiveLine(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, FName Row, const UObject* WorldContext, const FVector* PlayerLocation)
{
	const FMissionTableRow* MissionRow = (Progress && Data && !Row.IsNone()) ? Data->GetMissionRow(Row) : nullptr;
	if (!MissionRow)
	{
		return FString();
	}

	int32 Count = 0;
	int32 Target = 1;
	GetRowProgress(Progress, Data, Row, Count, Target);

	FString Line = MissionRow->Objective.ToString();
	if (Target > 1)
	{
		Line += FString::Printf(TEXT("  %d/%d"), Count, Target);
	}

	if (MissionRow->Type == EMissionObjective::ReachSpot && Count < Target && WorldContext && PlayerLocation)
	{
		FVector Spot = FVector::ZeroVector;
		if (ResolveSpotLocation(WorldContext, *MissionRow, Spot))
		{
			Line += FString::Printf(TEXT("  %d m"), FMath::RoundToInt(FVector::Dist2D(*PlayerLocation, Spot) / 100.0f));
		}
	}

	return Line;
}

FString LogLine(const UProgressComponent* Progress, const UWrecktideDataSubsystem* Data, const UObject* WorldContext, const FVector* PlayerLocation)
{
	const FName Row = GetCurrentMissionRow(Progress, Data);
	const FMissionTableRow* MissionRow = Row.IsNone() ? nullptr : Data->GetMissionRow(Row);
	if (!MissionRow)
	{
		return FString();
	}

	int32 Count = 0;
	int32 Target = 1;
	GetRowProgress(Progress, Data, Row, Count, Target);

	const FString Title = MissionRow->Title.IsEmpty() ? Row.ToString() : MissionRow->Title.ToString();
	FString Line = FString::Printf(TEXT("Mission: %s"), *Title);

	// A counted objective shows the count; a one-shot one would only ever read 0/1 or 1/1, which
	// says less than the objective text already on the giver's menu.
	if (Target > 1)
	{
		Line += FString::Printf(TEXT("  %d/%d"), Count, Target);
	}

	// A reach-the-spot objective says how far (2026-09-05): only when the caller passes a location, so
	// the probes' calls and the log lines keep their old text.
	if (MissionRow->Type == EMissionObjective::ReachSpot && Count < Target && WorldContext && PlayerLocation)
	{
		FVector Spot = FVector::ZeroVector;
		if (ResolveSpotLocation(WorldContext, *MissionRow, Spot))
		{
			const int32 Metres = FMath::RoundToInt(FVector::Dist2D(*PlayerLocation, Spot) / 100.0f);
			Line += FString::Printf(TEXT("  %d m"), Metres);
		}
	}

	if (Count >= Target)
	{
		const FString Giver = MissionRow->Giver.IsEmpty() ? FString(TEXT("the quartermaster")) : MissionRow->Giver.ToString();
		Line += FString::Printf(TEXT("  (see %s)"), *Giver);
	}

	return Line;
}

void GetJournalEntries(const AWrecktidePlayerState* PlayerState, TArray<FMissionJournalEntry>& OutEntries)
{
	OutEntries.Reset();

	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UWrecktideDataSubsystem* Data = MissionDataFor(PlayerState);
	if (!Progress || !Data)
	{
		return;
	}

	const FName Tracked = GetCurrentMissionRow(Progress, Data);
	const APawn* Pawn = PlayerState->GetPawn();
	const FVector PawnLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	const FVector* LocationPtr = Pawn ? &PawnLocation : nullptr;

	auto MakeEntry = [&](FName Row, const FMissionTableRow& MissionRow, EMissionEntryState State)
	{
		FMissionJournalEntry Entry;
		Entry.Row = Row;
		Entry.Title = MissionRow.Title.IsEmpty() ? Row.ToString() : MissionRow.Title.ToString();
		Entry.Giver = MissionRow.Giver.IsEmpty() ? FString(TEXT("the quartermaster")) : MissionRow.Giver.ToString();
		Entry.Objective = ObjectiveLine(Progress, Data, Row, PlayerState, LocationPtr);
		Entry.State = State;
		Entry.bTracked = (State == EMissionEntryState::Active) && Row == Tracked;
		Entry.bHasTarget = ResolveSpotLocation(PlayerState, MissionRow, Entry.TargetLocation);
		OutEntries.Add(Entry);
	};

	// ACTIVE first, in table Order, so the tracked job is near the top of the list.
	TArray<FName> ActiveRows;
	GetActiveMissionRows(Progress, Data, ActiveRows);
	for (const FName Row : ActiveRows)
	{
		if (const FMissionTableRow* MissionRow = Data->GetMissionRow(Row))
		{
			MakeEntry(Row, *MissionRow, EMissionEntryState::Active);
		}
	}

	// AVAILABLE: exactly what each NPC would hand over on the next F, asked through the one offer
	// rule so the journal can never advertise work the giver would refuse.
	TSet<FName> Givers;
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		Givers.Add(GiverOf(Entry.Value));
	}
	for (const FName GiverId : Givers)
	{
		const FName Offer = GetOfferForGiver(Progress, Data, GiverId);
		if (Offer.IsNone() || IsMissionActive(Progress, Offer))
		{
			continue;
		}
		if (const FMissionTableRow* MissionRow = Data->GetMissionRow(Offer))
		{
			MakeEntry(Offer, *MissionRow, EMissionEntryState::Available);
		}
	}

	// COMPLETED last: story rows marked done, and repeatable templates that have paid at least once.
	for (const TPair<FName, const FMissionTableRow*>& Entry : Data->GetMissionRows())
	{
		const bool bDone = Entry.Value->bRepeatable
			? Progress->GetQuestFlag(CountKey(Entry.Key)) > 0
			: Progress->GetQuestFlag(DoneKey(Entry.Key)) > 0;
		if (bDone)
		{
			MakeEntry(Entry.Key, *Entry.Value, EMissionEntryState::Completed);
		}
	}
}

}
