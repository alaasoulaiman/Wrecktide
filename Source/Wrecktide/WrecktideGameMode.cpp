// Copyright Epic Games, Inc. All Rights Reserved.

#include "WrecktideGameMode.h"
#include "Systems/WrecktidePlayerState.h"
#include "UI/WrecktideHUD.h"

AWrecktideGameMode::AWrecktideGameMode()
{
	PlayerStateClass = AWrecktidePlayerState::StaticClass();
	HUDClass = AWrecktideHUD::StaticClass();
}
