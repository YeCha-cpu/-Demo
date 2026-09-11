// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RPG_InputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * 一条"输入动作 → 输入标签"的映射。
 *
 * 注意方向：是 Action → Tag，不是 Tag → Action。
 * 因为同一个动作可能在不同情境下代表不同意图（比如长按/短按），
 * 而每个动作资产是唯一的配置点，在它这里指定标签最自然。
 */
USTRUCT(BlueprintType)
struct FRPG_InputActionMapping
{
	GENERATED_BODY()

	/** 增强输入的输入动作资产 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input")
	TObjectPtr<UInputAction> InputAction = nullptr;

	/** 该动作对应的输入标签，必须在 Input.* 命名空间下 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input", meta = (Categories = "Input"))
	FGameplayTag InputTag;

	/** 用于日志与调试的可读说明 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input")
	FString Description;
};

/**
 * 玩家输入配置资产。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么要有这个资产，而不是在 PlayerController 里写死】
 * ══════════════════════════════════════════════════════════════════════
 * 把"哪个按键对应哪个输入标签"从代码里挪到资产里，收益是：
 *   · 换一套输入方案（键鼠 / 手柄 / 触屏）只需换一个 DataAsset
 *   · 调试时可以运行时替换配置，不用重新编译
 *   · 输入映射关系一目了然，不用在代码里翻 BindAction 调用
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【两类输入的分工】
 * ══════════════════════════════════════════════════════════════════════
 *   Locomotion（移动/视角/蹲伏）—— 直接驱动 CharacterMovementComponent，
 *                                    不经过 GAS。这类输入没有冷却、没有消耗、
 *                                    不需要被其他技能打断，走 GAS 是纯粹的负担。
 *
 *   Ability（攻击/闪避/跳跃/奔跑/法术）—— 全部通过输入标签进入 GAS。
 *                                    因为跳跃和奔跑要消耗耐力，攻击要有冷却和
 *                                    前摇后摇，闪避要有无敌帧——它们都是"能力"。
 */
UCLASS(BlueprintType)
class RPG_API URPG_InputConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 默认的输入映射上下文。PlayerController 在 BeginPlay 时把它加进 EnhancedInput 子系统 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext = nullptr;

	/** 映射上下文优先级。数值高的会覆盖低的（用于"战斗模式"覆盖"探索模式"这类场景） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input")
	int32 MappingContextPriority = 0;

	// ── 直接驱动移动的输入（不走 GAS）──

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input|Locomotion")
	TObjectPtr<UInputAction> MoveAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input|Locomotion")
	TObjectPtr<UInputAction> LookAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input|Locomotion")
	TObjectPtr<UInputAction> CrouchAction = nullptr;

	// ── 走 GAS 的能力输入 ──

	/** 攻击、闪避、跳跃、奔跑、法术等，全部通过输入标签进入 GAS */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input|Abilities", meta = (TitleProperty = "Description"))
	TArray<FRPG_InputActionMapping> AbilityInputMappings;

	// ══════════════════════════════════════════════════════════════════
	//  查询
	// ══════════════════════════════════════════════════════════════════

	/** 由 InputAction 反查输入标签。找不到返回 false */
	bool FindInputTagForAction(const UInputAction* Action, FGameplayTag& OutTag) const;

	/** 由输入标签反查 InputAction。找不到返回 nullptr */
	UInputAction* FindActionForInputTag(FGameplayTag InputTag) const;

	/** 收集所有能力输入动作，供 PlayerController 批量绑定 */
	void GetAllAbilityActions(TArray<const UInputAction*>& OutActions) const;
};
