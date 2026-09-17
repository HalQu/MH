#include "UI/Screen/LobbyScreen.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GamePlay/MHPlayerController.h"
#include "GamePlay/MHPlayerState.h"
#include "UI/Screen/GameFlowUI.h"

ULobbyScreen::ULobbyScreen()
{
	InputModePolicy = EUIScreenInputMode::UIOnly;
	CloseAnimDuration = 0.f;
}

void ULobbyScreen::NativeConstruct()
{
	Super::NativeConstruct();
	BuildLayout();
}

void ULobbyScreen::OnOpen(UObject* Param)
{
	Super::OnOpen(Param);
	BuildLayout();
	RefreshAccumulator = 1.f;
	RefreshLobby();
}

void ULobbyScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (ActionLockRemaining > 0.f)
	{
		ActionLockRemaining = FMath::Max(0.f, ActionLockRemaining - InDeltaTime);
		if (ActionLockRemaining <= 0.f)
		{
			bActionPending = false;
		}
	}

	// 房主请求未被服务器接受时自动恢复按钮，避免卡在“正在进入狩猎地图”。
	if (bStartPending)
	{
		StartPendingRemaining = FMath::Max(0.f, StartPendingRemaining - InDeltaTime);
		if (StartPendingRemaining <= 0.f)
		{
			bStartPending = false;
		}
	}

	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 0.25f)
	{
		RefreshAccumulator = 0.f;
		RefreshLobby();
	}
}

void ULobbyScreen::BuildLayout()
{
	if (PlayerList || !WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = nullptr;
	MHGameFlowUI::CreateRootBackdrop(WidgetTree, RootCanvas);


	UBorder* Panel = MHGameFlowUI::CreateCenteredScaledPanel(WidgetTree, RootCanvas, 760.f, 680.f);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LobbyContent"));
	Panel->SetContent(Content);

	UTextBlock* Title = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "LobbyTitle", "狩猎集会所"),
		38);
	MHGameFlowUI::AddVertical(Content, Title, FMargin(0.f, 0.f, 0.f, 4.f));

	UTextBlock* Subtitle = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "LobbySubtitle", "全员准备后，由房主开始狩猎"),
		15,
		MHGameFlowUI::MutedTextColor);
	MHGameFlowUI::AddVertical(Content, Subtitle, FMargin(0.f, 0.f, 0.f, 20.f));

	UTextBlock* ListTitle = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "HunterList", "狩猎小队"),
		16,
		MHGameFlowUI::MutedTextColor);
	MHGameFlowUI::AddVertical(Content, ListTitle, FMargin(0.f, 0.f, 0.f, 8.f));

	UScrollBox* PlayerScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LobbyPlayerScroll"));
	PlayerScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	PlayerList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LobbyPlayerList"));
	PlayerScroll->AddChild(PlayerList);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, PlayerScroll, 290.f),
		FMargin(0.f, 0.f, 0.f, 14.f));

	StatusText = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "LobbyWaiting", "等待玩家准备..."),
		16,
		MHGameFlowUI::MutedTextColor);
	MHGameFlowUI::AddVertical(Content, StatusText, FMargin(0.f, 0.f, 0.f, 16.f));

	UTextBlock* ReadyLabel = nullptr;
	UTextBlock* StartLabel = nullptr;
	UTextBlock* LeaveLabel = nullptr;

	ReadyButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "ReadyAction", "准备"),
		ReadyLabel,
		MHGameFlowUI::AccentColor,
		20);
	ReadyButtonLabel = ReadyLabel;
	ReadyButton->OnClicked.AddDynamic(this, &ULobbyScreen::HandleReadyClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, ReadyButton, 52.f),
		FMargin(0.f, 0.f, 0.f, 10.f));

	StartButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "StartHunt", "开始狩猎"),
		StartLabel,
		MHGameFlowUI::PanelAltColor,
		20);
	StartButtonLabel = StartLabel;
	StartButton->OnClicked.AddDynamic(this, &ULobbyScreen::HandleStartClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, StartButton, 52.f),
		FMargin(0.f, 0.f, 0.f, 10.f));

	LeaveButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "LeaveLobby", "离开房间"),
		LeaveLabel,
		MHGameFlowUI::DangerColor,
		18);
	LeaveButtonLabel = LeaveLabel;
	LeaveButton->OnClicked.AddDynamic(this, &ULobbyScreen::HandleLeaveClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, LeaveButton, 46.f));
}

