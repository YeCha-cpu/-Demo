// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_LightAttack.generated.h"

class UAbilityTask_PlayMontageAndWait;
class URPG_AbilityTask_WeaponTrace;

/**
 * 轻击（5 段连段）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么是"一个 GA 管 5 段"而不是 5 个 GA】
 * ══════════════════════════════════════════════════════════════════════
 * 每段一个 GA 的话会有这些问题：
 *   · 连段状态（打到第几段）无处安放，得在 GA 之间传递
 *   · 段与段之间的衔接逻辑要重复 5 遍
 *   · 从第 3 段被衔接窗口接走时，要处理"旧 GA 结束 + 新 GA 开始"的时序竞争
 *
 * 一个 GA 内部循环播放 5 段蒙太奇则很自然：
 *   · 连段索引就是 GA 的一个成员变量
 *   · 衔接就是把"播下一段蒙太奇"这件事再做一遍
 *   · 没有跨 GA 的状态交接，也没有重复激活的开销
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【一次激活的生命周期】
 * ══════════════════════════════════════════════════════════════════════
 *   ActivateAbility
 *     → 决定起始段（看 CombatComponent 的连段索引）
 *     → 建立 GameplayEvent 监听（一次，覆盖全部段）
 *     → StartSegment(N)
 *          ├─ 扣耐力
 *          └─ 播蒙太奇 N
 *
 *   蒙太奇播放期间，通过 Notify 广播的事件驱动状态推进：
 *     AttackWindow.Open   → 启动武器轨迹检测
 *     AttackWindow.Close  → 停止检测
 *     ComboWindow.Open    → 允许衔接
 *     ComboWindow.Close   → 检查缓存，决定续段还是收尾
 *     AttackEnd           → EndAbility
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【没有蒙太奇时会怎样】
 * ══════════════════════════════════════════════════════════════════════
 * 蒙太奇留空时，GA 会走一条**模拟时序**的分支：用定时器代替动画事件，
 * 立刻施加一次伤害、短暂开启衔接窗口。
 *
 * 这样在动画做好之前就能用日志验证：连段能不能推进、倍率对不对、
 * 耐力扣得对不对。动画接上后这条分支自然就不再走了。
 */
UCLASS()
class RPG_API URPG_GA_LightAttack : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_LightAttack();

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

	// ══════════════════════════════════════════════════════════════════
	//  段落推进
	// ══════════════════════════════════════════════════════════════════

	/** 开始播放第 Index 段（0-based） */
	void StartSegment(int32 Index);

	/**
	 * 尝试从输入缓存取一个轻击输入并开始下一段。
	 * @return 成功续段返回 true；没有可续的段（或缓存里是别的意图）返回 false
	 */
	bool TryStartNextSegment();

	/** 收尾：重置连段并结束能力 */
	void FinishCombo(bool bWasCancelled);

	/** 建立 GameplayEvent 监听（每次激活只需一次） */
	void BindGameplayEventListeners();

	// ── 无蒙太奇时的模拟时序 ──
	// 动画做好之前，用定时器代替 Notify 事件驱动状态推进，
	// 这样连段、伤害、耐力三条链路都能提前验证。

	/** 启动本段的模拟时序 */
	void StartSimulatedSegment();

	/** 模拟模式下衔接窗口关闭（没有后续动画事件，所以直接收尾） */
	void OnSimulatedWindowClosed();

	/**
	 * 模拟一次命中：在角色身前做一次球形检测并施加伤害。
	 * 仅用于没有蒙太奇时验证伤害链路，动画接上后不再走这条路。
	 */
	void PerformSimulatedHit();

	// ══════════════════════════════════════════════════════════════════
	//  事件回调
	// ══════════════════════════════════════════════════════════════════

	UFUNCTION() void OnAttackWindowOpen(FGameplayEventData Payload);
	UFUNCTION() void OnAttackWindowClose(FGameplayEventData Payload);
	UFUNCTION() void OnComboWindowOpen(FGameplayEventData Payload);
	UFUNCTION() void OnComboWindowClose(FGameplayEventData Payload);
	UFUNCTION() void OnAttackEndEvent(FGameplayEventData Payload);

	UFUNCTION() void OnMontageCompleted();
	UFUNCTION() void OnMontageInterrupted();

	/** 轨迹检测命中 */
	UFUNCTION() void OnWeaponTraceHit(const TArray<FHitResult>& Hits);

	// ══════════════════════════════════════════════════════════════════
	//  配置
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 没有蒙太奇时，每段模拟持续多久（秒）。
	 * 只在开发期用于验证数值链路，动画接上后不再使用。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Debug", meta = (ClampMin = "0.1"))
	float SimulatedSegmentDuration = 0.8f;

	/** 模拟模式下衔接窗口持续多久 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Debug", meta = (ClampMin = "0.05"))
	float SimulatedComboWindowDuration = 0.5f;

private:
	/** 当前正在播放的段索引（0-based） */
	int32 CurrentSegmentIndex = 0;

	/** 当前段的伤害倍率（从攻击模组读，进入段时缓存） */
	float CurrentDamageMultiplier = 1.f;

	/** 衔接窗口是否开启中 */
	bool bComboWindowOpen = false;

	/** 本次挥砍的轨迹检测任务 */
	UPROPERTY()
	TObjectPtr<URPG_AbilityTask_WeaponTrace> TraceTask;

	/** 模拟时序用的定时器 */
	FTimerHandle SimulatedSegmentTimer;
	FTimerHandle SimulatedComboTimer;
};
