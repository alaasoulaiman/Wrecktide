// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktidePlayerState.h"

#include "Containers/Ticker.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/SkillComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktideWardrobeSubsystem.h"
#include "TimerManager.h"

namespace
{
const TCHAR* DefaultSaveSlotName = TEXT("Wrecktide_Slot0");
constexpr float AutosaveDelaySeconds = 1.0f;

/** Manual Save Voyage slots. Three is what the menu shows and what the beta testers were promised. */
constexpr int32 ManualSlotCount = 3;
/** Prefix for a manual slot OUTSIDE a probe run; inside one the probe's own slot name is used. */
const TCHAR* ManualSlotPrefix = TEXT("Wrecktide");
/** How long PlacePawnFromSave waits for a pawn to exist, in core-ticker steps of 0.25 s. */
constexpr int32 PlacePawnMaxAttempts = 24;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
/**
 * Every probe switch UWrecktideRewardSubsystem::OnWorldBeginPlay registers, plus the photo mode.
 * Matched as exact tokens (-Name) or value switches (-Name=...), never as substrings (review
 * 2026-09-02: a substring match let an unrelated -SaveProbeOutput=... suppress saving). A probe
 * missing from this list would load, and then overwrite, the player's real slot.
 */
const TCHAR* ProbeSwitchNames[] =
{
	TEXT("FungateProbe"),
	TEXT("PlateProbe"),
	TEXT("PlateForCapture"),
	TEXT("ShopProbe"),
	TEXT("ShootProbe"),
	TEXT("MusketProbe"),
	TEXT("MusketForCapture"),
	TEXT("GunsmithProbe"),
	TEXT("PistolForCapture"),
	TEXT("GunsmithForCapture"),
	TEXT("QuestProbe"),
	TEXT("MissionProbe"),
	TEXT("QuestFlowProbe"),
	TEXT("AttackProbe"),
	TEXT("LocoProbe"),
	TEXT("InputProbe"),
	TEXT("EnemySkinProbe"),
	TEXT("HitProbe"),
	TEXT("ComboProbe"),
	TEXT("ComboForCapture"),
	TEXT("DamageNumbersProbe"),
	TEXT("DamageNumbersForCapture"),
	TEXT("DebugPlates"),
	TEXT("PoseProbe"),
	TEXT("BoatProbe"),
	TEXT("BoatWalkProbe"),
	TEXT("ChestProbe"),
	TEXT("NoEnemies"),
	TEXT("MenuProbe"),
	TEXT("SfxProbe"),
	TEXT("NavProbe"),
	TEXT("Island2Probe"),
	TEXT("Island3TerrainProbe"),
	TEXT("Island3PopulationProbe"),
	TEXT("Island3StoryProbe"),
	TEXT("Island3ChartProbe"),
	TEXT("Island3Probe"),
	TEXT("SwimProbe"),
	TEXT("ChaseProbe"),
	TEXT("LeashProbe"),
	TEXT("FeelProbe"),
	TEXT("OwnershipProbe"),
	TEXT("SaveProbe"),
	TEXT("WardrobeProbe"),
	TEXT("XpSkillProbe"),
	TEXT("EconomyProbe"),
	TEXT("NotorietyProbe"),
	TEXT("Outfit"),
	TEXT("MapProbe"),
	TEXT("BeaconProbe"),
	TEXT("BeaconPostCapture"),
	TEXT("OpenMapForCapture"),
	TEXT("BeaconCapture"),
	TEXT("SmithProbe"),
	TEXT("OpenSmithForCapture"),
	TEXT("HotbarProbe"),
	TEXT("ChestButtonProbe"),
	TEXT("CursorProbe"),
	TEXT("InventoryProbe"),
	TEXT("OpenInventoryForCapture"),
	TEXT("OpenShopForCapture"),
	TEXT("SailFeelProbe"),
	TEXT("SailForCapture"),
	TEXT("HoldProbe"),
	TEXT("OpenBarrelForCapture"),
	TEXT("ShipwrightProbe"),
	TEXT("OpenShipwrightForCapture"),
	TEXT("PatrolProbe"),
	TEXT("PatrolForCapture"),
	TEXT("HelmProbe"),
	TEXT("HelmForCapture"),
	TEXT("OpenGiverForCapture"),
	TEXT("PhotoAt")
};

bool HasSwitch(const TCHAR* CommandLine, const TCHAR* Name)
{
	if (FParse::Param(CommandLine, Name))
	{
		return true;
	}
	FString Value;
	return FParse::Value(CommandLine, *(FString(Name) + TEXT("=")), Value, false);
}
#endif
}

