#include "UI/Screen/HuntingStatusHUD.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "GamePlay/MHGameState_Hunting.h"
#include "UI/Screen/GameFlowUI.h"

UMHHuntingStatusHUD::UMHHuntingStatusHUD()
{
	InputModePolicy = EUIScreenInputMode::GameOnly;
	CloseAnimDuration = 0.f;
}

void UMHHuntingStatusHUD::NativeConstruct()
{
	Super::NativeConstruct();
	BuildLayout();
	RefreshStatus();
}

void UMHHuntingStatusHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshStatus();
}

void UMHHuntingStatusHUD::BuildLayout()
{
	if (TimerText || !WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(),
		TEXT("HuntingStatusRoot"));
	if (!RootCanvas)
	{
		return;
	}

	if (!WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = RootCanvas;
	}

	UBorder* Panel = MHGameFlowUI::CreatePanel(WidgetTree, MHGameFlowUI::PanelColor);
	Panel->SetPadding(FMargin(22.f, 12.f));
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);

	USizeBox* ContentSize = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		TEXT("HuntingStatusSize"));
	ContentSize->SetWidthOverride(340.f);
	Panel->SetContent(ContentSize);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(),
		TEXT("HuntingStatusContent"));
	Content->SetVisibility(ESlateVisibility::HitTestInvisible);
	ContentSize->AddChild(Content);

	TimerText = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "HuntingStatusSyncing", "正在同步狩猎状态..."),
		24,
		MHGameFlowUI::AccentColor,
		ETextJustify::Center);
	TimerText->SetVisibility(ESlateVisibility::HitTestInvisible);
	MHGameFlowUI::AddVertical(Content, TimerText, FMargin(0.f, 0.f, 0.f, 4.f));

	ResultText = MHGameFlowUI::CreateText(
		WidgetTree,
		FText::GetEmpty(),
		17,
		MHGameFlowUI::TextColor,
		ETextJustify::Center);
	ResultText->SetVisibility(ESlateVisibility::Collapsed);
	MHGameFlowUI::AddVertical(Content, ResultText);

	MHGameFlowUI::AddCanvasChild(
		RootCanvas,
		Panel,
		FAnchors(0.5f, 0.f),
		FVector2D(0.5f, 0.f),
		FVector2D(0.f, 20.f),
		FVector2D::ZeroVector,
		true);
}

void UMHHuntingStatusHUD::RefreshStatus()
{
	if (!TimerText)
	{
		return;
	}

	const AMHGameState_Hunting* HuntingState = GetWorld()
		? GetWorld()->GetGameState<AMHGameState_Hunting>()
		: nullptr;

	if (!HuntingState)
	{
		TimerText->SetText(NSLOCTEXT("MHFlow", "HuntingStatusSyncing", "正在同步狩猎状态..."));
		if (ResultText)
		{
			ResultText->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (HuntingState->IsMatchActive())
	{
		const int32 TotalSeconds = FMath::Max(0, FMath::CeilToInt(HuntingState->GetRemainingTime()));
		TimerText->SetText(FText::FromString(FString::Printf(
			TEXT("剩余 %02d:%02d"),
			TotalSeconds / 60,
			TotalSeconds % 60)));
	}
	else
	{
		TimerText->SetText(NSLOCTEXT("MHFlow", "HuntingFinished", "狩猎结束"));
	}

	if (ResultText)
	{
		const FText Result = HuntingState->GetResultText();
		ResultText->SetText(Result);
		ResultText->SetVisibility(
			Result.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}
