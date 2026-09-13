#include "UI/Screen/SHuntingBaseHUD.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Styling/CoreStyle.h"

USHuntingBaseHUD::USHuntingBaseHUD()
{
}

void USHuntingBaseHUD::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureCombatDebugTextWidget();
}

void USHuntingBaseHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (UVMHuntingBaseHUD* VM = Cast<UVMHuntingBaseHUD>(GetViewModel()))
	{
		VM->RefreshCombatDebug();
	}
}

void USHuntingBaseHUD::OnOpen(UObject* Param)
{
	Super::OnOpen(Param);

	UVMHuntingBaseHUD* VM = Cast<UVMHuntingBaseHUD>(GetViewModel());
	BIND_VM_PROPERTY(VM, HealthPercent, &USHuntingBaseHUD::SetHealthBar);
	BIND_VM_PROPERTY(VM, CombatDebugText, &USHuntingBaseHUD::SetCombatDebugText);
}

void USHuntingBaseHUD::SetHealthBar(float Percent)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
	}
}

void USHuntingBaseHUD::SetCombatDebugText(FString Text)
{
	if (!CombatDebugTextBlock)
	{
		return;
	}

	CombatDebugTextBlock->SetText(FText::FromString(Text));
	CombatDebugTextBlock->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void USHuntingBaseHUD::EnsureCombatDebugTextWidget()
{
	if (CombatDebugTextBlock || !WidgetTree || !WidgetTree->RootWidget)
	{
		return;
	}

	UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	if (!RootPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SHuntingBaseHUD] Root widget is not a panel; combat debug text cannot be created."));
		return;
	}

	CombatDebugTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CombatDebugText"));
	CombatDebugTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.f)));
	CombatDebugTextBlock->SetShadowOffset(FVector2D(1.f, 1.f));
	CombatDebugTextBlock->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	CombatDebugTextBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 12));
	CombatDebugTextBlock->SetVisibility(ESlateVisibility::Collapsed);
	CombatDebugTextBlock->SetJustification(ETextJustify::Left);

	UPanelSlot* PanelSlot = RootPanel->AddChild(CombatDebugTextBlock);
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PanelSlot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(0.f, 1.f));
		CanvasSlot->SetPosition(FVector2D(24.f, -24.f));
		CanvasSlot->SetAutoSize(true);
	}
}
