// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Systems/SkillComponent.h"
#include "WrecktideSmithStall.generated.h"

class ACombatCharacter;
class AWrecktidePlayerState;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Which list the smithy is showing. Owned weapons first, then that weapon's five tracks. */
enum class ESmithMenuPage : uint8
{
	Weapons,
	Tracks
};

/**
 * The blacksmith (spec C10): spend gold to upgrade the weapon in hand. Spawned at runtime beside the
 * market by UWrecktideRewardSubsystem::SpawnSmithStall exactly as the shop stall is, so nothing is
 * written into Lvl_Island.umap.
 *
 * Upgrades bind to the WEAPON row, not to the pirate: a better cutlass starts its tracks at zero,
 * which is what stops the market and the smithy cannibalising each other (research brief section 6).
 */
UCLASS()
class WRECKTIDE_API AWrecktideSmithStall : public AActor
{
	GENERATED_BODY()

public:
	AWrecktideSmithStall();

	/** BuyUpgrade is the SINGLE authoritative purchase path: the menu and the probe both come here. */
	bool BuyUpgrade(FName WeaponRow, FName TrackRow);

	/** OpenSmithMenu is the SINGLE authoritative entry path for an in-range player's modal menu. */
	void OpenSmithMenu(ACombatCharacter* Character);

	/** ToggleSmithMenu opens or closes this stall for its currently overlapped player. */
	void ToggleSmithMenu(ACombatCharacter* Character);

	/** MoveMenuSelection is the SINGLE authoritative path used by menu keys and probes. */
	void MoveMenuSelection(int32 Direction);

	/** ConfirmMenuSelection drives the selected row: open a weapon, buy a tier, or leave. */
	void ConfirmMenuSelection();

	/** Shows one weapon's five tracks. Used by the menu and directly by the probe. */
	bool OpenWeaponTracks(FName WeaponRow);

	/** Returns true when Character is the player currently inside this stall's trigger. */
	bool IsUserInRange(const ACombatCharacter* Character) const;

	/** Returns true when this stall's modal menu is currently open. */
	bool IsMenuOpen() const { return bMenuOpen; }

	/** Row count on the page being shown, for automation probes. */
	int32 GetRowCount() const { return MenuRows.Num(); }

	/** The selected row, or NAME_None for the Close / Back row. */
	FName GetSelectedRow() const { return MenuRows.IsValidIndex(SelectedIndex) ? MenuRows[SelectedIndex] : NAME_None; }

	/** The weapon whose tracks are showing, or NAME_None on the weapon list. */
	FName GetOpenWeaponRow() const { return OpenWeaponRow; }

	/** Which list is showing; probe evidence for the two-level menu. */
	ESmithMenuPage GetPage() const { return Page; }

	/** The lines the panel is showing right now (probe evidence). */
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

	/** F: leaves the smithy from either page. */
	UFUNCTION()
	void CloseSmithMenu();

	/** Escape: one page back, and out of the smithy from the weapon list. */
	UFUNCTION()
	void BackOrClose();

	UFUNCTION()
	void OnSmithUserDied(AActor* InstigatorActor);

	UFUNCTION()
	void OnSmithUserNotorietyLevelUp(int32 NewLevel);

	void CloseSmith();
	void SetSmithMenuOpen(bool bOpen, bool bShowRangeHint);
	void BindMenuInput();
	void SetSmithUser(ACombatCharacter* Character);
	void RefreshMenu();
	void PushMenuToHUD() const;
	void ShowToast(const FString& Message) const;

	/** The player state this stall is serving, or null when nobody is in range. */
	AWrecktidePlayerState* GetUserPlayerState() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> BenchMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> BarrelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> ToolMesh;

	/** The cold brazier that replaced the Niagara forge fire (C21, 2026-09-06): Jerold stands at the
	 *  bench now, so the smithy reads as a smithy from the man, not from a flame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> BrazierMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Trigger;

	TWeakObjectPtr<ACombatCharacter> SmithUser;

	/** Row per menu line: a weapon row, a track row, or NAME_None for Close / Back. */
	TArray<FName> MenuRows;
	TArray<FString> MenuLines;
	int32 SelectedIndex = 0;
	bool bMenuOpen = false;
	ESmithMenuPage Page = ESmithMenuPage::Weapons;
	FName OpenWeaponRow;
};