AWrecktidePlayerState::AWrecktidePlayerState()
{
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));
	Skill = CreateDefaultSubobject<USkillComponent>(TEXT("Skill"));
	Progress = CreateDefaultSubobject<UProgressComponent>(TEXT("Progress"));

	// Sized once, here, and never resized: every slot read elsewhere is then a plain index and can
	// never fall off the end of a half-built array.
	AssignedSlots.Init(NAME_None, EquipSlotCount);
}

UEconomyComponent* AWrecktidePlayerState::GetEconomy() const
{
	return Economy;
}

USkillComponent* AWrecktidePlayerState::GetSkill() const
{
	return Skill;
}

UProgressComponent* AWrecktidePlayerState::GetProgress() const
{
	return Progress;
}

int32 AWrecktidePlayerState::GetGatingLevel() const
{
	// Notoriety is the single player level (SPEC C4, merged 2026-09-06): every wardrobe, shop and smithy
	// gate reads this one function so the gates can never disagree about what "level" means.
	return Skill ? FMath::Max(1, Skill->GetNotorietyLevel()) : 1;
}

bool AWrecktidePlayerState::CanEquipWeapon(FName Row) const
{
	if (Row == NAME_None)
	{
		return true;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FWeaponTableRow* WeaponRow = DataSubsystem ? DataSubsystem->GetWeaponRow(Row) : nullptr;
	if (!WeaponRow || !Skill)
	{
		return false;
	}

	return Skill->GetNotorietyLevel() >= WeaponRow->RequiredLevel;
}

bool AWrecktidePlayerState::EquipWeapon(FName Row)
{
	if (!HasAuthority() || !CanEquipWeapon(Row))
	{
		return false;
	}

	EquippedWeaponRow = Row;

	// Purchases, saves and the belt share this path. Emptying the hand keeps both assignments.
	if (Row != NAME_None && AssignedSlots.Num() == EquipSlotCount)
	{
		const EWrecktideSkill CombatClass = GetEquippedCombatClass();
		if (CombatClass == EWrecktideSkill::Melee) { AssignedSlots[0] = Row; }
		else if (CombatClass == EWrecktideSkill::Shooting) { AssignedSlots[1] = Row; }
	}

	OnWeaponEquipped.Broadcast(Row);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-EQUIP %s"), Row == NAME_None ? TEXT("Unarmed") : *Row.ToString());

	return true;
}

FName AWrecktidePlayerState::GetAssignedSlot(int32 Slot) const
{
	return AssignedSlots.IsValidIndex(Slot - 1) ? AssignedSlots[Slot - 1] : NAME_None;
}

FString AWrecktidePlayerState::GetSlotClassName(int32 Slot)
{
	switch (Slot)
	{
	case 1: return TEXT("Blade");
	case 2: return TEXT("Gun");
	case 3: return TEXT("Hex");
	default: return TEXT("Tonic");
	}
}

FString AWrecktidePlayerState::GetSlotHint(int32 Slot)
{
	// One line each, and the only place the bar's "not yet" wording lives.
	switch (Slot)
	{
	case 1: return TEXT("Blade: buy one at the market stall");
	case 2: return TEXT("Gun: Ada's powder magazine on Gallows Reach");
	case 3: return TEXT("Hex doll: the voodoo hut");
	default: return TEXT("Tonic: coming with the apothecary");
	}
}

bool AWrecktidePlayerState::AssignSlot(int32 Slot, FName Row)
{
	if (!HasAuthority() || !AssignedSlots.IsValidIndex(Slot - 1))
	{
		return false;
	}

	if (Slot != 1 && Slot != 2)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR assign refused, slot %d is not built yet"), Slot);
		return false;
	}

	if (Row == NAME_None)
	{
		const bool bUnequipped = EquippedWeaponRow != AssignedSlots[Slot - 1] || EquipWeapon(NAME_None);
		AssignedSlots[Slot - 1] = NAME_None;
		RequestSave();
		return bUnequipped;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FWeaponTableRow* WeaponRow = DataSubsystem ? DataSubsystem->GetWeaponRow(Row) : nullptr;
	const bool bOwned = Progress && Progress->IsItemUnlocked(Row);
	const EWrecktideSkill SlotClass = Slot == 1 ? EWrecktideSkill::Melee : EWrecktideSkill::Shooting;
	if (!WeaponRow || WeaponRow->CombatClass != SlotClass || !bOwned || !CanEquipWeapon(Row))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR assign refused row=%s known=%d class=%d owned=%d gate=%d"),
			*Row.ToString(), WeaponRow != nullptr,
			WeaponRow != nullptr && WeaponRow->CombatClass == SlotClass, bOwned ? 1 : 0,
			CanEquipWeapon(Row) ? 1 : 0);
		return false;
	}

	if (!EquipWeapon(Row))
	{
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR slot %d = %s"), Slot, *Row.ToString());
	RequestSave();
	return true;
}

