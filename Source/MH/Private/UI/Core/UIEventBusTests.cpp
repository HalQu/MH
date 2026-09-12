#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "UI/Core/UIEventBus.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUIEventBusTypedRoutingTest,
    "MH.UI.EventBus.TypedRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUIEventBusTypedRoutingTest::RunTest(const FString& Parameters)
{
    UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
    UUIEventBus* Bus = GameInstance ? NewObject<UUIEventBus>(GameInstance) : nullptr;
    if (!TestNotNull(TEXT("Game instance"), GameInstance) ||
        !TestNotNull(TEXT("Event bus instance"), Bus))
    {
        return false;
    }

    static const FName EventName(TEXT("MH.Test.UI.TypedRouting"));
    static const FName NestedEventName(TEXT("MH.Test.UI.NestedRouting"));

    int32 EmptyTypedCount = 0;
    int32 BaseTypedCount = 0;
    int32 LegacyCount = 0;
    int32 NestedCount = 0;
    int32 NestedHandlerCount = 0;

    Bus->ListenTyped<FUIEmptyPayload>(
        EventName,
        FUIEventHandler::CreateLambda(
            [&EmptyTypedCount](const FUIEventPayload&)
            {
                ++EmptyTypedCount;
            }));

    Bus->ListenTyped<FUIEventPayload>(
        EventName,
        FUIEventHandler::CreateLambda(
            [&BaseTypedCount](const FUIEventPayload&)
            {
                ++BaseTypedCount;
            }));

    Bus->Listen(
        EventName,
        FUIEventHandler::CreateLambda(
            [&LegacyCount](const FUIEventPayload&)
            {
                ++LegacyCount;
            }));

    Bus->Listen(
        NestedEventName,
        FUIEventHandler::CreateLambda(
            [&NestedCount](const FUIEventPayload&)
            {
                ++NestedCount;
            }));

    Bus->BroadcastTyped(EventName, FUIEmptyPayload());
    TestEqual(TEXT("Empty typed listener receives exact type"), EmptyTypedCount, 1);
    TestEqual(TEXT("Base typed listener ignores derived type"), BaseTypedCount, 0);
    TestEqual(TEXT("Legacy listener receives typed broadcast"), LegacyCount, 1);

    Bus->Broadcast(EventName);
    TestEqual(TEXT("Empty typed listener receives empty broadcast"), EmptyTypedCount, 2);
    TestEqual(TEXT("Base typed listener still ignores empty payload"), BaseTypedCount, 0);
    TestEqual(TEXT("Legacy listener receives empty broadcast"), LegacyCount, 2);

    FUIEventPayload BasePayload;
    Bus->BroadcastTyped(EventName, BasePayload);
    TestEqual(TEXT("Empty typed listener ignores base type"), EmptyTypedCount, 2);
    TestEqual(TEXT("Base typed listener receives exact type"), BaseTypedCount, 1);
    TestEqual(TEXT("Legacy listener receives base typed broadcast"), LegacyCount, 3);

    Bus->Broadcast(EventName, BasePayload);
    TestEqual(TEXT("Untyped broadcast does not reach typed empty listener"), EmptyTypedCount, 2);
    TestEqual(TEXT("Untyped broadcast does not reach typed base listener"), BaseTypedCount, 1);
    TestEqual(TEXT("Legacy listener receives untyped broadcast"), LegacyCount, 4);

    FDelegateHandle NestedHandle;
    NestedHandle = Bus->Listen(
        EventName,
        FUIEventHandler::CreateLambda(
            [Bus, &NestedHandlerCount](const FUIEventPayload&)
            {
                ++NestedHandlerCount;
                Bus->Broadcast(NestedEventName);
            }));

    Bus->Broadcast(EventName);
    TestEqual(TEXT("Nested broadcast of another event is allowed"), NestedCount, 1);

    Bus->Broadcast(EventName);
    TestEqual(TEXT("Nested listener remains stable"), NestedCount, 2);

    Bus->Unlisten(EventName, NestedHandle);
    Bus->Broadcast(EventName);
    TestEqual(TEXT("Unlisten removes only the requested handler"), NestedCount, 2);

    return true;
}

#endif
