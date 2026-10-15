// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Gameplay/WrecktideMissionGiver.h"
#include "Systems/WrecktideAudio.h"

#include "Components/InputComponent.h"
#include "Animation/AnimInstance.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WrecktideDataRows.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Systems/ProgressComponent.h"
#include "Systems/EconomyComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Combat/CombatCharacter.h"

AWrecktideMissionGiver::AWrecktideMissionGiver()
{
	PrimaryActorTick.bCanEverTick = false;

	DriverMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("DriverMesh"));
	RootComponent = DriverMesh;
	DriverMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DriverMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mannequin(TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (Mannequin.Succeeded())
	{
		DriverMesh->SetSkeletalMesh(Mannequin.Object);
	}
	static ConstructorHelpers::FClassFinder<UAnimInstance> RetargetABP(TEXT("/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget"));
	if (RetargetABP.Succeeded())
	{
		RetargetAnimClass = RetargetABP.Class;
	}
	static ConstructorHelpers::FObjectFinder<UObject> RetargeterAsset(TEXT("/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin.RTG_UEFN_to_UE4_Mannequin"));
	if (RetargeterAsset.Succeeded())
	{
		Retargeter = RetargeterAsset.Object;
	}

	GiverMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("GiverMesh"));
	GiverMesh->SetupAttachment(DriverMesh);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> FirstMate(TEXT("/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_First_Mate_01.SK_Chr_Pirate_First_Mate_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleFinder(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop"));
	if (IdleFinder.Succeeded())
	{
		IdleAnimation = IdleFinder.Object;
	}
	// C21 (2026-09-06): every NPC skin is found HERE, because ConstructorHelpers only resolves in a
	// constructor. ConfigureGiver then swaps GiverMesh to one of these before FinishSpawning.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SeamanSkin(TEXT("/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_Seaman_01.SK_Chr_Pirate_Seaman_01"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> WenchSkin(TEXT("/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Female_Wench_01.SK_Chr_Female_Wench_01"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> GovernorSkin(TEXT("/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_English_Govenor_02.SK_Chr_English_Govenor_02"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SergeantSkin(TEXT("/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_English_Soldier_01.SK_Chr_English_Soldier_01"));
	GiverSkins.SetNum(static_cast<int32>(EWrecktideGiverSkin::Count));
	GiverSkins[static_cast<int32>(EWrecktideGiverSkin::FirstMate)] = FirstMate.Succeeded() ? FirstMate.Object : nullptr;
	GiverSkins[static_cast<int32>(EWrecktideGiverSkin::Seaman)] = SeamanSkin.Succeeded() ? SeamanSkin.Object : nullptr;
	GiverSkins[static_cast<int32>(EWrecktideGiverSkin::Wench)] = WenchSkin.Succeeded() ? WenchSkin.Object : nullptr;
	GiverSkins[static_cast<int32>(EWrecktideGiverSkin::Governor)] = GovernorSkin.Succeeded() ? GovernorSkin.Object : nullptr;
	GiverSkins[static_cast<int32>(EWrecktideGiverSkin::Sergeant)] = SergeantSkin.Succeeded() ? SergeantSkin.Object : nullptr;

	if (FirstMate.Succeeded())
	{
		GiverMesh->SetSkeletalMesh(FirstMate.Object);
	}
	// No collision on the NPC himself: he is spawned at runtime in the middle of the camp, and a
	// solid body there would both trap the pirate against the stall and carve a hole in the island
	// navmesh the enemies path on.
	GiverMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GiverMesh->SetCanEverAffectNavigation(false);

	FallbackMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FallbackMesh"));
	FallbackMesh->SetupAttachment(RootComponent);
	FallbackMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackMesh->SetCanEverAffectNavigation(false);
	if (CubeMesh.Succeeded())
	{
		// 100uu engine cube scaled to a 60 x 60 x 180 person-sized marker, standing on the ground.
		FallbackMesh->SetStaticMesh(CubeMesh.Object);
		FallbackMesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.8f));
		FallbackMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
		if (BasicMaterial.Succeeded())
		{
			FallbackMesh->SetMaterial(0, BasicMaterial.Object);
		}
	}
	FallbackMesh->SetVisibility(!FirstMate.Succeeded());

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	// Absolute scale, the board's rule: a scaled mesh must never silently double the interact range.
	Trigger->SetUsingAbsoluteScale(true);
	Trigger->SetSphereRadius(300.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetCanEverAffectNavigation(false);
}

