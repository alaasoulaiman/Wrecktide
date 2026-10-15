// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "UI/WrecktideMapView.h"

#include "Data/WrecktideDataRows.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Gameplay/WrecktideBoat.h"
#include "Gameplay/WrecktideMissionGiver.h"
#include "Gameplay/WrecktidePatrolBoat.h"
#include "Gameplay/WrecktideQuestBoard.h"
#include "Gameplay/WrecktideShopStall.h"
#include "Gameplay/WrecktideSmithStall.h"
#include "Gameplay/WrecktideTreasureChest.h"
#include "Systems/ProgressComponent.h"
#include "Systems/StatsComponent.h"
#include "Systems/WrecktideDataSubsystem.h"
#include "Systems/WrecktideMissionLogic.h"
#include "Systems/WrecktidePlayerState.h"
#include "Variant_Combat/AI/CombatEnemy.h"

namespace WrecktideMap
{
const TCHAR* IslandTexturePath = TEXT("/Game/UI/Map/T_IslandMap.T_IslandMap");
const TCHAR* RimTexturePath = TEXT("/Game/UI/Map/T_MapRim.T_MapRim");
const TCHAR* GlowTexturePath = TEXT("/Game/UI/Map/T_MapGlow.T_MapGlow");
const TCHAR* RingTexturePath = TEXT("/Game/UI/Map/T_MapRing.T_MapRing");
const TCHAR* BeamTexturePath = TEXT("/Game/UI/Map/T_BeamGrad.T_BeamGrad");
const TCHAR* BeaconMaterialPath = TEXT("/Game/UI/Map/M_WrecktideBeacon.M_WrecktideBeacon");

namespace
{
constexpr float MapWorldSpan = 2.0f * MapWorldHalfExtent;

/** The nearest living enemy of a row, or null. Shared by the mission target and the beacon. */
const ACombatEnemy* FindNearestLivingEnemy(const UWorld* World, FName RowName, const FVector& From)
{
	if (!World)
	{
		return nullptr;
	}

	const ACombatEnemy* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<ACombatEnemy> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const ACombatEnemy* Enemy = *It;
		if (!Enemy || (!RowName.IsNone() && Enemy->EnemyRowName != RowName))
		{
			continue;
		}
		if (Enemy->Stats && !Enemy->Stats->IsAlive())
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared2D(Enemy->GetActorLocation(), From);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Enemy;
		}
	}
	return Best;
}

/** The NPC an errand names, by his Missions.csv GiverId. An empty id means Bell, the same rule
 *  WrecktideMissionLogic uses for a row that never named an owner (C33.2, 2026-09-07). */
bool FindGiverLocation(const UWorld* World, FName GiverId, FVector& OutLocation)
{
	if (!World)
	{
		return false;
	}
	const FName Wanted = GiverId.IsNone() ? FName(TEXT("Bell")) : GiverId;
	for (TActorIterator<AWrecktideMissionGiver> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->GetGiverId() == Wanted)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
	}
	return false;
}
}

const TArray<FIslandChart>& GetIslandCharts()
{
	// Index 0 is Wreck Cove. Adding an island is one row here plus one python bake; nothing else in
	// the HUD changes, because both draw paths already ask ChartFor() which plate to use.
	static const TArray<FIslandChart> Charts = {
		{TEXT("WreckCove"), TEXT("WRECK COVE"), MapWorldCentreX, MapWorldCentreY, MapWorldHalfExtent,
			7000.0f, TEXT("/Game/UI/Map/T_IslandMap.T_IslandMap")},
		{TEXT("GallowsReach"), TEXT("GALLOWS REACH"), GallowsReach::CentreX, GallowsReach::CentreY, 6000.0f,
			GallowsReach::BeachHalf, TEXT("/Game/UI/Map/T_Island2Map.T_Island2Map")},
		{TEXT("MangroveHollow"), TEXT("MANGROVE HOLLOW"), MangroveHollow::CentreX, MangroveHollow::CentreY,
			MangroveHollow::ChartHalf, MangroveHollow::BeachHalf, TEXT("/Game/UI/Map/T_Island3Map.T_Island3Map")}
	};
	return Charts;
}

