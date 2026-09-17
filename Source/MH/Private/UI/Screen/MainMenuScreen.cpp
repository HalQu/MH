#include "UI/Screen/MainMenuScreen.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "UI/Screen/GameFlowUI.h"

void UMHSessionButtonProxy::HandleClicked()
{
	if (UMHMainMenuScreen* MenuScreen = Cast<UMHMainMenuScreen>(Owner))
	{
		MenuScreen->JoinSessionByIndex(SessionIndex);
	}
}

UMHMainMenuScreen::UMHMainMenuScreen()
{
	InputModePolicy = EUIScreenInputMode::UIOnly;
	CloseAnimDuration = 0.f;
}

void UMHMainMenuScreen::NativeConstruct()
{
	Super::NativeConstruct();
	BuildLayout();
}

void UMHMainMenuScreen::BuildLayout()
{
	if (SessionList || !WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = nullptr;
	MHGameFlowUI::CreateRootBackdrop(WidgetTree, RootCanvas);

    UBorder* Panel = MHGameFlowUI::CreateCenteredScaledPanel(WidgetTree, RootCanvas, 720.f, 700.f);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MainMenuContent"));
	Panel->SetContent(Content);

	UTextBlock* Title = MHGameFlowUI::CreateText(WidgetTree, NSLOCTEXT("MHFlow", "MainMenuTitle", "狩猎行动"), 42);
	MHGameFlowUI::AddVertical(Content, Title, FMargin(0.f, 0.f, 0.f, 6.f));

	UTextBlock* Subtitle = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "MainMenuSubtitle", "局域网联机集会"),
		16,
		MHGameFlowUI::MutedTextColor);
	MHGameFlowUI::AddVertical(Content, Subtitle, FMargin(0.f, 0.f, 0.f, 22.f));

	UTextBlock* NameLabel = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "RoomNameLabel", "房间名称"),
		15,
		MHGameFlowUI::MutedTextColor);
	MHGameFlowUI::AddVertical(Content, NameLabel, FMargin(0.f, 0.f, 0.f, 6.f));

	ServerNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ServerNameInput"));
	ServerNameInput->SetHintText(NSLOCTEXT("MHFlow", "RoomNameHint", "输入房间名称"));
	ServerNameInput->SetForegroundColor(MHGameFlowUI::TextColor);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, ServerNameInput, 48.f),
		FMargin(0.f, 0.f, 0.f, 12.f));

	UTextBlock* HostLabel = nullptr;
	HostButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "HostRoom", "创建房间"),
		HostLabel,
		MHGameFlowUI::AccentColor,
		20);
	HostButton->OnClicked.AddDynamic(this, &UMHMainMenuScreen::HandleHostClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, HostButton, 54.f),
		FMargin(0.f, 0.f, 0.f, 24.f));

	UTextBlock* ListTitle = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "LanRooms", "可加入的房间"),
		16,
		MHGameFlowUI::MutedTextColor);
	MHGameFlowUI::AddVertical(Content, ListTitle, FMargin(0.f, 0.f, 0.f, 8.f));

	UScrollBox* RoomScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RoomScroll"));
	RoomScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	SessionList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SessionList"));
	RoomScroll->AddChild(SessionList);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, RoomScroll, 240.f),
		FMargin(0.f, 0.f, 0.f, 12.f));

	StatusText = MHGameFlowUI::CreateText(
		WidgetTree,
		NSLOCTEXT("MHFlow", "ReadyToSearch", "尚未搜索房间。"),
		15,
		MHGameFlowUI::MutedTextColor);
	MHGameFlowUI::AddVertical(Content, StatusText, FMargin(0.f, 0.f, 0.f, 14.f));

	UTextBlock* RefreshLabel = nullptr;
	RefreshButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "RefreshRooms", "刷新房间列表"),
		RefreshLabel);
	RefreshButton->OnClicked.AddDynamic(this, &UMHMainMenuScreen::HandleRefreshClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, RefreshButton, 48.f),
		FMargin(0.f, 0.f, 0.f, 10.f));

	UTextBlock* QuitLabel = nullptr;
	QuitButton = MHGameFlowUI::CreateButton(
		WidgetTree,
		NSLOCTEXT("MHFlow", "QuitGame", "退出游戏"),
		QuitLabel,
		MHGameFlowUI::DangerColor);
	QuitButton->OnClicked.AddDynamic(this, &UMHMainMenuScreen::HandleQuitClicked);
	MHGameFlowUI::AddVertical(
		Content,
		MHGameFlowUI::CreateFixedHeight(WidgetTree, QuitButton, 44.f));

	RefreshSessionList(TArray<FSessionData>());
}