bool AWrecktidePlayerState::WieldSlot(int32 Slot)
{
	if (!HasAuthority() || !AssignedSlots.IsValidIndex(Slot - 1) || (Slot != 1 && Slot != 2))
	{
		return false;
	}

	const FName Assigned = AssignedSlots[Slot - 1];
	if (Assigned != NAME_None && CanEquipWeapon(Assigned)
		&& (!Progress || Progress->IsItemUnlocked(Assigned)))
	{
		// Already in hand is still a success: pressing 1 twice must not report the slot empty.
		return EquippedWeaponRow == Assigned ? true : EquipWeapon(Assigned);
	}
	if (Slot == 2) { return false; }

	// Nothing assigned (or the assignment went stale): fall back to the best melee he owns, which
	// is what a player pressing 1 with a full sea chest expects.
	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!DataSubsystem || !Progress)
	{
		return false;
	}

	FName Best = NAME_None;
	float BestDamage = -1.0f;
	for (const TPair<FName, const FWeaponTableRow*>& Entry : DataSubsystem->GetWeaponRows())
	{
		if (!Entry.Value || Entry.Value->CombatClass != EWrecktideSkill::Melee
			|| !Progress->IsItemUnlocked(Entry.Key) || !CanEquipWeapon(Entry.Key))
		{
			continue;
		}
		if (Entry.Value->BaseDamage > BestDamage)
		{
			BestDamage = Entry.Value->BaseDamage;
			Best = Entry.Key;
		}
	}

	if (Best == NAME_None)
	{
		return false;
	}
	return EquipWeapon(Best);
}

void AWrecktidePlayerState::RestoreAssignedSlots(const TArray<FName>& InSlots)
{
	// Validate both classes without wielding either: the hand was restored before the belt.
	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	for (int32 Index = 0; Index < AssignedSlots.Num() && Index < InSlots.Num(); ++Index)
	{
		const FName Row = InSlots[Index];
		const FWeaponTableRow* Weapon = Data ? Data->GetWeaponRow(Row) : nullptr;
		const EWrecktideSkill Expected = Index == 0 ? EWrecktideSkill::Melee : EWrecktideSkill::Shooting;
		const bool bValid = Index < 2 && Weapon && Weapon->CombatClass == Expected
			&& Progress && Progress->IsItemUnlocked(Row) && CanEquipWeapon(Row);
		AssignedSlots[Index] = bValid ? Row : NAME_None;
		if (!Row.IsNone() && !bValid)
		{
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-HOTBAR saved slot %d %s refused"), Index + 1, *Row.ToString());
		}
	}
	// Keep a validated equipped row visible on the bar even if an older assignment is absent.
	if (!EquippedWeaponRow.IsNone())
	{
		const EWrecktideSkill CombatClass = GetEquippedCombatClass();
		if (CombatClass == EWrecktideSkill::Melee) { AssignedSlots[0] = EquippedWeaponRow; }
		else if (CombatClass == EWrecktideSkill::Shooting) { AssignedSlots[1] = EquippedWeaponRow; }
	}
}

