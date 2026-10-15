// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UI/WrecktideMenuTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class ACombatPlayerController;
class UWrecktideMenuStyle;

/**
 * The chest screen VIEW (spec C18). Same contract as SWrecktideMenu: it owns no state, every frame
 * of content comes from the controller's model (GetMenuRows / GetMenuSelectedIndex / GetChestTab),
 * and clicking a row or a page tab calls back into the SAME functions the keyboard drives.
 *
 * Shape follows the classic sea chest (research A1): a panel pinned to the RIGHT edge of the screen
 * with a vertical rail of page tabs down its LEFT edge, each carrying its own letter. Our art, our
 * page names, our motion.
 */
class SWrecktideChest : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWrecktideChest) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACombatPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the panel from the controller's current model. */
	void Rebuild();
	/** The canvas toolbar stays clickable below the panel while this Slate screen is open. */
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> BuildTabRail(const UWrecktideMenuStyle& Style);
	TSharedRef<SWidget> BuildTab(const UWrecktideMenuStyle& Style, EWrecktideChestTab Tab);
	TSharedRef<SWidget> BuildPanel(const UWrecktideMenuStyle& Style);
	TSharedRef<SWidget> BuildRow(const UWrecktideMenuStyle& Style, const FWrecktideMenuRow& Row,
		int32 Index, bool bSelected);

	TWeakObjectPtr<ACombatPlayerController> Controller;
};
