// Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.

#include "UI/SWrecktideChest.h"

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
constexpr float ChestPanelWidth = 430.0f;      // the right third of a 1280 wide window
constexpr float ChestSideInset = 18.0f;        // gap from the right edge of the screen
constexpr float ChestVerticalInset = 40.0f;
/** Leaves the bottom toolbar visible and reachable at the capture resolution, 1280x720. */
constexpr float ChestToolbarBottomInset = 128.0f;
constexpr float ChestFrameInsetX = 48.0f;      // clears the rope border and the parchment's burnt edge
constexpr float ChestFrameInsetY = 42.0f;
constexpr float ChestDimAlpha = 0.50f;
constexpr float TabRailWidth = 124.0f;         // the page rail down the panel's left edge; wide
                                               // enough for the longest name at the face size below
                                               // (96 clipped "Journal" and "Ledger", capture 09-06)
constexpr float TabHeight = 54.0f;

/** Page name and its own letter key (research A1: every page carried one). */
const TCHAR* TabLabel(EWrecktideChestTab Tab)
{
	switch (Tab)
	{
	case EWrecktideChestTab::Weapons: return TEXT("Belt");
	case EWrecktideChestTab::Outfits: return TEXT("Slops");
	case EWrecktideChestTab::Journal: return TEXT("Journal");
	default: return TEXT("Ledger");
	}
}

const TCHAR* TabKey(EWrecktideChestTab Tab)
{
	switch (Tab)
	{
	case EWrecktideChestTab::Weapons: return TEXT("1");
	case EWrecktideChestTab::Outfits: return TEXT("2");
	case EWrecktideChestTab::Journal: return TEXT("J");
	default: return TEXT("4");
	}
}

FLinearColor ChestDividerColour()
{
	return FLinearColor(0.24f, 0.15f, 0.07f, 0.55f);
}
}

void SWrecktideChest::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	// Visible, not SelfHitTestInvisible: an open chest is modal, so it swallows the clicks that miss
	// a row rather than letting them reach the game underneath.
	SetVisibility(EVisibility::Visible);
	Rebuild();
}

void SWrecktideChest::Rebuild()
{
	ACombatPlayerController* PlayerController = Controller.Get();
	const UWrecktideMenuStyle* StylePtr = UWrecktideMenuStyle::Get();
	if (!PlayerController || !StylePtr || !PlayerController->IsInventoryOpen())
	{
		ChildSlot[SNullWidget::NullWidget];
		return;
	}

	const UWrecktideMenuStyle& Style = *StylePtr;
	TSharedRef<SOverlay> Root = SNew(SOverlay);

	// 1. the dimming sheet over the paused world
	Root->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SImage)
		.Image(Style.GetDimBrush())
		.DesiredSizeOverride(FVector2D(4.0f, 4.0f))
		.ColorAndOpacity(FLinearColor(0.02f, 0.015f, 0.01f, ChestDimAlpha))
	];

	// 2. the rail and the panel, pinned to the right edge, rail on the panel's LEFT (research A1)
	Root->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Fill)
	.Padding(FMargin(0.0f, ChestVerticalInset, ChestSideInset, ChestToolbarBottomInset))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			BuildTabRail(Style)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(ChestPanelWidth)
			[
				BuildPanel(Style)
			]
		]
	];

	ChildSlot[Root];
}

FReply SWrecktideChest::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	ACombatPlayerController* PC = Controller.Get();
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	if (PC && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
		&& LocalSize.X > 0.0f && LocalSize.Y > 0.0f)
	{
		int32 ViewWidth = 0;
		int32 ViewHeight = 0;
		PC->GetViewportSize(ViewWidth, ViewHeight);
		// Slate units may be DPI-scaled; the HUD records physical viewport pixels.
		const FVector2D LocalPoint = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D ViewPoint(LocalPoint.X * ViewWidth / LocalSize.X, LocalPoint.Y * ViewHeight / LocalSize.Y);
		if (PC->HandleModalClickAt(ViewPoint))
		{
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SWrecktideChest::BuildTabRail(const UWrecktideMenuStyle& Style)
{
	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);
	for (int32 Index = 0; Index < static_cast<int32>(EWrecktideChestTab::Count); ++Index)
	{
		Rail->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 3.0f))
		[
			BuildTab(Style, static_cast<EWrecktideChestTab>(Index))
		];
	}
	return Rail;
}

TSharedRef<SWidget> SWrecktideChest::BuildTab(const UWrecktideMenuStyle& Style, EWrecktideChestTab Tab)
{
	ACombatPlayerController* PlayerController = Controller.Get();
	const bool bCurrent = PlayerController && PlayerController->GetChestTab() == Tab;
	const FLinearColor Ink = bCurrent ? UWrecktideMenuStyle::GoldColour() : UWrecktideMenuStyle::InkColour();

	TSharedRef<SVerticalBox> Face = SNew(SVerticalBox);
	Face->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Center)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TabLabel(Tab)))
		.Font(Style.GetEntryFont(bCurrent ? 17 : 16))
		.ColorAndOpacity(FSlateColor(Ink))
	];
	Face->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Center)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TabKey(Tab)))
		.Font(Style.GetBodyFont(13))
		.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::FadedInkColour()))
	];

	TWeakObjectPtr<ACombatPlayerController> WeakController = Controller;
	return SNew(SBox)
	.WidthOverride(TabRailWidth)
	.HeightOverride(TabHeight)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SImage)
			.Image(Style.GetParchmentBrush())
			.DesiredSizeOverride(FVector2D(4.0f, 4.0f))
			.ColorAndOpacity(bCurrent ? FLinearColor::White : FLinearColor(0.78f, 0.74f, 0.66f, 1.0f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(2.0f, 4.0f))
			// Not focusable, for the same reason the menu rows are not: Slate must never take the
			// keys away from the controller's bindings, which are the one source of truth.
			.IsFocusable(false)
			.OnClicked_Lambda([WeakController, Tab]()
			{
				if (ACombatPlayerController* Target = WeakController.Get())
				{
					Target->SetChestTab(Tab);
				}
				return FReply::Handled();
			})
			[
				Face
			]
		]
	];
}

