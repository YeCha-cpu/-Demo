// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_ApplyBuff.generated.h"

/**
 * 施加增益/减益。
 *
 * 这是一个**通用容器**：具体施加什么效果完全由 BuffEffectClass 决定。
 * 加攻、加防、减防减速……都可以用同一个 GA 类，只是配不同的 GE 蓝图子类。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么不像 Heal 那样用 SetByCaller 传数值】
 * ══════════════════════════════════════════════════════════════════════
 * 治疗是"同一个效果、不同强度"，所以用 SetByCaller 传量。
 * 而 Buff 是"不同的效果、各自有各自的持续时间和修饰符"——
 * 加攻 20% 持续 15 秒 和 加防 30% 持续 10 秒 之间没有可参数化的共性，
 * 硬要抽象反而会做出一堆含义不明的参数。
 *
 * 所以这里的做法是：**一个 Buff 一个 GE 资产**，GA 只负责施加。
 * 将来如果出现"同一种 Buff 的多个强度等级"，再给那个 GE 加 SetByCaller 支持。
 */
UCLASS()
class RPG_API URPG_GA_ApplyBuff : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_ApplyBuff();

	/** 运行时覆盖要施加的 GE（供法术/道具系统调用） */
	UFUNCTION(BlueprintCallable, Category = "RPG|Buff")
	void SetBuffEffect(TSubclassOf<UGameplayEffect> NewEffect) { BuffEffectClass = NewEffect; }

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	/**
	 * 要施加的 GE。
	 *
	 * 常用的几个：
	 *   GE_Buff_AttackUp       Duration 15s，Attack += 20%
	 *   GE_Buff_DefenseUp      Duration 15s，Defense += 30%
	 *   GE_Debuff_DefenseDown  Duration 10s，Defense ×= 0.7
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Buff")
	TSubclassOf<UGameplayEffect> BuffEffectClass;
};
