# 联机游戏流程闭环

## 1. 目标流程

当前版本使用 `OnlineSubsystemNull + LAN + Listen Server`，完整流程为：

```text
主菜单
  -> 创建房间 / 搜索房间 / 加入房间
  -> 集会所（Lobby）
  -> 全员准备，房主开始狩猎
  -> 狩猎（Hunting）
  -> 结算结果复制到所有客户端
  -> 自动返回集会所
  -> 离开房间并返回主菜单
```

最大房间人数为 4。房主既是监听服务器，也是普通玩家；客户端通过 LAN 会话搜索和连接。

## 2. 场景与默认 GameMode

| 场景 | 默认 GameMode | 作用 |
| --- | --- | --- |
| `/Game/Levels/BeginMap` | `/Script/MH.BeginGameMode` | 主菜单、创建房间、搜索并加入房间 |
| `/Game/Levels/LobbyMap` | `/Script/MH.MHGameMode_Lobby` | 玩家列表、准备、房主开战 |
| `/Game/Levels/HuntingMap` | `/Script/MH.MHGameMode_Hunting` | 倒计时、结算、自动回集会所 |

切图时同时显式传入原生 GameMode，避免蓝图或 WorldSettings 配置覆盖流程：

```text
LobbyMap?listen?game=/Script/MH.MHGameMode_Lobby
HuntingMap?listen?game=/Script/MH.MHGameMode_Hunting
BeginMap?game=/Script/MH.BeginGameMode
```

## 3. 核心类

| 类 | 职责 |
| --- | --- |
| `UMHGameInstance` | 会话创建、搜索、加入、销毁；连接地址解析；主菜单回程 |
| `AMHPlayerController` | 服务端 RPC、客户端 UI/回程 RPC、狩猎菜单 |
| `AMHPlayerState` | 复制 `bReady` 和 `bIsHost` |
| `AMHGameState_Hunting` | 复制 `bMatchActive`、`RemainingTime`、`ResultText` |
| `AMHGameMode_Lobby` | 玩家准备状态、房主校验、开始狩猎 |
| `AMHGameMode_Hunting` | 比赛状态、倒计时、结算、自动返回 Lobby |
| `UMHMainMenuScreen` | 创建/搜索/加入房间的原生 UI |
| `ULobbyScreen` | 玩家列表、准备按钮、房主开始按钮 |
| `UMHHuntingStatusHUD` | 狩猎期间常驻倒计时和结算文本 |
| `UMHHuntMenuScreen` | 提前结束狩猎、离开房间 |
| `FMHFlowAutoTest` | 开发期双进程端到端流程自检，Shipping 不编译 |

## 4. 会话生命周期

### 创建房间

1. 主菜单调用 `UMHGameInstance::HostSession`。
2. 房间参数暂存在 `PendingServerName`、`PendingIsLAN`、`PendingMaxPlayers`。
3. 主机执行 `ServerTravel` 到 `LobbyMap?listen`。
4. 到达 `LobbyMap` 后，`OnWorldChanged` 在监听端口已经建立时创建 `NAME_GameSession`。
5. 主机把可达的 `CONNECT_STR` 写入 SessionSettings，供客户端连接。

### 搜索和加入

1. 主菜单调用 `UMHGameInstance::FindSessions(true)`。
2. `OnFindSessionsComplete` 把搜索结果转换为 `FSessionData` 并广播给 UI。
3. 客户端选择房间，调用 `JoinSelectedSession`。
4. 优先使用房主写入的 `CONNECT_STR`，解析失败时才使用 OnlineSubsystem 的连接字符串。
5. 客户端执行 `ClientTravel`，进入房主的 `LobbyMap`。

### 销毁和回主菜单

1. `AMHPlayerController::RequestLeaveToMainMenu` 由本地玩家调用。
2. 如果房主离开，房主先通过 `Client_ReturnToMainMenu` 通知其他客户端。
3. `UMHGameInstance::ReturnToMainMenu` 销毁当前会话。
4. 会话销毁完成后，客户端 `ClientTravel` 到 `BeginMap?game=/Script/MH.BeginGameMode`。

## 5. Lobby 状态

`AMHPlayerState::bReady` 由服务器写入并复制。`AMHGameMode_Lobby::CanStartHunt` 只有在房间至少有一名玩家且所有玩家都已准备时返回 true。

房主校验在服务端使用 `RequestingController->IsLocalController()`：

- 监听服务器上，房主的 PlayerController 是本地控制器。
- 远程客户端在服务器侧的 PlayerController 不是本地控制器。
- 不能只用 `HasAuthority()` 判断房主，因为服务器上的客户端控制器同样具有服务器权威。

房主调用 `Server_RequestStartHunt` 后，服务器执行：

```text
LobbyMap?listen?game=/Script/MH.MHGameMode_Hunting
```

## 6. Hunting 状态与结算

`AMHGameState_Hunting` 是狩猎流程的唯一复制状态源：

- `bMatchActive`：是否仍在狩猎。
- `RemainingTime`：服务器倒计时，客户端只读复制值。
- `ResultText`：结算原因。

流程如下：

1. `AMHGameMode_Hunting::BeginPlay` 调用 `BeginMatch`。
2. 服务器每 tick 更新 `RemainingTime`。
3. 倒计时结束或全员倒下时，服务器调用 `FinishHunt`。
4. 房主可以通过 `Server_RequestEndHunt` 提前结束。
5. `FinishHunt` 写入结果，等待结果展示时间。
6. 服务器 `ServerTravel` 返回：