void AWrecktideMissionGiver::ConfigureGiver(FName InGiverId, const FString& InPlateName, const FString& InPromptName, EWrecktideGiverSkin Skin, bool bInOpensSmithy)
{
	GiverId = InGiverId.IsNone() ? GiverId : InGiverId;
	if (!InPlateName.IsEmpty())
	{
		PlateName = InPlateName;
	}
	if (!InPromptName.IsEmpty())
	{
		PromptName = InPromptName;
	}
	bOpensSmithy = bInOpensSmithy;

	const int32 SkinIndex = static_cast<int32>(Skin);
	if (GiverMesh && GiverSkins.IsValidIndex(SkinIndex) && GiverSkins[SkinIndex])
	{
		GiverMesh->SetSkeletalMesh(GiverSkins[SkinIndex]);
	}
	else
	{
		// Fail LOUD but not fatal: a missing skin leaves the first mate standing there with the
		// right name and the right chain, which is worth far more than a hole in the camp.
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MISSION-GIVER skin %d missing for %s; keeping the default skin"),
			SkinIndex, *GiverId.ToString());
	}

	if (FallbackMesh)
	{
		FallbackMesh->SetVisibility(GiverMesh == nullptr || GiverMesh->GetSkeletalMeshAsset() == nullptr);
	}
}

FVector AWrecktideMissionGiver::GetPlateWorldLocation() const
{
	// The Synty skins have a foot pivot and stand about 180 uu tall, so the plate sits just clear of
	// the hat. One number, here, so the plate and the quest glyph can never drift apart.
	return GetActorLocation() + FVector(0.0f, 0.0f, 210.0f);
}

FString AWrecktideMissionGiver::GetQuestSignal() const
{
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	const AWrecktidePlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	return WrecktideMissionLogic::GetGiverSignal(Progress, Data, GiverId);
}

void AWrecktideMissionGiver::BeginPlay()
{
	Super::BeginPlay();

	// A quest giver in the bind pose reads as broken (2026-09-05 capture). The hidden driver plays the
	// mannequin's stand idle and the skin retargets from it, exactly like the enemies and the player.
	if (DriverMesh && DriverMesh->GetSkeletalMeshAsset() && IdleAnimation)
	{
		DriverMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		DriverMesh->PlayAnimation(IdleAnimation, true);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MISSION-GIVER driver idle not playing mesh=%d anim=%d"), DriverMesh && DriverMesh->GetSkeletalMeshAsset() ? 1 : 0, IdleAnimation ? 1 : 0);
	}
	ApplySkinRetarget();

	Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AWrecktideMissionGiver::OnTriggerBeginOverlap);
	Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AWrecktideMissionGiver::OnTriggerEndOverlap);
}

void AWrecktideMissionGiver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseGiver();

	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AWrecktideMissionGiver::OnTriggerBeginOverlap);
		Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AWrecktideMissionGiver::OnTriggerEndOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void AWrecktideMissionGiver::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(OtherActor);
	if (!Character || !Character->ActorHasTag(FName("Player")) || !Character->Stats || !Character->Stats->IsAlive())
	{
		return;
	}

	if (GiverUser.IsValid())
	{
		return;
	}

	SetGiverUser(Character);
	ShowToast(FString::Printf(TEXT("Press F to speak to %s"), *PromptName));
}

void AWrecktideMissionGiver::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor == GiverUser.Get())
	{
		CloseGiver();
	}
}

void AWrecktideMissionGiver::SelectPrevious()
{
	MoveMenuSelection(-1);
}

void AWrecktideMissionGiver::SelectNext()
{
	MoveMenuSelection(1);
}

void AWrecktideMissionGiver::CloseGiverMenu()
{
	SetGiverMenuOpen(false, true);
}

