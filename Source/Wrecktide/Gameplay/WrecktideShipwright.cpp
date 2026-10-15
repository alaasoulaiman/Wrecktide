// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideShipwright.h"

#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Gameplay/WrecktideBoat.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "Gameplay/WrecktideSupplyBarrel.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

AWrecktideShipwright::AWrecktideShipwright()
{
	PrimaryActorTick.bCanEverTick = false;

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

	SignMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SignMesh"));
	SignMesh->SetupAttachment(RootComponent);
	SignMesh->SetRelativeLocation(FVector(-60.0f, -120.0f, 0.0f));
	SignMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SignAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Sign_01.SM_Prop_Sign_01"));
	if (SignAsset.Succeeded())
	{
		SignMesh->SetStaticMesh(SignAsset.Object);
	}

	// A stack of timbers on the bench is what reads as a shipwright's from across the camp; the pack
	// has no shipwright props, so the plank is the closest honest stand-in.
	TimberMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TimberMesh"));
	TimberMesh->SetupAttachment(RootComponent);
	TimberMesh->SetRelativeLocation(FVector(0.0f, 90.0f, 0.0f));
	TimberMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TimberAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Crate_01.SM_Prop_Crate_01"));
	if (TimberAsset.Succeeded())
	{
		TimberMesh->SetStaticMesh(TimberAsset.Object);
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(300.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AWrecktideShipwright::BeginPlay()
{
	Super::BeginPlay();
	Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideShipwright::OnTriggerBeginOverlap);
	Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideShipwright::OnTriggerEndOverlap);
}

void AWrecktideShipwright::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseShipwright();
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideShipwright::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideShipwright::OnTriggerEndOverlap);
	}
	Super::EndPlay(EndPlayReason);
}

void AWrecktideShipwright::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	if (!Character || !Character->ActorHasTag(FName("Player")) || !Character->Stats || !Character->Stats->IsAlive() || ShipwrightUser.IsValid())
	{
		return;
	}
	ShipwrightUser = Character;
	ShowToast(TEXT("Press F for the shipwright"));
}

void AWrecktideShipwright::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor == ShipwrightUser.Get())
	{
		CloseShipwright();
	}
}

AWrecktideBoat* AWrecktideShipwright::FindBoat() const
{
	for (TActorIterator<AWrecktideBoat> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AWrecktidePlayerState* AWrecktideShipwright::GetUserPlayerState() const
{
	ACombatCharacter* User = ShipwrightUser.Get();
	return User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
}

void AWrecktideShipwright::SelectPrevious()
{
	MoveMenuSelection(-1);
}

void AWrecktideShipwright::SelectNext()
{
	MoveMenuSelection(1);
}

void AWrecktideShipwright::CloseShipwrightMenu()
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
	ShowToast(TEXT("Press F for the shipwright"));
}

void AWrecktideShipwright::BindMenuInput()
{
	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-WIRING Shipwright input component missing"));
		return;
	}
	if (InputComponent->KeyBindings.Num() > 0)
	{
		return;
	}
	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AWrecktideShipwright::SelectPrevious).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AWrecktideShipwright::SelectNext).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWrecktideShipwright::ConfirmMenuSelection).bConsumeInput = true;
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideShipwright::CloseShipwrightMenu).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWrecktideShipwright::CloseShipwrightMenu).bConsumeInput = true;
}

void AWrecktideShipwright::OpenShipwrightMenu(ACombatCharacter* Character)
{
	const bool bInsideTrigger = Character && Trigger
		&& FVector::DistSquared(Character->GetActorLocation(), Trigger->GetComponentLocation())
			<= FMath::Square(Trigger->GetScaledSphereRadius());
	if (!Character || (ShipwrightUser.Get() != Character && !Trigger->IsOverlappingActor(Character) && !bInsideTrigger))
	{
		return;
	}

	// One modal at a time (the shop stall's rule): they all draw through the same HUD line list.
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
	for (TActorIterator<AWrecktideSupplyBarrel> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return;
		}
	}

	if (!ShipwrightUser.IsValid())
	{
		ShipwrightUser = Character;
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
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-WIRING Shipwright could not find PlayerController"));
		bMenuOpen = false;
		return;
	}
	EnableInput(PlayerController);
	BindMenuInput();
}

void AWrecktideShipwright::ToggleShipwrightMenu(ACombatCharacter* Character)
{
	if (bMenuOpen && ShipwrightUser.Get() == Character)
	{
		CloseShipwrightMenu();
	}
	else
	{
		OpenShipwrightMenu(Character);
	}
}

void AWrecktideShipwright::MoveMenuSelection(int32 Direction)
{
	if (!bMenuOpen || MenuRows.Num() == 0 || Direction == 0)
	{
		return;
	}
	SelectedIndex = (SelectedIndex + (Direction > 0 ? 1 : -1) + MenuRows.Num()) % MenuRows.Num();
	PushMenuToHUD();
}

void AWrecktideShipwright::ConfirmMenuSelection()
{
	if (!bMenuOpen || !MenuRows.IsValidIndex(SelectedIndex))
	{
		return;
	}
	const FName Row = MenuRows[SelectedIndex];
	if (Row.IsNone())
	{
		CloseShipwrightMenu();
		return;
	}
	BuyUpgrade(Row);
}

