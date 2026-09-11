// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "RPG_AttributeSet.generated.h"

/**
 * 敌我共用的属性集。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【数据流：属性是怎么被改的】
 * ══════════════════════════════════════════════════════════════════════
 * 这是整个 GAS 里最需要理解清楚的一条链路：
 *
 *   GE 被应用
 *     → 它的 Modifier（Additive / Multiplicative / Override）被求值
 *     → 结果写进 BaseValue（永久值）和 CurrentValue（含 Buff 的当前值）
 *     → PostGameplayEffectExecute() 被调用 ← 我们在这里做后处理
 *
 * 关键点：**Execution Calculation 捕获到的属性，是已经被所有 GE 修改过的当前值**。
 * 所以"伤害计算综合了攻防和 Buff/Debuff"这件事，不需要在计算里手动遍历 Buff——
 * 属性管线已经帮你算好了。这是最容易被误解的一点。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【BaseValue 与 CurrentValue 的区别】
 * ══════════════════════════════════════════════════════════════════════
 *   BaseValue    = 所有 Instant GE 和 SetByCaller 的结果累积，不含临时 Buff
 *   CurrentValue = BaseValue 叠加所有 Duration/Infinite GE 的 Modifier 后的结果
 *
 * 改 BaseValue → 永久生效（比如扣血）
 * 加 Duration GE → 只影响 CurrentValue，到期自动还原（比如加攻 Buff）
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【关于网络复制】
 * ══════════════════════════════════════════════════════════════════════
 * 本项目不做联机，因此**没有**实现 GetLifetimeReplicatedProps / OnRep_* 函数。
 * 如果将来要加联机，每个属性需要：
 *   1. UPROPERTY(ReplicatedUsing = OnRep_Health) FGameplayAttributeData Health;
 *   2. 实现 void OnRep_Health(const FGameplayAttributeData& OldValue)
 *      { GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, Health, OldValue); }
 *   3. GetLifetimeReplicatedProps 里 DOREPLIFETIME_CONDITION_NOTIFY(...)
 * 属性集是 GAS 里复制规则最繁琐的部分，这也是"一开始就把 ASC 挂对位置"的价值所在。
 */
UCLASS()
class RPG_API URPG_AttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	URPG_AttributeSet();

	//~ Begin UAttributeSet interface
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	//~ End UAttributeSet interface

	// ══════════════════════════════════════════════════════════════════
	//  生命
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Health")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Health);

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Health")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, MaxHealth);

	// ══════════════════════════════════════════════════════════════════
	//  战斗
	// ══════════════════════════════════════════════════════════════════

	/** 攻击力。伤害计算的基础值：BaseDamage = Attack × 攻击段倍率 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Combat")
	FGameplayAttributeData Attack;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Attack);

	/** 防御值。参与减伤公式 Mitigation = Defense / (Defense + K)，K 默认 100 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Combat")
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Defense);

	// ══════════════════════════════════════════════════════════════════
	//  法力（黑神话式的 3 个法术消耗）
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Mana")
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Mana);

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Mana")
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, MaxMana);

	// ══════════════════════════════════════════════════════════════════
	//  耐力
	//  闪避/攻击/跳跃分次消耗，奔跑/蓄力持续消耗，停手 3 秒后缓慢恢复
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Stamina")
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Stamina);

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Stamina")
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, MaxStamina);

	// ══════════════════════════════════════════════════════════════════
	//  元属性（Meta Attribute）
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 伤害中转站。**不是真正的属性**，只是一个"投递口"。
	 *
	 * 为什么要有它？——把所有伤害后处理集中到一个地方：
	 *   DamageExecution 算出最终伤害 → 写进 IncomingDamage
	 *   → PostGameplayEffectExecute 接住
	 *       ├─ 无敌帧检查
	 *       ├─ 扣 Health
	 *       ├─ 将来加：格挡减伤、伤害飘字、吸血、受击顿帧
	 *       └─ 死亡判定
	 *
	 * 如果不设这个中转站，让 Execution 直接改 Health，那么每加一种后处理
	 * （格挡、无敌、飘字…）都要去改 Execution 或写一堆 GE，很快就会失控。
	 *
	 * 注意：它用完立即清零，不承载任何持久状态。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Meta")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, IncomingDamage);

	/** 伤害计算里防御值的软化常数 K（减伤 50% 所需的防御值）。放这里是为了让 Execution 能读到 */
	static constexpr float DefenseConstant = 100.f;

	/** 单次伤害的保底值，避免高防目标完全免伤导致打不动 */
	static constexpr float MinDamage = 1.f;
};
