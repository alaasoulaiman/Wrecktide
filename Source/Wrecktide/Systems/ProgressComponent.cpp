// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/ProgressComponent.h"

#include "Data/WrecktideDataRows.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/EconomyComponent.h"
#include "Systems/WrecktidePlayerState.h"

namespace
{
constexpr EWrecktideSkill AllProgressSkills[] =
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

TArray<FName> MakeUniqueItems(const TArray<FName>& Items)
{
	TArray<FName> UniqueItems;
	UniqueItems.Reserve(Items.Num());

	for (const FName& Item : Items)
	{
		if (!UniqueItems.Contains(Item))
		{
			UniqueItems.Add(Item);
		}
	}

	return UniqueItems;
}
}

UProgressComponent::UProgressComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProgressComponent::SetQuestFlag(FName Flag, int32 Value)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	QuestFlags.Add(Flag, Value);
	OnProgressChanged.Broadcast();
}

int32 UProgressComponent::GetQuestFlag(FName Flag) const
{
	const int32* Value = QuestFlags.Find(Flag);
	return Value ? *Value : 0;
}

bool UProgressComponent::UnlockItem(FName ItemId)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || UnlockedItems.Contains(ItemId))
	{
		return false;
	}

	UnlockedItems.Add(ItemId);
	OnProgressChanged.Broadcast();
	return true;
}

bool UProgressComponent::IsItemUnlocked(FName ItemId) const
{
	return UnlockedItems.Contains(ItemId);
}

bool UProgressComponent::OwnsOutfit(FName OutfitId) const
{
	return OwnedOutfits.Contains(OutfitId);
}

bool UProgressComponent::GrantOutfit(FName OutfitId)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || OutfitId.IsNone() || OwnedOutfits.Contains(OutfitId))
	{
		return false;
	}

	OwnedOutfits.Add(OutfitId);
	OnProgressChanged.Broadcast();
	return true;
}

int32 UProgressComponent::GetUpgradeTier(FName WeaponRow, FName TrackRow) const
{
	if (WeaponRow.IsNone() || TrackRow.IsNone())
	{
		return 0;
	}

	const FWrecktideWeaponUpgrades* Upgrades = WeaponUpgrades.Find(WeaponRow);
	const int32* Tier = Upgrades ? Upgrades->Tiers.Find(TrackRow) : nullptr;
	return Tier ? FMath::Clamp(*Tier, 0, WRECKTIDE_UPGRADE_MAX_TIER) : 0;
}

bool UProgressComponent::SetUpgradeTier(FName WeaponRow, FName TrackRow, int32 Tier)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || WeaponRow.IsNone() || TrackRow.IsNone()
		|| Tier < 0 || Tier > WRECKTIDE_UPGRADE_MAX_TIER)
	{
		return false;
	}

	WeaponUpgrades.FindOrAdd(WeaponRow).Tiers.Add(TrackRow, Tier);
	OnProgressChanged.Broadcast();
	return true;
}

int32 UProgressComponent::GetBoatHull(int32 DefaultHull) const
{
	// A stored 0 means NEVER RECORDED, not sunk: bHasBoatState is raised by the first hold purchase
	// too, and a hull that actually reaches zero is reset to full inside the same call (the boat
	// sinks and respawns), so a resting zero can only ever be an unwritten field.
	return (bHasBoatState && BoatHullHealth > 0) ? BoatHullHealth : DefaultHull;
}

void UProgressComponent::SetBoatHull(int32 Hull)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	bHasBoatState = true;
	BoatHullHealth = Hull;
	OnProgressChanged.Broadcast();
}

int32 UProgressComponent::GetBoatHoldUnits(FName SupplyRow) const
{
	const int32* Units = BoatHold.Find(SupplyRow);
	return Units ? FMath::Max(*Units, 0) : 0;
}

void UProgressComponent::SetBoatHoldUnits(FName SupplyRow, int32 Units)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || SupplyRow.IsNone())
	{
		return;
	}
	bHasBoatState = true;
	if (Units > 0)
	{
		BoatHold.Add(SupplyRow, Units);
	}
	else
	{
		BoatHold.Remove(SupplyRow);
	}
	OnProgressChanged.Broadcast();
}

void UProgressComponent::ClearBoatHold()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	bHasBoatState = true;
	BoatHold.Reset();
	OnProgressChanged.Broadcast();
}

int32 UProgressComponent::GetBoatUpgradeTier(FName TrackRow) const
{
	if (TrackRow.IsNone())
	{
		return 0;
	}
	const int32* Tier = BoatUpgrades.Find(TrackRow);
	return Tier ? FMath::Max(*Tier, 0) : 0;
}

bool UProgressComponent::SetBoatUpgradeTier(FName TrackRow, int32 Tier)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || TrackRow.IsNone() || Tier < 0)
	{
		return false;
	}
	bHasBoatState = true;
	BoatUpgrades.Add(TrackRow, Tier);
	OnProgressChanged.Broadcast();
	return true;
}

void UProgressComponent::SetSavedLocationOverride(const FVector& Location, float Yaw)
{
	SavedLocationOverride = Location;
	SavedYawOverride = Yaw;
	bHasSavedLocationOverride = true;
}

