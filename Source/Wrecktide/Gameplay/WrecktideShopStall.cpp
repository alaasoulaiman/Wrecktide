// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideShopStall.h"

#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

AWrecktideShopStall::AWrecktideShopStall()
{
	PrimaryActorTick.bCanEverTick = false;

	StallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StallMesh"));
	RootComponent = StallMesh;

	// Synty market building (2026-09-04); the engine cube stays as the fallback so a missing pack
	// still yields a usable, if ugly, stall
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShopMesh(TEXT("/Game/PolygonPirates/Meshes/Buildings/SM_Bld_Shop_01.SM_Bld_Shop_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (ShopMesh.Succeeded())
	{
		StallMesh->SetStaticMesh(ShopMesh.Object);
	}
	else if (CubeMesh.Succeeded())
	{
		StallMesh->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicMaterial.Succeeded() && !ShopMesh.Succeeded())
	{
		StallMesh->SetMaterial(0, BasicMaterial.Object);
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(300.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	ShopSign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ShopSign"));
	ShopSign->SetupAttachment(RootComponent);
	ShopSign->SetHorizontalAlignment(EHTA_Center);
	ShopSign->SetVerticalAlignment(EVRTA_TextCenter);
	ShopSign->SetTextRenderColor(FColor(240, 201, 109));
	ShopSign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AWrecktideShopStall::BeginPlay()
{
	Super::BeginPlay();
	ShopSign->SetText(FText::FromString(SignText));
	ShopSign->SetWorldSize(SignLetterSize);
	ShopSign->SetRelativeLocationAndRotation(SignLocation, SignRotation);
	ShopSign->SetVisibility(!SignText.IsEmpty());

	Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideShopStall::OnTriggerBeginOverlap);
	Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideShopStall::OnTriggerEndOverlap);
}

void AWrecktideShopStall::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseShop();

	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideShopStall::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideShopStall::OnTriggerEndOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void AWrecktideShopStall::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	if (!Character || !Character->ActorHasTag(FName("Player")) || !Character->Stats || !Character->Stats->IsAlive())
	{
		return;
	}

	if (ShopUser.IsValid())
	{
		return;
	}

	SetShopUser(Character);
	ShowToast(StallClass == EWrecktideStallClass::Shooting
		? FString::Printf(TEXT("Press F: %s"), *PromptName) : FString(TEXT("Press F to shop")));
}

void AWrecktideShopStall::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor == ShopUser.Get())
	{
		CloseShop();
	}
}

void AWrecktideShopStall::SelectPrevious()
{
	MoveMenuSelection(-1);
}

void AWrecktideShopStall::SelectNext()
{
	MoveMenuSelection(1);
}

void AWrecktideShopStall::CloseShopMenu()
{
	SetShopMenuOpen(false, true);
}

void AWrecktideShopStall::OnShopUserDied(AActor* InstigatorActor)
{
	CloseShop();
}

void AWrecktideShopStall::OnShopUserNotorietyLevelUp(int32 NewLevel)
{
	static_cast<void>(NewLevel);
	RefreshShopOffers();
}

void AWrecktideShopStall::BindMenuInput()
{
	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING Shop stall input component missing"));
		return;
	}

	if (InputComponent->KeyBindings.Num() > 0)
	{
		return;
	}

	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AWrecktideShopStall::SelectPrevious).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AWrecktideShopStall::SelectNext).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWrecktideShopStall::ConfirmMenuSelection).bConsumeInput = true;
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideShopStall::CloseShopMenu).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWrecktideShopStall::CloseShopMenu).bConsumeInput = true;
}

void AWrecktideShopStall::OpenShopMenu(ACombatCharacter* Character)
{
	const bool bInsideTrigger = Character && Trigger
		&& FVector::DistSquared(Character->GetActorLocation(), Trigger->GetComponentLocation())
			<= FMath::Square(Trigger->GetScaledSphereRadius());
	if (!Character || (!IsUserInRange(Character) && !Trigger->IsOverlappingActor(Character) && !bInsideTrigger))
	{
		return;
	}

	// One modal at a time (bounty board, 2026-09-04). Both menus draw through the same HUD line
	// list and both bind Up/Down/Enter, so a second one opening would leave the first invisible
	// but still holding the keys.
	for (TActorIterator<AWrecktideQuestBoard> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return;
		}
	}

	// The quest giver is the third modal on the same list and the same keys (missions v0).
	for (TActorIterator<AWrecktideMissionGiver> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return;
		}
	}

	// The blacksmith is the fourth (spec C10, 2026-09-06).
	for (TActorIterator<AWrecktideSmithStall> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return;
		}
	}

	if (!ShopUser.IsValid())
	{
		SetShopUser(Character);
	}
	if (bShopMenuOpen)
	{
		return;
	}

	bShopMenuOpen = true;
	SelectedOfferIndex = 0;
	RefreshShopOffers();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING Shop stall could not find PlayerController"));
		bShopMenuOpen = false;
		return;
	}
	EnableInput(PlayerController);
	BindMenuInput();
}