float AWrecktidePlayerState::GetAttackDamage() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FWeaponTableRow* WeaponRow = DataSubsystem ? DataSubsystem->GetWeaponRow(EquippedWeaponRow) : nullptr;
	const float BaseDamage = WeaponRow ? WeaponRow->BaseDamage : UnarmedDamage;
	// Shooting has no upgrade track yet; every pistol shot uses the authored damage exactly.
	if (WeaponRow && WeaponRow->CombatClass == EWrecktideSkill::Shooting) { return BaseDamage; }
	const float PowerMultiplier = Skill ? Skill->GetPowerMultiplier(GetEquippedCombatClass()) : 1.0f;

	// Edge, the blacksmith's damage track (spec C10). It multiplies the weapon's own damage and it
	// is read from the WEAPON row's tiers, so a tier bought on the cutlass never follows the pirate
	// to the next weapon and never applies while he is unarmed.
	int32 EdgeTier = 0;
	float EdgeStep = 0.0f;
	const float EdgeMultiplier = GetTrackState(EWrecktideUpgradeTrack::Edge, EdgeTier, EdgeStep)
		? 1.0f + (EdgeStep * EdgeTier) : 1.0f;

	return BaseDamage * PowerMultiplier * EdgeMultiplier;
}

bool AWrecktidePlayerState::GetTrackState(EWrecktideUpgradeTrack Track, int32& OutTier, float& OutStep) const
{
	OutTier = 0;
	OutStep = 0.0f;

	if (EquippedWeaponRow.IsNone() || !Progress)
	{
		return false;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	FName TrackRow = NAME_None;
	const FUpgradeTrackTableRow* TrackData = DataSubsystem ? DataSubsystem->GetUpgradeRowForTrack(Track, &TrackRow) : nullptr;
	if (!TrackData || TrackRow.IsNone())
	{
		return false;
	}

	OutTier = Progress->GetUpgradeTier(EquippedWeaponRow, TrackRow);
	OutStep = TrackData->Step;
	return true;
}

int32 AWrecktidePlayerState::GetEquippedUpgradeTier(EWrecktideUpgradeTrack Track) const
{
	int32 Tier = 0;
	float Step = 0.0f;
	GetTrackState(Track, Tier, Step);
	return Tier;
}

float AWrecktidePlayerState::GetAttackSpeedMultiplier() const
{
	int32 Tier = 0;
	float Step = 0.0f;
	if (!GetTrackState(EWrecktideUpgradeTrack::Balance, Tier, Step))
	{
		return 1.0f;
	}

	// Full value up to BalanceFullValueTiers, half a step after that: the visible diminishing return
	// the research brief asks for, so tiers 4 and 5 of Balance are a real trade against Edge.
	const int32 FullTiers = FMath::Clamp(Tier, 0, FMath::Max(0, BalanceFullValueTiers));
	const int32 HalfTiers = FMath::Max(0, Tier - FullTiers);
	return 1.0f + (Step * FullTiers) + (Step * 0.5f * HalfTiers);
}

float AWrecktidePlayerState::GetCriticalChance() const
{
	if (ProbeCriticalChanceOverride >= 0.0f)
	{
		return FMath::Clamp(ProbeCriticalChanceOverride, 0.0f, 1.0f);
	}

	int32 Tier = 0;
	float Step = 0.0f;
	GetTrackState(EWrecktideUpgradeTrack::Cruelty, Tier, Step);
	return FMath::Clamp(BaseCriticalChance + (Step * Tier), 0.0f, 1.0f);
}

float AWrecktidePlayerState::GetCriticalMultiplier() const
{
	int32 Tier = 0;
	float Step = 0.0f;
	GetTrackState(EWrecktideUpgradeTrack::Brutal, Tier, Step);
	return FMath::Max(1.0f, BaseCriticalMultiplier + (Step * Tier));
}

float AWrecktidePlayerState::GetLifestealFraction() const
{
	int32 Tier = 0;
	float Step = 0.0f;
	GetTrackState(EWrecktideUpgradeTrack::Bloodletting, Tier, Step);
	return FMath::Clamp(Step * Tier, 0.0f, 1.0f);
}

EWrecktideSkill AWrecktidePlayerState::GetEquippedCombatClass() const
{
	if (EquippedWeaponRow.IsNone())
	{
		return EWrecktideSkill::Melee;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FWeaponTableRow* WeaponRow = DataSubsystem ? DataSubsystem->GetWeaponRow(EquippedWeaponRow) : nullptr;
	return WeaponRow ? WeaponRow->CombatClass : EWrecktideSkill::Melee;
}

void AWrecktidePlayerState::MarkOutfitChanged(FName InOutfitId, int32 InVariant)
{
	if (!HasAuthority() || (OutfitId == InOutfitId && OutfitVariant == InVariant))
	{
		return;
	}

	OutfitId = InOutfitId;
	OutfitVariant = InVariant;
	RequestSave();
}

FString AWrecktidePlayerState::GetSaveSlot()
{
	FString Slot;
	if (FParse::Value(FCommandLine::Get(), TEXT("WrecktideSaveSlot="), Slot) && !Slot.IsEmpty())
	{
		// The slot is a file name under Saved/SaveGames; keep it to a plain identifier (review
		// 2026-09-02: a raw value could name a path outside that folder).
		bool bClean = true;
		for (const TCHAR Char : Slot)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_') && Char != TEXT('-'))
			{
				bClean = false;
				break;
			}
		}
		if (bClean)
		{
			return Slot;
		}
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAVE rejected -WrecktideSaveSlot=%s (letters, digits, _ and - only); using %s"),
			*Slot, DefaultSaveSlotName);
	}
	return DefaultSaveSlotName;
}

