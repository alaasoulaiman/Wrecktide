// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideSmithStall.h"

#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

AWrecktideSmithStall::AWrecktideSmithStall()
{
	PrimaryActorTick.bCanEverTick = false;

	// Synty has no anvil and no hammer in this pack (checked 2026-09-06), so the smithy reads as a
	// workbench, a barrel of tools, an axe head laid by it and a forge fire. The engine cube stays
	// as the fallback so a missing pack still yields a usable, if ugly, stall (shop stall pattern).
	BenchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BenchMesh"));
	RootComponent = BenchMesh;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BenchAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Table_02.SM_Prop_Table_02"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (BenchAsset.Succeeded())
	{
		BenchMesh->SetStaticMesh(BenchAsset.Object);
	}
	else if (CubeMesh.Succeeded())
	{
		BenchMesh->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicMaterial.Succeeded() && !BenchAsset.Succeeded())
	{
		BenchMesh->SetMaterial(0, BasicMaterial.Object);
	}

	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(RootComponent);
	BarrelMesh->SetRelativeLocation(FVector(-40.0f, 120.0f, 0.0f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BarrelAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Barrel_01.SM_Prop_Barrel_01"));
	if (BarrelAsset.Succeeded())
	{
		BarrelMesh->SetStaticMesh(BarrelAsset.Object);
	}

	ToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToolMesh"));
	ToolMesh->SetupAttachment(BarrelMesh);
	// Leaning against the barrel: the pack's axe is the closest thing it has to a smith's hammer.
	ToolMesh->SetRelativeLocation(FVector(-30.0f, 0.0f, 20.0f));
	ToolMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, 25.0f));
	ToolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ToolAsset(TEXT("/Game/PolygonPirates/Meshes/Weapons/SM_Wep_Axe_01.SM_Wep_Axe_01"));
	if (ToolAsset.Succeeded())
	{
		ToolMesh->SetStaticMesh(ToolAsset.Object);
	}

	// C21 (2026-09-06): the Niagara forge fire is GONE. The play test asked for the flame out and a
	// blacksmith standing there instead, so the fire's spot now holds a cold iron brazier prop and
	// AWrecktideMissionGiver spawns Jerold beside the bench. No Niagara component is left on this
	// actor; the module dependency stays in Wrecktide.Build.cs for the lanes that still use it.
	BrazierMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BrazierMesh"));
	BrazierMesh->SetupAttachment(RootComponent);
	BrazierMesh->SetRelativeLocation(FVector(0.0f, -130.0f, 0.0f));
	BrazierMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BrazierMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BrazierAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Campfire_01.SM_Prop_Campfire_01"));
	if (BrazierAsset.Succeeded())
	{
		BrazierMesh->SetStaticMesh(BrazierAsset.Object);
	}
	else if (BarrelAsset.Succeeded())
	{
		// Fail visible, never invisible: a missing prop leaves a second barrel, not a hole.
		BrazierMesh->SetStaticMesh(BarrelAsset.Object);
		BrazierMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 0.5f));
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(300.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AWrecktideSmithStall::BeginPlay()
{
	Super::BeginPlay();

	Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideSmithStall::OnTriggerBeginOverlap);
	Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideSmithStall::OnTriggerEndOverlap);
}

void AWrecktideSmithStall::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseSmith();

	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideSmithStall::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideSmithStall::OnTriggerEndOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void AWrecktideSmithStall::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	if (!Character || !Character->ActorHasTag(FName("Player")) || !Character->Stats || !Character->Stats->IsAlive())
	{
		return;
	}

	if (SmithUser.IsValid())
	{
		return;
	}

	SetSmithUser(Character);
	ShowToast(TEXT("Press F for the smithy"));
}

void AWrecktideSmithStall::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor == SmithUser.Get())
	{
		CloseSmith();
	}
}

void AWrecktideSmithStall::SelectPrevious()
{
	MoveMenuSelection(-1);
}

