// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "Systems/WrecktideOptions.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
/** Option ranges live here, once. The menu steps by these and the loader clamps to them. */
constexpr int32 VolumeMin = 0;
constexpr int32 VolumeMax = 100;
// Named apart from CombatPlayerController.cpp's VolumeStep: both sit in anonymous namespaces and the unity build
// can fold the two files into one translation unit (it did once the patrol boat files reshuffled the blobs, 2026-09-06).
constexpr int32 OptionsVolumeStep = 10;
constexpr float SensitivityMin = 0.5f;
constexpr float SensitivityMax = 2.0f;

int32 ClampVolume(int32 Value)
{
	// Snap to the step as well as clamp: a hand-edited 37 becomes 40, so the menu's Left/Right can
	// never get stuck off-grid and show a value it cannot reproduce.
	const int32 Snapped = FMath::RoundToInt(Value / static_cast<float>(OptionsVolumeStep)) * OptionsVolumeStep;
	return FMath::Clamp(Snapped, VolumeMin, VolumeMax);
}

float ClampSensitivity(float Value)
{
	const float Snapped = FMath::RoundToFloat(Value * 10.0f) / 10.0f;
	return FMath::Clamp(Snapped, SensitivityMin, SensitivityMax);
}
}

FWrecktideOptionValues FWrecktideOptions::Values;
bool FWrecktideOptions::bLoaded = false;

const TCHAR* FWrecktideOptions::SectionName()
{
	return TEXT("Wrecktide.Options");
}

