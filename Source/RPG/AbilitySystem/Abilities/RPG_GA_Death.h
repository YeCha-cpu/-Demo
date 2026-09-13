// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_Death.generated.h"

class UAbilityTask_PlayMontageAndWait;

/**
 * 死亡。整个死亡流程的**编排者**。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【死亡的五个步骤，顺序不能乱】
 * ══════════════════════════════════════════════════════════════════════
 *   ① 挂 State.Dead          ← 必须最先做，理由见下
 *   ② 取消所有其它能力        ← 清理 State.Attacking / State.Invulnerable 等标签
 *   ③ 通知角色（停 AI / 停输入）
 *   ④ 播死亡蒙太奇
 *   ⑤ 蒙太奇播完 → 进布娃娃 → 启动重生倒计时
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么 ① 必须排在 ④ 前面】
 * ══════════════════════════════════════════════════════════════════════
 * 从"血量归零"到"死亡蒙太奇播完"之间有 1~2 秒。这段时间里角色**还是活的**
 * —— 玩家还能按攻击键，敌人 AI 还能发起攻击。
 * 如果等蒙太奇播完才挂标签，玩家会看到一具尸体在打拳。
 *
 * 挂上 State.Dead 之后，基类的 ActivationBlockedTags（见
 * RPG_GameplayAbilityBase 构造函数）会挡掉**所有**新的能力激活 ——
 * 一条声明就够，不需要在每个 GA 里写"死了不能放"的判断。
 *
 * 而且这一步还顺带解决了"死两次"的问题：State.Dead 挂着时，
 * 第二次死亡事件的触发会被同一个标签挡在门外，不会重播死亡动画。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【State.Dead 为什么用 loose tag 而不是 GE】
 * ══════════════════════════════════════════════════════════════════════
 * 因为复活的流程里有一步是"清掉身上所有 GE"（RemoveActiveEffects）——
 * 如果死亡标记是个 GE，它会被那一步顺手清掉，复活逻辑就得反过来
 * 依赖"我清掉了什么"来推断状态，非常绕。
 *
 * 用 loose tag 就没这个问题：**它不在 GE 的生命周期里，只能被显式增删**，
 * 复活时摘掉它是一句明确的代码，不依赖任何副作用。
 *
 * 用 CountToOwner 复制状态是因为：State.Dead 必须复制到**每个**客户端，
 * 否则会出现"服务器上人已经倒了，客户端看他还在站着"。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【这个能力怎么被授予】
 * ══════════════════════════════════════════════════════════════════════
 * 放进角色的 **StartupPassiveAbilities** 里。名字叫"被动"但
 * bActivateOnGranted 保持 false —— 它被授予后只是**待命**，
 * 等 Event.Combat.Death 事件来触发（AbilityTriggers 声明的那条）。
 *
 * 刻意不给它输入标签：它不是"玩家想做什么"，而是"发生了什么事"。
 */
UCLASS()
class RPG_API URPG_GA_Death : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_Death();

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

	UFUNCTION() void OnMontageCompleted();
	UFUNCTION() void OnMontageInterrupted();

private:
	/** 摘掉蒙太奇任务的回调并结束它（理由同 GA_HitReact） */
	void DetachMontageTask();

	/**
	 * 死亡流程的终点：倒地 + 启动重生倒计时 + 结束能力。
	 *
	 * 用 bDeathFinished 守住，因为"蒙太奇播完""被打断""没配蒙太奇"
	 * 三条路径最终都会走到这里，而没有蒙太奇时后者会立刻执行。
	 */
	void FinishDeath();

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 保证 FinishDeath 只执行一次 */
	bool bDeathFinished = false;
};
