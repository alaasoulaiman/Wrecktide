// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideQuestBoard.h"
#include "Systems/WrecktideAudio.h"

#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Systems/ProgressComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktidePlayerState.h"
#include "Systems/WrecktideQuestLogic.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

AWrecktideQuestBoard::AWrecktideQuestBoard()
{
	PrimaryActorTick.bCanEverTick = false;

	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	RootComponent = BoardMesh;

	// The Synty sign reads as a notice post on the island; the engine cube is the fallback so a
	// missing content pack costs the player a pretty mesh, never the quest loop itself.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SignMesh(TEXT("/Game/PolygonPirates/Meshes/Props/SM_Prop_Sign_Attachment_01.SM_Prop_Sign_Attachment_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (SignMesh.Succeeded())
	{
		BoardMesh->SetStaticMesh(SignMesh.Object);
		BoardMesh->SetRelativeScale3D(FVector(2.0f, 2.0f, 2.0f));
	}
	else if (CubeMesh.Succeeded())
	{
		// 100uu engine cube scaled to a 60 x 20 x 200 notice post.
		BoardMesh->SetStaticMesh(CubeMesh.Object);
		BoardMesh->SetRelativeScale3D(FVector(0.6f, 0.2f, 2.0f));
		if (BasicMaterial.Succeeded())
		{
			BoardMesh->SetMaterial(0, BasicMaterial.Object);
		}
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	// Absolute scale: the mesh above is scaled to look right, and without this the trigger would be
	// scaled with it, so the "300uu" interact range would silently become 600.
	Trigger->SetUsingAbsoluteScale(true);
	Trigger->SetSphereRadius(300.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AWrecktideQuestBoard::BeginPlay()
{
	Super::BeginPlay();

	Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideQuestBoard::OnTriggerBeginOverlap);
	Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideQuestBoard::OnTriggerEndOverlap);
}

void AWrecktideQuestBoard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseBoard();

	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideQuestBoard::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideQuestBoard::OnTriggerEndOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void AWrecktideQuestBoard::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	if (!Character || !Character->ActorHasTag(FName("Player")) || !Character->Stats || !Character->Stats->IsAlive())
	{
		return;
	}

	if (BoardUser.IsValid())
	{
		return;
	}

	SetBoardUser(Character);
	ShowToast(TEXT("Press F for bounties"));
}

void AWrecktideQuestBoard::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor == BoardUser.Get())
	{
		CloseBoard();
	}
}

void AWrecktideQuestBoard::SelectPrevious()
{
	MoveMenuSelection(-1);
}

void AWrecktideQuestBoard::SelectNext()
{
	MoveMenuSelection(1);
}

void AWrecktideQuestBoard::CloseBoardMenu()
{
	SetBoardMenuOpen(false, true);
}

void AWrecktideQuestBoard::OnBoardUserDied(AActor* InstigatorActor)
{
	CloseBoard();
}

void AWrecktideQuestBoard::BindMenuInput()
{
	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING Quest board input component missing"));
		return;
	}

	if (InputComponent->KeyBindings.Num() > 0)
	{
		return;
	}

	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AWrecktideQuestBoard::SelectPrevious).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AWrecktideQuestBoard::SelectNext).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWrecktideQuestBoard::ConfirmMenuSelection).bConsumeInput = true;
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideQuestBoard::CloseBoardMenu).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWrecktideQuestBoard::CloseBoardMenu).bConsumeInput = true;
}

bool AWrecktideQuestBoard::IsOtherMenuOpen() const
{
	for (TActorIterator<AWrecktideShopStall> It(GetWorld()); It; ++It)
	{
		if (It->IsShopMenuOpen())
		{
			return true;
		}
	}

	// The quest giver is the third modal on this HUD line list (missions v0, 2026-09-04).
	for (TActorIterator<AWrecktideMissionGiver> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return true;
		}
	}

	return false;
}