void AWrecktideMissionGiver::OnGiverUserDied(AActor* InstigatorActor)
{
	CloseGiver();
}

void AWrecktideMissionGiver::BindMenuInput()
{
	if (!InputComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING Mission giver input component missing"));
		return;
	}

	if (InputComponent->KeyBindings.Num() > 0)
	{
		return;
	}

	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AWrecktideMissionGiver::SelectPrevious).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AWrecktideMissionGiver::SelectNext).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWrecktideMissionGiver::ConfirmMenuSelection).bConsumeInput = true;
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AWrecktideMissionGiver::CloseGiverMenu).bConsumeInput = true;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWrecktideMissionGiver::CloseGiverMenu).bConsumeInput = true;
}

bool AWrecktideMissionGiver::IsOtherMenuOpen() const
{
	for (TActorIterator<AWrecktideShopStall> It(GetWorld()); It; ++It)
	{
		if (It->IsShopMenuOpen())
		{
			return true;
		}
	}

	for (TActorIterator<AWrecktideQuestBoard> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return true;
		}
	}

	// C22 (2026-09-06): there are FOUR of these actors now, and the smithy shares the same HUD line
	// list and the same Up/Down/Enter keys, so both join the one-modal-at-a-time rule.
	for (TActorIterator<AWrecktideMissionGiver> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->IsMenuOpen())
		{
			return true;
		}
	}

	for (TActorIterator<AWrecktideSmithStall> It(GetWorld()); It; ++It)
	{
		if (It->IsMenuOpen())
		{
			return true;
		}
	}

	return false;
}

void AWrecktideMissionGiver::OpenGiverMenu(ACombatCharacter* Character)
{
	const bool bInsideTrigger = Character && Trigger
		&& FVector::DistSquared(Character->GetActorLocation(), Trigger->GetComponentLocation())
			<= FMath::Square(Trigger->GetScaledSphereRadius());
	if (!Character || (!IsUserInRange(Character) && !Trigger->IsOverlappingActor(Character) && !bInsideTrigger))
	{
		return;
	}

	// One modal at a time. The stall, the board and this menu all draw through the same HUD line
	// list and all three grab Up/Down/Enter, so a second one opening would leave the first invisible
	// but still bound to the keys.
	if (IsOtherMenuOpen())
	{
		return;
	}

	if (!GiverUser.IsValid())
	{
		SetGiverUser(Character);
	}
	if (bMenuOpen)
	{
		return;
	}

	bMenuOpen = true;
	SelectedIndex = 0;

	// Talking to him IS the objective of a TalkTo or a Deliver (C22/C23). Fired BEFORE the menu is
	// built so the hand-off shows its result in the same breath: M08 pays out and Jerold's own first
	// job is already on the list the player is looking at.
	if (AWrecktidePlayerState* TalkPlayerState = Character->GetPlayerState<AWrecktidePlayerState>())
	{
		WrecktideMissionLogic::NotifyTalkedTo(TalkPlayerState, GiverId);
	}

	RefreshGiverMenu();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING Mission giver could not find PlayerController"));
		bMenuOpen = false;
		return;
	}
	EnableInput(PlayerController);
	BindMenuInput();
}

void AWrecktideMissionGiver::ToggleGiverMenu(ACombatCharacter* Character)
{
	if (bMenuOpen && IsUserInRange(Character))
	{
		CloseGiverMenu();
	}
	else
	{
		OpenGiverMenu(Character);
	}
}

void AWrecktideMissionGiver::MoveMenuSelection(int32 Direction)
{
	if (!bMenuOpen || MenuLines.Num() == 0 || Direction == 0)
	{
		return;
	}

	SelectedIndex = (SelectedIndex + (Direction > 0 ? 1 : -1) + MenuLines.Num()) % MenuLines.Num();
	PushMenuToHUD();
}

bool AWrecktideMissionGiver::SelectActionLine(EMissionGiverAction Action)
{
	const int32 Index = MenuActions.IndexOfByKey(Action);
	if (!MenuLines.IsValidIndex(Index))
	{
		return false;
	}

	SelectedIndex = Index;
	PushMenuToHUD();
	return true;
}

