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
 * 【网络复制：为什么属性集是 GAS 里最繁琐的部分】
 * ══════════════════════════════════════════════════════════════════════
 * 每个需要同步的属性都要"三件套"：
 *   1. UPROPERTY(ReplicatedUsing = OnRep_Xxx)          —— 声明这个属性要同步
 *   2. void OnRep_Xxx(const FGameplayAttributeData& Old) —— 客户端收到新值时的处理
 *   3. GetLifetimeReplicatedProps 里 DOREPLIFETIME_CONDITION_NOTIFY(...) —— 注册复制规则
 *
 * 三件套里最容易漏的是第 2 步。漏了它，服务器改属性后客户端**数值会同步，
 * 但不会触发任何回调**——UI 不刷新、依赖属性变化的逻辑不执行，表现为
 * "血条不动但实际血量已经变了"，是联机调试里最隐蔽的一类 bug。
 *
 * 这也正是"一开始就把 ASC 挂对位置"的价值：属性集挂在 PlayerState 上，
 * 复制路径天然正确，不需要额外处理重生时的属性同步。
 *
 * 注意 IncomingDamage（元属性）**不参与复制**：它只是服务器上伤害计算的
 * 临时投递口，用完立即清零，同步它没有意义、还浪费带宽。
 */
UCLASS()
class RPG_API URPG_AttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	URPG_AttributeSet();

	//~ Begin UAttributeSet interface
	/**
	 * 钳制**当前值**。
	 *
	 * ⚠️ 它只在**直接赋值**路径上被调用（`SetStamina()` 这类），
	 * GE 的 Modifier **不经过这里** —— 详见下面那个。
	 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/**
	 * 钳制**基础值**（BaseValue）。
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【★ 这个才是 GE 修改必经的那道关】
	 * ══════════════════════════════════════════════════════════════════
	 * 只重写 PreAttributeChange 是**拦不住 GE 的**。
	 *
	 * 引擎里这两条路是分开的：
	 *   · 直接赋值      → SetNumericValueChecked → PreAttributeChange
	 *   · GE 的 Modifier → SetAttributeBaseValue → **PreAttributeBaseChange**
	 *
	 * 整个 GAS 插件里 `PreAttributeChange` 只有两处调用点，都在
	 * AttributeSet.cpp 的属性拷贝函数里；而 GE 施加 Modifier 走的是
	 * GameplayEffect.cpp:4001 的这条。引擎自己的注释也写明了这一点：
	 *
	 *   "This function should enforce clamping (presuming you wish to clamp
	 *    the base value **along with** the final value in PreAttributeChange)"
	 *                                          —— AttributeSet.h:226-228
	 *
	 * 漏写的后果：**耐力/生命可以突破 [0, Max]**。
	 *   · 耐力耗尽后继续攻击 → BaseValue 变成负数 → 界面读数长时间停在 0
	 *     （恢复要先把负数填平），表现为"很久都不恢复"
	 *   · 耐力恢复是每 0.25 秒 +3.75 且没有上限 → 站着不动两分钟 BaseValue
	 *     能涨到几百，之后消耗 8 点根本看不出来，表现为"满耐力时消耗还是 100%"
	 *
	 * 而且这两个症状**都不会报错**，只会让人觉得"数值怪怪的"。
	 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UAttributeSet interface

	// ══════════════════════════════════════════════════════════════════
	//  生命
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Health", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Health);

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Health", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, MaxHealth);

	// ══════════════════════════════════════════════════════════════════
	//  战斗
	// ══════════════════════════════════════════════════════════════════

	/** 攻击力。伤害计算的基础值：BaseDamage = Attack × 攻击段倍率 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Combat", ReplicatedUsing = OnRep_Attack)
	FGameplayAttributeData Attack;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Attack);

	/** 防御值。参与减伤公式 Mitigation = Defense / (Defense + K)，K 默认 100 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Combat", ReplicatedUsing = OnRep_Defense)
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Defense);

	// ══════════════════════════════════════════════════════════════════
	//  法力（黑神话式的 3 个法术消耗）
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Mana", ReplicatedUsing = OnRep_Mana)
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Mana);

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Mana", ReplicatedUsing = OnRep_MaxMana)
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, MaxMana);

	// ══════════════════════════════════════════════════════════════════
	//  耐力
	//  闪避/攻击/跳跃分次消耗，奔跑/蓄力持续消耗，停手 3 秒后缓慢恢复
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Stamina", ReplicatedUsing = OnRep_Stamina)
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, Stamina);

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attributes|Stamina", ReplicatedUsing = OnRep_MaxStamina)
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, MaxStamina);

	// ── 网络复制回调 ──
	// 只在客户端被调用（服务器是数据源，不会回调自己）。
	// GAMEPLAYATTRIBUTE_REPNOTIFY 宏内部做两件事：
	//   1. SetBaseAttributeValueFromReplication —— 把同步过来的值写进本地属性
	//   2. 广播 OnGameplayAttributeValueChange 委托
	// 所以 UI 只要注册了那个委托就会自动刷新，不需要手写任何同步逻辑。
	// 这也是为什么"漏掉 OnRep"的症状是数值对了但界面不动。
	UFUNCTION() virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);
	UFUNCTION() virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);
	UFUNCTION() virtual void OnRep_Attack(const FGameplayAttributeData& OldAttack);
	UFUNCTION() virtual void OnRep_Defense(const FGameplayAttributeData& OldDefense);
	UFUNCTION() virtual void OnRep_Mana(const FGameplayAttributeData& OldMana);
	UFUNCTION() virtual void OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana);
	UFUNCTION() virtual void OnRep_Stamina(const FGameplayAttributeData& OldStamina);
	UFUNCTION() virtual void OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina);

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

private:
	/**
	 * 属性钳制规则 —— 上面两个 Pre* 回调共用同一套。
	 *
	 * 抽出来是为了保证"直接赋值"和"GE 修改"两条路径的行为**绝对一致**，
	 * 不会出现"用 SetStamina() 会被钳制、用 GE 就不会"这种诡异差异 ——
	 * 那种差异查起来极其痛苦，因为两条路看起来都"应该"是同一个结果。
	 */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};