FString AWrecktidePlayerState::GetDefaultSaveSlot()
{
	return DefaultSaveSlotName;
}

bool AWrecktidePlayerState::IsProbeRun()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	const TCHAR* CommandLine = FCommandLine::Get();
	for (const TCHAR* Name : ProbeSwitchNames)
	{
		if (HasSwitch(CommandLine, Name))
		{
			return true;
		}
	}
#endif
	return false;
}

bool AWrecktidePlayerState::IsPersistenceSuppressed()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	if (FParse::Param(CommandLine, TEXT("WrecktideNoSave")))
	{
		return true;
	}

	if (!IsProbeRun())
	{
		return false;
	}

	// A probe may opt in ONLY on a slot of its own; the real slot stays off limits to every probe
	// (review 2026-09-02: -OwnershipProbe -WrecktideSaveSlot=Wrecktide_Slot0 used to reach it).
	FString ExplicitSlot;
	const bool bOwnSlot = FParse::Value(CommandLine, TEXT("WrecktideSaveSlot="), ExplicitSlot)
		&& !ExplicitSlot.IsEmpty()
		&& !ExplicitSlot.Equals(DefaultSaveSlotName, ESearchCase::IgnoreCase);
	return !bOwnSlot;
}

void AWrecktidePlayerState::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (IsPersistenceSuppressed())
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE suppressed for this run (probe switch or -WrecktideNoSave): no load, no autosave"));
		return;
	}

	bPersistenceActive = true;
	const FString Slot = GetSaveSlot();
	const bool bSlotExists = UGameplayStatics::DoesSaveGameExist(Slot, 0);
	const bool bLoaded = LoadNow();

	// An EXISTING slot that failed to load is a file to protect, not to overwrite: arming the
	// autosave here would replace it with default state at the next gold change (review 2026-09-02).
	// Persistence stays off for this run and the log says so loudly.
	if (bSlotExists && !bLoaded)
	{
		bPersistenceActive = false;
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SAVE LOAD FAILED for existing slot %s; autosave DISABLED this run so the file survives for recovery"), *Slot);
		return;
	}

	// Bind AFTER the load so restoring state never counts as a change worth saving again.
	if (Economy)
	{
		Economy->OnGoldChanged.AddUniqueDynamic(this, &AWrecktidePlayerState::HandleGoldChangedForSave);
	}
	if (Skill)
	{
		Skill->OnSkillXPChanged.AddUniqueDynamic(this, &AWrecktidePlayerState::HandleSkillXPChangedForSave);
	}
	if (Progress)
	{
		Progress->OnProgressChanged.AddUniqueDynamic(this, &AWrecktidePlayerState::HandleProgressChangedForSave);
	}
	OnWeaponEquipped.AddUniqueDynamic(this, &AWrecktidePlayerState::HandleWeaponEquippedForSave);
}

void AWrecktidePlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bPersistenceActive && bSaveDirty && HasAuthority())
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE flush on EndPlay reason=%d"), static_cast<int32>(EndPlayReason));
		SaveNow();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutosaveTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void AWrecktidePlayerState::RequestSave()
{
	if (!bPersistenceActive || !HasAuthority())
	{
		return;
	}

	bSaveDirty = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Throttle, not reset: the first change arms the timer and everything inside the window rides
	// along, so a kill streak cannot postpone the save forever.
	FTimerManager& TimerManager = World->GetTimerManager();
	if (!TimerManager.IsTimerActive(AutosaveTimer))
	{
		TimerManager.SetTimer(AutosaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SaveNow();
		}), AutosaveDelaySeconds, false);
	}
}

bool AWrecktidePlayerState::SaveNow()
{
	// Inactive persistence (a probe run, -WrecktideNoSave, a client) never writes, whoever asks.
	if (!bPersistenceActive || !HasAuthority() || !Progress)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE SAVE refused active=%d"), bPersistenceActive ? 1 : 0);
		return false;
	}

	const FString Slot = GetSaveSlot();
	const bool bSaved = Progress->SaveToSlot(Slot);

	// Stays dirty on failure so the EndPlay flush gets one more try.
	bSaveDirty = !bSaved;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutosaveTimer);
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE SAVE slot=%s ok=%d gold=%lld unlocked=%d equipped=%s"),
		*Slot, bSaved ? 1 : 0, Economy ? Economy->GetGoldTotal() : -1, Progress->UnlockedItems.Num(),
		*EquippedWeaponRow.ToString());
	return bSaved;
}

bool AWrecktidePlayerState::LoadNow()
{
	if (!bPersistenceActive || !HasAuthority() || !Progress)
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE LOAD refused active=%d"), bPersistenceActive ? 1 : 0);
		return false;
	}

	const FString Slot = GetSaveSlot();
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE LOAD slot=%s none (fresh start)"), *Slot);
		return false;
	}

	// Only a SUCCESSFUL load clears pending work: a failed load (corrupt or wrong-class file) must
	// leave the dirty flag and the autosave timer alone, or the next quit drops unsaved changes.
	const bool bLoaded = Progress->LoadFromSlot(Slot);
	if (bLoaded)
	{
		bLoadedFromSlot = true;
		bSaveDirty = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AutosaveTimer);
		}
		// The startup load is the one Continue rides on: without this the saved position was parked
		// and never applied, and every Continue started the pirate back at the PlayerStart.
		PlacePawnFromSave();
	}

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE LOAD slot=%s ok=%d gold=%lld unlocked=%d equipped=%s"),
		*Slot, bLoaded ? 1 : 0, Economy ? Economy->GetGoldTotal() : -1, Progress->UnlockedItems.Num(),
		*EquippedWeaponRow.ToString());
	return bLoaded;
}

void AWrecktidePlayerState::HandleGoldChangedForSave(int64 Delta, EGoldReason Reason, int64 NewTotal)
{
	RequestSave();
}

void AWrecktidePlayerState::HandleSkillXPChangedForSave(EWrecktideSkill InSkill, int64 NewXP)
{
	RequestSave();
}

void AWrecktidePlayerState::HandleWeaponEquippedForSave(FName Row)
{
	RequestSave();
}

void AWrecktidePlayerState::HandleProgressChangedForSave()
{
	RequestSave();
}

bool AWrecktidePlayerState::AbandonSaveForNewGame()
{
	if (!HasAuthority())
	{
		return false;
	}
	if (IsPersistenceSuppressed())
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE newgame: persistence suppressed, slot kept, nothing to clear"));
		return true;
	}
	// Delete FIRST and verify; persistence is switched off only once the file is gone (review
	// 2026-09-05: switching it off before a failed delete left the player on the title able to pick
	// Continue into a game that would never save again). A failed delete changes nothing here.
	const FString Slot = GetSaveSlot();
	const bool bExisted = UGameplayStatics::DoesSaveGameExist(Slot, 0);
	const bool bDeleted = bExisted ? UGameplayStatics::DeleteGameInSlot(Slot, 0) : false;
	const bool bGone = !UGameplayStatics::DoesSaveGameExist(Slot, 0);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE newgame slot=%s existed=%d deleted=%d gone=%d"),
		*Slot, bExisted ? 1 : 0, bDeleted ? 1 : 0, bGone ? 1 : 0);
	if (!bGone)
	{
		return false;
	}
	// Off now: no EndPlay flush and no autosave can recreate the file during the restart. This runs
	// even when persistence was already off (an existing slot that failed to load): the player asked
	// for a fresh start, so the file went.
	bPersistenceActive = false;
	bSaveDirty = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutosaveTimer);
	}
	return true;
}


