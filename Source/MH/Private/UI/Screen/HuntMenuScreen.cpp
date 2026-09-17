#include "UI/Screen/HuntMenuScreen.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "GameFramework/PlayerController.h"
#include "GamePlay/MHGameState_Hunting.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/MHPlayerState.h"
#include "UI/Core/UIManager.h"
#include "UI/Screen/GameFlowUI.h"

UMHHuntMenuScreen::UMHHuntMenuScreen()
{
	InputModePolicy = EUIScreenInputMode::UIOnly;
	CloseAnimDuration = 0.f;
}

void UMHHuntMenuScreen::NativeConstruct()
{
	Super::NativeConstruct();
	BuildLayout();
}

void UMHHuntMenuScreen::OnOpen(UObject* Param)
{
	Super::OnOpen(Param);
	BuildLayout();
	RefreshStatus();
}

void UMHHuntMenuScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 请求未被服务器接受时自动恢复按钮，避免菜单卡在“正在结束本局狩猎”。
	if (bEndHuntPending)
	{
		EndHuntPendingRemaining = FMath::Max(0.f, EndHuntPendingRemaining - InDeltaTime);
		if (EndHuntPendingRemaining <= 0.f)
		{
			bEndHuntPending = false;
		}
	}

	RefreshStatus();
}

void UMHHuntMenuScreen::BuildLayout()
{
	if (TimerText || !WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = nullptr;
	UBorder* Backdrop = MHGameFlowUI::CreateRootBackdrop(WidgetTree, RootCanvas);
	Backdrop->SetBrushColor(FLinearColor(0.012f, 0.018f, 0.024f, 0.78f));


	UBorder* Panel = MHGameFlowUI::CreateCenteredScaledPanel(WidgetTree, RootCanvas, 620.f, 520.f);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HuntMenuContent"));
	Panel->SetContent(Content);

	UTextBlock* Title = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "HuntMenuTitle", "狩猎状态"),
		34);
	MHGameFlowUI::AddVertical(Content, Title, FMargin(0.f, 0.f, 0.f, 14.f));

	TimerText = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "HuntTimerPlaceholder", "剩余时间 00:00"),
		36,
		MHGameFlowUI::AccentColor,
		ETextJustify::Center);
	MHGameFlowUI::AddVertical(Content, TimerText, FMargin(0.f, 0.f, 0.f, 12.f));

	ResultText = MHGameFlowUI::CreateText(
		WidgetTree,
		FText::GetEmpty(),
		20,
		MHGameFlowUI::TextColor,
		ETextJustify::Center);
	MHGameFlowUI::AddVertical(Content, ResultText, FMargin(0.f, 0.f, 0.f, 8.f));

	StatusText = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "HuntStatusPlaceholder", "狩猎进行中"),
		15,
		MHGameFlowUI::MutedTextColor,
		ETextJustify::Center);
	MHGameFlowUI::AddVertical(Content, StatusText, FMargin(0.f, 0.f, 0.f, 24.f));

	UTextBlock* EndHuntLabel = nullptr;
	UTextBlock* CloseLabel = nullptr;
	UTextBlock* LeaveLabel = nullptr;

	EndHuntButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "EndHunt", "提前结束狩猎"),
		EndHuntLabel,
		MHGameFlowUI::DangerColor,
		19);
	EndHuntButtonLabel = EndHuntLabel;
	EndHuntButton->OnClicked.AddDynamic(this, &UMHHuntMenuScreen::HandleEndHuntClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, EndHuntButton, 50.f),
		FMargin(0.f, 0.f, 0.f, 10.f));

	CloseButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "ResumeHunt", "继续狩猎"),
		CloseLabel,
		MHGameFlowUI::PanelAltColor,
		19);
	CloseButtonLabel = CloseLabel;
	CloseButton->OnClicked.AddDynamic(this, &UMHHuntMenuScreen::HandleCloseClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, CloseButton, 50.f),
		FMargin(0.f, 0.f, 0.f, 10.f));

	LeaveButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "LeaveHunt", "离开房间"),
		LeaveLabel,
		MHGameFlowUI::DangerColor,
		18);
	LeaveButtonLabel = LeaveLabel;
	LeaveButton->OnClicked.AddDynamic(this, &UMHHuntMenuScreen::HandleLeaveClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, LeaveButton, 44.f));
}

void UMHHuntMenuScreen::RefreshStatus()
{
	const APlayerController* PlayerController = GetOwningPlayer();
	const AMHPlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AMHPlayerState>()
		: nullptr;
	const bool bLocalHost = PlayerState && PlayerState->bIsHost;
	const AMHGameState_Hunting* HuntingState = GetWorld()
		? GetWorld()->GetGameState<AMHGameState_Hunting>()
		: nullptr;

	if (EndHuntButton)
	{
		EndHuntButton->SetVisibility(bLocalHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		EndHuntButton->SetIsEnabled(bLocalHost && !bEndHuntPending && HuntingState && HuntingState->IsMatchActive());
	}

	if (!HuntingState)
	{
		if (TimerText)
		{
			TimerText->SetText(NSLOCTEXT("MHFlow", "HuntSyncing", "正在同步狩猎状态..."));
		}
		if (ResultText)
		{
			ResultText->SetText(FText::GetEmpty());
			ResultText->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (StatusText)
		{
			StatusText->SetText(FText::GetEmpty());
		}
		return;
	}

	const float RemainingTime = HuntingState->GetRemainingTime();
	const int32 TotalSeconds = FMath::Max(0, FMath::CeilToInt(RemainingTime));
	const int32 Minutes = TotalSeconds / 60;
	const int32 Seconds = TotalSeconds % 60;

	if (TimerText)
	{
		TimerText->SetText(FText::FromString(
			HuntingState->IsMatchActive()
				? FString::Printf(TEXT("剩余时间 %02d:%02d"), Minutes, Seconds)
				: TEXT("狩猎结束")));
	}

	if (ResultText)
	{
		const FText Result = HuntingState->GetResultText();
		ResultText->SetText(Result);
		ResultText->SetVisibility(Result.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (StatusText)
	{
		StatusText->SetText(HuntingState->IsMatchActive()
			? NSLOCTEXT("MHFlow", "HuntInProgress", "狩猎进行中，按 Esc 可关闭此菜单。")
			: NSLOCTEXT("MHFlow", "ReturningToLobby", "正在返回集会所..."));
	}
}

void UMHHuntMenuScreen::HandleEndHuntClicked()
{
	if (bEndHuntPending)
	{
		return;
	}

	if (AMHPlayerController* PlayerController = Cast<AMHPlayerController>(GetOwningPlayer()))
	{
		bEndHuntPending = true;
		EndHuntPendingRemaining = 2.5f;
		if (StatusText)
		{
			StatusText->SetText(NSLOCTEXT("MHFlow", "EndingHunt", "正在结束本局狩猎..."));
		}
		PlayerController->Server_RequestEndHunt();
	}
}

void UMHHuntMenuScreen::HandleCloseClicked()
{
	if (UUIManager* UIManager = GetUIManager())
	{
		UIManager->PopOverlay();
	}
}

void UMHHuntMenuScreen::HandleLeaveClicked()
{
	if (AMHPlayerController* PlayerController = Cast<AMHPlayerController>(GetOwningPlayer()))
	{
		PlayerController->RequestLeaveToMainMenu();
	}
}

void UMHHuntMenuScreen::OnBack_Implementation()
{
	if (UUIManager* UIManager = GetUIManager())
	{
		UIManager->PopOverlay();
	}
}
