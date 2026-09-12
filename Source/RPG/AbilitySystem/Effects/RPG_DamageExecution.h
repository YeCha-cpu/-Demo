// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "RPG_DamageExecution.generated.h"

/**
 * 伤害执行计算（Execution Calculation）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么伤害用 Execution 而不是普通 Modifier】
 * ══════════════════════════════════════════════════════════════════════
 * 普通 Modifier 只能做"属性 A 加减乘除一个值"这种线性运算。
 * 而伤害需要同时读**双方的属性**并做非线性计算：
 *
 *     最终伤害 = 攻击方.Attack × 倍率 × (1 - 防御方.Defense / (防御方.Defense + K))
 *
 * 这种跨 Actor、非线性的公式只能靠 Execution。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【Buff/Debuff 不需要在这里手动遍历 —— 这是最容易被误解的一点】
 * ══════════════════════════════════════════════════════════════════════
 * 捕获到的 Attack 和 Defense **已经是被所有 GE 修改过的当前值**。
 * 加攻 Buff 是通过 GE 的 Modifier 提升 Attack 属性的，减防 Debuff 同理 ——
 * 属性管线在 GE 求值阶段就处理完了，Execution 只需要读最终值。
 *
 * 所以"综合防御值、buff、debuff 得出最终伤害"这件事，代码上只体现为
 * 两行 AttemptCalculateCapturedAttributeMagnitude。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【Snapshot 的取舍】
 * ══════════════════════════════════════════════════════════════════════
 * 本 Execution 的两个属性都用 Snapshot = true，含义是"在 GE 被应用的瞬间
 * 捕获属性值，之后属性变化不影响本次计算"。
 *
 * 为什么伤害要用 Snapshot？
 *   玩家按下攻击键那一刻的 Attack 应该被**锁定**。否则如果一个投射物在飞行
 *   途中玩家吃了减攻 Debuff，伤害会莫名变低 —— 玩家无法理解"为什么我按的时候
 *   伤害是 100，打出去变成 60"。
 *
 * 什么时候不该用 Snapshot？持续型效果（DOT）通常需要实时反映当前状态，
 * 那才用 Snapshot = false。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它只负责算，不负责扣血】
 * ══════════════════════════════════════════════════════════════════════
 * Execution 把最终伤害写进**元属性 IncomingDamage**，然后由
 * URPG_AttributeSet::PostGameplayEffectExecute 统一处理扣血、无敌检查、
 * 死亡判定、将来的飘字与吸血。
 *
 * 这样所有伤害后处理集中在一处，加新机制不用回来改这个文件。
 */
UCLASS()
class RPG_API URPG_DamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	URPG_DamageExecution();

	virtual void Execute_Implementation(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
