// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WrecktideMissionGiver.generated.h"

class ACombatCharacter;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USphereComponent;
class UStaticMeshComponent;

/** What the selected giver menu line does when confirmed. Parallel to the drawn lines. */
enum class EMissionGiverAction : uint8
{
	/** Plain text (an intro, an outro, the finale line). Confirming it says the progress again. */
	Info,
	Accept,
	Complete,

	/** Hand over the gold a Pay mission asks for (C23.2). */
	Pay,

	/** Jerold only: step across to the smithy's upgrade menu (C21). */
	Smithy,
	Close
};

/**
 * Which Synty skin an NPC wears. Every mesh is found once in the constructor (ConstructorHelpers
 * only works there), so ConfigureGiver just picks one out of the array (C21, 2026-09-06).
 */
enum class EWrecktideGiverSkin : uint8
{
	FirstMate,
	Seaman,
	Wench,
	Governor,
	/** Gallows Reach (C34, 2026-09-07): Ada Kell still wears the redcoat she turned her back on. */
	Sergeant,
	Count
};

/**
 * The camp's quest givers (2026-09-04, made multi-NPC by C21/C22 on 2026-09-06): the NPCs who hand
 * out the story chains and pay them out.
 *
 * Runtime-spawned beside the bounty board by UWrecktideRewardSubsystem, and deliberately built as
 * the THIRD copy of the same interaction the player already knows: same F key, same 300uu trigger,
 * same HUD line list, same Up/Down/Enter keys. Every mission decision it makes goes through
 * WrecktideMissionLogic; this actor only draws lines and forwards confirmations.
 *
 * Animation (2026-09-05): a hidden UEFN mannequin driver loops the stand idle and the Synty first
 * mate skin retargets from it through ABP_GenericRetarget, the same hookup the enemies use. The
 * first capture of him in the bind pose read as broken, so scenery with a menu was not enough.
 */
class UAnimInstance;
class UAnimSequence;

UCLASS()
class WRECKTIDE_API AWrecktideMissionGiver : public AActor
{
	GENERATED_BODY()

public:
	/** Constructor creates the NPC mesh and the overlap trigger for the authoritative mission path. */
	AWrecktideMissionGiver();

	/** ConfigureGiver is the SINGLE authoritative way to say WHICH npc this actor is. Call it on a
	 *  deferred spawn, before FinishSpawning, so the skin is in place before BeginPlay retargets it.
	 *  An actor that is never configured stays Quartermaster Bell, the 2026-09-04 behaviour. */
	void ConfigureGiver(FName InGiverId, const FString& InPlateName, const FString& InPromptName, EWrecktideGiverSkin Skin, bool bInOpensSmithy);

	/** The Missions.csv GiverId this NPC answers for. */
	FName GetGiverId() const { return GiverId; }

	/** Nameplate text drawn over his head by the HUD, e.g. "Jerold the Blacksmith". */
	const FString& GetPlateName() const { return PlateName; }

	/** Same person in a sentence, e.g. "Jerold the blacksmith". */
	const FString& GetPromptName() const { return PromptName; }

	/** True for Jerold: F on him opens the smithy menu as well as his mission menu. */
	bool OpensSmithy() const { return bOpensSmithy; }

	/** Where the HUD draws his plate and his quest glyph: the top of his head, in world space. */
	FVector GetPlateWorldLocation() const;

	/** "!" when he has work to hand out, "?" when a job of his is waiting to be turned in, empty
	 *  otherwise. The HUD plate and the map marker both read THIS, so they cannot disagree. */
	FString GetQuestSignal() const;

	/** OpenGiverMenu is the SINGLE authoritative entry path for an in-range player's mission menu. */
	void OpenGiverMenu(ACombatCharacter* Character);

	/** ToggleGiverMenu opens or closes this giver for its currently overlapped player. */
	void ToggleGiverMenu(ACombatCharacter* Character);

	/** CloseGiverMenu hides the menu and hands the keys back. Public so probes can drive it. */
	UFUNCTION()
	void CloseGiverMenu();

	/** MoveMenuSelection is the SINGLE authoritative path used by menu keys and probes. */
	void MoveMenuSelection(int32 Direction);

	/** ConfirmMenuSelection drives the selected line through Accept, Complete or Close. */
	UFUNCTION()
	void ConfirmMenuSelection();

	/** SelectActionLine points the selection at the first line with that action; false when absent. */
	bool SelectActionLine(EMissionGiverAction Action);