void AWrecktideSmithStall::SelectNext()
{
	MoveMenuSelection(1);
}

void AWrecktideSmithStall::CloseSmithMenu()
{
	SetSmithMenuOpen(false, true);
}

void AWrecktideSmithStall::BackOrClose()
{
	if (bMenuOpen && Page == ESmithMenuPage::Tracks)
	{
		Page = ESmithMenuPage::Weapons;
		OpenWeaponRow = NAME_None;
		SelectedIndex = 0;
		RefreshMenu();
		return;
	}

	CloseSmithMenu();
}

void AWrecktideSmithStall::OnSmithUserDied(AActor* InstigatorActor)
{
	CloseSmith();
}

void AWrecktideSmithStall::OnSmithUserNotorietyLevelUp(int32 NewLevel)
{
	static_cast<void>(NewLevel);
	RefreshMenu();
}

void AWrecktideSmithStall::BindMenuInput()
{
	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-WIRING Smith stall input component missing"));
		return;
	}

	if (InputComponent->KeyBindings.Num() > 0)
	{
		return;
	}

	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AWrecktideSmithStall::SelectPrevious).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AWrecktideSmithStall::SelectNext).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWrecktideSmithStall::ConfirmMenuSelection).bConsumeInput = true;
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideSmithStall::CloseSmithMenu).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWrecktideSmithStall::BackOrClose).bConsumeInput = true;
}

void AWrecktideSmithStall::OpenSmithMenu(ACombatCharacter* Character)
{
	const bool bInsideTrigger = Character && Trigger
		&& FVector::DistSquared(Character->GetActorLocation(), Trigger->GetComponentLocation())
			<= FMath::Square(Trigger->GetScaledSphereRadius());
	if (!Character || (!IsUserInRange(Character) && !Trigger->IsOverlappingActor(Character) && !bInsideTrigger))
	{
		return;
	}

	// One modal at a time (the shop stall's rule, 2026-09-04): every one of these draws through the
	// same HUD line list and binds the same Up/Down/Enter keys, so a second one opening would leave
	// the first invisible but still holding the keys.
	for (TActorIterator<AWrecktideShopStall> It(GetWorld()); It; ++It)
	{
		if (It->IsShopMenuOpen())
		{
			return;
		}
	}

	for (TActorIterator<AWrecktideQuestBoard> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return;
		}
	}

	for (TActorIterator<AWrecktideMissionGiver> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return;
		}
	}

	if (!SmithUser.IsValid())
	{
		SetSmithUser(Character);
	}
	if (bMenuOpen)
	{
		return;
	}

	bMenuOpen = true;
	Page = ESmithMenuPage::Weapons;
	OpenWeaponRow = NAME_None;
	SelectedIndex = 0;
	RefreshMenu();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-WIRING Smith stall could not find PlayerController"));
		bMenuOpen = false;
		return;
	}
	EnableInput(PlayerController);
	BindMenuInput();
}

void AWrecktideSmithStall::ToggleSmithMenu(ACombatCharacter* Character)
{
	if (bMenuOpen && IsUserInRange(Character))
	{
		CloseSmithMenu();
	}
	else
	{
		OpenSmithMenu(Character);
	}
}

void AWrecktideSmithStall::MoveMenuSelection(int32 Direction)
{
	if (!bMenuOpen || MenuRows.Num() == 0 || Direction == 0)
	{
		return;
	}
	SelectedIndex = (SelectedIndex + (Direction > 0 ? 1 : -1) + MenuRows.Num()) % MenuRows.Num();
	PushMenuToHUD();
}

bool AWrecktideSmithStall::OpenWeaponTracks(FName WeaponRow)
{
	if (WeaponRow.IsNone())
	{
		return false;
	}

	Page = ESmithMenuPage::Tracks;
	OpenWeaponRow = WeaponRow;
	SelectedIndex = 1;
	RefreshMenu();
	return true;
}