FString FWrecktideOptions::GetIniFilePath()
{
	if (!GConfig)
	{
		return FString();
	}

	// GGameUserSettingsIni is a BASE NAME ("GameUserSettings"), not a path, and GetConfigFilename
	// hands that name straight back on this engine version: the first -MenuProbe=options run read a
	// file called "GameUserSettings" that does not exist. Try the resolvers in order and take the
	// first candidate that is actually on disk.
	const TCHAR* BaseName = *GGameUserSettingsIni;
	TArray<FString> Candidates;
	Candidates.Add(FConfigCacheIni::GetDestIniFilename(BaseName, nullptr, *FPaths::GeneratedConfigDir()));
	Candidates.Add(GConfig->GetConfigFilename(BaseName));
	Candidates.Add(FPaths::GeneratedConfigDir() / ANSI_TO_TCHAR(FPlatformProperties::PlatformName())
		/ (FString(BaseName) + TEXT(".ini")));
	for (const FString& Candidate : Candidates)
	{
		if (!Candidate.IsEmpty() && FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
	// Nothing on disk yet (nothing has been saved this install): name where it WILL be written.
	return Candidates[0];
}

const FWrecktideOptionValues& FWrecktideOptions::Get()
{
	if (!bLoaded)
	{
		Load();
	}
	return Values;
}

void FWrecktideOptions::Set(const FWrecktideOptionValues& NewValues)
{
	Values.MasterVolume = ClampVolume(NewValues.MasterVolume);
	Values.EffectsVolume = ClampVolume(NewValues.EffectsVolume);
	Values.AmbienceVolume = ClampVolume(NewValues.AmbienceVolume);
	Values.MouseSensitivity = ClampSensitivity(NewValues.MouseSensitivity);
	Values.bInvertLookY = NewValues.bInvertLookY;
	Values.bMinimapNorthUp = NewValues.bMinimapNorthUp;
	Values.RadarSize = FMath::Clamp(NewValues.RadarSize, 0, 1);
	bLoaded = true;
}

void FWrecktideOptions::Load()
{
	// Marked loaded first: a missing GConfig (very early startup) must leave the defaults in place
	// and NOT re-enter this every time a sound plays.
	bLoaded = true;
	if (!GConfig)
	{
		return;
	}

	FWrecktideOptionValues Loaded = Values;
	int32 IntValue = 0;
	float FloatValue = 0.0f;
	bool bBoolValue = false;

	if (GConfig->GetInt(SectionName(), TEXT("MasterVolume"), IntValue, GGameUserSettingsIni))
	{
		Loaded.MasterVolume = IntValue;
	}
	if (GConfig->GetInt(SectionName(), TEXT("EffectsVolume"), IntValue, GGameUserSettingsIni))
	{
		Loaded.EffectsVolume = IntValue;
	}
	if (GConfig->GetInt(SectionName(), TEXT("AmbienceVolume"), IntValue, GGameUserSettingsIni))
	{
		Loaded.AmbienceVolume = IntValue;
	}
	if (GConfig->GetFloat(SectionName(), TEXT("MouseSensitivity"), FloatValue, GGameUserSettingsIni))
	{
		Loaded.MouseSensitivity = FloatValue;
	}
	if (GConfig->GetBool(SectionName(), TEXT("InvertLookY"), bBoolValue, GGameUserSettingsIni))
	{
		Loaded.bInvertLookY = bBoolValue;
	}
	if (GConfig->GetInt(SectionName(), TEXT("RadarSize"), IntValue, GGameUserSettingsIni))
	{
		// Clamped, never trusted: a hand-edited ini can only ever choose between the two sizes.
		Loaded.RadarSize = FMath::Clamp(IntValue, 0, 1);
	}
	if (GConfig->GetBool(SectionName(), TEXT("MinimapNorthUp"), bBoolValue, GGameUserSettingsIni))
	{
		Loaded.bMinimapNorthUp = bBoolValue;
	}

	Set(Loaded);
	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-OPTIONS load master=%d effects=%d ambience=%d sens=%.1f invert=%d northup=%d"),
		Values.MasterVolume, Values.EffectsVolume, Values.AmbienceVolume, Values.MouseSensitivity,
		Values.bInvertLookY ? 1 : 0, Values.bMinimapNorthUp ? 1 : 0);
}

bool FWrecktideOptions::Save()
{
	if (!GConfig)
	{
		UE_LOG(LogTemp, Warning, TEXT("WRECKTIDE-OPTIONS save skipped, no config cache"));
		return false;
	}

	GConfig->SetInt(SectionName(), TEXT("MasterVolume"), Values.MasterVolume, GGameUserSettingsIni);
	GConfig->SetInt(SectionName(), TEXT("EffectsVolume"), Values.EffectsVolume, GGameUserSettingsIni);
	GConfig->SetInt(SectionName(), TEXT("AmbienceVolume"), Values.AmbienceVolume, GGameUserSettingsIni);
	GConfig->SetFloat(SectionName(), TEXT("MouseSensitivity"), Values.MouseSensitivity, GGameUserSettingsIni);
	GConfig->SetBool(SectionName(), TEXT("InvertLookY"), Values.bInvertLookY, GGameUserSettingsIni);
	GConfig->SetBool(SectionName(), TEXT("MinimapNorthUp"), Values.bMinimapNorthUp, GGameUserSettingsIni);
	GConfig->SetInt(SectionName(), TEXT("RadarSize"), Values.RadarSize, GGameUserSettingsIni);
	// Flush, not just Set: the probe reads the file off disk, and a player who alt-F4s should still
	// keep the options he just applied.
	GConfig->Flush(false, GGameUserSettingsIni);

	UE_LOG(LogTemp, Log, TEXT("WRECKTIDE-OPTIONS save master=%d effects=%d ambience=%d sens=%.1f invert=%d northup=%d file=%s"),
		Values.MasterVolume, Values.EffectsVolume, Values.AmbienceVolume, Values.MouseSensitivity,
		Values.bInvertLookY ? 1 : 0, Values.bMinimapNorthUp ? 1 : 0, *GetIniFilePath());
	return true;
}

float FWrecktideOptions::GetEffectsScale()
{
	const FWrecktideOptionValues& Current = Get();
	return (Current.MasterVolume * 0.01f) * (Current.EffectsVolume * 0.01f);
}

float FWrecktideOptions::GetAmbienceScale()
{
	const FWrecktideOptionValues& Current = Get();
	return (Current.MasterVolume * 0.01f) * (Current.AmbienceVolume * 0.01f);
}
