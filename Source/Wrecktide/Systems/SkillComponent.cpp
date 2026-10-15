// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/SkillComponent.h"

#include "GameFramework/Actor.h"
#include "Math/UnrealMathUtility.h"

#include <cmath>
#include <limits>

namespace
{
constexpr int32 MinSkillLevel = 1;
constexpr int32 MaxSkillLevel = 999;
constexpr int32 PrestigeStartLevel = 50;
constexpr int32 MaxNotorietyLevel = 75;
constexpr double BaseXP = 100.0;
constexpr double GrowthRate = 1.10;
constexpr double PowerGrowthRate = 1.06;
constexpr double PrestigePowerBonusPerLevel = 0.01;

constexpr EWrecktideSkill AllSkills[] =
{
	EWrecktideSkill::Shooting,
	EWrecktideSkill::Melee,
	EWrecktideSkill::Voodoo,
	EWrecktideSkill::Sailing,
	EWrecktideSkill::Gunnery,
	EWrecktideSkill::Plundering,
	EWrecktideSkill::Smuggling,
	EWrecktideSkill::Navigation
};

constexpr EWrecktideSkill CombatSkills[] =
{
	EWrecktideSkill::Shooting,
	EWrecktideSkill::Melee,
	EWrecktideSkill::Voodoo
};

bool IsTrackedSkill(EWrecktideSkill Skill)
{
	for (const EWrecktideSkill TrackedSkill : AllSkills)
	{
		if (TrackedSkill == Skill)
		{
			return true;
		}
	}

	return false;
}

FSkillState ClampSkillState(const FSkillState& State)
{
	FSkillState ClampedState;
	ClampedState.Level = FMath::Clamp(State.Level, MinSkillLevel, MaxSkillLevel);
	ClampedState.XP = FMath::Max<int64>(State.XP, 0);

	return ClampedState;
}

int64 SkillLifetimeXP(const FSkillState& State)
{
	const FSkillState Clamped = ClampSkillState(State);
	int64 Total = Clamped.XP;
	for (int32 Level = MinSkillLevel; Level < Clamped.Level; ++Level)
	{
		const int32 CurveLevel = Level >= PrestigeStartLevel ? PrestigeStartLevel : Level;
		const double Cost = BaseXP * FMath::Pow(GrowthRate, static_cast<double>(CurveLevel - 1));
		const int64 Step = static_cast<int64>(std::floor(Cost));
		Total = Total > std::numeric_limits<int64>::max() - Step
			? std::numeric_limits<int64>::max() : Total + Step;
	}
	return Total;
}
}

USkillComponent::USkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	for (const EWrecktideSkill Skill : AllSkills)
	{
		Skills.Add(Skill, FSkillState());
	}
}

void USkillComponent::AwardXP(EWrecktideSkill Skill, int64 Amount, FName Context)
{
	static_cast<void>(Context);

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0 || !IsTrackedSkill(Skill))
	{
		return;
	}

	FSkillState& State = Skills.FindOrAdd(Skill);
	State = ClampSkillState(State);
	const int32 OldNotorietyLevel = GetNotorietyLevel();
	NotorietyXP = FMath::Max<int64>(NotorietyXP, 0);
	const int64 NotorietyHeadroom = std::numeric_limits<int64>::max() - NotorietyXP;
	NotorietyXP += FMath::Min(Amount, NotorietyHeadroom);

	if (State.Level >= MaxSkillLevel)
	{
		State.XP = 0;
	}
	else
	{
		const int64 XPHeadroom = std::numeric_limits<int64>::max() - State.XP;
		State.XP += FMath::Min(Amount, XPHeadroom);

		while (State.Level < MaxSkillLevel)
		{
			const int64 XPToNext = GetXPToNext(State.Level);
			if (State.XP < XPToNext)
			{
				break;
			}

			State.XP -= XPToNext;
			++State.Level;
			OnSkillLevelUp.Broadcast(Skill, State.Level);
		}
	}

	if (State.Level >= MaxSkillLevel)
	{
		State.XP = 0;
	}

	OnSkillXPChanged.Broadcast(Skill, State.XP);

	const int32 NewNotorietyLevel = GetNotorietyLevel();
	for (int32 Level = OldNotorietyLevel + 1; Level <= NewNotorietyLevel; ++Level)
	{
		OnNotorietyLevelUp.Broadcast(Level);
	}
}