void AWrecktideMissionGiver::ConfirmMenuSelection()
{
	if (!bMenuOpen || !MenuActions.IsValidIndex(SelectedIndex))
	{
		return;
	}

	const EMissionGiverAction Action = MenuActions[SelectedIndex];
	const FName ActionRow = MenuRows.IsValidIndex(SelectedIndex) ? MenuRows[SelectedIndex] : NAME_None;
	if (Action == EMissionGiverAction::Close)
	{
		CloseGiverMenu();
		return;
	}

	if (Action == EMissionGiverAction::Smithy)
	{
		OpenSmithyFromHere();
		return;
	}

	ACombatCharacter* User = GiverUser.Get();
	AWrecktidePlayerState* PlayerState = User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!PlayerState || !Progress || !DataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING Giver action refused playerstate=%d progress=%d data=%d"),
			PlayerState != nullptr, Progress != nullptr, DataSubsystem != nullptr);
		ShowToast(FString::Printf(TEXT("%s has nothing for you."), *PromptName));
		return;
	}

	if (Action == EMissionGiverAction::Accept)
	{
		const FMissionTableRow* OfferRow = ActionRow.IsNone() ? nullptr : DataSubsystem->GetMissionRow(ActionRow);
		if (OfferRow && WrecktideMissionLogic::AcceptFromGiver(PlayerState, GiverId))
		{
			UWrecktideAudio::PlaySfx(this, TEXT("accept"));
			PendingOutro.Reset();
			ShowToast(FString::Printf(TEXT("Mission accepted: %s"), *OfferRow->Title.ToString()));
		}
		else
		{
			ShowToast(TEXT("Not yet, Captain."));
		}
	}
	else if (Action == EMissionGiverAction::Complete)
	{
		// The outro is read BEFORE the payout: completing clears the row off his menu, and after it
		// the offer is the NEXT job, not the one just finished.
		const FMissionTableRow* MissionRow = ActionRow.IsNone() ? nullptr : DataSubsystem->GetMissionRow(ActionRow);
		const FString Outro = MissionRow ? MissionRow->Outro.ToString() : FString();
		if (WrecktideMissionLogic::CompleteRow(PlayerState, ActionRow))
		{
			// CompleteRow already toasted and pushed the reward line; this actor only shows the words.
			PendingOutro = Outro;
		}
		else
		{
			ShowToast(TEXT("That is not finished yet."));
		}
	}
	else if (Action == EMissionGiverAction::Pay)
	{
		PayForMission(ActionRow);
	}
	else
	{
		// An Info line: say the progress out loud, the board's "keep hunting" gesture.
		if (!ActionRow.IsNone() && WrecktideMissionLogic::IsMissionActive(Progress, ActionRow))
		{
			int32 Count = 0;
			int32 Target = 1;
			WrecktideMissionLogic::GetRowProgress(Progress, DataSubsystem, ActionRow, Count, Target);
			ShowToast(FString::Printf(TEXT("%d/%d. Keep at it."), Count, Target));
		}
	}

	// Every action changes at least one line, so the list is redrawn unconditionally.
	RefreshGiverMenu();
}

void AWrecktideMissionGiver::PayForMission(FName Row)
{
	ACombatCharacter* User = GiverUser.Get();
	AWrecktidePlayerState* PlayerState = User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	UEconomyComponent* Economy = PlayerState ? PlayerState->GetEconomy() : nullptr;
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	const FMissionTableRow* MissionRow = (DataSubsystem && !Row.IsNone()) ? DataSubsystem->GetMissionRow(Row) : nullptr;
	if (!PlayerState || !Economy || !MissionRow)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING Pay refused row=%s economy=%d missionRow=%d"),
			*Row.ToString(), Economy != nullptr, MissionRow != nullptr);
		return;
	}

	const int64 Price = static_cast<int64>(FMath::Max(MissionRow->TargetCount, 1));
	if (Economy->GetGoldTotal() < Price)
	{
		// Fail closed and say why: no partial payment, no flag written.
		ShowToast(FString::Printf(TEXT("You need %lld gold for that."), Price));
		return;
	}

	// Gold moves FIRST, then the objective counts: a crash between the two costs the player nothing
	// he did not pay for and leaves the job payable again.
	Economy->ApplyGoldDelta(-Price, EGoldSource::Purchased, EGoldReason::ShopPurchase, Row);
	WrecktideMissionLogic::NotifyPaid(PlayerState, Price);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-PAY row=%s giver=%s gold=-%lld"), *Row.ToString(), *GiverId.ToString(), Price);
}