	/** Returns true when Character is the player currently inside this giver's trigger. */
	bool IsUserInRange(const ACombatCharacter* Character) const;

	/** Returns true when this giver's modal menu is currently open. */
	bool IsMenuOpen() const { return bMenuOpen; }

	/** Drawn line count (mission lines plus the trailing Close line) for probes. */
	int32 GetMenuLineCount() const { return MenuLines.Num(); }

	/** The drawn text of one menu line, empty past the end. Read path for probes: the brush-off
	 *  assertion has to compare the WORDS the player sees, not just the line count (C30). */
	FString GetMenuLine(int32 Index) const { return MenuLines.IsValidIndex(Index) ? MenuLines[Index] : FString(); }

	/** The Missions.csv row the selected line acts on. Read path for probes. */
	FName GetSelectedRow() const { return MenuRows.IsValidIndex(SelectedIndex) ? MenuRows[SelectedIndex] : NAME_None; }

	/** The action on the selected line, Close on an empty menu. Read path for probes. */
	EMissionGiverAction GetSelectedAction() const { return MenuActions.IsValidIndex(SelectedIndex) ? MenuActions[SelectedIndex] : EMissionGiverAction::Close; }

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
	void OnGiverUserDied(AActor* InstigatorActor);

	/** Pay a Pay mission's price out of the player's purse, then count it. */
	void PayForMission(FName Row);

	/** Jerold's [SMITHY] line: close this menu and open the nearest smithy stall for the same user. */
	void OpenSmithyFromHere();

	void CloseGiver();
	void SetGiverMenuOpen(bool bOpen, bool bShowRangeHint);
	void BindMenuInput();
	void SetGiverUser(ACombatCharacter* Character);
	void RefreshGiverMenu();
	void PushMenuToHUD() const;
	void ShowToast(const FString& Message) const;

	/** True when a shop stall or the bounty board has a menu up; all three share the HUD list. */
	bool IsOtherMenuOpen() const;

	/** Which NPC this is. Bell by default so an unconfigured spawn is exactly the shipped
	 *  quartermaster; C22's Missions.csv GiverId column matches against this. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	FName GiverId = FName(TEXT("Bell"));

	/** Nameplate wording. Ours, never a film or MMO name. */
	FString PlateName = TEXT("Quartermaster Bell");

	/** The same NPC named inside a sentence, for the F prompt and the toasts. */
	FString PromptName = TEXT("the quartermaster");

	/** Jerold stands at the smithy, so his F also opens the stall's upgrade menu (C21). */
	bool bOpensSmithy = false;

	/** Every skin the pack gives these NPCs, found once in the constructor and indexed by
	 *  EWrecktideGiverSkin. A null entry degrades to the mannequin exactly like a missing pack. */
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMesh>> GiverSkins;

	/** Hidden UEFN mannequin that plays the idle; the skin below retargets from it every frame (the
	 *  enemies' and the player's own pattern, 2026-09-05). Root component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> DriverMesh;

	/** The visible Synty skin, attached to the driver. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> GiverMesh;

	/** ABP_GenericRetarget, the skin's anim class. */
	UPROPERTY(VisibleAnywhere, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TSubclassOf<UAnimInstance> RetargetAnimClass;

	/** RTG_UEFN_to_UE4_Mannequin, pushed into the ABP's IKRetargeter variable (reflection, like the enemies). */
	UPROPERTY(VisibleAnywhere, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UObject> Retargeter;

	void ApplySkinRetarget();
	bool PushRetargeter();

	UFUNCTION()
	void OnSkinAnimInitialized();

	/** The mannequin's stand idle loop, played on the driver. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimSequence> IdleAnimation;

	/** Shown ONLY when the Synty character pack is missing, so a content gap costs looks, never the
	 *  mission chain. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> FallbackMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Trigger;

	TWeakObjectPtr<ACombatCharacter> GiverUser;

	/** Parallel to MenuLines. */
	TArray<EMissionGiverAction> MenuActions;
	TArray<FString> MenuLines;

	/** The Missions.csv row a line acts on, NAME_None for Info, Smithy and Close. Parallel too:
	 *  with several missions in play a line must never mean "whatever is current right now". */
	TArray<FName> MenuRows;

	/** The outro of the mission just completed, drawn until the menu closes. */
	FString PendingOutro;

	int32 SelectedIndex = 0;
	bool bMenuOpen = false;
};