void ULobbyScreen::RefreshLobby()
{
	if (!PlayerList)
	{
		return;
	}

	PlayerList->ClearChildren();
	PlayerRows.Reset();

	AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AGameStateBase>() : nullptr;
	TArray<APlayerState*> Players;
	if (GameState)
	{
		Players.Reserve(GameState->PlayerArray.Num());
		for (const TObjectPtr<APlayerState>& Player : GameState->PlayerArray)
		{
			Players.Add(Player.Get());
		}
	}
	Players.Sort(
		[](const APlayerState& Left, const APlayerState& Right)
		{
			const AMHPlayerState* LeftMH = Cast<AMHPlayerState>(&Left);
			const AMHPlayerState* RightMH = Cast<AMHPlayerState>(&Right);
			const bool bLeftHost = LeftMH && LeftMH->bIsHost;
			const bool bRightHost = RightMH && RightMH->bIsHost;
			if (bLeftHost != bRightHost)
			{
				return bLeftHost;
			}
			return Left.GetPlayerName() < Right.GetPlayerName();
		});

	int32 ReadyCount = 0;
	bool bAllReady = Players.Num() > 0;
	for (APlayerState* PlayerState : Players)
	{
		const AMHPlayerState* MHPlayerState = Cast<AMHPlayerState>(PlayerState);
		if (MHPlayerState && MHPlayerState->bReady)
		{
			++ReadyCount;
		}
		else
		{
			bAllReady = false;
		}

		if (!PlayerState)
		{
			continue;
		}

		const FString HostMark = MHPlayerState && MHPlayerState->bIsHost ? TEXT("[房主] ") : FString();
		const FString ReadyMark = MHPlayerState && MHPlayerState->bReady ? TEXT("  已准备") : TEXT("  未准备");
		const FString PlayerName = PlayerState->GetPlayerName().IsEmpty()
			? TEXT("未知猎人")
			: PlayerState->GetPlayerName();

		UBorder* Row = MHGameFlowUI::CreatePanel(WidgetTree, MHGameFlowUI::PanelAltColor);
		Row->SetPadding(FMargin(16.f, 10.f));
		UTextBlock* RowText = MHGameFlowUI::CreateText(
			WidgetTree,
			FText::FromString(FString::Printf(TEXT("%s%s%s"), *HostMark, *PlayerName, *ReadyMark)),
			17);
		Row->SetContent(RowText);

		UWidget* RowSize = MHGameFlowUI::CreateFixedHeight(WidgetTree, Row, 50.f);
		MHGameFlowUI::AddVertical(PlayerList, RowSize, FMargin(0.f, 0.f, 0.f, 8.f));
		PlayerRows.Add(RowSize);
	}

	const AMHPlayerState* LocalPlayerState = GetLocalPlayerState();
	const bool bLocalHost = LocalPlayerState && LocalPlayerState->bIsHost;
	const bool bLocalReady = LocalPlayerState && LocalPlayerState->bReady;

	if (ReadyButton)
	{
		ReadyButton->SetIsEnabled(LocalPlayerState && !bActionPending);
	}
	if (ReadyButtonLabel)
	{
		ReadyButtonLabel->SetText(bLocalReady
			? NSLOCTEXT("MHFlow", "CancelReady", "取消准备")
			: NSLOCTEXT("MHFlow", "ConfirmReady", "准备"));
	}

	if (StartButton)
	{
		StartButton->SetVisibility(bLocalHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		StartButton->SetIsEnabled(bLocalHost && bAllReady && !bStartPending);
	}
	if (StartButtonLabel)
	{
		StartButtonLabel->SetText(bAllReady
			? NSLOCTEXT("MHFlow", "StartHuntReady", "开始狩猎")
			: NSLOCTEXT("MHFlow", "WaitingForReady", "等待全员准备"));
	}

	if (bActionPending)
	{
		SetStatus(TEXT("正在同步准备状态..."));
	}
	else if (Players.IsEmpty())
	{
		SetStatus(TEXT("正在等待玩家加入..."));
	}
	else if (bAllReady)
	{
		SetStatus(bLocalHost
			? TEXT("全员已准备，可以开始狩猎。")
			: TEXT("全员已准备，等待房主开始狩猎。"));
	}
	else
	{
		SetStatus(FString::Printf(TEXT("已准备 %d/%d，等待其他猎人。"), ReadyCount, Players.Num()));
	}
}

void ULobbyScreen::SetStatus(const FString& Text)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Text));
	}
}

void ULobbyScreen::HandleReadyClicked()
{
	if (bActionPending)
	{
		return;
	}

	AMHPlayerState* LocalPlayerState = GetLocalPlayerState();
	AMHPlayerController* PlayerController = Cast<AMHPlayerController>(GetOwningPlayer());
	if (!LocalPlayerState || !PlayerController)
	{
		SetStatus(TEXT("无法读取本地玩家状态。"));
		return;
	}

	bActionPending = true;
	ActionLockRemaining = 0.4f;
	PlayerController->Server_SetReady(!LocalPlayerState->bReady);
	SetStatus(TEXT("正在同步准备状态..."));
}

void ULobbyScreen::HandleStartClicked()
{
	if (bStartPending)
	{
		return;
	}

	AMHPlayerController* PlayerController = Cast<AMHPlayerController>(GetOwningPlayer());
	if (!PlayerController)
	{
		return;
	}

	bStartPending = true;
	StartPendingRemaining = 2.5f;
	if (StartButton)
	{
		StartButton->SetIsEnabled(false);
	}
	SetStatus(TEXT("正在进入狩猎地图..."));
	PlayerController->Server_RequestStartHunt();
}

void ULobbyScreen::HandleLeaveClicked()
{
	if (AMHPlayerController* PlayerController = Cast<AMHPlayerController>(GetOwningPlayer()))
	{
		PlayerController->RequestLeaveToMainMenu();
	}
}

AMHPlayerState* ULobbyScreen::GetLocalPlayerState() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return PlayerController ? PlayerController->GetPlayerState<AMHPlayerState>() : nullptr;
}