void AWrecktideMissionGiver::OpenSmithyFromHere()
{
	ACombatCharacter* User = GiverUser.Get();
	if (!User)
	{
		return;
	}

	// Close first: the smithy draws through the SAME HUD line list and grabs the same keys, so the
	// two menus must never be open together (the one-modal rule the stalls already follow).
	CloseGiverMenu();

	AWrecktideSmithStall* Nearest = nullptr;
	float NearestDistanceSq = TNumericLimits<float>::Max();
	for (TActorIterator<AWrecktideSmithStall> It(GetWorld()); It; ++It)
	{
		const float DistanceSq = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (DistanceSq < NearestDistanceSq)
		{
			NearestDistanceSq = DistanceSq;
			Nearest = *It;
		}
	}

	if (!Nearest)
	{
		ShowToast(TEXT("The bench is cold today."));
		return;
	}

	Nearest->OpenSmithMenu(User);
	if (!Nearest->IsMenuOpen())
	{
		// The bench carries its own 300 uu trigger and the player can stand on Jerold's far side.
		// Say so instead of eating the keypress.
		ShowToast(TEXT("Step up to the bench and press F."));
	}
}

bool AWrecktideMissionGiver::IsUserInRange(const ACombatCharacter* Character) const
{
	return Character != nullptr && GiverUser.Get() == Character;
}

void AWrecktideMissionGiver::SetGiverUser(ACombatCharacter* Character)
{
	GiverUser = Character;
	if (Character && Character->Stats)
	{
		Character->Stats->OnOutOfHealth.AddUniqueDynamic(this, &AWrecktideMissionGiver::OnGiverUserDied);
	}
}

void AWrecktideMissionGiver::SetGiverMenuOpen(bool bOpen, bool bShowRangeHint)
{
	if (bMenuOpen == bOpen)
	{
		return;
	}

	bMenuOpen = bOpen;
	if (!bOpen)
	{
		PendingOutro.Reset();
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
			ShowToast(FString::Printf(TEXT("Press F to speak to %s"), *PromptName));
		}
	}
}

void AWrecktideMissionGiver::CloseGiver()
{
	SetGiverMenuOpen(false, false);

	if (ACombatCharacter* User = GiverUser.Get())
	{
		if (User->Stats)
		{
			User->Stats->OnOutOfHealth.RemoveDynamic(this, &AWrecktideMissionGiver::OnGiverUserDied);
		}
	}

	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		DisableInput(PlayerController);
	}

	MenuActions.Reset();
	MenuLines.Reset();
	GiverUser.Reset();
}