TSharedRef<SWidget> SWrecktideChest::BuildPanel(const UWrecktideMenuStyle& Style)
{
	ACombatPlayerController* PlayerController = Controller.Get();
	if (!PlayerController)
	{
		return SNullWidget::NullWidget;
	}

	const float TextWidth = ChestPanelWidth - 2.0f * ChestFrameInsetX - 12.0f;
	const TArray<FWrecktideMenuRow>& Rows = PlayerController->GetMenuRows();
	const int32 Selected = PlayerController->GetMenuSelectedIndex();

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Center)
	[
		SNew(STextBlock)
		.Text(FText::FromString(PlayerController->GetMenuTitle()))
		.Font(Style.GetTitleFont(34))
		.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::GoldColour()))
		.ShadowOffset(FVector2D(2.0f, 2.0f))
		.ShadowColorAndOpacity(FLinearColor(0.06f, 0.03f, 0.0f, 0.85f))
		.Justification(ETextJustify::Center)
	];

	const FString Subtitle = PlayerController->GetMenuSubtitle();
	if (!Subtitle.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Subtitle))
			.Font(Style.GetItalicFont(20))
			.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::FadedInkColour()))
			.Justification(ETextJustify::Center)
		];
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(4.0f, 10.0f, 4.0f, 10.0f))
	[
		SNew(SBox)
		.HeightOverride(2.0f)
		[
			SNew(SImage)
			.Image(Style.GetDimBrush())
			.DesiredSizeOverride(FVector2D(4.0f, 4.0f))
			.ColorAndOpacity(ChestDividerColour())
		]
	];

	// A scroll box, not a plain list: the journal page grows with the chain, and the wheel already
	// moves the selection, so this only ever has to survive a page that outgrows the parchment.
	TSharedRef<SScrollBox> List = SNew(SScrollBox);
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		List->AddSlot()
		.Padding(FMargin(0.0f, 2.0f))
		[
			BuildRow(Style, Rows[Index], Index, Index == Selected)
		];
	}
	Content->AddSlot()
	.FillHeight(1.0f)
	[
		List
	];

	const FString Hint = PlayerController->GetMenuHint();
	if (!Hint.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Hint))
			.Font(Style.GetBodyFont(14))
			.ColorAndOpacity(FSlateColor(UWrecktideMenuStyle::InkColour()))
			.Justification(ETextJustify::Center)
			.WrapTextAt(TextWidth)
		];
	}

	// DesiredSizeOverride on both brushes: an SImage otherwise reports its BRUSH size as its desired
	// size, and the 1024x1536 parchment would drag the panel to its own dimensions.
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
	.Padding(FMargin(ChestFrameInsetX, ChestFrameInsetY))
	[
		Content
	];
}

TSharedRef<SWidget> SWrecktideChest::BuildRow(const UWrecktideMenuStyle& Style, const FWrecktideMenuRow& Row,
	int32 Index, bool bSelected)
{
	const float RowTextWidth = ChestPanelWidth - 2.0f * ChestFrameInsetX - 44.0f;
	const bool bCaption = !Row.bSelectable;
	const FLinearColor LabelColour = bSelected
		? UWrecktideMenuStyle::GoldColour()
		: (bCaption ? UWrecktideMenuStyle::FadedInkColour() : UWrecktideMenuStyle::InkColour());

	TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);

	Line->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
	[
		SNew(SBox)
		.WidthOverride(22.0f)
		.HeightOverride(22.0f)
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
		.Font(bCaption ? Style.GetBodyFont(16) : Style.GetEntryFont(18))
		.ColorAndOpacity(FSlateColor(LabelColour))
		.WrapTextAt(RowTextWidth)
	];

	if (!Row.Value.IsEmpty())
	{
		Line->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Row.Value))
			.Font(Style.GetEntryFont(15))
			.ColorAndOpacity(FSlateColor(bSelected ? UWrecktideMenuStyle::GoldColour() : UWrecktideMenuStyle::InkColour()))
		];
	}

	TSharedRef<SVerticalBox> Block = SNew(SVerticalBox);
	Block->AddSlot().AutoHeight()[Line];
	if (!Row.SubLine.IsEmpty())
	{
		Block->AddSlot()
		.AutoHeight()
		.Padding(FMargin(28.0f, 0.0f, 0.0f, 3.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Row.SubLine))
			.Font(Style.GetItalicFont(13))
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