void UMHMainMenuScreen::OnOpen(UObject* Param)
{
	Super::OnOpen(Param);
	BuildLayout();
	BindSessionDelegates();

	if (ServerNameInput && ServerNameInput->GetText().IsEmpty())
	{
		ServerNameInput->SetText(NSLOCTEXT("MHFlow", "DefaultRoomName", "我的狩猎房间"));
	}

	SetBusy(false);
	SetStatus(TEXT(""));
}

void UMHMainMenuScreen::OnClose()
{
	UnbindSessionDelegates();
	Super::OnClose();
}

void UMHMainMenuScreen::RefreshSessionList(const TArray<FSessionData>& SessionResults)
{
	if (!SessionList)
	{
		return;
	}

	SessionList->ClearChildren();
	SessionRowWidgets.Reset();
	SessionButtonProxies.Reset();

	if (SessionResults.IsEmpty())
	{
		UTextBlock* EmptyText = MHGameFlowUI::CreateText(
			WidgetTree,
			NSLOCTEXT("MHFlow", "NoRooms", "没有发现可加入的房间。"),
			15,
			MHGameFlowUI::MutedTextColor,
			ETextJustify::Center);
		SessionList->AddChild(EmptyText);
		SessionRowWidgets.Add(EmptyText);
		return;
	}

	for (const FSessionData& Session : SessionResults)
	{
		const FText RowText = FText::FromString(
			FString::Printf(TEXT("%s    %d/%d    %d ms"), *Session.ServerName, Session.CurrentPlayers, Session.MaxPlayers, Session.Ping));

		UTextBlock* RowLabel = nullptr;
		UButton* RowButton = MHGameFlowUI::CreateButton(WidgetTree, RowText, RowLabel, MHGameFlowUI::PanelAltColor, 17);
		if (!RowButton)
		{
			continue;
		}

		UMHSessionButtonProxy* Proxy = NewObject<UMHSessionButtonProxy>(this);
		Proxy->Owner = this;
		Proxy->SessionIndex = Session.SessionIndex;
		RowButton->OnClicked.AddDynamic(Proxy, &UMHSessionButtonProxy::HandleClicked);

		UWidget* RowSize = MHGameFlowUI::CreateFixedHeight(WidgetTree, RowButton, 54.f);
		MHGameFlowUI::AddVertical(SessionList, RowSize, FMargin(0.f, 0.f, 0.f, 8.f));

		SessionRowWidgets.Add(RowSize);
		SessionButtonProxies.Add(Proxy);
	}
}

void UMHMainMenuScreen::SetStatus(const FString& Text)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Text));
		StatusText->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UMHMainMenuScreen::SetBusy(bool bNewBusy)
{
	bBusy = bNewBusy;
	if (HostButton)
	{
		HostButton->SetIsEnabled(!bBusy);
	}
	if (RefreshButton)
	{
		RefreshButton->SetIsEnabled(!bBusy);
	}
	if (ServerNameInput)
	{
		ServerNameInput->SetIsReadOnly(bBusy);
	}
}

void UMHMainMenuScreen::HandleHostClicked()
{
	if (bBusy)
	{
		return;
	}

	UMHGameInstance* GameInstance = GetGameInstance<UMHGameInstance>();
	if (!GameInstance)
	{
		SetStatus(TEXT("游戏实例不可用。"));
		return;
	}

	const FString ServerName = ServerNameInput ? ServerNameInput->GetText().ToString() : FString();
	SetBusy(true);
	SetStatus(TEXT("正在创建房间..."));
	GameInstance->HostSession(ServerName, true, 4);
}