void AWrecktideShopStall::ToggleShopMenu(ACombatCharacter* Character)
{
	if (bShopMenuOpen && IsUserInRange(Character))
	{
		CloseShopMenu();
	}
	else
	{
		OpenShopMenu(Character);
	}
}

void AWrecktideShopStall::MoveMenuSelection(int32 Direction)
{
	if (!bShopMenuOpen || OfferRows.Num() == 0 || Direction == 0)
	{
		return;
	}
	SelectedOfferIndex = (SelectedOfferIndex + (Direction > 0 ? 1 : -1) + OfferRows.Num()) % OfferRows.Num();
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AWrecktideHUD* HUD = Cast<AWrecktideHUD>(PlayerController->GetHUD()))
		{
			HUD->SetShopMenu(OfferLines, SelectedOfferIndex);
		}
	}
}

void AWrecktideShopStall::ConfirmMenuSelection()
{
	if (bShopMenuOpen)
	{
		Buy(SelectedOfferIndex);
	}
}

bool AWrecktideShopStall::IsUserInRange(const ACombatCharacter* Character) const
{
	return Character != nullptr && ShopUser.Get() == Character;
}

void AWrecktideShopStall::SetShopUser(ACombatCharacter* Character)
{
	ShopUser = Character;
	Character->Stats->OnOutOfHealth.AddUniqueDynamic(this, &AWrecktideShopStall::OnShopUserDied);
	if (AWrecktidePlayerState* PlayerState = Character->GetPlayerState<AWrecktidePlayerState>())
	{
		if (USkillComponent* Skill = PlayerState->GetSkill())
		{
			Skill->OnNotorietyLevelUp.AddUniqueDynamic(this, &AWrecktideShopStall::OnShopUserNotorietyLevelUp);
		}
	}
}

void AWrecktideShopStall::SetShopMenuOpen(bool bOpen, bool bShowRangeHint)
{
	if (bShopMenuOpen == bOpen)
	{
		return;
	}

	bShopMenuOpen = bOpen;
	if (!bOpen)
	{
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
			ShowToast(StallClass == EWrecktideStallClass::Shooting
				? FString::Printf(TEXT("Press F: %s"), *PromptName) : FString(TEXT("Press F to shop")));
		}
	}
}

void AWrecktideShopStall::Buy(int32 Index)
{
	ACombatCharacter* User = ShopUser.Get();
	if (!User || !User->Stats || !User->Stats->IsAlive())
	{
		UE_LOG(LogTemp, Verbose, TEXT("WRECKTIDE-FUNGATE-DEADBUY"));
		return;
	}

	if (!OfferRows.IsValidIndex(Index))
	{
		return;
	}

	// Progress is part of the wiring gate: it holds the ownership record, so a sale with no Progress
	// would charge gold for a weapon the player could never own again (code review 2026-08-22,
	// finding 3). Refuse the sale before any gold moves.
	AWrecktidePlayerState* PlayerState = User->GetPlayerState<AWrecktidePlayerState>();
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	if (!PlayerState || !Economy || !Progress)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING Shop user missing Wrecktide PlayerState, Economy or Progress; sale refused"));
		return;
	}

	const FName RowName = OfferRows[Index];
	if (Economy->HasPurchasedRow(RowName))
	{
		if (PlayerState->EquippedWeaponRow == RowName)
		{
			ShowToast(TEXT("Already equipped."));
		}
		else
		{
			PlayerState->EquipWeapon(RowName);
			ShowToast(TEXT("Equipped owned weapon."));
			RefreshShopOffers();
		}
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FWeaponTableRow* WeaponRow = DataSubsystem ? DataSubsystem->GetWeaponRow(RowName) : nullptr;
	if (!WeaponRow)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-FUNGATE-ROWMISS %s"), *RowName.ToString());
		ShowToast(TEXT("Offer unavailable."));
		return;
	}

	if (!PlayerState->CanEquipWeapon(RowName))
	{
		const int32 NotorietyLevel = PlayerState->GetSkill() ? PlayerState->GetSkill()->GetNotorietyLevel() : 1;
		ShowToast(FString::Printf(TEXT("Lv %d needed"), WeaponRow->RequiredLevel));
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-LEVELLOCK row=%s notoriety=%d needed=%d sale=refused"),
			*RowName.ToString(), NotorietyLevel, WeaponRow->RequiredLevel);
		return;
	}

	const int64 Price = WeaponRow->Price;
	if (Price > 0 && !Economy->ApplyGoldDelta(-Price, EGoldSource::Purchased, EGoldReason::ShopPurchase, RowName))
	{
		ShowToast(TEXT("Not enough gold."));
		return;
	}

	if (!PlayerState->EquipWeapon(RowName))
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-GRANTFAIL %s"), *RowName.ToString());
		if (Price > 0)
		{
			Economy->ApplyGoldDelta(Price, EGoldSource::Earned, EGoldReason::Adjustment, RowName);
		}
		return;
	}

	// Record ownership OUTSIDE the price branch: a Price==0 promo weapon must be owned too. This is
	// the persistent write that HasPurchasedRow reads; the gold delta above only pays for it.
	// Progress was checked in the wiring gate above, before any gold moved.
	Progress->UnlockItem(RowName);

	ShowToast(FString::Printf(TEXT("Bought %s!"), *WeaponRow->DisplayName.ToString()));
	RefreshShopOffers();

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-FUNGATE-BUY row=%s price=%lld goldAfter=%lld"),
		*RowName.ToString(),
		Price,
		Economy->GetGoldTotal());
}

