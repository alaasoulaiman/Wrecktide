// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/EconomyComponent.h"

#include "GameFramework/Actor.h"
#include "Systems/ProgressComponent.h"

UEconomyComponent::UEconomyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UEconomyComponent::ApplyGoldDelta(int64 Delta, EGoldSource Source, EGoldReason Reason, FName Context)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Delta == 0)
	{
		return false;
	}

	if (Delta > 0)
	{
		EGoldSource AppliedSource = Source;
		if (bLastStakeTaintedPurchased && Reason == EGoldReason::GamblingWinnings)
		{
			AppliedSource = EGoldSource::Purchased;
			bLastStakeTaintedPurchased = false;
		}

		if (AppliedSource == EGoldSource::Purchased)
		{
			PurchasedGold += Delta;
		}
		else
		{
			EarnedGold += Delta;
		}

		FGoldLedgerEntry Entry;
		Entry.Delta = Delta;
		Entry.Source = AppliedSource;
		Entry.Reason = Reason;
		Entry.Context = Context;
		Entry.EarnedAfter = EarnedGold;
		Entry.PurchasedAfter = PurchasedGold;
		Entry.Timestamp = FDateTime::UtcNow();
		Ledger.Add(Entry);
		if (Ledger.Num() > 256)
		{
			Ledger.RemoveAt(0, Ledger.Num() - 256, EAllowShrinking::No);
		}

		OnGoldChanged.Broadcast(Delta, Reason, GetGoldTotal());
		return true;
	}

	const int64 Debit = -Delta;
	if (Source == EGoldSource::Earned)
	{
		if (EarnedGold < Debit)
		{
			return false;
		}

		EarnedGold -= Debit;
	}
	else
	{
		if (GetGoldTotal() < Debit)
		{
			return false;
		}

		const int64 PurchasedDebit = FMath::Min(PurchasedGold, Debit);
		PurchasedGold -= PurchasedDebit;
		EarnedGold -= Debit - PurchasedDebit;

		if (Reason == EGoldReason::GamblingStake && PurchasedDebit > 0)
		{
			bLastStakeTaintedPurchased = true;
		}
	}

	FGoldLedgerEntry Entry;
	Entry.Delta = Delta;
	Entry.Source = Source;
	Entry.Reason = Reason;
	Entry.Context = Context;
	Entry.EarnedAfter = EarnedGold;
	Entry.PurchasedAfter = PurchasedGold;
	Entry.Timestamp = FDateTime::UtcNow();
	Ledger.Add(Entry);
	if (Ledger.Num() > 256)
	{
		Ledger.RemoveAt(0, Ledger.Num() - 256, EAllowShrinking::No);
	}

	OnGoldChanged.Broadcast(Delta, Reason, GetGoldTotal());
	return true;
}

int64 UEconomyComponent::GetGoldTotal() const
{
	return EarnedGold + PurchasedGold;
}

int64 UEconomyComponent::GetEarnedGold() const
{
	return EarnedGold;
}

int64 UEconomyComponent::GetPurchasedGold() const
{
	return PurchasedGold;
}

const TArray<FGoldLedgerEntry>& UEconomyComponent::GetLedger() const
{
	return Ledger;
}

bool UEconomyComponent::HasPurchasedRow(FName Row) const
{
	if (Row == NAME_None)
	{
		return false;
	}

	// Ownership is answered from the persistent unlocked set, never from Ledger. Ledger is capped at
	// 256 entries and every kill appends one, so a purchase used to be evicted after 256 gold events
	// and never survived a save/load at all.
	const AActor* Owner = GetOwner();
	const UProgressComponent* ProgressComponent = Owner ? Owner->FindComponentByClass<UProgressComponent>() : nullptr;

	return ProgressComponent && ProgressComponent->IsItemUnlocked(Row);
}

void UEconomyComponent::RestoreFromSave(int64 InEarnedGold, int64 InPurchasedGold)
{
	EarnedGold = FMath::Max<int64>(InEarnedGold, 0);
	PurchasedGold = FMath::Max<int64>(InPurchasedGold, 0);
	bLastStakeTaintedPurchased = false;
}