void UMHMainMenuScreen::HandleRefreshClicked()
{
	if (bBusy)
	{
		return;
	}

	UMHGameInstance* GameInstance = GetGameInstance<UMHGameInstance>();
	if (!GameInstance)
	{
		SetStatus(TEXT("游戏实例不可用。"));
		return;
	}

	SetBusy(true);
	SetStatus(TEXT("正在搜索局域网房间..."));
	GameInstance->FindSessions(true);
}

void UMHMainMenuScreen::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMHMainMenuScreen::HandleSessionsFound(const TArray<FSessionData>& SessionResults)
{
	RefreshSessionList(SessionResults);
	SetBusy(false);
	SetStatus(SessionResults.IsEmpty() ? TEXT("没有找到可加入的房间。") : FString::Printf(TEXT("发现 %d 个房间。"), SessionResults.Num()));
}

void UMHMainMenuScreen::HandleHostSessionComplete()
{
	SetBusy(false);
	SetStatus(TEXT("房间已创建。"));
}

void UMHMainMenuScreen::HandleSessionJoined()
{
	SetBusy(false);
	SetStatus(TEXT("正在进入房间..."));
}

void UMHMainMenuScreen::HandleSessionOperationFailed(const FString& ErrorMessage)
{
	SetBusy(false);
	SetStatus(ErrorMessage);
}

void UMHMainMenuScreen::JoinSessionByIndex(int32 SessionIndex)
{
	if (bBusy || SessionIndex == INDEX_NONE)
	{
		return;
	}

	UMHGameInstance* GameInstance = GetGameInstance<UMHGameInstance>();
	if (!GameInstance)
	{
		SetStatus(TEXT("游戏实例不可用。"));
		return;
	}

	SetBusy(true);
	SetStatus(TEXT("正在加入房间..."));
	GameInstance->JoinSelectedSession(SessionIndex);
}

void UMHMainMenuScreen::BindSessionDelegates()
{
	if (UMHGameInstance* GameInstance = GetGameInstance<UMHGameInstance>())
	{
		GameInstance->OnSessionsFound.RemoveDynamic(this, &UMHMainMenuScreen::HandleSessionsFound);
		GameInstance->OnSessionsFound.AddDynamic(this, &UMHMainMenuScreen::HandleSessionsFound);

		GameInstance->OnHostSessionComplete.RemoveDynamic(this, &UMHMainMenuScreen::HandleHostSessionComplete);
		GameInstance->OnHostSessionComplete.AddDynamic(this, &UMHMainMenuScreen::HandleHostSessionComplete);

		GameInstance->OnSessionJoined.RemoveDynamic(this, &UMHMainMenuScreen::HandleSessionJoined);
		GameInstance->OnSessionJoined.AddDynamic(this, &UMHMainMenuScreen::HandleSessionJoined);

		GameInstance->OnSessionOperationFailed.RemoveDynamic(this, &UMHMainMenuScreen::HandleSessionOperationFailed);
		GameInstance->OnSessionOperationFailed.AddDynamic(this, &UMHMainMenuScreen::HandleSessionOperationFailed);
	}
}

void UMHMainMenuScreen::UnbindSessionDelegates()
{
	if (UMHGameInstance* GameInstance = GetGameInstance<UMHGameInstance>())
	{
		GameInstance->OnSessionsFound.RemoveDynamic(this, &UMHMainMenuScreen::HandleSessionsFound);
		GameInstance->OnHostSessionComplete.RemoveDynamic(this, &UMHMainMenuScreen::HandleHostSessionComplete);
		GameInstance->OnSessionJoined.RemoveDynamic(this, &UMHMainMenuScreen::HandleSessionJoined);
		GameInstance->OnSessionOperationFailed.RemoveDynamic(this, &UMHMainMenuScreen::HandleSessionOperationFailed);
	}
}