const FIslandChart& ChartFor(const FVector& World)
{
	static const FIslandChart Fallback = {TEXT("WreckCove"), TEXT("WRECK COVE"),
		MapWorldCentreX, MapWorldCentreY, MapWorldHalfExtent, 7000.0f,
		TEXT("/Game/UI/Map/T_IslandMap.T_IslandMap")};

	const TArray<FIslandChart>& Charts = GetIslandCharts();
	if (Charts.Num() == 0)
	{
		return Fallback;
	}

	// NORMALISED distance, not raw distance: how far outside the plate the point is, measured in
	// plate half extents. Inside a plate that number is below 1, so "the island you are standing
	// on" always wins, and at sea the plate the point is least far outside wins.
	// WHY not plain nearest centre: Cobb's far rock at (-700, -14300) is 14,300 uu from Wreck Cove
	// and 12,800 from Gallows Reach, so nearest-centre would swap the chart under a player who is
	// still inside Wreck Cove's own plate and 12,700 uu off the second island's.
	int32 BestIndex = 0;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Charts.Num(); ++Index)
	{
		const float DeltaX = FMath::Abs(World.X - Charts[Index].CentreX);
		const float DeltaY = FMath::Abs(World.Y - Charts[Index].CentreY);
		const float Score = FMath::Max(DeltaX, DeltaY) / FMath::Max(Charts[Index].HalfExtent, 1.0f);
		if (Score < BestScore)
		{
			BestScore = Score;
			BestIndex = Index;
		}
	}
	return Charts[BestIndex];
}

FVector2D WorldToUV(const FVector& World)
{
	return FVector2D(
		(World.Y - MapWorldCentreY + MapWorldHalfExtent) / MapWorldSpan,
		(MapWorldHalfExtent - (World.X - MapWorldCentreX)) / MapWorldSpan);
}

FVector2D WorldToUV(const FVector& World, const FIslandChart& Chart)
{
	const float Span = 2.0f * FMath::Max(Chart.HalfExtent, 1.0f);
	return FVector2D(
		(World.Y - Chart.CentreY + Chart.HalfExtent) / Span,
		(Chart.HalfExtent - (World.X - Chart.CentreX)) / Span);
}

FVector2D UVToWorld(const FVector2D& UV)
{
	return FVector2D(
		MapWorldCentreX + MapWorldHalfExtent - UV.Y * MapWorldSpan,
		MapWorldCentreY + UV.X * MapWorldSpan - MapWorldHalfExtent);
}

