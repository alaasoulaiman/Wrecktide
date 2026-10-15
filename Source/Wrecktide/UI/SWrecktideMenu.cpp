// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "UI/SWrecktideMenu.h"

#include "UI/WrecktideMenuStyle.h"
#include "Variant_Combat/CombatPlayerController.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
/** Layout numbers, in one place. Tuned at 1280x720, the resolution every capture harness uses. */
constexpr float MainPanelWidth = 520.0f;      // the left third of a 1280 wide window
constexpr float SubPanelWidth = 700.0f;       // pause and sub-screens, centred
constexpr float MainPanelSideInset = 56.0f;   // gap from the left edge of the screen
constexpr float PanelVerticalInset = 34.0f;
constexpr float FrameInsetX = 54.0f;          // clears the rope border AND the parchment's burnt edge
constexpr float FrameInsetY = 44.0f;
constexpr float MainDimAlpha = 0.28f;         // the scenic view stays visible behind the main menu
constexpr float PauseDimAlpha = 0.55f;        // gameplay behind the pause menu is dimmed harder
/** Keeps the complete Controls list within the parchment at 1280x720, with Back always visible. */
constexpr float ControlsListHeight = 360.0f;

int32 TitleFontSize(EWrecktideMenuScreen Screen)
{
	return Screen == EWrecktideMenuScreen::Main ? 84 : 50;
}

/** Screens with a value column or many rows get a smaller entry face so nothing overflows at 720p. */
int32 EntryFontSize(EWrecktideMenuScreen Screen)
{
	switch (Screen)
	{
	case EWrecktideMenuScreen::Main:
	case EWrecktideMenuScreen::Pause:
		return 30;
	case EWrecktideMenuScreen::Controls:
	case EWrecktideMenuScreen::Credits:
		return 19;
	default:
		return 20;
	}
}

FLinearColor DividerColour()
{
	return FLinearColor(0.24f, 0.15f, 0.07f, 0.55f);
}
}

void SWrecktideMenu::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	// Visible, not SelfHitTestInvisible: an open menu is modal, so it must swallow the clicks that
	// miss a row rather than letting them reach the game underneath.
	SetVisibility(EVisibility::Visible);
	Rebuild();
}

void SWrecktideMenu::Rebuild()
{
	ACombatPlayerController* PlayerController = Controller.Get();
	const UWrecktideMenuStyle* StylePtr = UWrecktideMenuStyle::Get();
	if (!PlayerController || !StylePtr)
	{
		ChildSlot[SNullWidget::NullWidget];
		return;
	}

	const UWrecktideMenuStyle& Style = *StylePtr;
	const EWrecktideMenuScreen Screen = PlayerController->GetMenuScreen();
	if (Screen == EWrecktideMenuScreen::None)
	{
		ChildSlot[SNullWidget::NullWidget];
		return;
	}

	const bool bMain = Screen == EWrecktideMenuScreen::Main;
	const float PanelWidth = bMain ? MainPanelWidth : SubPanelWidth;

	TSharedRef<SOverlay> Root = SNew(SOverlay);

	// 1. the dimming sheet over the paused world
	Root->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SImage)
		.Image(Style.GetDimBrush())
		.DesiredSizeOverride(FVector2D(4.0f, 4.0f))
		.ColorAndOpacity(FLinearColor(0.02f, 0.015f, 0.01f, bMain ? MainDimAlpha : PauseDimAlpha))
	];

	// 2. the parchment panel: hard left on the title screen, centred everywhere else
	// The title panel fills the height of the screen; a sub-screen sizes to its own rows and sits in
	// the middle (a full-height pause panel buried its hint line in the parchment's burnt bottom edge).
	Root->AddSlot()
	.HAlign(bMain ? HAlign_Left : HAlign_Center)
	.VAlign(bMain ? VAlign_Fill : VAlign_Center)
	.Padding(bMain ? FMargin(MainPanelSideInset, PanelVerticalInset) : FMargin(0.0f, PanelVerticalInset))
	[
		SNew(SBox)
		.WidthOverride(PanelWidth)
		.MinDesiredHeight(bMain ? 0.0f : 300.0f)
		[
			BuildPanel(Style, Screen)
		]
	];

	// 3. build tag, bottom right of the SCREEN (not the panel), title screen only
	if (bMain)
	{
		Root->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 26.0f, 18.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Beta 0.9")))
			.Font(Style.GetHeadingFont(16))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.86f, 0.72f, 0.85f)))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
		];
	}

	ChildSlot[Root];
}

