#include "MHUtils.h"
#include "HAL/PlatformProcess.h"

namespace Utils
{
	float GetSignedAngleBetweenVectors(const FVector& A, const FVector& B, const FVector& UpAxis)
	{
		return 0.0f;
	}

	bool IsForwordBetweenVectors(const FVector& VecA, const FVector& VecB, const FVector& Axis)
	{
		return false;
	}

	bool IsGamepadConnected()
	{
#if PLATFORM_WINDOWS
		// 动态加载 XInput 检测手柄连接
		void* LibHandle = FPlatformProcess::GetDllHandle(TEXT("xinput1_4.dll"));
		if (!LibHandle) LibHandle = FPlatformProcess::GetDllHandle(TEXT("xinput9_1_0.dll"));
		if (!LibHandle) return false;

		typedef int32(__stdcall* XInputGetStateFunc)(int32, void*);
		XInputGetStateFunc XInputGetState = (XInputGetStateFunc)(FPlatformProcess::GetDllExport(LibHandle, TEXT("XInputGetState")));

		bool bFound = false;
		if (XInputGetState)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				uint8 Buffer[20];
				FMemory::Memzero(Buffer, sizeof(Buffer));
				if (XInputGetState(i, Buffer) == 0)
				{
					bFound = true;
					break;
				}
			}
		}
		FPlatformProcess::FreeDllHandle(LibHandle);
		return bFound;
#else
		return false;
#endif
	}
}
