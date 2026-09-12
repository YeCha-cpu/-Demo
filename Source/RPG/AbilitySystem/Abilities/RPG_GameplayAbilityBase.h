// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "RPG_GameplayAbilityBase.generated.h"

class ARPG_BaseCharacter;
class URPG_CombatComponent;
class URPG_AttributeSet;
class URPG_AttackModuleData;
class UAbilityTask_PlayMontageAndWait;

/**
 * 项目所有 GameplayAbility 的基类。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它提供什么】
 * ══════════════════════════════════════════════════════════════════════
 *   · 常用查询的快捷方式（角色 / 战斗组件 / 属性集 / 攻击模组）
 *   · 蒙太奇播放的空检查封装（没有动画也能跑逻辑）
 *   · 伤害施加与耐力消耗的统一入口（避免每个 GA 各写一遍）
 *   · 联机策略的默认值
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【关于"没有蒙太奇也能跑"】
 * ══════════════════════════════════════════════════════════════════════
 * PlayMontageOrSkip 在蒙太奇为空时返回 nullptr 并打一条日志，
 * GA 侧据此跳过"播放动画 + 监听动画事件"，但**其余逻辑照常执行**。
 *
 * 这不是妥协，而是刻意的设计：让数值链路（连段推进、伤害结算、耐力消耗）
 * 可以脱离动画独立验证。做动画时如果发现某个 Notify 位置不对，
 * 只会影响表现，不会连带数值一起崩 —— 排查范围小得多。
 */
UCLASS(Abstract)
class RPG_API URPG_GameplayAbilityBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	URPG_GameplayAbilityBase();

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	// ══════════════════════════════════════════════════════════════════
	//  便捷查询
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 能力持有者对应的 RPG 角色。取不到返回 nullptr
	 *
	 * ⚠️ 只能在 **ActivateAbility 及之后**调用（包括各种事件回调）。
	 * 它依赖 CurrentActorInfo，而 CanActivateAbility 可能在 CDO 上执行，
	 * 那时 CurrentActorInfo 是空的，会返回 nullptr —— 详见
	 * RPG_GameplayAbilityBase.cpp 里 CanActivateAbility 的说明。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
	ARPG_BaseCharacter* GetRPGCharacter() const;

	/** 战斗组件（输入缓存 + 连段索引）。取不到返回 nullptr */
	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
	URPG_CombatComponent* GetCombatComponent() const;

	/** 属性集 */
	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
	URPG_AttributeSet* GetRPGAttributeSet() const;

	/** 当前攻击模组 */
	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
	URPG_AttackModuleData* GetAttackModule() const;

protected:
	// ══════════════════════════════════════════════════════════════════
	//  行为封装
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 播放蒙太奇并返回 Task，调用方负责绑定委托并 ReadyForActivation。
	 *
	 * @return 蒙太奇为空时返回 nullptr（此时调用方应跳过动画相关逻辑，
	 *         但继续执行数值逻辑）
	 */
	UAbilityTask_PlayMontageAndWait* PlayMontageOrSkip(UAnimMontage* Montage, FName TaskName);

	/**
	 * 对目标施加伤害。
	 *
	 * 内部构造 GE_Damage 的 Spec，通过 SetByCaller(Data.Damage.Multiplier)
	 * 传入倍率 —— 这样**一个 GE_Damage 资产就能服务所有攻击段**，
	 * 不需要为每段做一个 GE。
	 *
	 * @param Target            受击目标
	 * @param DamageMultiplier  攻击力倍率（段倍率 / 蓄力倍率 / 切手倍率）
	 * @param HitResult         命中信息（可选）。传进来后 GameplayCue 能拿到
	 *                          命中点播特效，否则特效只能播在角色根位置
	 */
	void ApplyDamageToTarget(AActor* Target, float DamageMultiplier, const FHitResult* HitResult = nullptr);

	/**
	 * 消耗耐力。
	 * 走 GE_StaminaCost，通过 SetByCaller(Data.Stamina.Cost) 传消耗量。
	 * 属性集侧会在检测到耐力减少时自动挂上"恢复阻断"的 GE。
	 */
	void ConsumeStamina(float Amount);

	/**
	 * 检查耐力是否足够。
	 * 用于 CanActivateAbility 的前置判断 —— 不够就干脆不激活，
	 * 而不是激活后再扣成负数。
	 */
	bool HasEnoughStamina(float Amount) const;

	// ══════════════════════════════════════════════════════════════════
	//  GE 配置（在 GA 蓝图子类里指定）
	// ══════════════════════════════════════════════════════════════════

	/** 伤害 GE。里面挂的是 RPG_DamageExecution */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Effects")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** 耐力消耗 GE（Instant，SetByCaller 传消耗量） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Effects")
	TSubclassOf<UGameplayEffect> StaminaCostEffectClass;
};
