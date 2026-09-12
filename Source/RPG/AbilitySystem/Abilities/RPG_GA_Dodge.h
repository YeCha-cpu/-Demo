// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "GameplayEffectTypes.h"
#include "RPG_GA_Dodge.generated.h"

class UAnimMontage;

/**
 * 闪避（翻滚）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【一次闪避包含三件事，缺一不可】
 * ══════════════════════════════════════════════════════════════════════
 *   1. 位移  —— 沿面朝方向施加冲量，让身体真的翻出去
 *   2. 无敌  —— 在翻滚的某个窗口内免疫伤害，这是闪避的**核心价值**
 *   3. 代价  —— 消耗耐力，否则玩家会无限翻滚规避一切
 *
 * 三者里最容易做错的是第 2 项的**时机**：无敌帧的起止点必须和动画的
 * 视觉表现对齐（脚离地那帧开、落地前那帧关），所以它由蒙太奇上的
 * AnimNotifyState 精确控制，而不是代码里给个固定秒数。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么用 LaunchCharacter 而不是 RootMotion】
 * ══════════════════════════════════════════════════════════════════════
 *   RootMotion    位移与动画 100% 同步，但距离写死在动画里 ——
 *                 想调远一点就得让动画师重做动画
 *   LaunchCharacter  距离是可调参数，还能按方向键微调翻滚方向；
 *                 代价是位移和动画可能不完全贴合，需要手动调参
 *
 * 动作游戏里闪避距离是**要反复调的手感参数**，所以选后者。
 */
UCLASS()
class RPG_API URPG_GA_Dodge : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_Dodge();

protected:
	/**
	 * 激活前检查耐力。
	 *
	 * ⚠️ 这里必须用参数传入的 ActorInfo 取属性集，不能用基类的
	 * GetRPGAttributeSet() —— 后者依赖 CurrentActorInfo，而本函数
	 * 可能在 CDO 上执行（那时 CurrentActorInfo 是空的）。
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
	/** 监听无敌帧事件（由蒙太奇上的 AnimNotifyState 广播） */
	void BindGameplayEventListeners();

	/** 沿面朝方向施加冲量 */
	void ApplyDodgeImpulse();

	void ApplyInvulnerability();
	void RemoveInvulnerability();

	/** 收尾：清理无敌 GE 与定时器，结束能力 */
	void FinishDodge(bool bWasCancelled);

	/** 没有蒙太奇时的模拟收尾 */
	void OnSimulatedDodgeFinished();

	UFUNCTION() void OnMontageCompleted();
	UFUNCTION() void OnMontageInterrupted();
	UFUNCTION() void OnInvulnerabilityBegin(FGameplayEventData Payload);
	UFUNCTION() void OnInvulnerabilityEnd(FGameplayEventData Payload);

protected:
	// ══════════════════════════════════════════════════════════════════
	//  配置
	// ══════════════════════════════════════════════════════════════════

	/** 翻滚蒙太奇。留空则走模拟时序（仍会施加冲量并给无敌帧，便于验证逻辑） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Dodge")
	TObjectPtr<UAnimMontage> DodgeMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Dodge", meta = (ClampMin = "0.0"))
	float StaminaCost = 20.f;

	/** 冲量强度（厘米/秒）。600 约等于一次缓慢位移，1500 接近一次真正的翻滚 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Dodge", meta = (ClampMin = "0.0"))
	float DodgeImpulse = 1200.f;

	/**
	 * 无敌 GE。它需要用 TargetTags 组件授予 State.Invulnerable。
	 * 留空则无敌帧不生效（会有 Warning）。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Dodge")
	TSubclassOf<UGameplayEffect> InvulnerabilityEffectClass;

	// ── 无蒙太奇时的模拟参数 ──

	/** 模拟的翻滚总时长 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Debug", meta = (ClampMin = "0.1"))
	float SimulatedDodgeDuration = 0.7f;

	/** 模拟时无敌帧占前多少比例（0.6 = 前 60% 无敌，后 40% 有破绽） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Debug", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SimulatedInvulnerabilityRatio = 0.6f;

private:
	/** 当前无敌 GE 的句柄。有效表示正处于无敌中 */
	FActiveGameplayEffectHandle InvulnerabilityHandle;

	/** 模拟模式：翻滚结束的定时器 */
	FTimerHandle SimulatedTimer;

	/** 模拟模式：无敌帧结束的定时器（早于翻滚结束，留出破绽期） */
	FTimerHandle SimulatedInvulnerabilityTimer;
};
