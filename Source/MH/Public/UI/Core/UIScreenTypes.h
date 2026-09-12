#pragma once

#include "CoreMinimal.h"
#include "UIScreenTypes.generated.h"

/**
 * UI 层级。数值同时作为 AddToViewport 的基础 ZOrder。
 */
UENUM(BlueprintType)
enum class EUILayer : uint8
{
    World = 0    UMETA(DisplayName = "World"),
    HUD = 10     UMETA(DisplayName = "HUD"),
    Screen = 20  UMETA(DisplayName = "Screen"),
    Popup = 30   UMETA(DisplayName = "Popup"),
    Overlay = 40 UMETA(DisplayName = "Overlay")
};

/**
 * 页面激活时需要使用的输入模式。
 */
UENUM(BlueprintType)
enum class EUIScreenInputMode : uint8
{
    GameOnly UMETA(DisplayName = "Game Only"),
    UIOnly UMETA(DisplayName = "UI Only"),
    GameAndUI UMETA(DisplayName = "Game And UI")
};
