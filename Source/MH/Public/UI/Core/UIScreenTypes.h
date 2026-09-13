#pragma once

#include "CoreMinimal.h"
#include "UIScreenTypes.generated.h"

/**
 * UI 层级。数值同时作为 AddToViewport 的基础 ZOrder。
 */
UENUM(BlueprintType)
enum class EUILayer : uint8
{
    /** 世界空间/背景层，ZOrder 最低。 */
    World = 0    UMETA(DisplayName = "World"),
    /** 常驻 HUD（血条、调试信息等），排在 Screen 之下。 */
    HUD = 10     UMETA(DisplayName = "HUD"),
    /** 普通页面栈，菜单/设置等主交互层。 */
    Screen = 20  UMETA(DisplayName = "Screen"),
    /** 弹窗层，通常需要独占输入。 */
    Popup = 30   UMETA(DisplayName = "Popup"),
    /** 覆盖层，最高优先级，如加载遮罩、全局提示。 */
    Overlay = 40 UMETA(DisplayName = "Overlay")
};

/**
 * 页面激活时需要使用的输入模式。
 */
UENUM(BlueprintType)
enum class EUIScreenInputMode : uint8
{
    /** 输入完全交给游戏（HUD 常驻、非交互页面）。 */
    GameOnly UMETA(DisplayName = "Game Only"),
    /** 输入完全交给 UI（设置菜单、暂停菜单）。 */
    UIOnly UMETA(DisplayName = "UI Only"),
    /** 游戏与 UI 同时可操作（背包界面希望保留角色移动时）。 */
    GameAndUI UMETA(DisplayName = "Game And UI")
};