void CollectMarkers(const UObject* WorldContext, TArray<FMapMarker>& OutMarkers)
{
	OutMarkers.Reset();

	UWorld* World = WorldContext ? const_cast<UWorld*>(WorldContext->GetWorld()) : nullptr;
	if (!World)
	{
		return;
	}

	for (TActorIterator<AWrecktideShopStall> It(World); It; ++It)
	{
		OutMarkers.Add({It->GetActorLocation(), EMapIcon::Stall,
			It->StallClass == EWrecktideStallClass::Shooting ? TEXT("Gunsmith") : TEXT("Market")});
	}
	for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
	{
		OutMarkers.Add({It->GetActorLocation(), EMapIcon::Board, TEXT("Bounty board")});
	}
	// C22 (2026-09-06): four NPCs now, each marked by his own name, and a giver with work waiting
	// carries the same "!" / "?" the world plate shows so the map and the head glyph never disagree.
	for (TActorIterator<AWrecktideMissionGiver> It(World); It; ++It)
	{
		const FString Signal = It->GetQuestSignal();
		FString Label = It->GetPlateName();
		if (!Signal.IsEmpty())
		{
			Label = Signal + TEXT(" ") + Label;
		}
		OutMarkers.Add({It->GetActorLocation(), EMapIcon::Giver, Label});
	}
	for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
	{
		OutMarkers.Add({It->GetActorLocation(), EMapIcon::Boat, TEXT("Your boat")});
	}

	// Opened chests drop off the map: a marker that pays nothing is a lie the player walks to.
	for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
	{
		if (It->IsOpened())
		{
			continue;
		}
		OutMarkers.Add({It->GetActorLocation(), EMapIcon::Chest, TEXT("Treasure")});
	}

	// PATROL lane, ONE self-contained block (spec S3.3): the navy patrol is a red ship on the map
	// only while she is hostile. Off duty she is a sail on the horizon like any other, and marking
	// her then would turn a loop route into a permanent red dot the player learns to ignore.
	for (TActorIterator<AWrecktidePatrolBoat> It(World); It; ++It)
	{
		if (!It->IsHostile())
		{
			continue;
		}
		OutMarkers.Add({It->GetActorLocation(), EMapIcon::Patrol, TEXT("Navy patrol")});
	}

	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		const ACombatEnemy* Enemy = *It;
		if (!Enemy || (Enemy->Stats && !Enemy->Stats->IsAlive()))
		{
			continue;
		}
		// The bone captain is always on the map; ordinary enemies only once they have noticed you,
		// so the island does not read as a wall of red dots before the first fight.
		if (Enemy->EnemyRowName == FName(TEXT("SkeletonCaptain")))
		{
			OutMarkers.Add({Enemy->GetActorLocation(), EMapIcon::Boss, TEXT("Bone captain")});
			continue;
		}
		const EEnemyAggroState State = Enemy->GetAggroState();
		if (State == EEnemyAggroState::Alert || State == EEnemyAggroState::Chasing)
		{
			OutMarkers.Add({Enemy->GetActorLocation(), EMapIcon::Enemy, TEXT("Enemy")});
		}
	}
}