void AWrecktideMissionGiver::RefreshGiverMenu()
{
	ACombatCharacter* User = GiverUser.Get();
	AWrecktidePlayerState* PlayerState = User ? User->GetPlayerState<AWrecktidePlayerState>() : nullptr;
	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	UGameInstance* GameInstance = GetGameInstance();
	const UWrecktideDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;

	MenuActions.Reset();
	MenuLines.Reset();
	MenuRows.Reset();

	auto AddLine = [this](EMissionGiverAction Action, FName Row, const FString& Line)
	{
		MenuActions.Add(Action);
		MenuRows.Add(Row);
		MenuLines.Add(Line);
	};

	// C30 (2026-09-06): an NPC whose turn has not come says ONE line of his own and nothing else.
	// Hoisted out of the branch below because it also silences the smithy line: a Jerold who says
	// the bench is spoken for must not then offer the bench.
	bool bLocked = false;

	if (!Progress || !DataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("WRECKTIDE-MISSION-WIRING Giver menu missing progress=%d data=%d"),
			Progress != nullptr, DataSubsystem != nullptr);
	}
	else if (!WrecktideMissionLogic::IsGiverUnlocked(Progress, DataSubsystem, GiverId))
	{
		bLocked = true;
		const FString BrushOff = WrecktideMissionLogic::GetGiverBrushOff(DataSubsystem, GiverId);
		// A missing Npcs.csv row degrades to the camp's generic line rather than an empty menu.
		const FString Line = BrushOff.IsEmpty() ? FString(TEXT("Nothing for you just yet. Ask around the camp.")) : BrushOff;
		AddLine(EMissionGiverAction::Info, NAME_None, Line);
		UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-GIVER brushoff id=%s line=%s"), *GiverId.ToString(), *Line);
	}
	else
	{
		// The outro of the mission just paid out, above whatever comes next.
		if (!PendingOutro.IsEmpty())
		{
			AddLine(EMissionGiverAction::Info, NAME_None, PendingOutro);
		}

		// ONE question decides this whole menu: what is THIS npc offering right now (C22.3). The
		// offer rule lives in the mission logic, so the menu, the head glyph, the map marker and the
		// journal can never say three different things.
		const FName Offer = WrecktideMissionLogic::GetOfferForGiver(Progress, DataSubsystem, GiverId);
		const FMissionTableRow* OfferRow = Offer.IsNone() ? nullptr : DataSubsystem->GetMissionRow(Offer);
		if (OfferRow && WrecktideMissionLogic::IsMissionActive(Progress, Offer))
		{
			int32 Count = 0;
			int32 Target = 1;
			WrecktideMissionLogic::GetRowProgress(Progress, DataSubsystem, Offer, Count, Target);
			if (WrecktideMissionLogic::IsRowReadyToComplete(Progress, DataSubsystem, Offer))
			{
				AddLine(EMissionGiverAction::Complete, Offer, FString::Printf(TEXT("[COMPLETE] %s  (%lld gold, %lld XP)"),
					*OfferRow->Title.ToString(),
					WrecktideMissionLogic::GetRewardGold(*OfferRow, PlayerState),
					WrecktideMissionLogic::GetRewardXP(*OfferRow, PlayerState)));
			}
			else if (OfferRow->Type == EMissionObjective::Pay)
			{
				// A Pay job is done AT this menu, so the payment is the line rather than a reminder.
				AddLine(EMissionGiverAction::Pay, Offer, FString::Printf(TEXT("[PAY] %lld gold  %s"),
					static_cast<int64>(FMath::Max(OfferRow->TargetCount, 1)), *OfferRow->Title.ToString()));
			}
			else
			{
				AddLine(EMissionGiverAction::Info, Offer, FString::Printf(TEXT("%s (%d/%d)"), *OfferRow->Objective.ToString(), Count, Target));
			}
		}
		else if (OfferRow)
		{
			AddLine(EMissionGiverAction::Accept, Offer, FString::Printf(TEXT("[ACCEPT] %s"), *OfferRow->Title.ToString()));
			AddLine(EMissionGiverAction::Info, Offer, OfferRow->Intro.ToString());
		}
		else if (WrecktideMissionLogic::IsChainComplete(Progress, DataSubsystem))
		{
			// Story over and every template of his still cooling down.
			AddLine(EMissionGiverAction::Info, NAME_None, TEXT("The cove is yours, Captain. Rest a while."));
		}
		else
		{
			// Somebody else holds the thread of the story right now.
			AddLine(EMissionGiverAction::Info, NAME_None, TEXT("Nothing for you just yet. Ask around the camp."));
		}
	}

	// Jerold works the bench, so his menu is the way into the forge (C21). One F, one Enter.
	// Not while he is still locked (C30): the bench opens when he does.
	if (bOpensSmithy && !bLocked)
	{
		AddLine(EMissionGiverAction::Smithy, NAME_None, TEXT("[SMITHY] Put your steel on the bench"));
	}

	// Always offered, even with an empty or missing table, so the menu is never a dead end.
	AddLine(EMissionGiverAction::Close, NAME_None, TEXT("Close"));

	SelectedIndex = FMath::Clamp(SelectedIndex, 0, MenuLines.Num() - 1);
	if (bMenuOpen)
	{
		PushMenuToHUD();
	}
}

