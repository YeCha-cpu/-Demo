// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_Jump.generated.h"

/**
 * 跳跃（一次性消耗耐力，支持可变高度）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么跳跃也要做成 GA】
 * ══════════════════════════════════════════════════════════════════════
 * 单看"往上跳一下"，调用 ACharacter::Jump() 就够了。但跳跃有两个
 * GAS 该管的特征：
 *   · **消耗耐力** —— 它和闪避、攻击竞争同一份资源，
 *     构成"跳一次等于花掉半次闪避"这样的战术取舍
 *   · **可被规则限制** —— 将来要做"空中只能跳一次""受击时禁止跳跃"，
 *     有 GA 才有地方挂这些条件
 *
 * 做成 GA 之后，这些规则都变成能力上的标签与配置，不会散落到输入层。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么激活后不立刻结束】
 * ══════════════════════════════════════════════════════════════════════
 * 因为要实现**可变高度跳跃**：短按跳得低、长按跳得高。
 * 这需要"松手时调 StopJumping()"，也就要求能力在滞空期间保持激活。
 *
 * 代价是跳跃期间角色身上会挂着一个激活中的能力。这没有害处 ——
 * 攻击、闪避都能正常激活（它们没有把这些标签设为阻断条件）。
 *
 * 用 MaxJumpHoldTime 超时兜底，避免玩家一直不松手导致能力永久激活。
 */
UCLASS()
class RPG_API URPG_GA_Jump : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_Jump();

	/** 松手 → 停止上升（可变高度跳跃） */
	virtual void OnInputReleased() override;

protected:
	/**
	 * 检查两件事：耐力够不够、角色能不能跳（在不在空中）。
	 *
	 * ⚠️ 全部走参数里的 ActorInfo，不能用 CurrentActorInfo 系的便捷函数
	 * —— 本函数可能在 CDO 上执行（详见 RPG_GameplayAbilityBase.cpp 的说明）。
	 */
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	/** 结束跳跃能力 */
	void FinishJump();

protected:
	/** 跳跃的耐力消耗 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Jump", meta = (ClampMin = "0.0"))
	float JumpStaminaCost = 10.f;

	/**
	 * 最长按住时间（秒）。超过就强制结束能力。
	 * 防止玩家一直不松手导致能力永久激活、永远收不到清理。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Jump", meta = (ClampMin = "0.1"))
	float MaxJumpHoldTime = 1.5f;

private:
	FTimerHandle MaxJumpHoldTimer;
};