void AWrecktideShopStall::CloseShop()
{
	SetShopMenuOpen(false, false);
	ACombatCharacter* User = ShopUser.Get();
	if (User)
	{
		if (User->Stats)
		{
			User->Stats->OnOutOfHealth.RemoveDynamic(this, &AWrecktideShopStall::OnShopUserDied);
		}

		if (AWrecktidePlayerState* PlayerState = User->GetPlayerState<AWrecktidePlayerState>())
		{
			if (USkillComponent* Skill = PlayerState->GetSkill())
			{
				Skill->OnNotorietyLevelUp.RemoveDynamic(this, &AWrecktideShopStall::OnShopUserNotorietyLevelUp);
			}
		}
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (PlayerController)
	{
		DisableInput(PlayerController);
	}

	OfferRows.Reset();
	OfferLines.Reset();
	ShopUser.Reset();
}

bool AWrecktideShopStall::AcceptsWeaponClass(EWrecktideSkill CombatClass) const
{
	switch (StallClass)
	{
	case EWrecktideStallClass::None: return true;
	case EWrecktideStallClass::Melee: return CombatClass == EWrecktideSkill::Melee;
	case EWrecktideStallClass::Shooting: return CombatClass == EWrecktideSkill::Shooting;
	case EWrecktideStallClass::Voodoo: return CombatClass == EWrecktideSkill::Voodoo;
	default: return false;
	}
}

void AWrecktideShopStall::RefreshShopOffers()
{
	ACombatCharacter* User = ShopUser.Get();
	AWrecktidePlayerState* PlayerState = User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	if (!PlayerState)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING Shop offers missing Wrecktide PlayerState"));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!DataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-FUNGATE-WIRING Shop offers missing data subsystem"));
		return;
	}

	OfferRows.Reset();
	OfferLines.Reset();
	const UEconomyComponent* Economy = PlayerState->GetEconomy();
	const TArray<TPair<FName, const FWeaponTableRow*>> ShopRows = DataSubsystem->GetWeaponRows();
	for (int32 Index = 0; Index < ShopRows.Num(); ++Index)
	{
		const FName RowName = ShopRows[Index].Key;
		const FWeaponTableRow* WeaponRow = ShopRows[Index].Value;
		if (!WeaponRow || !WeaponRow->bPurchasableWithGold || !AcceptsWeaponClass(WeaponRow->CombatClass))
		{
			continue;
		}

		OfferRows.Add(RowName);

		FString Marker;
		if (Economy && Economy->HasPurchasedRow(RowName))
		{
			Marker = PlayerState->EquippedWeaponRow == RowName ? TEXT("  OWNED  EQUIPPED") : TEXT("  OWNED");
		}
		else if (!PlayerState->CanEquipWeapon(RowName))
		{
			Marker = FString::Printf(TEXT("  Lv %d needed"), WeaponRow->RequiredLevel);
		}

		OfferLines.Add(FString::Printf(TEXT("%s  %lldg  (Notoriety Lv %d)%s"),
			*WeaponRow->DisplayName.ToString(),
			WeaponRow->Price,
			WeaponRow->RequiredLevel,
			*Marker));
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	SelectedOfferIndex = OfferRows.Num() > 0 ? FMath::Clamp(SelectedOfferIndex, 0, OfferRows.Num() - 1) : 0;
	if (bShopMenuOpen)
	{
		if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
		{
			HUD->SetShopMenu(OfferLines, SelectedOfferIndex);
		}
	}
}

void AWrecktideShopStall::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}
