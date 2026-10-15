// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WrecktideTreasureChest.generated.h"

class ACombatCharacter;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A treasure chest the player opens once with F for a gold payout (2026-09-04, exploration reward).
 *
 * Level-placed (Tools\island_dress.py) with a ChestId; "opened" persists as the quest flag
 * Chest_<ChestId> in UProgressComponent::QuestFlags, so a chest stays open across relaunches and
 * can never pay twice. Open is the SINGLE authoritative path: the dockside F binding and the probe
 * both go through it, and the gold goes through UEconomyComponent::ApplyGoldDelta like every other
 * gold source. The F key lives on this actor's own input component (the boat's pattern), so nothing
 * in the pirate's own bindings changes.
 */
UCLASS()
class WRECKTIDE_API AWrecktideTreasureChest : public AActor
{
	GENERATED_BODY()

public:
	AWrecktideTreasureChest();

	/** Open is the SINGLE authoritative payout path. False when already opened, out of range or unwired. */
	bool Open(ACombatCharacter* Character);

	/** True when the persisted flag says this chest has paid out (reads the player's progress). */
	bool IsOpened() const;

	/** Persisted-flag spelling, in one place. */
	FName FlagKey() const;

	/** True when Character stands inside the trigger sphere. */
	bool IsCharacterInRange(const ACombatCharacter* Character) const;

	/** Level authoring identity; two chests with the same id share one payout. */
	UPROPERTY(EditAnywhere, Category="Treasure")
	FName ChestId = FName(TEXT("Chest"));

	/** Gold paid on the first open. */
	UPROPERTY(EditAnywhere, Category="Treasure")
	int64 GoldAmount = 100;

	/** Runtime payout after Chests.csv overrides the level-authored fallback. */
	int64 GetPayoutGold() const { return ResolvedGoldAmount >= 0 ? ResolvedGoldAmount : GoldAmount; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UFUNCTION()
	void OnOpenKey();

	void RefreshLid();
	void ResolveGoldAmount();
	void ShowToast(const FString& Message) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Treasure", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> ChestMesh;

	/** The gold pile inside; shown only while the chest is unopened. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Treasure", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> TreasureMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Treasure", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Trigger;

	TWeakObjectPtr<ACombatCharacter> NearbyCharacter;
	int64 ResolvedGoldAmount = -1;
	bool bLoggedMissingChestRow = false;
};
