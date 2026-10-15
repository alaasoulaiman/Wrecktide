// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "WrecktideAudio.generated.h"

/**
 * The one door to the game's feedback sounds (2026-09-04 night). Every gameplay event that should be
 * heard calls PlaySfx with a name; the name maps to a USoundWave under /Game/Audio/SFX (synthesised
 * by tools\gen_sfx.py, imported by Tools\import_sfx.py). Missing or unloaded sounds are a logged
 * no-op, never a crash: audio is feedback, not a gate. Sounds are 2D (UI style) because the source
 * of every event is the player or something the player just touched.
 */
UCLASS()
class WRECKTIDE_API UWrecktideAudio : public UObject
{
	GENERATED_BODY()

public:
	/** PlaySfx is the SINGLE authoritative play path. Names: hit, kill, coin, levelup, chest, fanfare,
	 *  wood, ui_move, ui_confirm, hurt, death, accept, splash. Returns false when nothing played. */
	static bool PlaySfx(const UObject* WorldContextObject, FName Sfx, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/** Number of successful plays so far this process (probe evidence that the hooks fire). */
	static int32 GetPlayCount();

	/** Name of the last sound that played (probe evidence). */
	static FName GetLastPlayed();

	/** Starts a looping 2D bed by asset name under /Game/Audio/SFX (ambient_sea). One per world; a second
	 *  call on the same world is a no-op. Returns false when the asset is missing. */
	static bool StartAmbient(const UObject* WorldContextObject, FName AssetName, float VolumeMultiplier = 0.35f);

	/** True while the ambient bed's audio component is alive and playing (probe evidence). */
	static bool IsAmbientPlaying();
};
