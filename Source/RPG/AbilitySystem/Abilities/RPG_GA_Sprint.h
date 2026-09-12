// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_Sprint.generated.h"

/**
 * 奔跑（按住型，持续消耗耐力）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么奔跑要走 GAS，而走路不走】
 * ══════════════════════════════════════════════════════════════════════
 * 走路是"没有代价的移动"——它不该被任何东西阻断，也不该消耗任何资源。
 * 而奔跑有两个 GAS 该管的特征：
 *   · **持续消耗耐力** —— 需要跟属性系统打交道
 *   · **耐力耗尽要自动中断** —— 需要感知属性变化并做出决策
 *
 * 这两件事放在 PlayerController 里手写也能跑，但那样耐力规则就散落在
 * 输入层了。放进 GA 之后，"什么行为消耗多少耐力"全部集中在能力层。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【持续消耗用定时器而不是周期 GE】
 * ══════════════════════════════════════════════════════════════════════
 * 更"GAS 原生"的做法是挂一个 Duration + Period 的 GE_StaminaDrain。
 * 这里选了定时器 + 复用 ConsumeStamina，原因是：
 *   · ConsumeStamina 已经处理了"刷新恢复阻断"，走它才能保证
 *     奔跑期间耐力不会偷偷恢复
 *   · 周期 GE 的 SetByCaller 速率配置比较绕，多一个容易配错的资产
 *   · 0.1 秒一次的 GE 应用开销在这个量级下可以忽略
 *
 * 将来如果要做"奔跑速度随耐力衰减"这类复杂效果，再换成周期 GE 更合适。
 */
UCLASS()
class RPG_API URPG_GA_Sprint : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_Sprint();

	/** 松开按键 → 停止奔跑 */
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
	/** 定时回调：按速率扣耐力，耗尽则自动停止 */
	void TickStaminaDrain();

	/** 收尾：恢复速度、清理定时器、结束能力 */
	void FinishSprint(bool bWasCancelled);

protected:
	// ── 配置 ──
	// ⚠️ 带 BlueprintReadOnly 的 UPROPERTY 必须放在 protected/public ——
	// UHT 不允许 private 成员暴露给蓝图。这个错误在本项目里犯过两次，
	// 记住：**配置项放 protected，纯内部状态放 private**。

	/** 每秒消耗的耐力 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Sprint", meta = (ClampMin = "0.0"))
	float StaminaDrainPerSecond = 5.f;

	/** 扣耐力的检查间隔（秒）。越密越平滑，但 GE 应用也越频繁 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Sprint", meta = (ClampMin = "0.02"))
	float DrainTickInterval = 0.1f;

private:
	FTimerHandle DrainTimer;
};