void AWrecktideSmithStall::ConfirmMenuSelection()
{
	if (!bMenuOpen || !MenuRows.IsValidIndex(SelectedIndex))
	{
		return;
	}

	const FName Row = MenuRows[SelectedIndex];
	if (Row.IsNone())
	{
		// Close on the weapon list, one page back from the tracks.
		BackOrClose();
		return;
	}

	if (Page == ESmithMenuPage::Weapons)
	{
		OpenWeaponTracks(Row);
		return;
	}

	BuyUpgrade(OpenWeaponRow, Row);
}

bool AWrecktideSmithStall::IsUserInRange(const ACombatCharacter* Character) const
{
	return Character != nullptr && SmithUser.Get() == Character;
}

AWrecktidePlayerState* AWrecktideSmithStall::GetUserPlayerState() const
{
	ACombatCharacter* User = SmithUser.Get();
	return User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
}

void AWrecktideSmithStall::SetSmithUser(ACombatCharacter* Character)
{
	SmithUser = Character;
	Character->Stats->OnOutOfHealth.AddUniqueDynamic(this, &AWrecktideSmithStall::OnSmithUserDied);
	if (AWrecktidePlayerState* PlayerState = Character->GetPlayerState<AWrecktidePlayerState>())
	{
		if (USkillComponent* Skill = PlayerState->GetSkill())
		{
			Skill->OnNotorietyLevelUp.AddUniqueDynamic(this, &AWrecktideSmithStall::OnSmithUserNotorietyLevelUp);
		}
	}
}

void AWrecktideSmithStall::SetSmithMenuOpen(bool bOpen, bool bShowRangeHint)
{
	if (bMenuOpen == bOpen)
	{
		return;
	}

	bMenuOpen = bOpen;
	if (!bOpen)
	{
		Page = ESmithMenuPage::Weapons;
		OpenWeaponRow = NAME_None;
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
			{
				HUD->ClearShopMenu();
			}
			DisableInput(PlayerController);
		}
		if (bShowRangeHint)
		{
			ShowToast(TEXT("Press F for the smithy"));
		}
	}
}

