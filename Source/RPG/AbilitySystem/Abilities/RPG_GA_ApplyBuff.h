// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_ApplyBuff.generated.h"

/**
 * 施加增益 / 减益。
 *
 * 这是一个**通用施加者**：具体施加什么效果完全由 GE 决定。
 * 加攻、加防、减防、减速、中毒……都用这**同一个 GA 类**。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【★ GE 从哪来：两个来源，事件优先】
 * ══════════════════════════════════════════════════════════════════════
 *   1. **事件载荷**（优先）—— 场景里的效果触发器（`ARPG_EffectVolume`）
 *      发 `Event.Item.Buff` / `Event.Item.Debuff` 时，会把要用的 GE 一起带过来
 *   2. **本能力的 `BuffEffectClass`**（兜底）—— 手动激活、或载荷里没带 GE 时用
 *
 * 有了第 1 条，**新增一种 Buff 不需要改 C++、也不需要新建 GA 蓝图**：
 * 做一个新 GE 资产，在拾取物上指过去就完事。
 *
 * ⚠️ 代价是：这个能力在角色身上**只能授予一次**（它同时响应增益和减益事件）。
 * 授予两份的话一次事件会激活两次，效果翻倍。
 * 要多个不同效果靠"不同 GE + 不同拾取物"，不是"不同 GA 蓝图"。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么不像 Heal 那样用 SetByCaller 传数值】
 * ══════════════════════════════════════════════════════════════════════
 * 治疗是"同一个效果、不同强度"，所以用 SetByCaller 传量。
 * 而 Buff 是"不同的效果、各自有各自的持续时间和修饰符"——
 * 加攻 20% 持续 15 秒 和 加防 30% 持续 10 秒 之间没有可参数化的共性，
 * 硬要抽象反而会做出一堆含义不明的参数。
 *
 * 所以这里的做法是：**一个效果一个 GE 资产**，GA 只负责施加。
 * 将来如果出现"同一种 Buff 的多个强度等级"，再给那个 GE 加 SetByCaller 支持。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【减益为什么不单独建一个 GA 类】
 * ══════════════════════════════════════════════════════════════════════
 * 因为机制上完全一样 —— 减益就是一个修饰符为负、GrantedTags 带 `State.Debuff.*`
 * 的 GE。分成两个类只会得到两份一模一样的代码。
 *
 * 真正需要区分"增益 / 减益"的地方（UI 图标、驱散只清减益、免疫判定）
 * 查的是 **GE 的标签**，不是 GA 的类。
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