TSharedRef<SWidget> SWrecktideMenu::BuildPanel(const UWrecktideMenuStyle& Style, EWrecktideMenuScreen Screen)
{
	ACombatPlayerController* PlayerController = Controller.Get();
	if (!PlayerController)
	{
		return SNullWidget::NullWidget;
	}

	const bool bMain = Screen == EWrecktideMenuScreen::Main;
	// Explicit wrap width, not AutoWrapText alone: the first capture had the hint line spilling past
	// both sides of the rope frame because the text block sized itself from its own content.
	const float TextWidth = (bMain ? MainPanelWidth : SubPanelWidth) - 2.0f * FrameInsetX - 12.0f;
	const TArray<FWrecktideMenuRow>& Rows = PlayerController->GetMenuRows();
	const int32 Selected = PlayerController->GetMenuSelectedIndex();
	const FString Subtitle = PlayerController->GetMenuSubtitle();
	const FString Hint = PlayerController->GetMenuHint();

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// title
	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Center)
	[
		SNew(STextBlock)
		.Text(FText::FromString(PlayerController->GetMenuTitle()))
		.Font(Style.GetTitleFont(TitleFontSize(Screen)))
		.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::GoldColour()))
		.ShadowOffset(FVector2D(3.0f, 3.0f))
		.ShadowColorAndOpacity(FLinearColor(0.06f, 0.03f, 0.0f, 0.85f))
		.Justification(ETextJustify::Center)
	];

	if (!Subtitle.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.0f, bMain ? 0.0f : 4.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Subtitle))
			.Font(Style.GetItalicFont(26))
			.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::FadedInkColour()))
			.Justification(ETextJustify::Center)
			.WrapTextAt(TextWidth)
		];
	}

	// rule under the header
	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(6.0f, 12.0f, 6.0f, 14.0f))
	[
		SNew(SBox)
		.HeightOverride(2.0f)
		[
			SNew(SImage)
			.Image(Style.GetDimBrush())
			.DesiredSizeOverride(FVector2D(4.0f, 4.0f))
			.ColorAndOpacity(DividerColour())
		]
	];

	// Controls has more bindings than fit on one page. Its Back row stays outside the scroll area.
	TSharedPtr<SScrollBox> ControlsList;
	if (Screen == EWrecktideMenuScreen::Controls)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			SNew(SBox).HeightOverride(ControlsListHeight)
			[
				SAssignNew(ControlsList, SScrollBox)
			]
		];
	}

	// the rows themselves
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (ControlsList.IsValid() && !Rows[Index].bSelectable)
		{
			ControlsList->AddSlot()
			.Padding(FMargin(0.0f, 2.0f))
			[
				BuildRow(Style, Screen, Rows[Index], Index, Index == Selected)
			];
			continue;
		}
		Content->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 2.0f))
		[
			BuildRow(Style, Screen, Rows[Index], Index, Index == Selected)
		];
	}

	// push the hint to the foot of a full-height panel; harmless on an auto-sized one
	Content->AddSlot()
	.FillHeight(1.0f)
	.MinHeight(10.0f)
	[
		SNew(SSpacer)
	];

	if (!Hint.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Hint))
			.Font(Style.GetBodyFont(17))
			.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::InkColour()))
			.Justification(ETextJustify::Center)
			.WrapTextAt(TextWidth)
		];
	}

	if (bMain)
	{
		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(52.0f)
			.HeightOverride(52.0f)
			[
				SNew(SImage).Image(Style.GetSealBrush())
			]
		];
	}

	// DesiredSizeOverride on both: an SImage otherwise reports its BRUSH size as its desired size, and
	// the 1024x1536 parchment dragged every panel to the full height of the screen (pause capture,
	// 2026-09-06). The overlay slots below stretch them to whatever the rows actually need.
	return SNew(SOverlay)
	+ SOverlay::Slot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SImage).Image(Style.GetParchmentBrush()).DesiredSizeOverride(FVector2D(4.0f, 4.0f))
	]
	+ SOverlay::Slot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SImage).Image(Style.GetFrameBrush()).DesiredSizeOverride(FVector2D(4.0f, 4.0f))
	]
	+ SOverlay::Slot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	.Padding(FMargin(FrameInsetX, FrameInsetY))
	[
		Content
	];
}