bool ResolveMissionTarget(const UObject* WorldContext, const AWrecktidePlayerState* PlayerState, FVector& OutLocation, FString& OutLabel,
	EMissionTargetFlavour Flavour)
{
	OutLabel.Reset();

	UWorld* World = WorldContext ? const_cast<UWorld*>(WorldContext->GetWorld()) : nullptr;
	if (!World || !PlayerState)
	{
		return false;
	}

	const UProgressComponent* Progress = PlayerState->GetProgress();
	const UGameInstance* GameInstance = World->GetGameInstance();
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!Progress || !Data)
	{
		return false;
	}

	const FName Row = WrecktideMissionLogic::GetCurrentMissionRow(Progress, Data);
	const FMissionTableRow* MissionRow = Row.IsNone() ? nullptr : Data->GetMissionRow(Row);
	if (!MissionRow)
	{
		return false;
	}

	OutLabel = MissionRow->Title.ToString();

	switch (MissionRow->Type)
	{
	case EMissionObjective::ReachSpot:
		// The one case the mission logic already owns; reuse it rather than a second spelling.
		return WrecktideMissionLogic::ResolveSpotLocation(WorldContext, *MissionRow, OutLocation);

	case EMissionObjective::KillRow:
	case EMissionObjective::KillBoss:
	{
		const FVector From = PlayerState->GetPawn() ? PlayerState->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		if (const ACombatEnemy* Enemy = FindNearestLivingEnemy(World, MissionRow->TargetName, From))
		{
			// Anchor = his POST, not the man: a chasing enemy must not drag the gold beam across the
			// island (C33.1). A home of exactly zero means BeginPlay has not captured the post yet,
			// and pointing at the world origin would be worse than pointing at him, so fall back.
			const FVector Home = Enemy->GetHomeLocation();
			OutLocation = (Flavour == EMissionTargetFlavour::Anchor && !Home.IsNearlyZero())
				? Home : Enemy->GetActorLocation();
			return true;
		}
		return false;
	}

	case EMissionObjective::TalkTo:
		// The hand-off missions had NO beam and NO arrow before C33: "Find Marta at the stash camp"
		// is the one errand where a direction matters most. Both flavours answer the same NPC.
		return FindGiverLocation(World, MissionRow->TargetName, OutLocation);

	case EMissionObjective::Deliver:
	{
		// Two legs, and the mission logic already knows which one you are on: while the count is
		// short you are carrying it OUT to the named NPC, and once he has taken it the errand ends
		// back at the NPC who set it (WrecktideMissionLogic::NotifyTalkedTo counts but never pays a
		// Deliver out). Point at the leg the player is actually walking.
		const bool bCarryingBack = WrecktideMissionLogic::IsRowReadyToComplete(Progress, Data, Row);
		const FName Leg = bCarryingBack ? MissionRow->GiverId : MissionRow->TargetName;
		if (FindGiverLocation(World, Leg, OutLocation))
		{
			return true;
		}
		// A missing NPC on one leg still leaves the other worth pointing at.
		return FindGiverLocation(World, bCarryingBack ? MissionRow->TargetName : MissionRow->GiverId, OutLocation);
	}

	case EMissionObjective::Pay:
		// A Pay row names an amount, not a person: the gold goes to whoever set the row.
		return FindGiverLocation(World, MissionRow->GiverId, OutLocation);

	case EMissionObjective::Upgrade:
		for (TActorIterator<AWrecktideSmithStall> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
		return false;

	case EMissionObjective::OpenChest:
		for (TActorIterator<AWrecktideTreasureChest> It(World); It; ++It)
		{
			if (It->ChestId != MissionRow->TargetName || It->IsOpened())
			{
				continue;
			}
			OutLocation = It->GetActorLocation();
			return true;
		}
		return false;

	case EMissionObjective::BoardBoat:
		for (TActorIterator<AWrecktideBoat> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
		return false;

	case EMissionObjective::TurnInBounty:
		for (TActorIterator<AWrecktideQuestBoard> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			return true;
		}
		return false;

	default:
		return false;
	}
}

const ACombatEnemy* ResolveMissionKillTarget(const UObject* WorldContext, const AWrecktidePlayerState* PlayerState, const ACombatEnemy* Preferred)
{
	UWorld* World = WorldContext ? const_cast<UWorld*>(WorldContext->GetWorld()) : nullptr;
	const UProgressComponent* Progress = PlayerState ? PlayerState->GetProgress() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UWrecktideDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UWrecktideDataSubsystem>() : nullptr;
	if (!World || !Progress || !Data)
	{
		return nullptr;
	}

	const FName Row = WrecktideMissionLogic::GetCurrentMissionRow(Progress, Data);
	const FMissionTableRow* MissionRow = Row.IsNone() ? nullptr : Data->GetMissionRow(Row);
	if (!MissionRow || (MissionRow->Type != EMissionObjective::KillRow && MissionRow->Type != EMissionObjective::KillBoss))
	{
		return nullptr;
	}

	// The man we already named keeps the job while he is alive and still of this row. Re-picking the
	// nearest one every second is what made the beam hop from post to post as the player walked
	// between two cutthroat camps, which is the moving light in another form.
	const FName EnemyRow = MissionRow->TargetName;
	if (Preferred && IsValid(Preferred)
		&& (EnemyRow.IsNone() || Preferred->EnemyRowName == EnemyRow)
		&& (!Preferred->Stats || Preferred->Stats->IsAlive()))
	{
		return Preferred;
	}

	const FVector From = PlayerState->GetPawn() ? PlayerState->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	return FindNearestLivingEnemy(World, EnemyRow, From);
}

FVector2D ClampToRect(const FVector2D& Offset, const FVector2D& HalfSize)
{
	if (HalfSize.X <= KINDA_SMALL_NUMBER || HalfSize.Y <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	if (FMath::Abs(Offset.X) <= HalfSize.X && FMath::Abs(Offset.Y) <= HalfSize.Y)
	{
		return Offset;
	}

	// Ray versus rectangle: the smallest positive scale that puts the ray on an edge. Clamping the
	// two axes independently would leave the centre-to-target line and point wrong near a corner.
	float Scale = TNumericLimits<float>::Max();
	if (FMath::Abs(Offset.X) > KINDA_SMALL_NUMBER)
	{
		Scale = FMath::Min(Scale, HalfSize.X / FMath::Abs(Offset.X));
	}
	if (FMath::Abs(Offset.Y) > KINDA_SMALL_NUMBER)
	{
		Scale = FMath::Min(Scale, HalfSize.Y / FMath::Abs(Offset.Y));
	}
	if (Scale >= TNumericLimits<float>::Max())
	{
		return FVector2D::ZeroVector;
	}
	return Offset * Scale;
}

FVector2D ClampToCircle(const FVector2D& Offset, float Radius)
{
	const float Length = Offset.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	return Length <= Radius ? Offset : Offset * (Radius / Length);
}

FVector2D WorldToRadarOffset(const FVector& PlayerLocation, float ViewYawDegrees, const FVector& Target, float PixelsPerUU)
{
	const FVector Delta = Target - PlayerLocation;
	const float YawRadians = FMath::DegreesToRadians(ViewYawDegrees);
	const float CosYaw = FMath::Cos(YawRadians);
	const float SinYaw = FMath::Sin(YawRadians);

	// Forward = (cos, sin), Right = (-sin, cos): the UE yaw basis. Screen Y points DOWN, so the
	// forward component becomes negative Y and the radar reads heading-up.
	const float Forward = Delta.X * CosYaw + Delta.Y * SinYaw;
	const float Right = -Delta.X * SinYaw + Delta.Y * CosYaw;
	return FVector2D(Right * PixelsPerUU, -Forward * PixelsPerUU);
}

float RadarOffsetAngle(const FVector2D& Offset)
{
	// Clockwise from straight up, in degrees, matching how a compass bearing reads.
	return FMath::RadiansToDegrees(FMath::Atan2(Offset.X, -Offset.Y));
}

FLinearColor IconColour(EMapIcon Icon)
{
	switch (Icon)
	{
	case EMapIcon::Stall:   return FLinearColor(1.00f, 0.82f, 0.25f);
	case EMapIcon::Board:   return FLinearColor(0.92f, 0.86f, 0.70f);
	case EMapIcon::Giver:   return FLinearColor(1.00f, 0.90f, 0.45f);
	case EMapIcon::Boat:    return FLinearColor(0.95f, 0.98f, 1.00f);
	case EMapIcon::Chest:   return FLinearColor(0.72f, 0.48f, 0.22f);
	case EMapIcon::Boss:    return FLinearColor(0.90f, 0.90f, 0.95f);
	case EMapIcon::Enemy:   return FLinearColor(0.95f, 0.20f, 0.18f);
	case EMapIcon::Patrol:  return FLinearColor(0.98f, 0.26f, 0.24f);
	case EMapIcon::Mission: return FLinearColor(1.00f, 0.78f, 0.20f);
	default:                return FLinearColor::White;
	}
}

FString IconGlyph(EMapIcon Icon)
{
	switch (Icon)
	{
	case EMapIcon::Stall:   return TEXT("o");
	case EMapIcon::Board:   return TEXT("=");
	case EMapIcon::Giver:   return TEXT("!");
	case EMapIcon::Boat:    return TEXT("A");
	case EMapIcon::Chest:   return TEXT("#");
	case EMapIcon::Boss:    return TEXT("X");
	case EMapIcon::Enemy:   return TEXT("*");
	case EMapIcon::Patrol:  return TEXT("^");
	case EMapIcon::Mission: return TEXT("<>");
	default:                return TEXT("?");
	}
}

FString IconLegend(EMapIcon Icon)
{
	switch (Icon)
	{
	case EMapIcon::Stall:   return TEXT("o   Market stall");
	case EMapIcon::Board:   return TEXT("=   Bounty board");
	case EMapIcon::Giver:   return TEXT("!   Quartermaster");
	case EMapIcon::Boat:    return TEXT("A   Your boat");
	case EMapIcon::Chest:   return TEXT("#   Unopened chest");
	case EMapIcon::Boss:    return TEXT("X   Bone captain");
	case EMapIcon::Enemy:   return TEXT("*   Enemy on your trail");
	case EMapIcon::Patrol:  return TEXT("^   Navy patrol, hostile");
	case EMapIcon::Mission: return TEXT("<>  Mission target");
	default:                return FString();
	}
}
}
