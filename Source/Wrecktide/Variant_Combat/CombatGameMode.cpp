// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Combat/CombatGameMode.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"

ACombatGameMode::ACombatGameMode()
{
	PlayerStateClass = AWrecktidePlayerState::StaticClass();
	HUDClass = AWrecktideHUD::StaticClass();
}