void AWrecktideQuestBoard::OpenBoardMenu(ACombatCharacter* Character)
{
	const bool bInsideTrigger = Character && Trigger
		&& FVector::DistSquared(Character->GetActorLocation(), Trigger->GetComponentLocation())
			<= FMath::Square(Trigger->GetScaledSphereRadius());
	if (!Character || (!IsUserInRange(Character) && !Trigger->IsOverlappingActor(Character) && !bInsideTrigger))
	{
		return;
	}

	// One modal at a time. Both menus draw through the same HUD line list and both grab Up/Down/
	// Enter, so a second one opening would leave the first invisible but still bound to the keys.
	if (IsOtherMenuOpen())
	{
		return;
	}

	if (!BoardUser.IsValid())
	{
		SetBoardUser(Character);
	}
	if (bMenuOpen)
	{
		return;
	}

	bMenuOpen = true;
	SelectedIndex = 0;
	RefreshBoardMenu();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING Quest board could not find PlayerController"));
		bMenuOpen = false;
		return;
	}
	EnableInput(PlayerController);
	BindMenuInput();
}

void AWrecktideQuestBoard::ToggleBoardMenu(ACombatCharacter* Character)
{
	if (bMenuOpen && IsUserInRange(Character))
	{
		CloseBoardMenu();
	}
	else
	{
		OpenBoardMenu(Character);
	}
}

void AWrecktideQuestBoard::MoveMenuSelection(int32 Direction)
{
	if (!bMenuOpen || MenuLines.Num() == 0 || Direction == 0)
	{
		return;
	}

	SelectedIndex = (SelectedIndex + (Direction > 0 ? 1 : -1) + MenuLines.Num()) % MenuLines.Num();
	PushMenuToHUD();
}

bool AWrecktideQuestBoard::SelectQuestRow(FName Row)
{
	const int32 Index = MenuRows.IndexOfByKey(Row);
	if (!MenuLines.IsValidIndex(Index))
	{
		return false;
	}

	SelectedIndex = Index;
	PushMenuToHUD();
	return true;
}

void AWrecktideQuestBoard::ConfirmMenuSelection()
{
	if (!bMenuOpen || !MenuRows.IsValidIndex(SelectedIndex))
	{
		return;
	}

	const FName Row = MenuRows[SelectedIndex];
	if (Row.IsNone())
	{
		// The trailing Close line.
		CloseBoardMenu();
		return;
	}

	ACombatCharacter* User = BoardUser.Get();
	AWrecktidePlayerState* PlayerState = User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FQuestTableRow* QuestRow = DataSubsystem ? DataSubsystem->GetQuestRow(Row) : nullptr;
	if (!PlayerState || !Progress || !DataSubsystem || !QuestRow)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING Board action refused row=%s playerstate=%d progress=%d data=%d"),
			*Row.ToString(), PlayerState != nullptr, Progress != nullptr, DataSubsystem != nullptr);
		ShowToast(TEXT("Bounty unavailable."));
		return;
	}

	const int32 State = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(Row));
	if (State == WrecktideQuestLogic::QuestState_Completed)
	{
		ShowToast(TEXT("Already done."));
	}
	else if (State == WrecktideQuestLogic::QuestState_Active)
	{
		if (WrecktideQuestLogic::IsReadyToTurnIn(Progress, DataSubsystem, Row))
		{
			if (WrecktideQuestLogic::TurnIn(PlayerState, Row))
			{
				UWrecktideAudio::PlaySfx(this, TEXT("fanfare"));
				ShowToast(FString::Printf(TEXT("Bounty paid: +%lld gold, +%lld %s XP"),
					QuestRow->RewardGold, QuestRow->RewardXP, *WrecktideQuestLogic::SkillDisplayName(QuestRow->RewardSkill)));
			}
			else
			{
				ShowToast(TEXT("Bounty unavailable."));
			}
		}
		else
		{
			const int32 Kills = Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(Row));
			ShowToast(FString::Printf(TEXT("%d/%d. Keep hunting."), Kills, FMath::Max(QuestRow->TargetCount, 1)));
		}
	}
	else if (!WrecktideQuestLogic::GetActiveQuest(Progress, DataSubsystem).IsNone())
	{
		ShowToast(TEXT("Finish your current bounty first."));
	}
	else if (WrecktideQuestLogic::Accept(PlayerState, Row))
	{
		UWrecktideAudio::PlaySfx(this, TEXT("accept"));
		ShowToast(FString::Printf(TEXT("Bounty accepted: %s"), *QuestRow->DisplayName.ToString()));
	}
	else
	{
		ShowToast(TEXT("Bounty unavailable."));
	}

	// Every action changes at least one marker, so the list is redrawn unconditionally.
	RefreshBoardMenu();
}

