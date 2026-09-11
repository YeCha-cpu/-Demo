// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/RPG_AttributeSet.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectExtension.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_AttributeSet::URPG_AttributeSet()
{
	// ── 保底默认值 ──
	// 真正的数值应该由 GE_InitAttributes 在角色初始化时覆盖（数据驱动：改数值不用重新编译）。
	// 这里给值只保证一件事：即使忘了配那个 GE，角色也不会因为属性全为 0 而除零或瞬间暴毙。
	//
	// "构造函数给安全默认值 + GE 覆盖实际数值" 是实际项目的标准做法——
	// 纯靠 GE 初始化的话，一旦资产没配好，出现的是 NaN 之类的诡异问题，非常难查。
	InitHealth(100.f);
	InitMaxHealth(100.f);

	InitAttack(10.f);
	InitDefense(10.f);

	InitMana(100.f);
	InitMaxMana(100.f);

	InitStamina(100.f);
	InitMaxStamina(100.f);

	InitIncomingDamage(0.f);
}

void URPG_AttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// ── 为什么在 PreAttributeChange 里做 clamp ──
	// 它的调用时机最早：在属性值被真正写入**之前**修改待写入的值。
	// 而且**任何**修改途径都会经过它——GE 的 Modifier、SetByCaller、代码里的 SetXxx，
	// 没有例外。所以它是最可靠的兜底位置。
	//
	// 代价：拿不到"是谁改的"这类上下文（那要用 PostGameplayEffectExecute）。
	// 另外注意 NewValue 是引用参数，改它不会触发属性变化回调，也不会递归。

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	// 最大值类属性只需保证非负。
	// 如果被 Debuff 减成负数，上面几个 Clamp 的上下界会反转（Min > Max），
	// FMath::Clamp 在那种情况下行为未定义——所以这里必须挡住。
	else if (Attribute == GetMaxHealthAttribute()
		|| Attribute == GetMaxManaAttribute()
		|| Attribute == GetMaxStaminaAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
}

void URPG_AttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// ══════════════════════════════════════════════════════════════════
	//  伤害落地 —— 整个游戏里唯一的"扣血"入口
	// ══════════════════════════════════════════════════════════════════
	// 只有元属性 IncomingDamage 会走到下面的逻辑；其他属性的修改（加攻、扣耐力等）
	// 到这里就直接返回了，它们由各自的使用方去响应。
	if (Data.EvaluatedData.Attribute != GetIncomingDamageAttribute())
	{
		return;
	}

	const float LocalDamage = GetIncomingDamage();
	SetIncomingDamage(0.f);   // 元属性用完立即清零，它不承载任何持久状态

	if (LocalDamage <= 0.f)
	{
		return;
	}

	AActor* OwningActor = GetOwningActor();

	// ── 无敌帧兜底检查 ──
	// 第一道防线在 GE_Damage 资产上（TargetTagRequirements 组件，GE 层面直接拒绝应用，
	// 连 Execution 都不会跑，更省性能）。这里再判一次是防御性编程：
	// 万一将来有别的途径直接写 IncomingDamage（陷阱、脚本伤害、DOT），也不至于打穿无敌。
	if (Data.Target.HasMatchingGameplayTag(RPGTags::State_Invulnerable))
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 伤害被无敌帧挡下：%.1f"),
			*GetNameSafe(OwningActor), LocalDamage);
		return;
	}

	const float OldHealth = GetHealth();
	const float NewHealth = FMath::Clamp(OldHealth - LocalDamage, 0.f, GetMaxHealth());
	SetHealth(NewHealth);

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 受到 %.1f 点伤害：%.0f → %.0f"),
		*GetNameSafe(OwningActor), LocalDamage, OldHealth, NewHealth);

	// ── 死亡判定 ──
	// 用 OldHealth > 0 做前置条件，保证"从活到死"这个跨越只触发一次。
	// 否则若同一帧有多个伤害源结算，会重复广播死亡事件。
	if (NewHealth <= 0.f && OldHealth > 0.f)
	{
		UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 生命归零，广播死亡事件"), *GetNameSafe(OwningActor));

		// 用 GameplayEvent 而不是直接调用死亡逻辑——保持"表现层不做逻辑"的分层原则：
		// 死亡的表现（蒙太奇、掉落、AI 停止）由 GA_Death 去处理，属性集只管广播事实。
		FGameplayEventData Payload;
		Payload.EventTag = RPGTags::Event_Combat_Death;
		Payload.Instigator = Data.EffectSpec.GetContext().GetInstigator();
		Payload.Target = OwningActor;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			OwningActor, RPGTags::Event_Combat_Death, Payload);
	}
}
