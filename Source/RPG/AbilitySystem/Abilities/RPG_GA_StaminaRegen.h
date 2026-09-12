// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_StaminaRegen.generated.h"

/**
 * 耐力恢复（被动、常驻）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【这个类里没有一行"恢复逻辑" —— 这是刻意的】
 * ══════════════════════════════════════════════════════════════════════
 * 它只做一件事：在激活时把一个 Infinite 的 GE_StaminaRegen 挂到角色身上，
 * 然后自己就一直保持激活状态。
 *
 * 真正决定"什么时候恢复"的是 GE 上的 **Ongoing Tag Requirements**：
 *
 *     GE_StaminaRegen（Infinite，Period = 0.25s）
 *       ├─ Modifier:            Stamina += 3.75（即每秒 15 点）
 *       └─ OngoingTagRequirements:
 *            IgnoreTags: State.Stamina.Blocked
 *                        ↑ 角色有这个标签时，整个 GE 被抑制
 *
 * 而 State.Stamina.Blocked 由 GE_StaminaRegenDelay（Duration = 3 秒）授予，
 * 每次 ConsumeStamina 都会重新挂一次来刷新它的持续时间。
 *
 * 于是"停手 3 秒后缓慢恢复"这条规则变成了：
 *
 *     消耗耐力 → 挂上阻断标签（3 秒）→ 恢复 GE 被抑制
 *              → 3 秒内没再消耗 → 标签过期消失 → 恢复 GE 自动生效
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么不自己写个计时器】
 * ══════════════════════════════════════════════════════════════════════
 * 手写方案（GA 里存一个"最后消耗时间"，Tick 里判断是否超过 3 秒）的问题：
 *   · 需要 Tick 或定时器，多一份状态要维护
 *   · 联机下这份状态在客户端和服务器要各自维护，容易不一致
 *   · 调试时看不出"现在为什么没恢复"——只能翻代码
 *   · 策划想改"3 秒"就得改代码或加一堆配置项
 *
 * 标签方案则：
 *   · 零计时代码，时间纯粹由 GE 的 Duration 表达
 *   · 标签会复制，两端表现天然一致
 *   · 用 GameplayDebugger 一眼能看到"恢复被 State.Stamina.Blocked 挡住了"
 *   · 改恢复延迟 = 改 GE 的 Duration，改恢复速度 = 改 Modifier，都不碰代码
 *
 * 这是本项目里最"GAS 原生"的一处设计。
 */
UCLASS()
class RPG_API URPG_GA_StaminaRegen : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_StaminaRegen();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/**
	 * 常驻恢复 GE。需要配成：
	 *   · Duration Policy: Infinite
	 *   · Period: 0.25
	 *   · Modifier: Stamina += 每秒恢复量 × Period
	 *   · Components → Target Tag Requirements → Ongoing Tag Requirements
	 *       Ignore Tags: State.Stamina.Blocked
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Regen")
	TSubclassOf<UGameplayEffect> RegenEffectClass;
};