void UProgressComponent::ClearSavedLocationOverride()
{
	bHasSavedLocationOverride = false;
}

FWrecktideSaveData UProgressComponent::BuildSaveData() const
{
	FWrecktideSaveData SaveData;
	SaveData.SaveVersion = WRECKTIDE_SAVE_VERSION;

	const AActor* Owner = GetOwner();
	if (Owner)
	{
		if (const UEconomyComponent* EconomyComponent = Owner->FindComponentByClass<UEconomyComponent>())
		{
			SaveData.EarnedGold = EconomyComponent->GetEarnedGold();
			SaveData.PurchasedGold = EconomyComponent->GetPurchasedGold();
		}

		if (const USkillComponent* SkillComponent = Owner->FindComponentByClass<USkillComponent>())
		{
			SaveData.NotorietyXP = SkillComponent->GetNotorietyXP();
			for (const EWrecktideSkill Skill : AllProgressSkills)
			{
				FSkillState SkillState;
				SkillState.Level = SkillComponent->GetSkillLevel(Skill);
				SkillState.XP = SkillComponent->GetSkillXP(Skill);
				SaveData.Skills.Add(Skill, SkillState);
			}
		}
	}

	if (const AWrecktidePlayerState* PlayerState = Cast<AWrecktidePlayerState>(Owner))
	{
		SaveData.EquippedWeaponRow = PlayerState->EquippedWeaponRow;
		SaveData.OutfitId = PlayerState->GetOutfitId();
		SaveData.OutfitVariant = PlayerState->GetOutfitVariant();
		// The weapon bar (save version 6). The ASSIGNMENT is the state worth keeping; the equipped
		// row above follows it back on load.
		SaveData.AssignedSlots = PlayerState->GetAssignedSlots();
	}

	SaveData.QuestFlags = QuestFlags;
	SaveData.UnlockedItems = UnlockedItems;
	SaveData.OwnedOutfits = OwnedOutfits;
	SaveData.WeaponUpgrades = WeaponUpgrades;
	SaveData.bHasBoatState = bHasBoatState;
	SaveData.BoatHullHealth = BoatHullHealth;
	SaveData.BoatHold = BoatHold;
	SaveData.BoatUpgrades = BoatUpgrades;
	SaveData.CrewId = CrewId;
	SaveData.bBetaVeteran = bBetaVeteran;

	// Save version 3: where the pirate was standing. Only ON THE GROUND, and only unattached: a
	// position taken aboard the boat, in the water or mid-fall would reload him where he cannot
	// stand. A save taken afloat simply keeps bHasLocation false, and the load falls back to the
	// PlayerStart.
	if (bHasPendingLocation)
	{
		// A load just parked a position and the teleport has not landed yet (it waits for a pawn on
		// the core ticker). Anything written in that window - the mirror LoadSlotInternal writes, an
		// autosave riding along - must carry the position that was LOADED, not the one the pirate is
		// still standing on.
		SaveData.PlayerLocation = PendingPlayerLocation;
		SaveData.PlayerYaw = PendingPlayerYaw;
		SaveData.bHasLocation = true;
	}
	else if (bHasSavedLocationOverride)
	{
		// The wardrobe has him in the change room, which is nowhere in the world: store the spot he
		// walked in from, or a save taken with the screen open would reload him inside the room.
		SaveData.PlayerLocation = SavedLocationOverride;
		SaveData.PlayerYaw = SavedYawOverride;
		SaveData.bHasLocation = true;
	}
	else if (const APlayerState* OwnerState = Cast<APlayerState>(Owner))
	{
		const ACharacter* PlayerPawn = Cast<ACharacter>(OwnerState->GetPawn());
		const UCharacterMovementComponent* Movement = PlayerPawn ? PlayerPawn->GetCharacterMovement() : nullptr;
		// IsMovingOnGround, not "any mode but swimming": MOVE_Falling stored a mid-air position, and
		// reloading it dropped the pirate out of the sky (or through the pier).
		const bool bOnFoot = Movement && Movement->IsMovingOnGround();
		if (PlayerPawn && IsValid(PlayerPawn) && !PlayerPawn->IsActorBeingDestroyed()
			&& bOnFoot && PlayerPawn->GetAttachParentActor() == nullptr)
		{
			SaveData.PlayerLocation = PlayerPawn->GetActorLocation();
			SaveData.PlayerYaw = PlayerPawn->GetActorRotation().Yaw;
			SaveData.bHasLocation = true;
		}
	}

	return SaveData;
}

