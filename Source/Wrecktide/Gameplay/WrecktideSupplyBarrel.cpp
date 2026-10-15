// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideSupplyBarrel.h"

#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Gameplay/WrecktideBoat.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/EconomyComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

AWrecktideSupplyBarrel::AWrecktideSupplyBarrel()
{
	PrimaryActorTick.bCanEverTick = false;

	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	RootComponent = BarrelMesh;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BarrelAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Barrel_01.SM_Prop_Barrel_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (BarrelAsset.Succeeded())
	{
		BarrelMesh->SetStaticMesh(BarrelAsset.Object);
	}
	else if (CubeMesh.Succeeded())
	{
		// The shop stall's rule: a missing pack still yields a usable, if ugly, prop.
		BarrelMesh->SetStaticMesh(CubeMesh.Object);
	}

	CrateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrateMesh"));
	CrateMesh->SetupAttachment(RootComponent);
	CrateMesh->SetRelativeLocation(FVector(90.0f, 30.0f, 0.0f));
	CrateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CrateAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Crate_01.SM_Prop_Crate_01"));
	if (CrateAsset.Succeeded())
	{
		CrateMesh->SetStaticMesh(CrateAsset.Object);
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(300.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AWrecktideSupplyBarrel::BeginPlay()
{
	Super::BeginPlay();
	Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideSupplyBarrel::OnTriggerBeginOverlap);
	Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideSupplyBarrel::OnTriggerEndOverlap);
}

void AWrecktideSupplyBarrel::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseBarrel();
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideSupplyBarrel::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideSupplyBarrel::OnTriggerEndOverlap);
	}
	Super::EndPlay(EndPlayReason);
}

void AWrecktideSupplyBarrel::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	if (!Character || !Character->ActorHasTag(FName("Player")) || !Character->Stats || !Character->Stats->IsAlive() || BarrelUser.IsValid())
	{
		return;
	}
	BarrelUser = Character;
	ShowToast(TEXT("Press F for supplies"));
}

void AWrecktideSupplyBarrel::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor == BarrelUser.Get())
	{
		CloseBarrel();
	}
}

AWrecktideBoat* AWrecktideSupplyBarrel::FindBoat() const
{
	for (TActorIterator<AWrecktideBoat> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AWrecktidePlayerState* AWrecktideSupplyBarrel::GetUserPlayerState() const
{
	ACombatCharacter* User = BarrelUser.Get();
	return User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
}

void AWrecktideSupplyBarrel::SelectPrevious()
{
	MoveMenuSelection(-1);
}

void AWrecktideSupplyBarrel::SelectNext()
{
	MoveMenuSelection(1);
}

void AWrecktideSupplyBarrel::CloseBarrelMenu()
{
	if (!bMenuOpen)
	{
		return;
	}
	bMenuOpen = false;
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
		{
			HUD->ClearShopMenu();
		}
		DisableInput(PlayerController);
	}
	ShowToast(TEXT("Press F for supplies"));
}

void AWrecktideSupplyBarrel::BindMenuInput()
{
	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD-WIRING Supply barrel input component missing"));
		return;
	}
	if (InputComponent->KeyBindings.Num() > 0)
	{
		return;
	}
	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AWrecktideSupplyBarrel::SelectPrevious).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AWrecktideSupplyBarrel::SelectNext).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWrecktideSupplyBarrel::ConfirmMenuSelection).bConsumeInput = true;
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideSupplyBarrel::CloseBarrelMenu).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWrecktideSupplyBarrel::CloseBarrelMenu).bConsumeInput = true;
}

void AWrecktideSupplyBarrel::OpenBarrelMenu(ACombatCharacter* Character)
{
	const bool bInsideTrigger = Character && Trigger
		&& FVector::DistSquared(Character->GetActorLocation(), Trigger->GetComponentLocation())
			<= FMath::Square(Trigger->GetScaledSphereRadius());
	if (!Character || (BarrelUser.Get() != Character && !Trigger->IsOverlappingActor(Character) && !bInsideTrigger))
	{
		return;
	}

	// One modal at a time (the shop stall's rule): every one of these draws through the same HUD line
	// list and binds the same keys, so a second one would leave the first invisible but still bound.
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
	for (TActorIterator<AWrecktideSmithStall> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return;
		}
	}

	if (!BarrelUser.IsValid())
	{
		BarrelUser = Character;
	}
	if (bMenuOpen)
	{
		return;
	}

	bMenuOpen = true;
	SelectedIndex = 0;
	RefreshMenu();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD-WIRING Supply barrel could not find PlayerController"));
		bMenuOpen = false;
		return;
	}
	EnableInput(PlayerController);
	BindMenuInput();
}

void AWrecktideSupplyBarrel::ToggleBarrelMenu(ACombatCharacter* Character)
{
	if (bMenuOpen && BarrelUser.Get() == Character)
	{
		CloseBarrelMenu();
	}
	else
	{
		OpenBarrelMenu(Character);
	}
}

void AWrecktideSupplyBarrel::MoveMenuSelection(int32 Direction)
{
	if (!bMenuOpen || MenuRows.Num() == 0 || Direction == 0)
	{
		return;
	}
	SelectedIndex = (SelectedIndex + (Direction > 0 ? 1 : -1) + MenuRows.Num()) % MenuRows.Num();
	PushMenuToHUD();
}