bool AWrecktideShipwright::BuyUpgrade(FName TrackRow)
{
	ACombatCharacter* User = ShipwrightUser.Get();
	AWrecktidePlayerState* PlayerState = GetUserPlayerState();
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	if (!User || !Economy || !Progress)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-WIRING Sale refused: user=%d economy=%d progress=%d"),
			User != nullptr, Economy != nullptr, Progress != nullptr);
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FBoatUpgradeTableRow* TrackData = DataSubsystem ? DataSubsystem->GetBoatUpgradeRow(TrackRow) : nullptr;
	if (!TrackData)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-SHIPWRIGHT-ROWMISS track=%s"), *TrackRow.ToString());
		ShowToast(TEXT("Nothing to fit."));
		return false;
	}

	const int32 CurrentTier = Progress->GetBoatUpgradeTier(TrackRow);
	const int32 NextTier = CurrentTier + 1;
	if (NextTier > WrecktideSea::MaxUpgradeTier)
	{
		ShowToast(FString::Printf(TEXT("%s is already at its best."), *TrackData->DisplayName.ToString()));
		return false;
	}

	// Notoriety gate FIRST, then gold: a refused sale must never move a coin (the smithy's rule).
	const int32 GateLevel = TrackData->GetTierLevel(NextTier);
	const int32 PlayerLevel = PlayerState->GetGatingLevel();
	if (PlayerLevel < GateLevel)
	{
		ShowToast(FString::Printf(TEXT("Notoriety %d needed"), GateLevel));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-LEVELLOCK track=%s tier=%d notoriety=%d needed=%d sale=refused"),
			*TrackRow.ToString(), NextTier, PlayerLevel, GateLevel);
		return false;
	}

	const int64 Cost = TrackData->GetTierCost(NextTier);
	const FName LedgerContext(*FString::Printf(TEXT("Boat.%s.T%d"), *TrackRow.ToString(), NextTier));
	if (Cost > 0 && !Economy->ApplyGoldDelta(-Cost, EGoldSource::Purchased, EGoldReason::ShipPurchase, LedgerContext))
	{
		ShowToast(TEXT("Not enough gold."));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-NOGOLD track=%s tier=%d cost=%lld gold=%lld"),
			*TrackRow.ToString(), NextTier, Cost, Economy->GetGoldTotal());
		return false;
	}

	if (!Progress->SetBoatUpgradeTier(TrackRow, NextTier))
	{
		// Refund on the same terms the smithy refunds a failed grant: never pay for nothing.
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-GRANTFAIL track=%s tier=%d"), *TrackRow.ToString(), NextTier);
		if (Cost > 0)
		{
			Economy->ApplyGoldDelta(Cost, EGoldSource::Earned, EGoldReason::Adjustment, LedgerContext);
		}
		return false;
	}

	ShowToast(FString::Printf(TEXT("%s tier %d!"), *TrackData->DisplayName.ToString(), NextTier));
	RefreshMenu();
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-SHIPWRIGHT-BUY track=%s tier=%d cost=%lld goldAfter=%lld"),
		*TrackRow.ToString(), NextTier, Cost, Economy->GetGoldTotal());
	return true;
}

void AWrecktideShipwright::CloseShipwright()
{
	CloseShipwrightMenu();
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}
	MenuRows.Reset();
	MenuLines.Reset();
	ShipwrightUser.Reset();
}

void AWrecktideShipwright::RefreshMenu()
{
	const AWrecktidePlayerState* PlayerState = GetUserPlayerState();
	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!Progress || !Economy || !DataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-SHIPWRIGHT-WIRING Menu missing progress, economy or data subsystem"));
		return;
	}

	MenuRows.Reset();
	MenuLines.Reset();

	const int32 PlayerLevel = PlayerState->GetGatingLevel();
	for (const TPair<FName, const FBoatUpgradeTableRow*>& Entry : DataSubsystem->GetBoatUpgradeRows())
	{
		const FBoatUpgradeTableRow* TrackData = Entry.Value;
		const int32 Tier = Progress->GetBoatUpgradeTier(Entry.Key);
		const int32 NextTier = Tier + 1;

		FString Line;
		if (NextTier > WrecktideSea::MaxUpgradeTier)
		{
			Line = FString::Printf(TEXT("%s  %d/%d  MAX   %s"),
				*TrackData->DisplayName.ToString(), Tier, WrecktideSea::MaxUpgradeTier,
				*TrackData->Description.ToString());
		}
		else
		{
			const int32 GateLevel = TrackData->GetTierLevel(NextTier);
			const FString Marker = PlayerLevel < GateLevel
				? FString::Printf(TEXT("  Notoriety %d needed"), GateLevel) : FString();
			Line = FString::Printf(TEXT("%s  %d/%d  next %lldg   %s%s"),
				*TrackData->DisplayName.ToString(), Tier, WrecktideSea::MaxUpgradeTier,
				TrackData->GetTierCost(NextTier), *TrackData->Description.ToString(), *Marker);
		}

		MenuRows.Add(Entry.Key);
		MenuLines.Add(Line);
	}

	if (MenuRows.Num() == 0)
	{
		MenuRows.Add(NAME_None);
		MenuLines.Add(TEXT("The shipwright has nothing on the bench."));
	}

	// Always offered, so the menu is never a dead end (the bounty board's rule).
	MenuRows.Add(NAME_None);
	MenuLines.Add(FString::Printf(TEXT("Gold %lld   (Close)"), Economy->GetGoldTotal()));

	SelectedIndex = FMath::Clamp(SelectedIndex, 0, MenuRows.Num() - 1);
	if (bMenuOpen)
	{
		PushMenuToHUD();
	}
}

void AWrecktideShipwright::PushMenuToHUD() const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->SetShopMenu(MenuLines, SelectedIndex);
	}
}

void AWrecktideShipwright::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}
