// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Systems/SkillComponent.h"
#include "WrecktideShopStall.generated.h"

class ACombatCharacter;
class AWrecktidePlayerState;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

/** None lists all purchasable weapon classes. Independent of the persisted skill enum. */
UENUM(BlueprintType)
enum class EWrecktideStallClass : uint8
{
	None,
	Melee,
	Shooting,
	Voodoo
};

/**
 * Runtime-spawned weapon shop for the combat economy loop.
 */
UCLASS()
class WRECKTIDE_API AWrecktideShopStall : public AActor
{
	GENERATED_BODY()

public:
	/** Constructor creates the shop mesh and overlap trigger for the authoritative purchase path. */
	AWrecktideShopStall();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
	EWrecktideStallClass StallClass = EWrecktideStallClass::Melee;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
	FString PromptName = TEXT("market stall");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
	FString SignText;

	UPROPERTY(EditAnywhere, Category="Shop")
	FVector SignLocation = FVector(0.0f, -160.0f, 290.0f);

	UPROPERTY(EditAnywhere, Category="Shop")
	FRotator SignRotation = FRotator(0.0f, -90.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="Shop", meta=(ClampMin="1"))
	float SignLetterSize = 32.0f;

	bool AcceptsWeaponClass(EWrecktideSkill CombatClass) const;
	const TArray<FName>& GetOfferRows() const { return OfferRows; }

	/** Buy is the SINGLE authoritative path for shop purchases and weapon grants. */
	void Buy(int32 Index);

	/** OpenShopMenu is the SINGLE authoritative entry path for an in-range player's modal menu. */
	void OpenShopMenu(ACombatCharacter* Character);

	/** ToggleShopMenu opens or closes this stall for its currently overlapped player. */
	void ToggleShopMenu(ACombatCharacter* Character);

	/** MoveMenuSelection is the SINGLE authoritative path used by menu keys and probes. */
	void MoveMenuSelection(int32 Direction);

	/** ConfirmMenuSelection drives the selected row through the existing authoritative Buy path. */
	void ConfirmMenuSelection();

	/** Returns true when Character is the player currently inside this stall's trigger. */
	bool IsUserInRange(const ACombatCharacter* Character) const;

	/** Returns true when this stall's modal menu is currently open. */
	bool IsShopMenuOpen() const { return bShopMenuOpen; }

	/** Returns the current authoritative offer-row count for automation probes. */
	int32 GetOfferCount() const { return OfferRows.Num(); }

	/** Returns the selected offer row, or NAME_None when the menu has no valid selection. */
	FName GetSelectedOfferRow() const { return OfferRows.IsValidIndex(SelectedOfferIndex) ? OfferRows[SelectedOfferIndex] : NAME_None; }

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
	void CloseShopMenu();

	UFUNCTION()
	void OnShopUserDied(AActor* InstigatorActor);

	UFUNCTION()
	void OnShopUserNotorietyLevelUp(int32 NewLevel);

	void CloseShop();
	void SetShopMenuOpen(bool bOpen, bool bShowRangeHint);
	void BindMenuInput();
	void SetShopUser(ACombatCharacter* Character);
	void RefreshShopOffers();
	void ShowToast(const FString& Message) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> StallMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wrecktide", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category="Shop")
	TObjectPtr<class UTextRenderComponent> ShopSign;

	TWeakObjectPtr<ACombatCharacter> ShopUser;
	TArray<FName> OfferRows;
	TArray<FString> OfferLines;
	int32 SelectedOfferIndex = 0;
	bool bShopMenuOpen = false;
};