int64 USkillComponent::GetXPToNext(int32 Level) const
{
	const int32 ClampedLevel = FMath::Max(Level, MinSkillLevel);
	const int32 CurveLevel = ClampedLevel >= PrestigeStartLevel ? PrestigeStartLevel : ClampedLevel;
	const double Cost = BaseXP * FMath::Pow(GrowthRate, static_cast<double>(CurveLevel - 1));
	return static_cast<int64>(std::floor(Cost));
}

float USkillComponent::GetPowerMultiplier(EWrecktideSkill Skill) const
{
	const int32 Level = GetSkillLevel(Skill);
	const double PowerMultiplier =
		FMath::Pow(PowerGrowthRate, static_cast<double>(FMath::Min(Level, PrestigeStartLevel) - 1))
		* (1.0 + PrestigePowerBonusPerLevel * FMath::Max(0, Level - PrestigeStartLevel));

	return static_cast<float>(PowerMultiplier);
}

bool USkillComponent::MeetsRequirement(const FSkillRequirement& Req) const
{
	return GetSkillLevel(Req.Skill) >= Req.MinLevel;
}

bool USkillComponent::MeetsAllRequirements(const TArray<FSkillRequirement>& Reqs) const
{
	for (const FSkillRequirement& Req : Reqs)
	{
		if (!MeetsRequirement(Req))
		{
			return false;
		}
	}

	return true;
}

int32 USkillComponent::GetSkillLevel(EWrecktideSkill Skill) const
{
	if (!IsTrackedSkill(Skill))
	{
		return MinSkillLevel;
	}

	const FSkillState* State = Skills.Find(Skill);
	return State ? FMath::Clamp(State->Level, MinSkillLevel, MaxSkillLevel) : MinSkillLevel;
}

int64 USkillComponent::GetSkillXP(EWrecktideSkill Skill) const
{
	if (!IsTrackedSkill(Skill))
	{
		return 0;
	}

	const FSkillState* State = Skills.Find(Skill);
	return State ? FMath::Max<int64>(State->XP, 0) : 0;
}

int32 USkillComponent::GetNotorietyLevel() const
{
	return GetNotorietyLevelForXP(NotorietyXP);
}

int32 USkillComponent::GetNotorietyLevelForXP(int64 XP) const
{
	const int64 ClampedXP = FMath::Max<int64>(XP, 0);
	int32 Level = 1;
	for (int32 Candidate = 2; Candidate <= MaxNotorietyLevel; ++Candidate)
	{
		if (ClampedXP < GetNotorietyXPForLevel(Candidate))
		{
			break;
		}
		Level = Candidate;
	}
	return Level;
}

int64 USkillComponent::GetNotorietyXPForLevel(int32 Level) const
{
	const int32 ClampedLevel = FMath::Clamp(Level, 1, MaxNotorietyLevel);
	if (ClampedLevel <= 1)
	{
		return 0;
	}
	return static_cast<int64>(std::llround(
		NotorietyCurveScale * FMath::Pow(static_cast<double>(ClampedLevel), NotorietyCurvePower)));
}

int64 USkillComponent::DeriveNotorietyXPFromSkills(const TMap<EWrecktideSkill, FSkillState>& InSkills)
{
	int64 Total = 0;
	for (const EWrecktideSkill Skill : AllSkills)
	{
		const FSkillState* State = InSkills.Find(Skill);
		const int64 SkillTotal = State ? SkillLifetimeXP(*State) : 0;
		Total = Total > std::numeric_limits<int64>::max() - SkillTotal
			? std::numeric_limits<int64>::max() : Total + SkillTotal;
	}
	return Total;
}

int32 USkillComponent::GetTotalLevel() const
{
	return GetNotorietyLevel();
}

bool USkillComponent::HasCombatClassAt(int32 MinLevel) const
{
	for (const EWrecktideSkill Skill : CombatSkills)
	{
		if (GetSkillLevel(Skill) >= MinLevel)
		{
			return true;
		}
	}

	return false;
}

void USkillComponent::RestoreFromSave(const TMap<EWrecktideSkill, FSkillState>& InSkills, int64 InNotorietyXP)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	Skills.Reset();

	for (const EWrecktideSkill Skill : AllSkills)
	{
		const FSkillState* SavedState = InSkills.Find(Skill);
		Skills.Add(Skill, SavedState ? ClampSkillState(*SavedState) : FSkillState());
	}

	NotorietyXP = FMath::Max<int64>(InNotorietyXP, 0);
}