TSharedRef<SWidget> SWrecktideMenu::BuildRow(const UWrecktideMenuStyle& Style, EWrecktideMenuScreen Screen,
	const FWrecktideMenuRow& Row, int32 Index, bool bSelected)
{
	const int32 FontSize = EntryFontSize(Screen);
	const float RowTextWidth = (Screen == EWrecktideMenuScreen::Main ? MainPanelWidth : SubPanelWidth)
		- 2.0f * FrameInsetX - 46.0f;
	const bool bCaption = !Row.bSelectable;
	const FLinearColor LabelColour = bSelected
		? UWrecktideMenuStyle::GoldColour()
		: (bCaption ? UWrecktideMenuStyle::FadedInkColour() : UWrecktideMenuStyle::InkColour());

	TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);

	// the compass rose marks the selection; the slot is always there so labels never shift sideways
	Line->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
	[
		SNew(SBox)
		.WidthOverride(30.0f)
		.HeightOverride(30.0f)
		[
			SNew(SImage)
			.Image(Style.GetCompassBrush())
			.Visibility(bSelected ? EVisibility::Visible : EVisibility::Hidden)
		]
	];

	Line->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Row.Label))
		.Font(bCaption ? Style.GetBodyFont(FontSize) : Style.GetEntryFont(FontSize))
		.ColorAndOpacity(FSlateColor(LabelColour))
		.WrapTextAt(bCaption ? RowTextWidth : 0.0f)
		.ShadowOffset(bSelected ? FVector2D(1.0f, 1.0f) : FVector2D::ZeroVector)
		.ShadowColorAndOpacity(FLinearColor(0.10f, 0.05f, 0.0f, 0.7f))
	];

	if (!Row.Value.IsEmpty())
	{
		Line->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(12.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Row.Value))
			.Font(Style.GetEntryFont(FontSize - 4))
			.ColorAndOpacity(FSlateColor(bSelected ? UWrecktideMenuStyle::GoldColour() : UWrecktideMenuStyle::InkColour()))
		];
	}

	TSharedRef<SVerticalBox> Block = SNew(SVerticalBox);
	Block->AddSlot().AutoHeight()[Line];
	if (!Row.SubLine.IsEmpty())
	{
		Block->AddSlot()
		.AutoHeight()
		.Padding(FMargin(34.0f, 0.0f, 0.0f, 4.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Row.SubLine))
			.Font(Style.GetItalicFont(16))
			.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::FadedInkColour()))
			.WrapTextAt(RowTextWidth)
		];
	}

	if (bCaption)
	{
		return Block;
	}

	TWeakObjectPtr<ACombatPlayerController> WeakController = Controller;
	return SNew(SButton)
	.ButtonStyle(FCoreStyle::Get(), "NoBorder")
	.ContentPadding(FMargin(2.0f, 1.0f))
	// Not focusable: Slate must never steal the Enter key from the controller's key bindings, which
	// are the single source of truth for what a confirm does.
	.IsFocusable(false)
	.OnHovered_Lambda([WeakController, Index]()
	{
		if (ACombatPlayerController* Target = WeakController.Get())
		{
			// MenuHover, not MenuSetSelected: a rebuild under a stationary mouse re-fires this and
			// must not steal the selection from the row the keyboard is on.
			Target->MenuHover(Index);
		}
	})
	.OnClicked_Lambda([WeakController, Index]()
	{
		if (ACombatPlayerController* Target = WeakController.Get())
		{
			Target->MenuClick(Index);
		}
		return FReply::Handled();
	})
	[
		Block
	];
}