```text
LobbyMap?listen?game=/Script/MH.MHGameMode_Lobby
```

返回 Lobby 后，玩家准备状态会清空，但会话继续存在，可以再次开战。

## 7. RPC 和运行角色

| RPC | 方向 | 用途 |
| --- | --- | --- |
| `Server_SetReady` | Client -> Server | 同步准备状态 |
| `Server_RequestStartHunt` | Client -> Server | 房主请求开战 |
| `Server_RequestEndHunt` | Client -> Server | 房主提前结束狩猎 |
| `Server_RequestReturnToLobby` | Client -> Server | 返回 Lobby 的保留入口 |
| `Client_OpenPersistentScreen` | Server -> Client | 在客户端打开对应的 UI |
| `Client_ReturnToMainMenu` | Server -> Client | 房主离开时通知客户端回主菜单 |

## 8. 编辑器内 PIE 测试

### 主菜单单机检查

1. 打开 `/Game/Levels/BeginMap`。
2. 在 Play 下拉框中选择 `Play Standalone`，客户端数量设为 1。
3. 点击 Play，应看到“狩猎行动”主菜单，并能点击“创建房间”。

`BeginMap` 是空的菜单场景，没有可见地形、角色或相机画面。主菜单完全由 UMG 覆盖显示，因此 PIE 必须使用 `Play Standalone` 或 `Play As Listen Server`。如果使用 `Play As Client`，客户端在连接服务器成功并创建 PlayerController 前不会打开主菜单；没有服务器时该阶段会表现为黑屏或连接失败。

项目本地设置应至少满足以下值：

```ini
PlayNetMode=PIE_Standalone
RunUnderOneProcess=True
PlayNumberOfClients=1
```

### 局域网联机检查

PIE 可以改成 `Play As Listen Server` 和 2 个客户端，用于快速检查房间创建、搜索和加入。完整联机闭环仍优先使用下一节的两个独立 `-game` 进程，因为它更接近实际客户端/监听服务器结构，也能避免多个 PIE 窗口焦点和日志混在一个编辑器进程里。

手工联机步骤：

1. 第一个窗口创建房间，进入 Lobby。
2. 第二个窗口刷新列表，选择同一个房间并加入。
3. 所有玩家准备，房主点击开始狩猎。
4. 狩猎结束后双方应看到相同结算，并自动回到 Lobby。
5. 任何一方离开房间，双方应回到主菜单。

出现黑屏时，先检查 `Saved/Logs/MH.log` 是否包含：

```text
[UIManager] Initialized
LogNet: Welcomed by server (Game: /Script/MH.BeginGameMode)
[UIManager] Opened persistent screen 'MainMenu'
```

如果第一行存在、第二行长时间没有出现，说明当前仍是客户端模式且在等待服务器；如果第二行已出现但没有第三行，才需要继续检查主菜单 Widget 的创建和布局。

## 9. 自动闭环测试

开发期可以用两个 `UnrealEditor -game` 进程验证完整流程。测试要求同一个项目构建，并且两个进程使用不同的日志文件。

```powershell
$engine = 'E:\Program Files\EpicGame\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$project = 'E:\Program Files\Epic Games\UE_5.7\Leaning\MH\MH.uproject'
$root = 'E:\Program Files\Epic Games\UE_5.7\Leaning\MH'
$hostLog = Join-Path $root 'Saved\Logs\FlowHost.log'
$clientLog = Join-Path $root 'Saved\Logs\FlowClient.log'
$common = @(
    '-game', '-nullrhi', '-unattended', '-NoSound', '-NoSplash',
    '-NoDDCCleanup', '-MHAutoTestQuit',
    '-ExecCmds="t.IdleWhenNotForeground 0"', '-log'
)

$hostArgs = @("`"$project`"") + $common + @('-MHAutoTest=Host', "-abslog=`"$hostLog`"")
$host = Start-Process -FilePath $engine -ArgumentList $hostArgs -WorkingDirectory $root -PassThru -WindowStyle Hidden

Start-Sleep -Seconds 5

$clientArgs = @("`"$project`"") + $common + @('-MHAutoTest=Client', "-abslog=`"$clientLog`"")
$client = Start-Process -FilePath $engine -ArgumentList $clientArgs -WorkingDirectory $root -PassThru -WindowStyle Hidden

$host.WaitForExit()
$client.WaitForExit()
```

成功时两端日志应出现：

```text
RESULT=HOST_PASS
RESULT=CLIENT_PASS
```

同时应覆盖以下关键节点：

- 房主创建房间并进入 Lobby。
- 客户端搜索并加入同一个房间。
- 双方准备，房主开始狩猎。
- Hunting GameMode 和 HUD 生效。
- 房主提前结束狩猎。
- 双方收到相同的结算文本。
- 双方自动回到 Lobby。
- 双方请求离开并回到主菜单。

自检使用单调墙钟计时，而不是直接依赖 Core Ticker 的 `DeltaTime`。无渲染或后台运行时 Ticker 的 delta 可能是零，如果用它判断 5 秒阶段条件，流程会被错误地无限延长。

## 10. 当前边界

- 目前只实现 LAN/Null 子系统的房间发现和监听服务器联机。
- 会话发现依赖局域网广播和 `CONNECT_STR`，尚未接入 Steam、EOS 等平台会话。
- 当前没有断线重连、房间密码、观战和独立 Dedicated Server。
- 狩猎结算由流程事件驱动，尚未接入实际怪物、任务目标和奖励结算。
