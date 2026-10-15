// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UI/WrecktideMenuTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class ACombatPlayerController;
class SVerticalBox;
class UWrecktideMenuStyle;

/**
 * The menu VIEW (2026-09-06). It owns no state: every frame of content comes from the controller's
 * model (GetMenuRows / GetMenuSelectedIndex / GetMenuTitle), and it rebuilds when the controller
 * broadcasts OnMenuChanged. Clicking a row or hovering it calls straight back into the SAME model
 * functions the keyboard uses, so mouse and keys can never disagree about what is selected.
 *
 * Added to the viewport with GEngine->GameViewport->AddViewportWidgetContent; no UMG asset exists.
 */
class SWrecktideMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWrecktideMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACombatPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the whole panel from the controller's current model. Cheap: ~20 widgets. */
	void Rebuild();

private:
	TSharedRef<SWidget> BuildPanel(const UWrecktideMenuStyle& Style, EWrecktideMenuScreen Screen);
	TSharedRef<SWidget> BuildRow(const UWrecktideMenuStyle& Style, EWrecktideMenuScreen Screen,
		const FWrecktideMenuRow& Row, int32 Index, bool bSelected);

	TWeakObjectPtr<ACombatPlayerController> Controller;
};
