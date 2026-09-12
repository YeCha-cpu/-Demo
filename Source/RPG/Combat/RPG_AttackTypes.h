// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPG_AttackTypes.generated.h"

class UAnimMontage;

/**
 * 攻击模组的数据结构。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么蒙太奇可以留空】
 * ══════════════════════════════════════════════════════════════════════
 * 每个 Montage 字段都允许为空，GA 侧会做空检查：没有蒙太奇就跳过动画播放，
 * **但连段推进、伤害结算、耐力消耗全部照常执行**。
 *
 * 这不是"偷懒"，而是有意的设计：
 *   · 数值链路（倍率对不对、伤害公式对不对）可以用日志独立验证，
 *     不必等动画做好
 *   · 动画是表现层，把它变成数值逻辑的硬依赖会让调试变得很痛苦
 *   · 做动画时如果发现某个 Notify 位置不对，只影响表现，不会连带数值一起崩
 */

/**
 * 一个轻击段。
 *
 * 5 段的默认倍率：1.0 / 1.15 / 1.4 / 1.6 / 2.0
 * 这个递增曲线让连段的"最后一段"有明确的收益感，鼓励玩家打完整套。
 */
USTRUCT(BlueprintType)
struct FRPG_AttackSegment
{
	GENERATED_BODY()

	/** 这一段的蒙太奇。留空时跳过动画，但逻辑照常执行 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/** 伤害倍率（相对攻击力）。BaseDamage = Attack × 本值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.0f;

	/** 耐力消耗 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0.0"))
	float StaminaCost = 8.f;

	/** 该段的攻击标签，用于日志区分与 GameplayEvent 过滤 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tag")
	FGameplayTag AttackTag;
};

/**
 * 一个蓄力等级。
 *
 * 三段蓄力的默认倍率：3.0 / 4.5 / 6.5
 * 蓄力越久收益越高，但蓄力期间持续掉耐力 —— 这是"风险与收益"的设计：
 * 贪满蓄力可能被敌人打断，也可能因为耐力耗尽而无法闪避。
 */
USTRUCT(BlueprintType)
struct FRPG_HeavyAttackLevel
{
	GENERATED_BODY()

	/**
	 * 达到该等级所需的蓄力时间（秒）。
	 * 从按下右键开始计时，累积时间跨过这个阈值就升到这一级。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0"))
	float RequiredChargeTime = 0.5f;

	/** 松开右键释放时播放的攻击蒙太奇 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> ReleaseMontage = nullptr;

	/** 伤害倍率 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 3.0f;

	/** 释放时的耐力消耗 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0.0"))
	float StaminaCost = 15.f;

	/** 该等级对应的标签（用于给角色挂 State.Attack.Charging.LvN） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tag")
	FGameplayTag ChargeLevelTag;
};

/** 重击（蓄力）配置 */
USTRUCT(BlueprintType)
struct FRPG_HeavyAttackSet
{
	GENERATED_BODY()

	/** 起手蒙太奇：按下右键后播放，进入蓄力状态 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> ChargeStartMontage = nullptr;

	/**
	 * 蓄力中循环播放的蒙太奇（可选）。
	 * 留空时停在起手蒙太奇的最后一帧 —— 取决于动画本身怎么设计，
	 * 有些项目喜欢"蓄力时身体保持一个张力姿势"，那就留空。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> ChargeLoopMontage = nullptr;

	/** 三个蓄力等级，按 RequiredChargeTime 升序排列 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")
	TArray<FRPG_HeavyAttackLevel> Levels;

	/** 蓄力期间每秒消耗的耐力 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0.0"))
	float ChargeStaminaDrainPerSecond = 10.f;

	/** 蓄力时的移动速度倍率。蓄力中通常应该走得慢一些（0.3 = 30% 速度） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeMoveSpeedScale = 0.3f;
};