bool AWrecktideQuestBoard::IsUserInRange(const ACombatCharacter* Character) const
{
	return Character != nullptr && BoardUser.Get() == Character;
}

void AWrecktideQuestBoard::SetBoardUser(ACombatCharacter* Character)
{
	BoardUser = Character;
	Character->Stats->OnOutOfHealth.AddUniqueDynamic(this, &AWrecktideQuestBoard::OnBoardUserDied);
}

void AWrecktideQuestBoard::SetBoardMenuOpen(bool bOpen, bool bShowRangeHint)
{
	if (bMenuOpen == bOpen)
	{
		return;
	}

	bMenuOpen = bOpen;
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
			ShowToast(TEXT("Press F for bounties"));
		}
	}
}

void AWrecktideQuestBoard::CloseBoard()
{
	SetBoardMenuOpen(false, false);

	if (ACombatCharacter* User = BoardUser.Get())
	{
		if (User->Stats)
		{
			User->Stats->OnOutOfHealth.RemoveDynamic(this, &AWrecktideQuestBoard::OnBoardUserDied);
		}
	}

	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}

	MenuRows.Reset();
	MenuLines.Reset();
	BoardUser.Reset();
}

void AWrecktideQuestBoard::RefreshBoardMenu()
{
	ACombatCharacter* User = BoardUser.Get();
	AWrecktidePlayerState* PlayerState = User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	MenuRows.Reset();
	MenuLines.Reset();

	if (!Progress || !DataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-QUEST-WIRING Board menu missing progress=%d data=%d"),
			Progress != nullptr, DataSubsystem != nullptr);
	}
	else
	{
		for (const TPair<FName, const FQuestTableRow*>& Entry : DataSubsystem->GetQuestRows())
		{
			const FQuestTableRow* QuestRow = Entry.Value;
			if (!QuestRow)
			{
				continue;
			}

			// The gate lives in WrecktideQuestLogic, so what a board DRAWS and what it will ACCEPT
			// can never drift apart. An island's bounties stay off every other island's board.
			if (!WrecktideQuestLogic::IsQuestOffered(Progress, DataSubsystem, Entry.Key))
			{
				continue;
			}

			const int32 State = Progress->GetQuestFlag(WrecktideQuestLogic::StateKey(Entry.Key));
			const FString Name = QuestRow->DisplayName.ToString();
			const FString Reward = FString::Printf(TEXT("(%lld gold, %lld %s XP)"),
				QuestRow->RewardGold, QuestRow->RewardXP, *WrecktideQuestLogic::SkillDisplayName(QuestRow->RewardSkill));

			FString Line;
			if (State == WrecktideQuestLogic::QuestState_Completed)
			{
				Line = FString::Printf(TEXT("[DONE] %s"), *Name);
			}
			else if (State == WrecktideQuestLogic::QuestState_Active)
			{
				const int32 Target = FMath::Max(QuestRow->TargetCount, 1);
				const int32 Kills = FMath::Min(Progress->GetQuestFlag(WrecktideQuestLogic::KillsKey(Entry.Key)), Target);
				Line = Kills >= Target
					? FString::Printf(TEXT("[TURN IN] %s  %s"), *Name, *Reward)
					: FString::Printf(TEXT("[ACTIVE %d/%d] %s  %s"), Kills, Target, *Name, *Reward);
			}
			else
			{
				Line = FString::Printf(TEXT("%s  %s"), *Name, *Reward);
			}

			MenuRows.Add(Entry.Key);
			MenuLines.Add(Line);
		}
	}

	// Always offered, even with an empty or missing table, so the menu is never a dead end.
	MenuRows.Add(NAME_None);
	MenuLines.Add(TEXT("Close"));

	SelectedIndex = FMath::Clamp(SelectedIndex, 0, MenuLines.Num() - 1);
	if (bMenuOpen)
	{
		PushMenuToHUD();
	}
}

void AWrecktideQuestBoard::PushMenuToHUD() const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->SetShopMenu(MenuLines, SelectedIndex);
	}
}

void AWrecktideQuestBoard::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}
