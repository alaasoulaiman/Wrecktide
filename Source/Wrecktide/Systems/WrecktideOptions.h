// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"

/**
 * Player options that the engine has no home for (2026-09-06 menu pass).
 *
 * Video settings (window mode, resolution, VSync, scalability) already live on UGameUserSettings and
 * stay there: this holds only what the engine does not persist for us, audio mix and look feel, in
 * the [Wrecktide.Options] section of the SAME GameUserSettings.ini so the player has one settings
 * file, not two. Values are cached in a process-wide struct because the audio path reads them on
 * every sound and must not touch the config system per play.
 *
 * Every value is validated on read: a hand-edited ini can never make the game silent-and-unfixable
 * or the mouse unusable, it just clamps back into range.
 */
struct FWrecktideOptionValues
{
	/** 0..100 in steps of 10. Scales every sound the game plays. */
	int32 MasterVolume = 100;

	/** 0..100 in steps of 10. Scales UWrecktideAudio::PlaySfx on top of Master. */
	int32 EffectsVolume = 100;

	/** 0..100 in steps of 10. Scales the ambient bed on top of Master. */
	int32 AmbienceVolume = 70;

	/** 0.5..2.0 in steps of 0.1. Multiplies yaw and pitch input on the player controller. */
	float MouseSensitivity = 1.0f;

	/** Flips pitch input (look up moves the camera down). */
	bool bInvertLookY = false;

	/** Minimap north-up instead of heading-up (the default). Heading-up disorients some players,
	 *  which is why the classic MMO shipped the same toggle. */
	bool bMinimapNorthUp = false;

	/** Radar size the player left it on (spec C27): 0 small, 1 medium. The FULL map is a moment,
	 *  not a size to come back to, so it is never stored; M cycling through it lands on small. */
	int32 RadarSize = 0;
};

/**
 * The single door to the option values. Static because sounds, the controller and the menu all read
 * them and none of them owns the others.
 */
class WRECKTIDE_API FWrecktideOptions
{
public:
	/** Current values, loading them from the ini the first time anything asks. */
	static const FWrecktideOptionValues& Get();

	/** Replaces the live values (clamped). Does NOT write the ini; Save() does that. */
	static void Set(const FWrecktideOptionValues& NewValues);

	/** Reads [Wrecktide.Options] out of GameUserSettings.ini. Safe to call repeatedly. */
	static void Load();

	/** Writes [Wrecktide.Options] and flushes the file to disk. Returns false when GConfig is gone. */
	static bool Save();

	/** Master * Effects, 0..1. What UWrecktideAudio::PlaySfx multiplies by. */
	static float GetEffectsScale();

	/** Master * Ambience, 0..1. What UWrecktideAudio::StartAmbient multiplies by. */
	static float GetAmbienceScale();

	/** The ini section every value above lives in (probe evidence reads the raw file for it). */
	static const TCHAR* SectionName();

	/**
	 * Absolute path of the file the values are written to. GGameUserSettingsIni is a BASE NAME
	 * ("GameUserSettings"), not a path: reading it directly finds nothing, which is exactly how the
	 * first -MenuProbe=options run failed. GConfig resolves it to the generated config file.
	 */
	static FString GetIniFilePath();

private:
	static FWrecktideOptionValues Values;
	static bool bLoaded;
};
