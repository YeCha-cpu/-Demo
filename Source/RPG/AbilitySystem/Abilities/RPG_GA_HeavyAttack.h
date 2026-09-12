// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_HeavyAttack.generated.h"

class URPG_AbilityTask_WeaponTrace;
class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;

/**
 * 重击 —— 一个能力，两种形态。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【形态由"当前是否在轻击连段中"决定】
 * ══════════════════════════════════════════════════════════════════════
 *
 *   不在轻击连段中 → **蓄力重击**
 *       按住右键开始蓄力，累计时间达到门槛就升段（最多 3 段）
 *       蓄力期间持续掉耐力；耐力耗尽会强制释放
 *       松手时按当前段位播对应的释放蒙太奇并造成伤害
 *
 *   正在轻击连段中 → **切手技**
 *       直接打断当前轻击，播放专门的切手蒙太奇，一次性伤害后收招
 *       不可蓄力 —— 它的价值在于"变招"，不是"蓄力"
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么合并成一个 GA 而不是两个】
 * ══════════════════════════════════════════════════════════════════════
 * 两者共用同一个输入（右键）、同一套伤害窗口机制、同一个攻击模组配置。
 * 拆成两个 GA 的话：
 *   · 需要处理"哪个该被激活"的优先级竞争，还要防止两个同时激活
 *   · 打断轻击的逻辑要写两遍
 *   · DA_AttackModule 上的配置要分成两处
 * 合并之后，分支判断就在 ActivateAbility 开头的一行标签查询里。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【切手技怎么打断轻击】
 * ══════════════════════════════════════════════════════════════════════
 * 靠构造函数里的 CancelAbilitiesWithTag = Ability.Attack.Light ——
 * 本能力激活时，GAS 会自动取消所有带该标签的激活中能力。
 * 不需要手写"找到轻击 GA 然后取消它"这类脆弱代码。
 */
UCLASS()
class RPG_API URPG_GA_HeavyAttack : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_HeavyAttack();

	/** 输入释放 —— 蓄力到此结束，按累计时间决定释放段位 */
	virtual void OnInputReleased() override;

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

