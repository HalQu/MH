#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "Widgets/Layout/Anchors.h"
#include "Layout/Margin.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class UHorizontalBoxSlot;
class UPanelWidget;
class UTextBlock;
class UVerticalBoxSlot;
class UWidget;
class UWidgetTree;
class UBoxPanel;

namespace MHGameFlowUI
{
    MH_API extern const FLinearColor BackdropColor;
    MH_API extern const FLinearColor PanelColor;
    MH_API extern const FLinearColor PanelAltColor;
    MH_API extern const FLinearColor AccentColor;
    MH_API extern const FLinearColor TextColor;
    MH_API extern const FLinearColor MutedTextColor;
    MH_API extern const FLinearColor DangerColor;

    MH_API UBorder* CreateRootBackdrop(UWidgetTree* WidgetTree, UCanvasPanel*& OutRootCanvas);
    MH_API UBorder* CreatePanel(UWidgetTree* WidgetTree, const FLinearColor& Color = PanelColor);
    /**
     * 全屏居中面板：视口装不下时整体等比缩小，装得下时保持设计尺寸。
     * 避免 720p 或小窗口下底部按钮被裁掉。
     */
    MH_API UBorder* CreateCenteredScaledPanel(
        UWidgetTree* WidgetTree,
        UCanvasPanel* RootCanvas,
        float DesignWidth,
        float DesignHeight,
        const FLinearColor& Color = PanelColor);
    MH_API UTextBlock* CreateText(
        UWidgetTree* WidgetTree,
        const FText& Text,
        int32 FontSize = 18,
        const FLinearColor& Color = TextColor,
        ETextJustify::Type Justification = ETextJustify::Left);
    MH_API UButton* CreateButton(
        UWidgetTree* WidgetTree,
        const FText& Text,
        UTextBlock*& OutLabel,
        const FLinearColor& Color = PanelAltColor,
        int32 FontSize = 18);
    MH_API UWidget* CreateFixedHeight(UWidgetTree* WidgetTree, UWidget* Content, float Height);
    MH_API UCanvasPanelSlot* AddCanvasChild(
        UCanvasPanel* Canvas,
        UWidget* Widget,
        const FAnchors& Anchors,
        const FVector2D& Alignment,
        const FVector2D& Position,
        const FVector2D& Size,
        bool bAutoSize = false);
    MH_API UVerticalBoxSlot* AddVertical(
        UPanelWidget* Panel,
        UWidget* Widget,
        const FMargin& Padding = FMargin(0.f),
        EHorizontalAlignment HorizontalAlignment = HAlign_Fill);
    MH_API UHorizontalBoxSlot* AddHorizontal(
        UPanelWidget* Panel,
        UWidget* Widget,
        const FMargin& Padding = FMargin(0.f),
        EHorizontalAlignment HorizontalAlignment = HAlign_Fill,
        EVerticalAlignment VerticalAlignment = VAlign_Fill);
}
