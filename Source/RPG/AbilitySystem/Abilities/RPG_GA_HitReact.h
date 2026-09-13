// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_HitReact.generated.h"

class UAbilityTask_PlayMontageAndWait;

/**
 * 受击反应（挨打时的踉跄动作）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它是怎么被触发的：全程没有一个调用点】
 * ══════════════════════════════════════════════════════════════════════
 * 整条链路没有任何"直接调用"：
 *
 *   RPG_DamageExecution 算出伤害
 *     → GE_Damage 写 IncomingDamage
 *       → RPG_AttributeSet::PostGameplayEffectExecute（服务器）
 *         → SendGameplayEventToActor(Event.Combat.Hit)
 *           → ASC 查 AbilityTriggers 表
 *             → 本能力
 *
 * 连接点是**标签**而不是函数调用：能力的构造函数里声明
 * "我监听 Event.Combat.Hit"，剩下的由 GAS 自己接。
 * 好处是伤害逻辑完全不需要知道"有受击反应这回事"——
 * 将来加"格挡反击""霸体免硬直"都是加新能力或改标签，不用回头动伤害链路。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么 NetExecutionPolicy 是 ServerOnly】
 * ══════════════════════════════════════════════════════════════════════
 * 事件本身只在服务器广播（见 RPG_AttributeSet.cpp 里的权威判断），
 * 所以客户端根本收不到触发。设成 ServerOnly 有两个作用：
 *   · 明确表达意图，读到代码的人不用去追"为什么客户端不播"
 *   · 顺手堵死"客户端伪造受击事件让敌人硬直"这条路
 *
 * 客户端看到的受击表现来自复制：蒙太奇信息（RepAnimMontageInfo）
 * 和 State.Hit 标签都会同步过去。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么受击要打断攻击】
 * ══════════════════════════════════════════════════════════════════════
 * "被打断"就是动作游戏里受击的核心意义 —— 否则玩家可以顶着伤害把连段打完，
 * 战斗就没有博弈可言。实现方式是 CancelAbilitiesWithTag（见构造函数），
 * 用的是 `Ability.Attack` **父标签**，所以将来加新的攻击类型不用回来改。
 *
 * ⚠️ 被打断的攻击由它自己的 EndAbility 负责收尾（停蒙太奇、摘标签），
 * 本能力不碰别人的动画。这条边界很重要 —— 互相清理一定会漏。
 */
UCLASS()
class RPG_API URPG_GA_HitReact : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_HitReact();

protected:
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

	/** 蒙太奇正常播完 —— 结束能力，硬直自然解除 */
	UFUNCTION() void OnMontageCompleted();

	/**
	 * 蒙太奇被打断 / 被取消。
	 *
	 * 有两种情况会走到这里：
	 *   · 又挨了一下（bRetriggerInstancedAbility 会 EndAbility 旧实例后再激活）
	 *   · 角色死了（GA_Death 的 CancelAllAbilities 把它扫掉）
	 * 两种都不该在本能力里做额外补偿 —— 该做的事各自的上游已经在做了。
	 */
	UFUNCTION() void OnMontageInterrupted();

private:
	/**
	 * 摘掉旧蒙太奇任务的回调并结束它 ★
	 *
	 * ⚠️ 只调 EndTask() 是不够的。`ShouldBroadcastAbilityTaskDelegates()`
	 * 判断的是**能力**是否激活（AbilityTask.cpp:199），不是任务自己的状态 ——
	 * 所以"结束掉的任务"照样会把 OnCompleted 回调送过来。
	 * 重生时的表现就是：上一次受击的收尾回调把这一次刚播的蒙太奇当成
	 * "播完了"，硬直莫名其妙提前结束。
	 *
	 * 必须先 RemoveDynamic 再 EndTask，两件事缺一不可。
	 * （这个坑本项目在轻击连段上已经踩过一次，见 RPG_GA_LightAttack。）
	 */
	void DetachMontageTask();

	/** 没有蒙太奇时用定时器模拟一段硬直，保证"受击有反馈"这件事不依赖美术资源 */
	void StartSimulatedHitReact();
	void FinishHitReact(bool bWasCancelled);

	UFUNCTION() void OnSimulatedHitReactFinished();

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	FTimerHandle SimulatedTimer;

	// ══════════════════════════════════════════════════════════════════
	//  配置
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 角色没配受击蒙太奇时的硬直时长（秒）。
	 *
	 * 存在的意义和攻击能力的"模拟命中检测"一样：让"挨打会被打断"
	 * 这条逻辑在动画做好之前就能验证。配了蒙太奇时这个值不参与。
	 */
	// 注意：没有 BlueprintReadOnly —— 它是 private 成员，
	// UHT 不允许在 private 上标蓝图可读（会直接编译失败）。
	UPROPERTY(EditDefaultsOnly, Category = "RPG|HitReact|Debug", meta = (ClampMin = "0.05"))
	float SimulatedHitDuration = 0.4f;
};