// ---------------------------------------------------------------------------------------------
// Manual save slots (2026-09-06): Save Voyage / Load Voyage. The autosave slot is untouched by all
// of this; it keeps its own name, its own triggers and its own suppression rules.
// ---------------------------------------------------------------------------------------------

int32 AWrecktidePlayerState::GetManualSlotCount()
{
	return ManualSlotCount;
}

FString AWrecktidePlayerState::GetManualSlot(int32 Index)
{
	const int32 Clamped = FMath::Clamp(Index, 1, ManualSlotCount);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	// A probe run writes ITS slot's manual files (<ProbeSlot>_Manual1). The player's own manual
	// slots are named from the fixed prefix and no probe can produce that name.
	if (IsProbeRun())
	{
		return FString::Printf(TEXT("%s_Manual%d"), *GetSaveSlot(), Clamped);
	}
#endif
	return FString::Printf(TEXT("%s_Manual%d"), ManualSlotPrefix, Clamped);
}

FString AWrecktidePlayerState::GetSlotSummary(const UObject* WorldContextObject, const FString& SlotName)
{
	if (SlotName.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		return TEXT("Empty");
	}

	const UWrecktideSaveGame* SaveGame = Cast<UWrecktideSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!SaveGame)
	{
		// A file that exists but will not read is NOT reported as a voyage: the player must not be
		// offered a load that cannot happen.
		return TEXT("Unreadable");
	}
	const FWrecktideSaveData& Data = SaveGame->Data;

	const int64 NotorietyXP = Data.SaveVersion >= 4
		? Data.NotorietyXP : USkillComponent::DeriveNotorietyXPFromSkills(Data.Skills);
	const USkillComponent* SkillDefaults = GetDefault<USkillComponent>();
	const int32 NotorietyLevel = SkillDefaults ? SkillDefaults->GetNotorietyLevelForXP(NotorietyXP) : 1;

	int32 MissionsDone = 0;
	int32 MissionsTotal = 0;
	const UGameInstance* GameInstance = WorldContextObject
		? UGameplayStatics::GetGameInstance(WorldContextObject) : nullptr;
	if (const UWrecktideDataSubsystem* DataSubsystem = GameInstance
		? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr)
	{
		const TArray<TPair<FName, const FMissionTableRow*>> Rows = DataSubsystem->GetMissionRows();
		MissionsTotal = Rows.Num();
		for (const TPair<FName, const FMissionTableRow*>& Row : Rows)
		{
			const int32* Flag = Data.QuestFlags.Find(WrecktideMissionLogic::DoneKey(Row.Key));
			if (Flag && *Flag != 0)
			{
				++MissionsDone;
			}
		}
	}

	// The file's own timestamp, converted from UTC to the player's clock.
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav"));
	const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*SavePath);
	FString When;
	if (Stamp != FDateTime::MinValue())
	{
		const FDateTime Local = Stamp + (FDateTime::Now() - FDateTime::UtcNow());
		When = Local.ToString(TEXT("%Y-%m-%d %H:%M"));
	}

	FString Summary = FString::Printf(TEXT("Notoriety Lv %d, %lld gold, %d/%d missions"),
		NotorietyLevel, Data.EarnedGold + Data.PurchasedGold, MissionsDone, MissionsTotal);
	if (!When.IsEmpty())
	{
		Summary += TEXT(", ") + When;
	}
	return Summary;
}

