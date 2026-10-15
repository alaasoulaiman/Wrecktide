// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WrecktideSupplyBarrel.generated.h"

class ACombatCharacter;
class AWrecktideBoat;
class AWrecktidePlayerState;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * The supply barrel on the pier (spec S2.2): planks to patch the hull at sea and shot for the guns a
 * later wave will fit. Spawned at runtime beside the mooring by
 * UWrecktideRewardSubsystem::SpawnSupplyBarrel exactly as the shop and the smithy are, so nothing is
 * written into Lvl_Island.umap.
 *
 * The list is the smithy's menu pattern: one page, Up/Down/Enter, F or Escape to leave, drawn through
 * the HUD's shop-menu line list, and only ONE such modal may be open at a time. Gold moves through
 * the economy ledger with a reason, and the goods go into the BOAT's hold, which is where the save
 * keeps them.
 */
UCLASS()
class WRECKTIDE_API AWrecktideSupplyBarrel : public AActor
{
	GENERATED_BODY()

public:
	AWrecktideSupplyBarrel();

	/** BuySupply is the SINGLE authoritative purchase path: the menu and the probe both come here. */
	bool BuySupply(FName SupplyRow);

	/** OpenBarrelMenu is the SINGLE authoritative entry path for an in-range player's modal list. */
	void OpenBarrelMenu(ACombatCharacter* Character);

	/** Opens or closes this barrel for its currently overlapped player. */
	void ToggleBarrelMenu(ACombatCharacter* Character);

	/** MoveMenuSelection is the SINGLE authoritative path used by menu keys and probes. */
	void MoveMenuSelection(int32 Direction);

	/** Buys the selected row, or leaves on the Close row. */
	void ConfirmMenuSelection();

	bool IsMenuOpen() const { return bMenuOpen; }
	int32 GetRowCount() const { return MenuRows.Num(); }
	FName GetSelectedRow() const { return MenuRows.IsValidIndex(SelectedIndex) ? MenuRows[SelectedIndex] : NAME_None; }
	const TArray<FString>& GetMenuLines() const { return MenuLines; }

	/** Gold one plank costs. One plank patches AWrecktideBoat::PlankRepairAmount of hull. */
	UPROPERTY(EditAnywhere, Category="Supplies", meta=(ClampMin="0"))
	int64 PlankCost = 20;

	/** Gold one lot of shot costs, and how many rounds a lot is. */
	UPROPERTY(EditAnywhere, Category="Supplies", meta=(ClampMin="0"))
	int64 ShotLotCost = 10;

	UPROPERTY(EditAnywhere, Category="Supplies", meta=(ClampMin="1"))
	int32 ShotLotUnits = 5;

	/** How many planks one purchase puts aboard. */
	UPROPERTY(EditAnywhere, Category="Supplies", meta=(ClampMin="1"))
	int32 PlankLotUnits = 1;

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
	void CloseBarrelMenu();

	void CloseBarrel();
	void BindMenuInput();
	void RefreshMenu();
	void PushMenuToHUD() const;
	void ShowToast(const FString& Message) const;

	/** The player's boat, which is where the goods actually go. Null when none exists. */
	AWrecktideBoat* FindBoat() const;

	AWrecktidePlayerState* GetUserPlayerState() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> BarrelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> CrateMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Trigger;

	TWeakObjectPtr<ACombatCharacter> BarrelUser;

	TArray<FName> MenuRows;
	TArray<FString> MenuLines;
	int32 SelectedIndex = 0;
	bool bMenuOpen = false;
};
