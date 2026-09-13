// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Effects/RPG_DamageExecution.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffectTypes.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

/**
 * 属性捕获定义。
 *
 * DECLARE_ATTRIBUTE_CAPTUREDEF 会生成两个成员：
 *   FProperty* AttackProperty;                          —— 属性的反射信息
 *   FGameplayEffectAttributeCaptureDefinition AttackDef; —— 捕获配置（含 Snapshot 标志）
 *   
 * DEFINE_ATTRIBUTE_CAPTUREDEF 则生成一个 FGameplayEffectAttributeCaptureDefinition，用于捕获属性。
 *
 * 用一个静态结构体集中管理，避免每个 Execution 实例都重建一遍。
 */
struct FRPGDamageStatics
{
	DECLARE_ATTRIBUTE_CAPTUREDEF(Attack);
	DECLARE_ATTRIBUTE_CAPTUREDEF(Defense);
	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingDamage);

	FRPGDamageStatics()
	{
		// 攻击力：从**来源**（攻击者）捕获，Snapshot = true（出手瞬间锁定）
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPG_AttributeSet, Attack, Source, true);

		// 防御力：从**目标**（受击者）捕获，Snapshot = true
		// 用 Snapshot 而不是实时值，是为了让"命中瞬间的防御"决定减伤 ——
		// 否则一个飞行道具在途中目标吃了减防 Debuff，伤害会莫名变高。
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPG_AttributeSet, Defense, Target, true);

		// 元属性：这是**输出**目标，Snapshot 无意义（我们不读它，只写它）
		DEFINE_ATTRIBUTE_CAPTUREDEF(URPG_AttributeSet, IncomingDamage, Target, false);
	}
};

static const FRPGDamageStatics& DamageStatics()
{
	// 函数内静态变量：保证只构造一次，且线程安全（C++11 起）
	static FRPGDamageStatics Statics;
	return Statics;
}

URPG_DamageExecution::URPG_DamageExecution()
{
	// 声明本 Execution 需要哪些属性 ——
	// 没在这里注册的属性，AttemptCalculateCapturedAttributeMagnitude 会拿不到值。
	RelevantAttributesToCapture.Add(DamageStatics().AttackDef);
	RelevantAttributesToCapture.Add(DamageStatics().DefenseDef);
	RelevantAttributesToCapture.Add(DamageStatics().IncomingDamageDef);
}

void URPG_DamageExecution::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	// 聚合标签：属性求值时需要考虑双方的标签（比如"对不死族伤害翻倍"这类规则）
	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	// ══════════════════════════════════════════════════════════════════
	//  1. 读取双方属性
	// ══════════════════════════════════════════════════════════════════
	// 注意：这里读到的是**已经被所有 GE 修改过**的值 ——
	// 加攻 Buff、减防 Debuff 都已经体现在里面了，不需要手动遍历。

	float Attack = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().AttackDef, EvalParams, Attack);
	Attack = FMath::Max(Attack, 0.f);

	float Defense = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics().DefenseDef, EvalParams, Defense);
	Defense = FMath::Max(Defense, 0.f);

	// ══════════════════════════════════════════════════════════════════
	//  2. 取伤害倍率（SetByCaller）
	// ══════════════════════════════════════════════════════════════════
	// 这是"一个 GE_Damage 服务所有攻击段"的关键：
	// GE 资产里不写死数值，具体倍率由 GA 在运行时通过 SetByCaller 填入。
	//
	// 第三个参数是"找不到时的默认值"——传 1.0 而不是 0，
	// 这样即使忘了传倍率也只是伤害偏低，而不是完全没伤害（后者更难排查）。
	const float Multiplier = Spec.GetSetByCallerMagnitude(RPGTags::Data_Damage_Multiplier, /*WarnIfNotFound*/ false, /*DefaultIfNotFound*/ 1.f);

	const float BaseDamage = Attack * FMath::Max(Multiplier, 0.f);

	// ══════════════════════════════════════════════════════════════════
	//  3. 防御减伤曲线
	// ══════════════════════════════════════════════════════════════════
	// 用 Defense / (Defense + K) 而不是减法或百分比，原因：
	//   · 减法（Damage - Defense）→ 防御堆到攻击力以上就完全免伤，数值失控
	//   · 百分比（Damage × (1 - Def%))→ 堆到 100% 就无敌，同样失控
	//   · 本公式是收益递减曲线，永远到不了 100%，且 K 有直观含义：
	//     K 就是"减伤 50% 所需的防御值"
	const float Mitigation = Defense / (Defense + URPG_AttributeSet::DefenseConstant);

	// 【最终伤害】 = 基础伤害 × (1 - 减伤)
	float FinalDamage = BaseDamage * (1.f - Mitigation);

	// 保底伤害：避免高防目标把伤害压到 0 导致"打不动"
	FinalDamage = FMath::Max(FinalDamage, URPG_AttributeSet::MinDamage);

	// ══════════════════════════════════════════════════════════════════
	//  4. 写进元属性
	// ══════════════════════════════════════════════════════════════════
	// 注意是写 IncomingDamage 而不是直接改 Health ——
	// 扣血、无敌判定、死亡广播都由 AttributeSet::PostGameplayEffectExecute 统一处理。
	OutExecutionOutput.AddOutputModifier(
		FGameplayModifierEvaluatedData(
			DamageStatics().IncomingDamageProperty,
			EGameplayModOp::Additive,
			FinalDamage));

	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("伤害计算：攻击 %.1f × 倍率 %.2f = %.1f | 防御 %.1f → 减伤 %.1f%% | 最终 %.1f"),
		Attack, Multiplier, BaseDamage, Defense, Mitigation * 100.f, FinalDamage);
}