void AWrecktideSupplyBarrel::ConfirmMenuSelection()
{
	if (!bMenuOpen || !MenuRows.IsValidIndex(SelectedIndex))
	{
		return;
	}
	const FName Row = MenuRows[SelectedIndex];
	if (Row.IsNone())
	{
		CloseBarrelMenu();
		return;
	}
	BuySupply(Row);
}

bool AWrecktideSupplyBarrel::BuySupply(FName SupplyRow)
{
	ACombatCharacter* User = BarrelUser.Get();
	AWrecktidePlayerState* PlayerState = GetUserPlayerState();
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	AWrecktideBoat* Boat = FindBoat();
	if (!User || !Economy || !Boat)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD-WIRING Supply sale refused: user=%d economy=%d boat=%d"),
			User != nullptr, Economy != nullptr, Boat != nullptr);
		return false;
	}

	int64 Cost = 0;
	int32 Units = 0;
	if (SupplyRow == WrecktideSea::SupplyPlanks)
	{
		Cost = PlankCost * PlankLotUnits;
		Units = PlankLotUnits;
	}
	else if (SupplyRow == WrecktideSea::SupplyShot)
	{
		Cost = ShotLotCost;
		Units = ShotLotUnits;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-HOLD-ROWMISS supply=%s"), *SupplyRow.ToString());
		return false;
	}

	// Room FIRST, then gold: a refused sale must never move a coin (the smithy's rule).
	if (Boat->GetHoldRoomFor(SupplyRow) < Units)
	{
		ShowToast(TEXT("The hold is full."));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD-NOROOM supply=%s want=%d room=%d slots=%d/%d"),
			*SupplyRow.ToString(), Units, Boat->GetHoldRoomFor(SupplyRow), Boat->GetHoldSlotsUsed(), Boat->GetHoldSlots());
		return false;
	}

	const FName LedgerContext(*FString::Printf(TEXT("Supplies.%s"), *SupplyRow.ToString()));
	if (Cost > 0 && !Economy->ApplyGoldDelta(-Cost, EGoldSource::Purchased, EGoldReason::ShopPurchase, LedgerContext))
	{
		ShowToast(TEXT("Not enough gold."));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD-NOGOLD supply=%s cost=%lld gold=%lld"),
			*SupplyRow.ToString(), Cost, Economy->GetGoldTotal());
		return false;
	}

	const int32 Stowed = Boat->AddToHold(SupplyRow, Units);
	if (Stowed <= 0)
	{
		// Refund on the same terms the smithy refunds a failed grant: never pay for nothing.
		if (Cost > 0)
		{
			Economy->ApplyGoldDelta(Cost, EGoldSource::Earned, EGoldReason::Adjustment, LedgerContext);
		}
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD-STOWFAIL supply=%s units=%d refunded=%lld"), *SupplyRow.ToString(), Units, Cost);
		return false;
	}

	ShowToast(FString::Printf(TEXT("%d %s aboard"), Stowed, *SupplyRow.ToString()));
	RefreshMenu();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-HOLD-BUY supply=%s units=%d cost=%lld goldAfter=%lld slots=%d/%d"),
		*SupplyRow.ToString(), Stowed, Cost, Economy->GetGoldTotal(), Boat->GetHoldSlotsUsed(), Boat->GetHoldSlots());
	return true;
}

void AWrecktideSupplyBarrel::CloseBarrel()
{
	CloseBarrelMenu();
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}
	MenuRows.Reset();
	MenuLines.Reset();
	BarrelUser.Reset();
}

void AWrecktideSupplyBarrel::RefreshMenu()
{
	const AWrecktidePlayerState* PlayerState = GetUserPlayerState();
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	const AWrecktideBoat* Boat = FindBoat();
	if (!Economy || !Boat)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-HOLD-WIRING Supply menu missing economy or boat"));
		return;
	}

	MenuRows.Reset();
	MenuLines.Reset();

	MenuRows.Add(WrecktideSea::SupplyPlanks);
	MenuLines.Add(FString::Printf(TEXT("Planks   %lldg each   patch %d hull   aboard %d"),
		PlankCost, Boat->PlankRepairAmount, Boat->GetHoldUnits(WrecktideSea::SupplyPlanks)));

	MenuRows.Add(WrecktideSea::SupplyShot);
	MenuLines.Add(FString::Printf(TEXT("Shot     %lldg for %d   for the guns to come   aboard %d"),
		ShotLotCost, ShotLotUnits, Boat->GetHoldUnits(WrecktideSea::SupplyShot)));

	MenuRows.Add(NAME_None);
	MenuLines.Add(FString::Printf(TEXT("Hold %d/%d slots   Gold %lld   (Close)"),
		Boat->GetHoldSlotsUsed(), Boat->GetHoldSlots(), Economy->GetGoldTotal()));

	SelectedIndex = FMath::Clamp(SelectedIndex, 0, MenuRows.Num() - 1);
	if (bMenuOpen)
	{
		PushMenuToHUD();
	}
}

void AWrecktideSupplyBarrel::PushMenuToHUD() const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->SetShopMenu(MenuLines, SelectedIndex);
	}
}

void AWrecktideSupplyBarrel::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}
