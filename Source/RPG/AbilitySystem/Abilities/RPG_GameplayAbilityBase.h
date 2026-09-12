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

	/**
	 * 输入释放回调。
	 *
	 * 按住型能力（重击蓄力）重写它来响应"玩家松开按键"。
	 * 瞬发能力不需要处理 —— 所以这里是**空实现而不是纯虚函数**。
	 *
	 * 调用链：
	 *   PlayerController 收到 EnhancedInput 的 Completed 事件
	 *     → ASC::NotifyInputReleased(InputTag)
	 *       → 找到该输入标签对应的激活中能力实例
	 *         → 本函数
	 *
	 * 注意：只有能力**正在激活中**才会收到这个回调。如果松开时能力已经结束，
	 * 调用链在 Spec->GetPrimaryInstance() 那一步就断了，不会走到这里。
	 */
	virtual void OnInputReleased() {}

	/**
	 * 是否在授予时自动激活（被动能力用）。
	 *
	 * 供 URPG_AbilitySystemComponent 在 GiveAbility 之后判断要不要立即拉起。
	 * 用 getter 而不是把 bActivateOnGranted 直接公开，是为了保持
	 * "配置只能由子类和编辑器改、外部只读"的边界 —— 否则任何代码都能
	 * 在运行时把某个能力改成被动，那会很难排查。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
	bool ShouldActivateOnGranted() const { return bActivateOnGranted; }

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

	/**
	 * 没有蒙太奇时的简化命中检测：在角色身前做一次球形检测并施加伤害。
	 *
	 * ⚠️ 这是**纯开发期辅助**，不是最终实现。它的价值在于让连段推进、
	 * 伤害结算、耐力消耗三条链路在动画做好之前就能独立验证 ——
	 * 动画接上后由 WeaponTrace 接管，这个函数不再被调用。
	 *
	 * @param DamageMultiplier 伤害倍率
	 * @param ForwardOffset    检测中心相对角色的前向偏移（厘米）
	 * @param Radius           检测球半径（厘米）
	 */
	void PerformSimulatedMeleeHit(float DamageMultiplier, float ForwardOffset = 150.f, float Radius = 80.f);

	// ══════════════════════════════════════════════════════════════════
	//  GE 配置（在 GA 蓝图子类里指定）
	// ══════════════════════════════════════════════════════════════════

	/** 伤害 GE。里面挂的是 RPG_DamageExecution */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Effects")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** 耐力消耗 GE（Instant，SetByCaller 传消耗量） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Effects")
	TSubclassOf<UGameplayEffect> StaminaCostEffectClass;

	/**
	 * 耐力恢复阻断 GE（Duration）。
	 *
	 * 每次消耗耐力后重新挂一次，**它的持续时间就是"停手多久才开始恢复"**。
	 * 恢复用的 GE_StaminaRegen 通过 OngoingTagRequirements 检查它授予的
	 * State.Stamina.Blocked 标签来决定是否生效。
	 *
	 * 于是"停手 3 秒后缓慢恢复"这条规则**完全由标签驱动**：
	 * 不需要 Tick、不需要计时器、不需要一行判断代码。
	 * 而且联机下天然正确 —— 标签会复制，进度条在两端表现一致。
	 *
	 * ⚠️ 这个 GE 需要配成"每次应用刷新持续时间"（Stacking 相关设置），
	 * 否则连续消耗时阻断时间不会延长。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Effects")
	TSubclassOf<UGameplayEffect> StaminaRegenDelayEffectClass;

	// ══════════════════════════════════════════════════════════════════
	//  被动能力
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 授予后立刻自动激活。
	 *
	 * 用于耐力恢复这类**常驻被动能力** —— 它们没有输入触发，需要在角色
	 * 初始化时就开始工作。激活后会一直保持激活状态（GA 内部不调 EndAbility），
	 * 直到角色死亡或能力被强制结束。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Ability")
	bool bActivateOnGranted = false;
};
