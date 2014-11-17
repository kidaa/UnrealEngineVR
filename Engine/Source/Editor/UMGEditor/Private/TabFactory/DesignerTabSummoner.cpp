// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"

#include "DesignerTabSummoner.h"
#include "SDesignerView.h"

#define LOCTEXT_NAMESPACE "UMG"

const FName FDesignerTabSummoner::TabID(TEXT("SlatePreview"));

FDesignerTabSummoner::FDesignerTabSummoner(TSharedPtr<class FWidgetBlueprintEditor> InBlueprintEditor)
		: FWorkflowTabFactory(TabID, InBlueprintEditor)
		, BlueprintEditor(InBlueprintEditor)
{
	TabLabel = LOCTEXT("DesignerTabLabel", "Designer");
	TabIcon = FEditorStyle::GetBrush("UMGEditor.Tabs.Designer");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("SlatePreview_ViewMenu_Desc", "Designer");
	ViewMenuTooltip = LOCTEXT("SlatePreview_ViewMenu_ToolTip", "Show the Designer");
}

TSharedRef<SWidget> FDesignerTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(STutorialWrapper, TEXT("Designer"))
			[
				SNew(SDesignerView, BlueprintEditor.Pin())
			]
		];
}

#undef LOCTEXT_NAMESPACE 