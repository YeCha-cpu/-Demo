// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RPG : ModuleRules
{
	public RPG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"RPG",
			"RPG/Variant_Platforming",
			"RPG/Variant_Platforming/Animation",
			"RPG/Variant_Combat",
			"RPG/Variant_Combat/AI",
			"RPG/Variant_Combat/Animation",
			"RPG/Variant_Combat/Gameplay",
			"RPG/Variant_Combat/Interfaces",
			"RPG/Variant_Combat/UI",
			"RPG/Variant_SideScrolling",
			"RPG/Variant_SideScrolling/AI",
			"RPG/Variant_SideScrolling/Gameplay",
			"RPG/Variant_SideScrolling/Interfaces",
			"RPG/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
