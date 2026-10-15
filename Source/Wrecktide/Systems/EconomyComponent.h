// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Misc/DateTime.h"
#include "EconomyComponent.generated.h"

UENUM(BlueprintType)
enum class EGoldSource : uint8
{
	Earned,
	Purchased
};

UENUM(BlueprintType)
enum class EGoldReason : uint8
{
	MissionReward,
	LootPickup,
	HeistPayout,
	ShipRaid,
	ShopSale,
	ShopPurchase,
	ShipPurchase,
	UpgradePurchase,
	GamblingStake,
	GamblingWinnings,
	HeistSetupFee,
	StoreGoldGrant,
	Adjustment
};

USTRUCT(BlueprintType)
struct FGoldLedgerEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Economy")
	int64 Delta = 0;

	UPROPERTY(BlueprintReadOnly, Category="Economy")
	EGoldSource Source = EGoldSource::Earned;

	UPROPERTY(BlueprintReadOnly, Category="Economy")
	EGoldReason Reason = EGoldReason::Adjustment;

	UPROPERTY(BlueprintReadOnly, Category="Economy")
	FName Context = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="Economy")
	int64 EarnedAfter = 0;

	UPROPERTY(BlueprintReadOnly, Category="Economy")
	int64 PurchasedAfter = 0;

	UPROPERTY(BlueprintReadOnly, Category="Economy")
	FDateTime Timestamp;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGoldChanged, int64, Delta, EGoldReason, Reason, int64, NewTotal);

UCLASS(ClassGroup=(Wrecktide), meta=(BlueprintSpawnableComponent))
class WRECKTIDE_API UEconomyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEconomyComponent();

	/** SINGLE authoritative path for gold changes. */
	UFUNCTION(BlueprintCallable, Category="Economy")
	bool ApplyGoldDelta(int64 Delta, EGoldSource Source, EGoldReason Reason, FName Context);

	UFUNCTION(BlueprintPure, Category="Economy")
	int64 GetGoldTotal() const;

	UFUNCTION(BlueprintPure, Category="Economy")
	int64 GetEarnedGold() const;

	UFUNCTION(BlueprintPure, Category="Economy")
	int64 GetPurchasedGold() const;

	const TArray<FGoldLedgerEntry>& GetLedger() const;

	/**
	 * HasPurchasedRow is the SINGLE authoritative read path for shop-row ownership.
	 * Ownership itself lives in UProgressComponent::UnlockedItems on the same owning actor, because
	 * that set is SaveGame tagged and unbounded. Ledger below is a rolling 256 entry provenance
	 * buffer and must NEVER be used to answer an ownership question.
	 */
	UFUNCTION(BlueprintPure, Category="Economy")
	bool HasPurchasedRow(FName Row) const;

	/** Save-restore only: overwrites gold state without ledger entries or change broadcasts. */
	void RestoreFromSave(int64 InEarnedGold, int64 InPurchasedGold);

	UPROPERTY(BlueprintAssignable, Category="Economy")
	FOnGoldChanged OnGoldChanged;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Economy", meta=(AllowPrivateAccess="true"))
	int64 EarnedGold = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Economy", meta=(AllowPrivateAccess="true"))
	int64 PurchasedGold = 0;

	UPROPERTY(Transient)
	bool bLastStakeTaintedPurchased = false;

	/** Rolling 256 entry provenance buffer. Deliberately not SaveGame tagged; diagnostics only. */
	TArray<FGoldLedgerEntry> Ledger;
};
