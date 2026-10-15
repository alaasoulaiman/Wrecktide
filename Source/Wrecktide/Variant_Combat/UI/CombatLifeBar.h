// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CombatLifeBar.generated.h"

class UTextBlock;

/**
 *  A basic life bar user widget.
 */
UCLASS(abstract)
class UCombatLifeBar : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Applies an enemy display name and its colour band to the runtime nameplate text. */
	void SetEnemyName(const FText& DisplayName, int32 EnemyLevel, int32 NotorietyLevel);

	/** Brief volley tell beside the name; the authoritative name stays unchanged. */
	void SetRangedTell(bool bVisible);

	/** The single relative level colour rule shared by presentation and the contract probe. */
	static FLinearColor GetRelativeLevelNameColor(int32 EnemyLevel, int32 NotorietyLevel);

	/** Exact display name currently presented by this plate, used by the plate probe. */
	const FText& GetEnemyNameText() const { return PendingEnemyName; }

	/** Sets the life bar to the provided 0-1 percentage value*/
	UFUNCTION(BlueprintImplementableEvent, Category="Life Bar")
	void SetLifePercentage(float Percent);

	// Sets the life bar fill color
	UFUNCTION(BlueprintImplementableEvent, Category="Life Bar")
	void SetBarColor(FLinearColor Color);

protected:
	virtual void NativeConstruct() override;

private:
	void EnsureEnemyNameText();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EnemyNameText;

	FText PendingEnemyName;
	bool bRangedTellVisible = false;
	FLinearColor PendingEnemyNameColor = FLinearColor::White;
};
