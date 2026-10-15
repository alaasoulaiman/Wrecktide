// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideTreasureChest.h"
#include "Systems/WrecktideAudio.h"

#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/EconomyComponent.h"
#include "Systems/ProgressComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

namespace
{
/** The player's progress component through the one player controller, or null when unwired. */
UProgressComponent* PlayerProgress(const UObject* WorldContext)
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(WorldContext, 0);
	AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	return PlayerState ? PlayerState->GetProgress() : nullptr;
}
}

AWrecktideTreasureChest::AWrecktideTreasureChest()
{
	PrimaryActorTick.bCanEverTick = false;

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
	RootComponent = ChestMesh;
	// Synty chest body; the engine cube is the fallback so a missing pack costs looks, not gold
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ChestAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Chest_01.SM_Prop_Chest_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (ChestAsset.Succeeded())
	{
		ChestMesh->SetStaticMesh(ChestAsset.Object);
	}
	else if (CubeMesh.Succeeded())
	{
		ChestMesh->SetStaticMesh(CubeMesh.Object);
		ChestMesh->SetRelativeScale3D(FVector(0.8f, 0.5f, 0.5f));
	}

	TreasureMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TreasureMesh"));
	TreasureMesh->SetupAttachment(ChestMesh);
	TreasureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TreasureAsset(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Chest_Treasure_01.SM_Prop_Chest_Treasure_01"));
	if (TreasureAsset.Succeeded())
	{
		TreasureMesh->SetStaticMesh(TreasureAsset.Object);
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(ChestMesh);
	Trigger->SetUsingAbsoluteScale(true);
	Trigger->SetSphereRadius(260.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetCanEverAffectNavigation(false);
}

void AWrecktideTreasureChest::BeginPlay()
{
	Super::BeginPlay();
	ResolveGoldAmount();
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideTreasureChest::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideTreasureChest::OnTriggerEndOverlap);
	}
	// the PlayerState's save load runs in its own BeginPlay (possibly after this one); the lid is
	// re-evaluated again on the first overlap, so a late load still reads right by the time it matters
	RefreshLid();
}

void AWrecktideTreasureChest::ResolveGoldAmount()
{
	ResolvedGoldAmount = GoldAmount;
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (const FChestTableRow* Row = Data ? Data->GetChestRow(ChestId) : nullptr)
	{
		ResolvedGoldAmount = FMath::Max<int64>(Row->Gold, 0);
		return;
	}

	if (!bLoggedMissingChestRow)
	{
		bLoggedMissingChestRow = true;
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-CHEST-DATA no Chests.csv row for %s; using level fallback %lld"),
			*ChestId.ToString(), GoldAmount);
	}
}

void AWrecktideTreasureChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideTreasureChest::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideTreasureChest::OnTriggerEndOverlap);
	}
	Super::EndPlay(EndPlayReason);
}

FName AWrecktideTreasureChest::FlagKey() const
{
	return FName(*FString::Printf(TEXT("Chest_%s"), *ChestId.ToString()));
}

bool AWrecktideTreasureChest::IsOpened() const
{
	const UProgressComponent* Progress = PlayerProgress(this);
	return Progress != nullptr && Progress->GetQuestFlag(FlagKey()) != 0;
}

bool AWrecktideTreasureChest::IsCharacterInRange(const ACombatCharacter* Character) const
{
	if (!Character || !Trigger)
	{
		return false;
	}
	return FVector::DistSquared(Character->GetActorLocation(), Trigger->GetComponentLocation()) <= FMath::Square(Trigger->GetScaledSphereRadius());
}

void AWrecktideTreasureChest::RefreshLid()
{
	if (TreasureMesh)
	{
		TreasureMesh->SetVisibility(!IsOpened());
	}
}

void AWrecktideTreasureChest::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	// the ONE player controller, never Character->GetController(): a freshly respawned pawn fires its
	// first overlap before it is possessed, and a dying pawn is detached before EndOverlap (audit 2026-09-04)
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!Character || !PlayerController || !Character->ActorHasTag(FName("Player")))
	{
		return;
	}
	RefreshLid();
	if (IsOpened())
	{
		return;
	}
	NearbyCharacter = Character;
	EnableInput(PlayerController);
	if (InputComponent && InputComponent->KeyBindings.Num() == 0)
	{
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideTreasureChest::OnOpenKey).bConsumeInput = true;
	}
	ShowToast(TEXT("Press F to open the chest"));
}

void AWrecktideTreasureChest::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor != NearbyCharacter.Get())
	{
		return;
	}
	// unconditional: the pawn leaving may be dying (controller already null), and a leaked input
	// component on the controller's stack would swallow F for the rest of the session
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}
	NearbyCharacter = nullptr;
}

void AWrecktideTreasureChest::OnOpenKey()
{
	Open(NearbyCharacter.Get());
}

bool AWrecktideTreasureChest::Open(ACombatCharacter* Character)
{
	if (!Character || !IsCharacterInRange(Character))
	{
		return false;
	}
	if (Character->Stats && !Character->Stats->IsAlive())
	{
		return false;
	}
	AWrecktidePlayerState* PlayerState = Character->GetPlayerState<AWrecktidePlayerState>();
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	if (!Progress || !Economy)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-CHEST-WIRING open refused chest=%s progress=%d economy=%d"),
			*ChestId.ToString(), Progress != nullptr, Economy != nullptr);
		return false;
	}
	if (Progress->GetQuestFlag(FlagKey()) != 0)
	{
		ShowToast(TEXT("Already emptied."));
		RefreshLid();
		return false;
	}

	// flag first, then pay: a crash between the two loses one chest's gold, never pays it twice
	Progress->SetQuestFlag(FlagKey(), 1);
	UWrecktideAudio::PlaySfx(this, TEXT("chest"));
	const int64 PayoutGold = GetPayoutGold();
	if (PayoutGold > 0)
	{
		Economy->ApplyGoldDelta(PayoutGold, EGoldSource::Earned, EGoldReason::LootPickup, ChestId);
	}
	RefreshLid();
	ShowToast(FString::Printf(TEXT("Treasure! +%lld gold"), PayoutGold));
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-CHEST opened chest=%s gold=%lld"), *ChestId.ToString(), PayoutGold);

	// A story mission may ask for THIS chest (missions v0, 2026-09-04); after the payout, so a
	// mission can never advance on an open that paid nothing.
	WrecktideMissionLogic::NotifyChestOpened(PlayerState, ChestId);

	// the pirate is still standing here: drop the F binding so a second press is a no-op
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}
	NearbyCharacter = nullptr;
	return true;
}

void AWrecktideTreasureChest::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}