void AWrecktideMissionGiver::PushMenuToHUD() const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->SetShopMenu(MenuLines, SelectedIndex);
	}
}

void AWrecktideMissionGiver::ShowToast(const FString& Message) const
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (AWrecktideHUD* HUD = PlayerController ? Cast<AWrecktideHUD>(PlayerController->GetHUD()) : nullptr)
	{
		HUD->ShowToast(Message);
	}
}

// ---------------------------------------------------------------------------------------------
// Skin retarget (2026-09-05): mirrors ACombatEnemy::ApplyCosmeticMesh. The skin's ABP reads
// ComponentTags[0] for the retargeter name and its IKRetargeter variable for the asset; both go in
// before the anim instance initializes, and the push repeats whenever the instance is recreated.
// Any missing piece degrades to the VISIBLE animated mannequin, never to a T-posed skin.
// ---------------------------------------------------------------------------------------------
void AWrecktideMissionGiver::ApplySkinRetarget()
{
	if (!DriverMesh || !GiverMesh)
	{
		return;
	}
	const bool bDriverOK = DriverMesh->GetSkeletalMeshAsset() != nullptr;
	const bool bSkinOK = GiverMesh->GetSkeletalMeshAsset() != nullptr;
	if (!bDriverOK || !bSkinOK || !RetargetAnimClass || !Retargeter)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MISSION-GIVER skin retarget skipped driver=%d skin=%d abp=%d rtg=%d; showing the mannequin"),
			bDriverOK ? 1 : 0, bSkinOK ? 1 : 0, RetargetAnimClass ? 1 : 0, Retargeter ? 1 : 0);
		GiverMesh->SetVisibility(false);
		DriverMesh->SetVisibility(bDriverOK);
		return;
	}

	GiverMesh->ComponentTags.Reset();
	GiverMesh->ComponentTags.Add(Retargeter->GetFName());
	GiverMesh->SetAnimInstanceClass(RetargetAnimClass);
	if (!PushRetargeter())
	{
		GiverMesh->SetVisibility(false);
		DriverMesh->SetVisibility(true);
		return;
	}
	GiverMesh->OnAnimInitialized.AddUniqueDynamic(this, &AWrecktideMissionGiver::OnSkinAnimInitialized);
	GiverMesh->AddTickPrerequisiteComponent(DriverMesh);
	GiverMesh->SetComponentTickEnabled(true);

	DriverMesh->SetVisibility(false);
	DriverMesh->SetCastShadow(false);
	DriverMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GiverMesh->SetVisibility(true);
	GiverMesh->SetCastShadow(true);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-MISSION-GIVER skin retarget live skin=%s abp=%s rtg=%s"),
		*GiverMesh->GetSkeletalMeshAsset()->GetName(), *RetargetAnimClass->GetName(), *Retargeter->GetName());
}

bool AWrecktideMissionGiver::PushRetargeter()
{
	UAnimInstance* SkinAnim = GiverMesh ? GiverMesh->GetAnimInstance() : nullptr;
	FObjectProperty* RetargeterProperty = SkinAnim
		? FindFProperty<FObjectProperty>(SkinAnim->GetClass(), FName(TEXT("IKRetargeter")))
		: nullptr;
	if (!SkinAnim || !RetargeterProperty || !Retargeter || !Retargeter->IsA(RetargeterProperty->PropertyClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-MISSION-GIVER retargeter push failed anim=%d prop=%d rtg=%d; showing the mannequin"),
			SkinAnim != nullptr ? 1 : 0, RetargeterProperty != nullptr ? 1 : 0, Retargeter != nullptr ? 1 : 0);
		return false;
	}
	RetargeterProperty->SetObjectPropertyValue_InContainer(SkinAnim, Retargeter);
	return true;
}

void AWrecktideMissionGiver::OnSkinAnimInitialized()
{
	PushRetargeter();
}
