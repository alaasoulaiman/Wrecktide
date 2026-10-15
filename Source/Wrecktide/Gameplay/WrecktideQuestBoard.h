// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WrecktideQuestBoard.generated.h"

class ACombatCharacter;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Runtime-spawned bounty board: the quest loop's one accept/turn-in surface. Mirrors
 * AWrecktideShopStall (same trigger, same F key, same HUD line list) because the player learns one
 * interaction, not two. Every bounty decision it makes goes through WrecktideQuestLogic.
 */
UCLASS()
class WRECKTIDE_API AWrecktideQuestBoard : public AActor
{
	GENERATED_BODY()

public:
	/** Constructor creates the board mesh and overlap trigger for the authoritative bounty path. */
	AWrecktideQuestBoard();

	/** OpenBoardMenu is the SINGLE authoritative entry path for an in-range player's bounty menu. */
	void OpenBoardMenu(ACombatCharacter* Character);

	/** ToggleBoardMenu opens or closes this board for its currently overlapped player. */
	void ToggleBoardMenu(ACombatCharacter* Character);

	/** MoveMenuSelection is the SINGLE authoritative path used by menu keys and probes. */
	void MoveMenuSelection(int32 Direction);

	/** ConfirmMenuSelection drives the selected line through Accept, TurnIn or Close. */
	void ConfirmMenuSelection();

	/** SelectQuestRow points the selection at a specific bounty; false when it is not listed. */
	bool SelectQuestRow(FName Row);

	/** Returns true when Character is the player currently inside this board's trigger. */
	bool IsUserInRange(const ACombatCharacter* Character) const;

	/** Returns true when this board's modal menu is currently open. */
	bool IsMenuOpen() const { return bMenuOpen; }

	/** Returns the drawn line count (bounty rows plus the trailing Close line) for probes. */
	int32 GetMenuLineCount() const { return MenuLines.Num(); }

	/** Returns the selected bounty row, or NAME_None on the Close line / an empty menu. */
	FName GetSelectedQuestRow() const { return MenuRows.IsValidIndex(SelectedIndex) ? MenuRows[SelectedIndex] : NAME_None; }

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
	void CloseBoardMenu();

	UFUNCTION()
	void OnBoardUserDied(AActor* InstigatorActor);

	void CloseBoard();
	void SetBoardMenuOpen(bool bOpen, bool bShowRangeHint);
	void BindMenuInput();
	void SetBoardUser(ACombatCharacter* Character);
	void RefreshBoardMenu();
	void PushMenuToHUD() const;
	void ShowToast(const FString& Message) const;

	/** True when any shop stall has a menu up; the two modals share the HUD list and the keys. */
	bool IsOtherMenuOpen() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Trigger;

	TWeakObjectPtr<ACombatCharacter> BoardUser;

	/** Parallel to MenuLines. The trailing Close line carries NAME_None. */
	TArray<FName> MenuRows;
	TArray<FString> MenuLines;
	int32 SelectedIndex = 0;
	bool bMenuOpen = false;
};
