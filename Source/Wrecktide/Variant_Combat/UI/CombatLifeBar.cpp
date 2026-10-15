// Copyright Epic Games, Inc. All Rights Reserved.
// Modifications Copyright (c) 2026 Alaa Soulaiman. Licensed under the MIT License, see LICENSE.


#include "CombatLifeBar.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"

void UCombatLifeBar::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureEnemyNameText();
}

FLinearColor UCombatLifeBar::GetRelativeLevelNameColor(int32 EnemyLevel, int32 NotorietyLevel)
{
	const int32 Difference = EnemyLevel - NotorietyLevel;
	if (Difference <= -5)
	{
		return FLinearColor(0.55f, 0.55f, 0.55f, 1.0f);
	}
	if (Difference <= -2)
	{
		return FLinearColor(0.20f, 0.90f, 0.28f, 1.0f);
	}
	if (Difference <= 1)
	{
		return FLinearColor(1.0f, 0.82f, 0.18f, 1.0f);
	}
	return FLinearColor(0.95f, 0.12f, 0.10f, 1.0f);
}

void UCombatLifeBar::SetEnemyName(const FText& DisplayName, int32 EnemyLevel, int32 NotorietyLevel)
{
	PendingEnemyName = DisplayName;
	PendingEnemyNameColor = GetRelativeLevelNameColor(EnemyLevel, NotorietyLevel);
	EnsureEnemyNameText();
	if (EnemyNameText)
	{
		EnemyNameText->SetText(PendingEnemyName);
		EnemyNameText->SetColorAndOpacity(FSlateColor(PendingEnemyNameColor));
	}
}

void UCombatLifeBar::EnsureEnemyNameText()
{
	if (EnemyNameText || !WidgetTree)
	{
		return;
	}

	EnemyNameText = Cast<UTextBlock>(WidgetTree->FindWidget(FName(TEXT("EnemyName"))));
	if (!EnemyNameText)
	{
		UOverlay* RootOverlay = Cast<UOverlay>(WidgetTree->RootWidget);
		if (!RootOverlay)
		{
			return;
		}
		EnemyNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(TEXT("EnemyName")));
		if (UOverlaySlot* NameSlot = RootOverlay->AddChildToOverlay(EnemyNameText))
		{
			NameSlot->SetHorizontalAlignment(HAlign_Center);
			NameSlot->SetVerticalAlignment(VAlign_Top);
		}
	}

	if (EnemyNameText)
	{
		FSlateFontInfo Font = EnemyNameText->GetFont();
		Font.Size = 16;
		EnemyNameText->SetFont(Font);
		EnemyNameText->SetJustification(ETextJustify::Center);
		EnemyNameText->SetShadowOffset(FVector2D(1.0f, 1.0f));
		EnemyNameText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		EnemyNameText->SetText(PendingEnemyName);
		EnemyNameText->SetColorAndOpacity(FSlateColor(PendingEnemyNameColor));
	}
}

void UCombatLifeBar::SetRangedTell(bool bVisible)
{
	bRangedTellVisible = bVisible;
	EnsureEnemyNameText();
	if (EnemyNameText)
	{
		EnemyNameText->SetText(bRangedTellVisible
			? FText::Format(NSLOCTEXT("Wrecktide", "RangedVolleyTell", "{0}  !"), PendingEnemyName)
			: PendingEnemyName);
	}
}

