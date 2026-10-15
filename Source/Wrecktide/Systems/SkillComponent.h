// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SkillComponent.generated.h"

UENUM(BlueprintType)
enum class EWrecktideSkill : uint8
{
	Shooting,
	Melee,
	Voodoo,
	Sailing,
	Gunnery,
	Plundering,
	Smuggling,
	Navigation
};

USTRUCT(BlueprintType)
struct FSkillState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Skills")
	int32 Level = 1;

	UPROPERTY(BlueprintReadWrite, Category="Skills")
	int64 XP = 0;
};

USTRUCT(BlueprintType)
struct FSkillRequirement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Skills")
	EWrecktideSkill Skill = EWrecktideSkill::Shooting;

	UPROPERTY(BlueprintReadWrite, Category="Skills")
	int32 MinLevel = 1;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSkillLevelUp, EWrecktideSkill, Skill, int32, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSkillXPChanged, EWrecktideSkill, Skill, int64, NewXP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotorietyLevelUp, int32, NewLevel);

UCLASS(ClassGroup=(Wrecktide), meta=(BlueprintSpawnableComponent))
class WRECKTIDE_API USkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkillComponent();

	/** SINGLE authoritative path for XP changes. */
	UFUNCTION(BlueprintCallable, Category="Skills")
	void AwardXP(EWrecktideSkill Skill, int64 Amount, FName Context);

	UFUNCTION(BlueprintPure, Category="Skills")
	int64 GetXPToNext(int32 Level) const;

	UFUNCTION(BlueprintPure, Category="Skills")
	float GetPowerMultiplier(EWrecktideSkill Skill) const;

	UFUNCTION(BlueprintPure, Category="Skills")
	bool MeetsRequirement(const FSkillRequirement& Req) const;

	UFUNCTION(BlueprintPure, Category="Skills")
	bool MeetsAllRequirements(const TArray<FSkillRequirement>& Reqs) const;

	UFUNCTION(BlueprintPure, Category="Skills")
	int32 GetSkillLevel(EWrecktideSkill Skill) const;

	UFUNCTION(BlueprintPure, Category="Skills")
	int64 GetSkillXP(EWrecktideSkill Skill) const;

	UFUNCTION(BlueprintPure, Category="Notoriety")
	int64 GetNotorietyXP() const { return NotorietyXP; }

	/** One player level derived from cumulative notoriety XP, capped at 75. */
	UFUNCTION(BlueprintPure, Category="Notoriety")
	int32 GetNotorietyLevel() const;

	/** Curve helpers used by UI, shops, save migration and deterministic probes. */
	UFUNCTION(BlueprintPure, Category="Notoriety")
	int32 GetNotorietyLevelForXP(int64 XP) const;

	UFUNCTION(BlueprintPure, Category="Notoriety")
	int64 GetNotorietyXPForLevel(int32 Level) const;

	/** Derives old-save notoriety from every skill's cumulative lifetime XP. */
	static int64 DeriveNotorietyXPFromSkills(const TMap<EWrecktideSkill, FSkillState>& InSkills);

	/** Legacy API name retained for callers; now returns the single player notoriety level. */
	UFUNCTION(BlueprintPure, Category="Skills")
	int32 GetTotalLevel() const;

	UFUNCTION(BlueprintPure, Category="Skills")
	bool HasCombatClassAt(int32 MinLevel) const;

	/** Save-restore only: overwrites skill and notoriety state without level-up broadcasts. */
	void RestoreFromSave(const TMap<EWrecktideSkill, FSkillState>& InSkills, int64 InNotorietyXP);

	UPROPERTY(BlueprintAssignable, Category="Skills")
	FOnSkillLevelUp OnSkillLevelUp;

	/** OnSkillXPChanged is the SINGLE authoritative notification path for XP changes. */
	UPROPERTY(BlueprintAssignable, Category="Skills")
	FOnSkillXPChanged OnSkillXPChanged;

	UPROPERTY(BlueprintAssignable, Category="Notoriety")
	FOnNotorietyLevelUp OnNotorietyLevelUp;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Skills", meta=(AllowPrivateAccess="true"))
	TMap<EWrecktideSkill, FSkillState> Skills;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Notoriety", meta=(AllowPrivateAccess="true"))
	int64 NotorietyXP = 0;

	/** Cumulative XP threshold = NotorietyCurveScale * Level ^ NotorietyCurvePower. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Notoriety", meta=(AllowPrivateAccess="true", ClampMin="1.0"))
	double NotorietyCurveScale = 30.0;

	/** Curve exponent; 2.2 makes early levels quick and the climb to 75 progressively longer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Notoriety", meta=(AllowPrivateAccess="true", ClampMin="1.0"))
	double NotorietyCurvePower = 2.2;
};
