// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/StatsComponent.h"

#include "GameFramework/Actor.h"
#include "Math/UnrealMathUtility.h"

UStatsComponent::UStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStatsComponent::BeginPlay()
{
	Super::BeginPlay();

	MaxHealth = FMath::Max(MaxHealth, 1.0f);

	if (!bStatsInitialized)
	{
		Health = MaxHealth;
	}
	else
	{
		Health = FMath::Clamp(Health, 0.0f, MaxHealth);
	}

	bOutOfHealthBroadcasted = Health <= 0.0f;
}

void UStatsComponent::InitStats(float InMaxHealth)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	MaxHealth = FMath::Max(InMaxHealth, 1.0f);
	Health = MaxHealth;
	bStatsInitialized = true;
	bOutOfHealthBroadcasted = false;
}

float UStatsComponent::ApplyDamage(float RawDamage, AActor* InstigatorActor, bool bPvP)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || RawDamage <= 0.0f || !FMath::IsFinite(RawDamage))
	{
		return 0.0f;
	}

	if (Health <= 0.0f)
	{
		return 0.0f;
	}

	const float OldHealth = Health;
	const float DamageCap = MaxHealth * (bPvP ? 0.35f : 0.40f);
	const float ClampedDamage = FMath::Min(RawDamage, DamageCap);
	const float AppliedDamage = FMath::Min(ClampedDamage, Health);

	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	Health = FMath::Clamp(Health - AppliedDamage, 0.0f, MaxHealth);
	OnHealthChanged.Broadcast(OldHealth, Health);

	if (Health <= 0.0f && !bOutOfHealthBroadcasted)
	{
		bOutOfHealthBroadcasted = true;
		OnOutOfHealth.Broadcast(InstigatorActor);
	}

	return AppliedDamage;
}

bool UStatsComponent::Heal(float Amount)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0.0f || !FMath::IsFinite(Amount))
	{
		return false;
	}

	const float OldHealth = Health;
	Health = FMath::Clamp(Health + Amount, 0.0f, MaxHealth);

	if (Health == OldHealth)
	{
		return false;
	}

	OnHealthChanged.Broadcast(OldHealth, Health);

	if (Health > 0.0f)
	{
		bOutOfHealthBroadcasted = false;
	}

	return true;
}

bool UStatsComponent::IsAlive() const
{
	return Health > 0.0f;
}

float UStatsComponent::GetHealth() const
{
	return Health;
}

float UStatsComponent::GetMaxHealth() const
{
	return MaxHealth;
}

void UStatsComponent::RestoreFromSave(float InMaxHealth, float InHealth)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	MaxHealth = FMath::Max(InMaxHealth, 1.0f);
	Health = FMath::Clamp(InHealth, 0.0f, MaxHealth);
	bStatsInitialized = true;
	bOutOfHealthBroadcasted = Health <= 0.0f;
}
