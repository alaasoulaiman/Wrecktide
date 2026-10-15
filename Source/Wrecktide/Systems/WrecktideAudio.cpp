// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktideAudio.h"

#include "Systems/WrecktideOptions.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/SoftObjectPath.h"

namespace
{
int32 GPlayCount = 0;
FName GLastPlayed = NAME_None;
TWeakObjectPtr<UAudioComponent> GAmbient;

/** Resolved once per name; a failed load stays null so a missing asset logs once, not per hit. */
TMap<FName, TWeakObjectPtr<USoundBase>>& LoadedSounds()
{
	static TMap<FName, TWeakObjectPtr<USoundBase>> Sounds;
	return Sounds;
}

TSet<FName>& WarnedSounds()
{
	static TSet<FName> Warned;
	return Warned;
}
}

bool UWrecktideAudio::PlaySfx(const UObject* WorldContextObject, FName Sfx, float VolumeMultiplier, float PitchMultiplier)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || Sfx.IsNone())
	{
		return false;
	}

	USoundBase* Sound = nullptr;
	if (TWeakObjectPtr<USoundBase>* Cached = LoadedSounds().Find(Sfx))
	{
		Sound = Cached->Get();
	}
	if (!Sound)
	{
		const FString Path = FString::Printf(TEXT("/Game/Audio/SFX/sfx_%s.sfx_%s"), *Sfx.ToString(), *Sfx.ToString());
		Sound = Cast<USoundBase>(FSoftObjectPath(Path).TryLoad());
		LoadedSounds().Add(Sfx, Sound);
		if (!Sound && !WarnedSounds().Contains(Sfx))
		{
			WarnedSounds().Add(Sfx);
			UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-AUDIO missing sound %s (%s)"), *Sfx.ToString(), *Path);
		}
	}
	if (!Sound)
	{
		return false;
	}

	// The player's mix (Options: Master x Effects) rides on top of the caller's own multiplier. The
	// play still HAPPENS at zero volume so -SfxProbe keeps counting the hooks that fired.
	UGameplayStatics::PlaySound2D(World, Sound, VolumeMultiplier * FWrecktideOptions::GetEffectsScale(), PitchMultiplier);
	++GPlayCount;
	GLastPlayed = Sfx;
	return true;
}

int32 UWrecktideAudio::GetPlayCount()
{
	return GPlayCount;
}

FName UWrecktideAudio::GetLastPlayed()
{
	return GLastPlayed;
}

bool UWrecktideAudio::StartAmbient(const UObject* WorldContextObject, FName AssetName, float VolumeMultiplier)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || AssetName.IsNone())
	{
		return false;
	}
	if (GAmbient.IsValid() && GAmbient->GetWorld() == World && GAmbient->IsPlaying())
	{
		return true;
	}
	const FString Path = FString::Printf(TEXT("/Game/Audio/SFX/%s.%s"), *AssetName.ToString(), *AssetName.ToString());
	USoundBase* Sound = Cast<USoundBase>(FSoftObjectPath(Path).TryLoad());
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-AUDIO ambient missing %s"), *Path);
		return false;
	}
	// bPersistAcrossLevelTransition false: New Game restarts the level and the new world starts its own bed
	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, Sound,
		VolumeMultiplier * FWrecktideOptions::GetAmbienceScale(), 1.0f, 0.0f, nullptr, false, false);
	GAmbient = Component;
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-AUDIO ambient %s started=%d"), *AssetName.ToString(), Component != nullptr ? 1 : 0);
	return Component != nullptr;
}

bool UWrecktideAudio::IsAmbientPlaying()
{
	return GAmbient.IsValid() && GAmbient->IsPlaying();
}
