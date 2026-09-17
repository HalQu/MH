#include "UI/Screen/GameFlowUI.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace MHGameFlowUI
{
    const FLinearColor BackdropColor(0.018f, 0.024f, 0.032f, 0.94f);
    const FLinearColor PanelColor(0.035f, 0.048f, 0.062f, 0.98f);
    const FLinearColor PanelAltColor(0.105f, 0.145f, 0.175f, 1.f);
    const FLinearColor AccentColor(0.20f, 0.72f, 0.56f, 1.f);
    const FLinearColor TextColor(0.94f, 0.96f, 0.97f, 1.f);
    const FLinearColor MutedTextColor(0.62f, 0.68f, 0.72f, 1.f);
    const FLinearColor DangerColor(0.84f, 0.30f, 0.25f, 1.f);

    UBorder* CreateRootBackdrop(UWidgetTree* WidgetTree, UCanvasPanel*& OutRootCanvas)
    {
        OutRootCanvas = WidgetTree ? WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FlowRootCanvas")) : nullptr;
        if (!OutRootCanvas)
        {
            return nullptr;
        }

        if (!WidgetTree->RootWidget)
        {
            WidgetTree->RootWidget = OutRootCanvas;
        }

        UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FlowBackdrop"));
        Backdrop->SetBrushColor(BackdropColor);
        Backdrop->SetPadding(FMargin(0.f));
        AddCanvasChild(
            OutRootCanvas,
            Backdrop,
            FAnchors(0.f, 0.f, 1.f, 1.f),
            FVector2D::ZeroVector,
            FVector2D::ZeroVector,
            FVector2D::ZeroVector);
        return Backdrop;
    }

    UBorder* CreatePanel(UWidgetTree* WidgetTree, const FLinearColor& Color)
    {
        UBorder* Panel = WidgetTree ? WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass()) : nullptr;
        if (Panel)
        {
            Panel->SetBrushColor(Color);
            Panel->SetPadding(FMargin(26.f, 24.f));
        }
        return Panel;
    }

    UBorder* CreateCenteredScaledPanel(
        UWidgetTree* WidgetTree,
        UCanvasPanel* RootCanvas,
        float DesignWidth,
        float DesignHeight,
        const FLinearColor& Color)
    {
        if (!WidgetTree || !RootCanvas)
        {
            return nullptr;
        }

        // 只缩小不放大：大分辨率保持设计尺寸，小分辨率或小窗口整体等比缩放，避免底部被裁切。
        UScaleBox* ScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
        ScaleBox->SetStretch(EStretch::ScaleToFit);
        ScaleBox->SetStretchDirection(EStretchDirection::DownOnly);
        AddCanvasChild(
            RootCanvas,
            ScaleBox,
            FAnchors(0.f, 0.f, 1.f, 1.f),
            FVector2D::ZeroVector,
            FVector2D::ZeroVector,
            FVector2D::ZeroVector);

        USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        SizeBox->SetWidthOverride(DesignWidth);
        SizeBox->SetHeightOverride(DesignHeight);
        ScaleBox->AddChild(SizeBox);

        UBorder* Panel = CreatePanel(WidgetTree, Color);
        SizeBox->AddChild(Panel);
        return Panel;
    }

    UTextBlock* CreateText(
        UWidgetTree* WidgetTree,
        const FText& Text,
        int32 FontSize,
        const FLinearColor& Color,
        ETextJustify::Type Justification)
    {
        UTextBlock* TextBlock = WidgetTree ? WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()) : nullptr;
        if (!TextBlock)
        {
            return nullptr;
        }

        TextBlock->SetText(Text);
        TextBlock->SetColorAndOpacity(FSlateColor(Color));
        TextBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize));
        TextBlock->SetJustification(Justification);
        TextBlock->SetAutoWrapText(true);
        return TextBlock;
    }

    UButton* CreateButton(
        UWidgetTree* WidgetTree,
        const FText& Text,
        UTextBlock*& OutLabel,
        const FLinearColor& Color,
        int32 FontSize)
    {
        UButton* Button = WidgetTree ? WidgetTree->ConstructWidget<UButton>(UButton::StaticClass()) : nullptr;
        if (!Button)
        {
            OutLabel = nullptr;
            return nullptr;
        }

        Button->SetBackgroundColor(Color);
        OutLabel = CreateText(WidgetTree, Text, FontSize, TextColor, ETextJustify::Center);
        if (OutLabel)
        {
            Button->AddChild(OutLabel);
        }
        return Button;
    }

    UWidget* CreateFixedHeight(UWidgetTree* WidgetTree, UWidget* Content, float Height)
    {
        USizeBox* SizeBox = WidgetTree ? WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()) : nullptr;
        if (SizeBox)
        {
            SizeBox->SetHeightOverride(Height);
            if (Content)
            {
                SizeBox->AddChild(Content);
            }
        }
        return SizeBox;
    }

    UCanvasPanelSlot* AddCanvasChild(
        UCanvasPanel* Canvas,
        UWidget* Widget,
        const FAnchors& Anchors,
        const FVector2D& Alignment,
        const FVector2D& Position,
        const FVector2D& Size,
        bool bAutoSize)
    {
        if (!Canvas || !Widget)
        {
            return nullptr;
        }

        UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
        if (!Slot)
        {
            return nullptr;
        }

        Slot->SetAnchors(Anchors);
        Slot->SetAlignment(Alignment);
        Slot->SetPosition(Position);
        Slot->SetSize(Size);
        Slot->SetAutoSize(bAutoSize);
        if (Anchors.Minimum == FVector2D(0.f, 0.f) && Anchors.Maximum == FVector2D(1.f, 1.f))
        {
            Slot->SetOffsets(FMargin(0.f));
        }
        return Slot;
    }

    UVerticalBoxSlot* AddVertical(
        UPanelWidget* Panel,
        UWidget* Widget,
        const FMargin& Padding,
        EHorizontalAlignment HorizontalAlignment)
    {
        if (!Panel || !Widget)
        {
            return nullptr;
        }

        UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Panel->AddChild(Widget));
        if (Slot)
        {
            Slot->SetPadding(Padding);
            Slot->SetHorizontalAlignment(HorizontalAlignment);
        }
        return Slot;
    }

    UHorizontalBoxSlot* AddHorizontal(
        UPanelWidget* Panel,
        UWidget* Widget,
        const FMargin& Padding,
        EHorizontalAlignment HorizontalAlignment,
        EVerticalAlignment VerticalAlignment)
    {
        if (!Panel || !Widget)
        {
            return nullptr;
        }

        UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Panel->AddChild(Widget));
        if (Slot)
        {
            Slot->SetPadding(Padding);
            Slot->SetHorizontalAlignment(HorizontalAlignment);
            Slot->SetVerticalAlignment(VerticalAlignment);
        }
        return Slot;
    }
}
