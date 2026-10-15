// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"

/**
 * The menu MODEL, shared by the controller (which owns it) and the Slate view (which draws it).
 * Plain C++ on purpose: no UObject, no reflection, so both sides can include this without a cycle.
 *
 * The rule that keeps a relabel from firing the wrong action: every screen builds its rows and
 * handles its confirm in the SAME switch, indexed by position in the same array.
 */
enum class EWrecktideMenuScreen : uint8
{
	None,
	Main,
	Pause,
	SaveVoyage,
	LoadVoyage,
	Options,
	Wardrobe,
	Controls,
	Credits,
	/** The chest screen on Tab (spec C18): its own root, its own Slate view, four tabs. */
	Inventory
};

/**
 * What the radar is doing right now (spec C27). M cycles them in this order and comes back round;
 * SMALL is what a fresh install starts on.
 *   Small  : a 120 px compass ring, no island plate, blips only. Reads at a glance while fighting.
 *   Medium : the 220 px plate radar (what the build shipped with).
 *   Full   : the full-screen chart, which pauses the world.
 */
enum class EWrecktideRadarState : uint8
{
	Small,
	Medium,
	Full
};

/** Tabs across the top of the chest screen, in drawn order (spec C18). */
enum class EWrecktideChestTab : uint8
{
	Weapons,
	Outfits,
	Journal,
	Gold,
	Count
};

/** One drawn line. A row with bSelectable false is a caption: navigation skips it. */
struct FWrecktideMenuRow
{
	/** Left-hand text, and the token the probe path matches on. */
	FString Label;

	/** Right-hand text: an option's value, or a save slot's summary. */
	FString Value;

	/** Small line under the label (outfit flavour, slot detail). */
	FString SubLine;

	/** False for captions and for empty save slots on the load screen. */
	bool bSelectable = true;

	FWrecktideMenuRow() = default;

	explicit FWrecktideMenuRow(const FString& InLabel, const FString& InValue = FString(),
		const FString& InSubLine = FString(), bool bInSelectable = true)
		: Label(InLabel)
		, Value(InValue)
		, SubLine(InSubLine)
		, bSelectable(bInSelectable)
	{
	}
};

/** Fired whenever the rows, the selection, the title or the hint changed. The view rebuilds on it. */
DECLARE_MULTICAST_DELEGATE(FOnWrecktideMenuChanged);