private:
	// ══════════════════════════════════════════════════════════════════
	//  两条分支
	// ══════════════════════════════════════════════════════════════════

	/** 切手技分支：播专门蒙太奇，一次伤害后收招 */
	void StartTransition();

	/** 蓄力分支：播起手蒙太奇并启动蓄力计时 */
	void StartCharge();

	/** 蓄力计时回调：累计时间、检查升段、持续扣耐力、耐力耗尽强制释放 */
	void TickCharge();

	/** 释放蓄力攻击 */
	void ReleaseCharge();

	// ══════════════════════════════════════════════════════════════════
	//  蓄力标签同步
	// ══════════════════════════════════════════════════════════════════
	// 蓄力状态既不用 bool 成员、也不用 GE 表达，而是挂成 GameplayTag。
	// 理由和"是否攻击中"一样：标签是谁都能查的公共状态，
	// 而成员变量只有 Cast 到本类才读得到 —— GA 实例还会随能力结束被回收。
	//
	// 顺带解决了动画要显示"蓄到第几段"的需求：
	// 段位本身就是 State.Attack.Charging.Lv1/2/3 三个标签。

	/** 把蓄力标签同步到指定段位（会先摘掉上一次挂的） */
	void UpdateChargeTags(int32 NewLevel);

	/** 摘掉全部蓄力标签。释放、被打断、能力结束时都要调 */
	void ClearChargeTags();

	// ══════════════════════════════════════════════════════════════════
	//  蒙太奇任务的清理
	// ══════════════════════════════════════════════════════════════════
	// 和轻击连段是同一类问题：一个能力里先后播多段蒙太奇时，
	// 上一段的蒙太奇和它的 PlayMontageAndWait 任务会继续活着，
	// 在它自己动画播完/被打断时广播回调，干扰当前这一段的判断。
	//
	// 本能力的播放顺序：
	//     切手技：ComboTransition（一次）
	//     蓄力：  ChargeStart → ChargeLoop → Release
	// 每次换段都必须先把上一段拆干净。

	/** 当前正在播的蒙太奇任务 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> CurrentStageMontageTask;

	/** 当前正在播的蒙太奇 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentStageMontage;

	/** 摘掉注册在任务上的全部回调并结束任务。不停蒙太奇本身 */
	void DetachCurrentStageMontageTask();

	/** 换段前的完整清理：摘回调 + 停蒙太奇 + 结束任务 */
	void StopCurrentStageMontage();

	/** 记下刚播起来的这一段，供下次换段时清理 */
	void TrackCurrentStageMontage(UAbilityTask_PlayMontageAndWait* Task, UAnimMontage* Montage);

	// ══════════════════════════════════════════════════════════════════
	//  事件与收尾
	// ══════════════════════════════════════════════════════════════════

	void BindGameplayEventListeners();
	void FinishHeavyAttack(bool bWasCancelled);

	UFUNCTION() void OnAttackWindowOpen(FGameplayEventData Payload);
	UFUNCTION() void OnAttackWindowClose(FGameplayEventData Payload);
	UFUNCTION() void OnAttackEndEvent(FGameplayEventData Payload);
	UFUNCTION() void OnMontageCompleted();
	UFUNCTION() void OnMontageInterrupted();

	/**
	 * 蓄力起手动画播完。
	 *
	 * 注意它和 OnMontageCompleted 是**两件不同的事**：
	 *   · 起手播完 → 进入蓄力循环姿势，能力继续
	 *   · 释放播完 → 整套动作结束，能力结束
	 * 合成一个回调的话，玩家按下右键的瞬间招式就打完了。
	 */
	UFUNCTION() void OnChargeStartMontageCompleted();
	UFUNCTION() void OnWeaponTraceHit(const TArray<FHitResult>& Hits);

	// ══════════════════════════════════════════════════════════════════
	//  状态
	// ══════════════════════════════════════════════════════════════════

	/** 本次激活走的是切手技分支 */
	bool bTransitionBranch = false;

	/**
	 * 是否已进入"释放"阶段。
	 *
	 * 用途：释放时会播新蒙太奇，那会打断仍在播放的蓄力起手蒙太奇，
	 * 从而触发 OnMontageInterrupted。没有这个标记的话，
	 * 会把正常的"蓄力→释放"误判成"蓄力被打断"而结束能力。
	 */
	bool bReleasing = false;

	/** 当前蓄力段位（0 = 还没到最低门槛，1~3 = 对应段位） */
	int32 CurrentChargeLevel = 0;

	/** 已累计的蓄力时间（秒） */
	float ChargeElapsed = 0.f;

	/**
	 * 当前挂在 ASC 上的蓄力标签集合（父标签 + 段位标签）。
	 *
	 * 保存"挂了哪些"是为了精确摘除 —— 直接调 Clear 摘固定的几个标签也行，
	 * 但那样一旦将来加了新标签而忘了同步两边，就会留下永久残留的状态标签。
	 * 用集合记录实际挂上去的内容，增删永远成对。
	 */
	FGameplayTagContainer ActiveChargeTags;

	/** 本次攻击的伤害倍率 */
	float CurrentDamageMultiplier = 1.f;

	FTimerHandle ChargeTimer;

	UPROPERTY()
	TObjectPtr<URPG_AbilityTask_WeaponTrace> TraceTask;

protected:
	// ══════════════════════════════════════════════════════════════════
	//  配置
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 蓄力检查频率（秒）。
	 * 0.1 秒够用 —— 再密也不能让蓄力段位变化更快，
	 * 因为段位门槛本来就是零点几秒级别的。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Charge", meta = (ClampMin = "0.02"))
	float ChargeTickInterval = 0.1f;

	/**
	 * 最大蓄力时间（秒）。到顶自动释放。
	 * 防止玩家一直按着不放导致蓄力无限累积。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Charge", meta = (ClampMin = "0.1"))
	float MaxChargeTime = 3.f;
};
