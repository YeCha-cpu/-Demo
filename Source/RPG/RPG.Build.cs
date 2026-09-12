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

			// ── GAS 三件套 ──
			// GameplayAbilities: ASC / GameplayAbility / GameplayEffect / GameplayCue / AttributeSet
			// GameplayTags:      FGameplayTag 与原生标签定义宏（UE_DEFINE_GAMEPLAY_TAG_*）
			// GameplayTasks:     UAbilityTask 的基类 UGameplayTask 所在模块，缺它 AbilityTask 编译不过
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",

			// ── AI ──
			// AIModule:          AIController / BehaviorTree / Blackboard / AIPerception
			// NavigationSystem:  UBTService 中做寻路查询与 MoveTo 需要
			"AIModule",
			"NavigationSystem",

			// ── UI ──
			"UMG",
			"Slate",
			"SlateCore",

			// ── 特效 ──
			"Niagara",

			// 工程已启用这两个插件但当前未使用。
			// 保留依赖是为了阶段 5 若要把行为树换成 StateTree 时无需回头改 Build.cs。
			"StateTreeModule",
			"GameplayStateTreeModule",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// ★ Feature Folder 结构的前提 ★
		// 把模块根目录加入 include 搜索路径，让下面这种"从模块根开始"的写法生效：
		//     #include "AbilitySystem/Abilities/RPG_GA_LightAttack.h"
		//     #include "Core/RPG_GameplayTags.h"
		// 好处：include 路径自带层级信息，一眼看出文件属于哪一层，且不会与引擎头文件重名。
		PublicIncludePaths.Add(ModuleDirectory);

		// 注意：不要用 Public/Private 镜像结构了。那套机制是为插件/跨模块依赖设计的
		// （Public/ 下的头文件会自动加入依赖方的 include 路径），单一游戏模块用不上。
	}
}
