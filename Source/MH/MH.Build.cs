// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MH : ModuleRules
{
	public MH(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDefinitions.Add("MH_DEBUG=1");

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "OnlineSubsystem", "OnlineSubsystemUtils"});

        // 命中特效走 Niagara（SpawnSystemAtLocation），需要该模块。
        PublicDependencyModuleNames.Add("Niagara");

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}