bool AWrecktidePlayerState::SaveToManualSlot(int32 Index)
{
	if (!HasAuthority() || !Progress)
	{
		return false;
	}
	// -WrecktideNoSave means this PROCESS writes nothing at all, by hand or otherwise.
	if (FParse::Param(FCommandLine::Get(), TEXT("WrecktideNoSave")))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE MANUAL refused (-WrecktideNoSave)"));
		return false;
	}

	const FString Slot = GetManualSlot(Index);
	const bool bSaved = Progress->SaveToSlot(Slot);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE MANUAL SAVE slot=%s ok=%d gold=%lld"),
		*Slot, bSaved ? 1 : 0, Economy ? Economy->GetGoldTotal() : -1);
	return bSaved;
}

bool AWrecktidePlayerState::LoadManualSlot(int32 Index)
{
	return LoadSlotInternal(GetManualSlot(Index), true);
}

bool AWrecktidePlayerState::LoadAutosaveSlot()
{
	// No mirror: this IS the autosave slot.
	return LoadSlotInternal(GetSaveSlot(), false);
}

bool AWrecktidePlayerState::LoadSlotInternal(const FString& Slot, bool bMirrorToAutosave)
{
	if (!HasAuthority() || !Progress)
	{
		return false;
	}
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE MANUAL LOAD slot=%s none"), *Slot);
		return false;
	}
	if (!Progress->LoadFromSlot(Slot))
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-SAVE MANUAL LOAD slot=%s FAILED to read"), *Slot);
		return false;
	}

	bLoadedFromSlot = true;

	// Continue must reflect what the player just loaded, so the autosave slot is rewritten now
	// rather than at the next gold change. A suppressed run (a probe) simply skips the write.
	if (bMirrorToAutosave && bPersistenceActive)
	{
		bSaveDirty = true;
		SaveNow();
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UWrecktideWardrobeSubsystem* Wardrobe = GameInstance->GetSubsystem<UWrecktideWardrobeSubsystem>())
		{
			Wardrobe->ApplyPersistedOutfit(Cast<ACharacter>(GetPawn()));
		}
	}

	PlacePawnFromSave();

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE MANUAL LOAD slot=%s ok=1 gold=%lld mirrored=%d"),
		*Slot, Economy ? Economy->GetGoldTotal() : -1, bMirrorToAutosave ? 1 : 0);
	return true;
}

bool AWrecktidePlayerState::TryPlacePawnFromSaveNow()
{
	if (!Progress || !Progress->HasPendingPlayerLocation())
	{
		return true;
	}

	APawn* OwnPawn = GetPawn();
	if (!OwnPawn)
	{
		return false;
	}

	const FVector Location = Progress->GetPendingPlayerLocation();
	const float Yaw = Progress->GetPendingPlayerYaw();
	OwnPawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	OwnPawn->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
	if (AController* OwnController = OwnPawn->GetController())
	{
		OwnController->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
	}
	// Cleared so a second load cannot replay this teleport with a stale position.
	Progress->ClearPendingPlayerLocation();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE placed pirate at %s yaw=%.0f"), *Location.ToString(), Yaw);
	return true;
}

void AWrecktidePlayerState::PlacePawnFromSave()
{
	if (!Progress || !Progress->HasPendingPlayerLocation())
	{
		return;
	}

	// The common case: the pawn is already possessed, so the teleport lands on this very frame and
	// the player never sees the pirate at the PlayerStart.
	if (TryPlacePawnFromSaveNow())
	{
		return;
	}

	TWeakObjectPtr<AWrecktidePlayerState> WeakThis(this);
	TSharedRef<int32> Attempts = MakeShared<int32>(0);

	// Core ticker, not a world timer: the load can happen while the title menu has the world paused,
	// and a world timer would never fire (2026-09-05 trap 1).
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakThis, Attempts](float) -> bool
	{
		AWrecktidePlayerState* State = WeakThis.Get();
		if (!State || !State->Progress)
		{
			return false;
		}
		if (State->TryPlacePawnFromSaveNow())
		{
			return false;
		}
		// Keep waiting for a pawn, but not forever: a run that never possesses one just starts
		// at the PlayerStart, which is what an older save does anyway.
		if (++(*Attempts) >= PlacePawnMaxAttempts)
		{
			// Dropped, not kept: a position that will never be applied must not go on being written
			// into every later save in place of where the pirate really is.
			State->Progress->ClearPendingPlayerLocation();
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-SAVE gave up placing the pirate: no pawn after %d attempts"), *Attempts);
			return false;
		}
		return true;
	}), 0.25f);
}
