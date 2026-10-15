// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WrecktideShipwright.generated.h"

class ACombatCharacter;
class AWrecktideBoat;
class AWrecktidePlayerState;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * The shipwright's bench beside the bounty board (spec S4): three trading paths, three tiers each,
 * Timbers, Canvas and Hold, priced and gated by Content\Data\BoatUpgrades.csv. Spawned at runtime by
 * UWrecktideRewardSubsystem::SpawnShipwright exactly as the shop, the smithy and the barrel are, so
 * nothing is written into Lvl_Island.umap.
 *
 * There is no NPC here on purpose. Cobb the harbourmaster belongs to the QUESTS lane; this is the
 * BENCH and its sign, so when Cobb arrives his F can be pointed at OpenShipwrightMenu without either
 * lane having to unpick the other's work.
 *
 * Upgrades bind to the SAVE, not to the hull actor, so they survive a sinking (spec S2.3 takes the
 * hold, never the shipwright's work) and a reload.
 */
UCLASS()
class WRECKTIDE_API AWrecktideShipwright : public AActor
{
	GENERATED_BODY()

public:
	AWrecktideShipwright();

	/** BuyUpgrade is the SINGLE authoritative purchase path: the menu and the probe both come here. */
	bool BuyUpgrade(FName TrackRow);

	/** OpenShipwrightMenu is the SINGLE authoritative entry path for an in-range player's modal list. */
	void OpenShipwrightMenu(ACombatCharacter* Character);

	void ToggleShipwrightMenu(ACombatCharacter* Character);

	/** MoveMenuSelection is the SINGLE authoritative path used by menu keys and probes. */
	void MoveMenuSelection(int32 Direction);

	void ConfirmMenuSelection();

	bool IsMenuOpen() const { return bMenuOpen; }
	int32 GetRowCount() const { return MenuRows.Num(); }
	FName GetSelectedRow() const { return MenuRows.IsValidIndex(SelectedIndex) ? MenuRows[SelectedIndex] : NAME_None; }
	const TArray<FString>& GetMenuLines() const { return MenuLines; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UFUNCTION()
	void SelectPrevious();

	UFUNCTION()
	void SelectNext();

	UFUNCTION()
	void CloseShipwrightMenu();

	void CloseShipwright();
	void BindMenuInput();
	void RefreshMenu();
	void PushMenuToHUD() const;
	void ShowToast(const FString& Message) const;

	AWrecktideBoat* FindBoat() const;
	AWrecktidePlayerState* GetUserPlayerState() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> BenchMesh;

	/** The sign that says whose bench this is, since there is no NPC standing at it yet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> SignMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> TimberMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Trigger;

	TWeakObjectPtr<ACombatCharacter> ShipwrightUser;

	TArray<FName> MenuRows;
	TArray<FString> MenuLines;
	int32 SelectedIndex = 0;
	bool bMenuOpen = false;
};