bool AWrecktideSmithStall::BuyUpgrade(FName WeaponRow, FName TrackRow)
{
	ACombatCharacter* User = SmithUser.Get();
	if (!User || !User->Stats || !User->Stats->IsAlive())
	{
		UE_LOG(LogTemp, Verbose, TEXT("WRECKTIDE-SMITH-DEADBUY"));
		return false;
	}

	// Progress holds the tier record, so a sale without it would charge gold for an upgrade the
	// player could never own. Refuse before any gold moves (the shop stall's wiring gate).
	AWrecktidePlayerState* PlayerState = User->GetPlayerState<AWrecktidePlayerState>();
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	if (!PlayerState || !Economy || !Progress)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-WIRING Smith user missing Wrecktide PlayerState, Economy or Progress; sale refused"));
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FUpgradeTrackTableRow* TrackData = DataSubsystem ? DataSubsystem->GetUpgradeRow(TrackRow) : nullptr;
	const FWeaponTableRow* WeaponData = DataSubsystem ? DataSubsystem->GetWeaponRow(WeaponRow) : nullptr;
	if (!TrackData || !WeaponData)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-SMITH-ROWMISS weapon=%s track=%s"), *WeaponRow.ToString(), *TrackRow.ToString());
		ShowToast(TEXT("Nothing to forge."));
		return false;
	}

	const int32 CurrentTier = Progress->GetUpgradeTier(WeaponRow, TrackRow);
	const int32 NextTier = CurrentTier + 1;
	if (NextTier > WRECKTIDE_UPGRADE_MAX_TIER)
	{
		ShowToast(FString::Printf(TEXT("%s is already at its best."), *TrackData->DisplayName.ToString()));
		return false;
	}

	// Level gate FIRST, then gold: a refused sale must never move a coin. Rare parts for tiers 4 and
	// 5 (research brief) wait on the loot pass; gold and notoriety are the only gates today.
	const int32 GateLevel = TrackData->GetTierLevel(NextTier);
	const int32 PlayerLevel = PlayerState->GetGatingLevel();
	if (PlayerLevel < GateLevel)
	{
		ShowToast(FString::Printf(TEXT("Lv %d needed"), GateLevel));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH-LEVELLOCK weapon=%s track=%s tier=%d notoriety=%d needed=%d sale=refused"),
			*WeaponRow.ToString(), *TrackRow.ToString(), NextTier, PlayerLevel, GateLevel);
		return false;
	}

	const int64 Cost = TrackData->GetTierCost(NextTier);
	const FName LedgerContext(*FString::Printf(TEXT("%s.%s.T%d"), *WeaponRow.ToString(), *TrackRow.ToString(), NextTier));
	if (Cost > 0 && !Economy->ApplyGoldDelta(-Cost, EGoldSource::Purchased, EGoldReason::UpgradePurchase, LedgerContext))
	{
		ShowToast(TEXT("Not enough gold."));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH-NOGOLD weapon=%s track=%s tier=%d cost=%lld gold=%lld"),
			*WeaponRow.ToString(), *TrackRow.ToString(), NextTier, Cost, Economy->GetGoldTotal());
		return false;
	}

	if (!Progress->SetUpgradeTier(WeaponRow, TrackRow, NextTier))
	{
		// Refund on the same terms the shop refunds a failed equip: the player must never pay for
		// nothing.
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-GRANTFAIL weapon=%s track=%s tier=%d"),
			*WeaponRow.ToString(), *TrackRow.ToString(), NextTier);
		if (Cost > 0)
		{
			Economy->ApplyGoldDelta(Cost, EGoldSource::Earned, EGoldReason::Adjustment, LedgerContext);
		}
		return false;
	}

	ShowToast(FString::Printf(TEXT("%s tier %d!"), *TrackData->DisplayName.ToString(), NextTier));
	RefreshMenu();

	// A smithy purchase is an Upgrade objective (C23.2). Fired AFTER the tier is banked and the gold
	// has moved, so a refused buy can never count.
	WrecktideMissionLogic::NotifyUpgradePurchased(PlayerState);

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SMITH-BUY weapon=%s track=%s tier=%d cost=%lld goldAfter=%lld"),
		*WeaponRow.ToString(), *TrackRow.ToString(), NextTier, Cost, Economy->GetGoldTotal());
	return true;
}

void AWrecktideSmithStall::CloseSmith()
{
	SetSmithMenuOpen(false, false);
	ACombatCharacter* User = SmithUser.Get();
	if (User)
	{
		if (User->Stats)
		{
			User->Stats->OnOutOfHealth.RemoveDynamic(this, &AWrecktideSmithStall::OnSmithUserDied);
		}

		if (AWrecktidePlayerState* PlayerState = User->GetPlayerState<AWrecktidePlayerState>())
		{
			if (USkillComponent* Skill = PlayerState->GetSkill())
			{
				Skill->OnNotorietyLevelUp.RemoveDynamic(this, &AWrecktideSmithStall::OnSmithUserNotorietyLevelUp);
			}
		}
	}

	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}

	MenuRows.Reset();
	MenuLines.Reset();
	SmithUser.Reset();
}