void UProgressComponent::ApplySaveData(const FWrecktideSaveData& Data)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (UEconomyComponent* EconomyComponent = Owner->FindComponentByClass<UEconomyComponent>())
	{
		EconomyComponent->RestoreFromSave(Data.EarnedGold, Data.PurchasedGold);
	}

	if (USkillComponent* SkillComponent = Owner->FindComponentByClass<USkillComponent>())
	{
		const int64 RestoredNotorietyXP = Data.SaveVersion >= 4
			? Data.NotorietyXP : USkillComponent::DeriveNotorietyXPFromSkills(Data.Skills);
		SkillComponent->RestoreFromSave(Data.Skills, RestoredNotorietyXP);
		if (Data.SaveVersion < 4)
		{
			UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE-NOTORIETY-MIGRATE version=%d xp=%lld level=%d"),
				Data.SaveVersion, SkillComponent->GetNotorietyXP(), SkillComponent->GetNotorietyLevel());
		}
	}

	QuestFlags = Data.QuestFlags;
	UnlockedItems = MakeUniqueItems(Data.UnlockedItems);
	// Earned outfits (save version 4). The migration keys off the DATA, not the version number: a
	// file written before this field existed reads back EMPTY, which no real save can be, so that is
	// the signal to grant the outfit the player was already wearing. The starting outfits are added
	// by the wardrobe when it reads the table, so an empty list here is never left empty for long.
	OwnedOutfits = MakeUniqueItems(Data.OwnedOutfits);
	if (OwnedOutfits.Num() == 0 && !Data.OutfitId.IsNone())
	{
		OwnedOutfits.Add(Data.OutfitId);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SAVE outfit migration: granted the worn outfit %s to a save with no owned list"),
			*Data.OutfitId.ToString());
	}
	// Blacksmith tiers (save version 5). No version gate and no migration: a file written before the
	// smithy existed carries no map, and an empty map IS the right state for a pirate who never
	// bought a tier. The stall re-validates every tier against Upgrades.csv before it prices one.
	WeaponUpgrades = Data.WeaponUpgrades;
	// The boat (save version 6). Gated on the DATA, never on the version number: two lanes bump the
	// version in the same wave, so bHasBoatState is what decides whether this file ever had a hull.
	// False leaves every boat field at its default and the boat reads a full hull and an empty hold.
	bHasBoatState = Data.bHasBoatState;
	BoatHullHealth = Data.BoatHullHealth;
	BoatHold = Data.BoatHold;
	BoatUpgrades = Data.BoatUpgrades;
	if (!bHasBoatState)
	{
		BoatHullHealth = 0;
		BoatHold.Reset();
		BoatUpgrades.Reset();
	}
	CrewId = Data.CrewId;
	bBetaVeteran = Data.bBetaVeteran;

	// The pawn may not exist yet (the autosave load runs from PlayerState::BeginPlay), so the
	// position is parked here and AWrecktidePlayerState puts it on the pirate when one is possessed.
	PendingPlayerLocation = Data.PlayerLocation;
	PendingPlayerYaw = Data.PlayerYaw;
	bHasPendingLocation = Data.bHasLocation && Data.SaveVersion >= 3;

	// Equipped weapon arrived in save version 2. A version 1 save carries no value, so the LIVE row is
	// the one revalidated: UnlockedItems and the skill levels were just replaced above, so whatever
	// was equipped before the load may now be unowned or under-levelled (code review 2026-08-22,
	// finding 2). Runs LAST because the checks below depend on everything restored above.
	if (AWrecktidePlayerState* PlayerState = Cast<AWrecktidePlayerState>(Owner))
	{
		PlayerState->OutfitId = Data.OutfitId;
		PlayerState->OutfitVariant = Data.OutfitVariant;

		const FName RowToRestore = (Data.SaveVersion >= 2) ? Data.EquippedWeaponRow : PlayerState->EquippedWeaponRow;

		// Never trust the file. A hand-edited save must not equip an unowned or under-levelled
		// weapon; EquipWeapon applies the level gate, IsItemUnlocked applies the ownership gate.
		if (RowToRestore == NAME_None || IsItemUnlocked(RowToRestore))
		{
			if (!PlayerState->EquipWeapon(RowToRestore))
			{
				UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-SAVE-EQUIPREJECT level gate rejected %s, staying unarmed"),
					*RowToRestore.ToString());
				PlayerState->EquipWeapon(NAME_None);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-SAVE-EQUIPREJECT unowned %s, staying unarmed"),
				*RowToRestore.ToString());
			PlayerState->EquipWeapon(NAME_None);
		}

		// The weapon bar (save version 6). AFTER the equip above, which already put the restored
		// weapon in slot 1: a file that carries real assignments overwrites that, an older file
		// carries none and keeps it. Nothing here is trusted blindly, the equip gate above already
		// decided what the pirate may hold.
		PlayerState->RestoreAssignedSlots(Data.AssignedSlots);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOTBAR restored version=%d saved=%d slot1=%s"),
			Data.SaveVersion, Data.AssignedSlots.Num(), *PlayerState->GetAssignedSlot(1).ToString());
	}
}

bool UProgressComponent::SaveToSlot(const FString& SlotName)
{
	UWrecktideSaveGame* SaveGame = Cast<UWrecktideSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UWrecktideSaveGame::StaticClass()));
	if (!SaveGame)
	{
		return false;
	}

	SaveGame->Data = BuildSaveData();
	return UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0);
}

bool UProgressComponent::LoadFromSlot(const FString& SlotName)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	UWrecktideSaveGame* SaveGame = Cast<UWrecktideSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!SaveGame)
	{
		return false;
	}

	ApplySaveData(SaveGame->Data);
	return true;
}
