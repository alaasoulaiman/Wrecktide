// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StatsComponent.generated.h"

class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, OldHealth, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOutOfHealth, AActor*, InstigatorActor);

UCLASS(ClassGroup=(Wrecktide), meta=(BlueprintSpawnableComponent))
class WRECKTIDE_API UStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStatsComponent();

	/** Spawn-time only by convention. Authority sets max health, fills health, and re-arms death notification. */
	UFUNCTION(BlueprintCallable, Category="Stats")
	void InitStats(float InMaxHealth);

	/** ApplyDamage/Heal are the SINGLE authoritative path for health changes. */
	UFUNCTION(BlueprintCallable, Category="Stats")
	float ApplyDamage(float RawDamage, AActor* InstigatorActor, bool bPvP);

	/** ApplyDamage/Heal are the SINGLE authoritative path for health changes. */
	UFUNCTION(BlueprintCallable, Category="Stats")
	bool Heal(float Amount);

	UFUNCTION(BlueprintPure, Category="Stats")
	bool IsAlive() const;

	/** GetHealth is the SINGLE authoritative read path for current health. */
	UFUNCTION(BlueprintPure, Category="Stats")
	float GetHealth() const;

	/** GetMaxHealth is the SINGLE authoritative read path for max health. */
	UFUNCTION(BlueprintPure, Category="Stats")
	float GetMaxHealth() const;

	/** Save-restore only: overwrites stats without health-change or out-of-health broadcasts. */
	void RestoreFromSave(float InMaxHealth, float InHealth);

	UPROPERTY(BlueprintAssignable, Category="Stats")
	FOnHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category="Stats")
	FOnOutOfHealth OnOutOfHealth;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="1.0", AllowPrivateAccess="true"))
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Stats", meta=(ClampMin="0.0", AllowPrivateAccess="true"))
	float Health = 100.0f;

	bool bStatsInitialized = false;

	bool bOutOfHealthBroadcasted = false;
};