void AWrecktideSmithStall::RefreshMenu()
{
	AWrecktidePlayerState* PlayerState = GetUserPlayerState();
	if (!PlayerState)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-WIRING Smith menu missing Wrecktide PlayerState"));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const UProgressComponent* Progress = PlayerState->GetProgress();
	const UEconomyComponent* Economy = PlayerState->GetEconomy();
	if (!DataSubsystem || !Progress || !Economy)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SMITH-WIRING Smith menu missing data subsystem, progress or economy"));
		return;
	}

	MenuRows.Reset();
	MenuLines.Reset();

	const TArray<TPair<FName, const FUpgradeTrackTableRow*>> Tracks = DataSubsystem->GetUpgradeRows();

	if (Page == ESmithMenuPage::Tracks && !OpenWeaponRow.IsNone())
	{
		const FWeaponTableRow* WeaponData = DataSubsystem->GetWeaponRow(OpenWeaponRow);
		const FString WeaponName = WeaponData ? WeaponData->DisplayName.ToString() : OpenWeaponRow.ToString();

		// Row 0 is the way back AND the header: the panel has no title line of its own.
		MenuRows.Add(NAME_None);
		MenuLines.Add(FString::Printf(TEXT("< %s   (back)"), *WeaponName));

		const int32 PlayerLevel = PlayerState->GetGatingLevel();
		for (const TPair<FName, const FUpgradeTrackTableRow*>& Entry : Tracks)
		{
			const FUpgradeTrackTableRow* TrackData = Entry.Value;
			const int32 Tier = Progress->GetUpgradeTier(OpenWeaponRow, Entry.Key);
			const int32 NextTier = Tier + 1;

			FString Line;
			if (NextTier > WRECKTIDE_UPGRADE_MAX_TIER)
			{
				Line = FString::Printf(TEXT("%s  %d/%d  MAX"),
					*TrackData->DisplayName.ToString(), Tier, WRECKTIDE_UPGRADE_MAX_TIER);
			}
			else
			{
				const int32 GateLevel = TrackData->GetTierLevel(NextTier);
				const FString Marker = PlayerLevel < GateLevel
					? FString::Printf(TEXT("  Lv %d needed"), GateLevel) : FString();
				Line = FString::Printf(TEXT("%s  %d/%d  next %lldg  (Lv %d)%s"),
					*TrackData->DisplayName.ToString(), Tier, WRECKTIDE_UPGRADE_MAX_TIER,
					TrackData->GetTierCost(NextTier), GateLevel, *Marker);
			}

			MenuRows.Add(Entry.Key);
			MenuLines.Add(Line);
		}
	}
	else
	{
		// Owned weapons. HasPurchasedRow reads the persistent unlocked set; the equipped row is
		// included whatever its provenance, so a weapon granted rather than bought is still forgeable.
		const int32 MaxTiers = Tracks.Num() * WRECKTIDE_UPGRADE_MAX_TIER;
		for (const TPair<FName, const FWeaponTableRow*>& Entry : DataSubsystem->GetWeaponRows())
		{
			const FName RowName = Entry.Key;
			const FWeaponTableRow* WeaponData = Entry.Value;
			if (!WeaponData || (!Economy->HasPurchasedRow(RowName) && PlayerState->EquippedWeaponRow != RowName))
			{
				continue;
			}

			int32 OwnedTiers = 0;
			for (const TPair<FName, const FUpgradeTrackTableRow*>& Track : Tracks)
			{
				OwnedTiers += Progress->GetUpgradeTier(RowName, Track.Key);
			}

			MenuRows.Add(RowName);
			MenuLines.Add(FString::Printf(TEXT("%s   %.0f dmg   %d/%d tiers%s"),
				*WeaponData->DisplayName.ToString(), WeaponData->BaseDamage, OwnedTiers, MaxTiers,
				PlayerState->EquippedWeaponRow == RowName ? TEXT("   EQUIPPED") : TEXT("")));
		}

		if (MenuRows.Num() == 0)
		{
			MenuRows.Add(NAME_None);
			MenuLines.Add(TEXT("No weapons yet. The market sells them."));
		}

		// Always offered, so the menu is never a dead end (the bounty board's rule).
		MenuRows.Add(NAME_None);
		MenuLines.Add(TEXT("Close"));
	}

	SelectedIndex = MenuRows.Num() > 0 ? FMath::Clamp(SelectedIndex, 0, MenuRows.Num() - 1) : 0;
	if (bMenuOpen)
	{
		PushMenuToHUD();
	}
}

void AWrecktideSmithStall::PushMenuToHUD() const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->SetShopMenu(MenuLines, SelectedIndex);
	}
}

void AWrecktideSmithStall::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}
