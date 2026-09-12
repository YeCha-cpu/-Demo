// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"
#include "Combat/RPG_AttackModuleData.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GameplayAbilityBase::URPG_GameplayAbilityBase()
{
	// ══════════════════════════════════════════════════════════════════
	//  实例化策略 —— 必须是 InstancedPerActor
	// ══════════════════════════════════════════════════════════════════
	// ⚠️ UE 5.8 的 UGameplayAbility 构造函数把默认值设成了
	// InstancedPerExecution（见 GameplayAbility.cpp:102），这对我们**完全不适用**。
	//
	// 两种策略的区别（引擎头文件里的原话）：
	//   InstancedPerActor      每个 Actor 一个实例，同一时刻只能有一个激活，
	//                          **状态在激活之间保留**
	//   InstancedPerExecution  每次执行都实例化，可同时运行多个，
	//                          **状态不保留**
	//
	// 对我们的连段系统来说，InstancedPerExecution 是灾难性的：
	//   · 连按三次会创建三个实例同时跑（各自播各的动画、各扣各的耐力）
	//   · 连段进度（CurrentSegmentIndex）存在实例成员里，新实例永远从第 1 段开始
	//   · bComboWindowOpen 之类的窗口状态也全部丢失
	// 表现就是"连按没有衔接，每次都从第一段重来"。
	//
	// 所以显式设回 InstancedPerActor —— 连段状态必须跨激活保留，
	// 而且我们**不希望**同一个攻击能力有多个实例同时跑。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// ══════════════════════════════════════════════════════════════════
	//  联机策略默认值
	// ══════════════════════════════════════════════════════════════════
	// LocalPredicted：客户端按下按键后**立刻在本地执行**，不等服务器往返。
	// 这是动作游戏手感的前提 —— 攻击、闪避如果等一个 RTT 才响应，
	// 玩家的感受就是"按键延迟很高"。
	//
	// 服务器同时执行一份权威版本，不一致时纠正客户端。
	//
	// 敌人 AI 的能力需要在自己的类里覆盖成 ServerOnly —— AI 只在服务器跑，
	// 客户端执行它毫无意义还会造成表现重复。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// ClientOrServer：允许客户端主动请求激活。
	// 攻击、闪避需要这个；纯服务器逻辑的能力应改成 ServerOnly 防伪造。
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;

	// 能力被这些标签阻断时无法激活。
	// 注意这里是**实例级**的阻断（比 GE 的标签要求更轻量），
	// 适合"死亡后不能用任何技能"这类全局规则。
	ActivationBlockedTags.AddTag(RPGTags::State_Dead);
}

bool URPG_GameplayAbilityBase::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		// ⚠️ 父类拒绝时**必须**把原因打出来。
		// 引擎在这里只会往 OptionalRelevantTags 填一个标签就返回 false，
		// 不打印任何东西 —— 表现为"按键没反应，日志一片空白"，
		// 排查时完全无从下手（这个坑我们踩过）。
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] CanActivateAbility 被引擎拒绝。原因标签：%s"),
			*GetName(),
			(OptionalRelevantTags && !OptionalRelevantTags->IsEmpty())
				? *OptionalRelevantTags->ToStringSimple()
				: TEXT("(空) —— 常见原因：标签阻断 / 冷却中 / Cost 资源不足 / 未满足 ActivationRequiredTags"));

		return false;
	}

	// ══════════════════════════════════════════════════════════════════
	//  ⚠️ 这里必须用参数里的 ActorInfo，绝不能用 GetAbilitySystemComponentFromActorInfo()
	// ══════════════════════════════════════════════════════════════════
	// 这是 GAS 里一个非常隐蔽的坑，我们实际踩过：
	//
	// CanActivateAbility 的调用时机比直觉更早 —— 它在**能力实例化之前**
	// 就要判断"这个能力现在能不能激活"。那时 this 指向的是 CDO（类默认对象），
	// 而 CDO 的 CurrentActorInfo **永远是空的**。
	//
	// 所以凡是走 CurrentActorInfo 的便捷函数（GetAbilitySystemComponentFromActorInfo、
	// GetAvatarActorFromActorInfo 等）在这里全部返回 nullptr —— 结果是
	// **能力永远无法激活**，而且日志里的表现极具误导性：
	// 会显示成"拿不到 ASC / ActorInfo 没初始化"，让人往角色初始化方向去查，
	// 实际上调用方传进来的 ActorInfo 完全是好的。
	//
	// 记住这条规则：**CanActivateAbility 是 const 函数，它只能依赖参数，
	// 不能依赖任何实例状态。**
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] CanActivateAbility 失败：传入的 ActorInfo 无效，或它没有关联 ASC"), *GetName());

		return false;
	}

	// ── 死亡检查 ──
	// 虽然基类已经用 ActivationBlockedTags 加了 State.Dead，这里再判一次是因为
	// 标签阻断依赖 GE 正确授予标签 —— 如果将来有别的途径让角色"死亡但没标签"
	// （比如脚本直接扣血到 0 而没走属性集），这道检查能兜住。
	const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ASC->HasMatchingGameplayTag(RPGTags::State_Dead))
	{
		UE_LOG(LogRPG_Ability, Verbose, TEXT("[%s] CanActivateAbility 失败：角色处于死亡状态"), *GetName());
		return false;
	}

	return true;
}

// ══════════════════════════════════════════════════════════════════════
//  便捷查询
// ══════════════════════════════════════════════════════════════════════

ARPG_BaseCharacter* URPG_GameplayAbilityBase::GetRPGCharacter() const
{
	// AvatarActor 才是"能力通过什么身体表现"，玩家和敌人都是角色本身。
	// 不要用 OwnerActor —— 玩家的 Owner 是 PlayerState，不是角色。
	return Cast<ARPG_BaseCharacter>(GetAvatarActorFromActorInfo());
}

URPG_CombatComponent* URPG_GameplayAbilityBase::GetCombatComponent() const
{
	const ARPG_BaseCharacter* RPGChar = GetRPGCharacter();
	return RPGChar ? RPGChar->GetCombatComponent() : nullptr;
}

URPG_AttributeSet* URPG_GameplayAbilityBase::GetRPGAttributeSet() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	return ASC ? const_cast<URPG_AttributeSet*>(ASC->GetSet<URPG_AttributeSet>()) : nullptr;
}

URPG_AttackModuleData* URPG_GameplayAbilityBase::GetAttackModule() const
{
	const URPG_CombatComponent* Combat = GetCombatComponent();
	return Combat ? Combat->GetAttackModule() : nullptr;
}

// ══════════════════════════════════════════════════════════════════════
//  行为封装
// ══════════════════════════════════════════════════════════════════════

UAbilityTask_PlayMontageAndWait* URPG_GameplayAbilityBase::PlayMontageOrSkip(
	UAnimMontage* Montage,
	FName TaskName)
{
	if (!Montage)
	{
		// 没有蒙太奇不是错误 —— 这是"数值链路先行、动画后补"的正常状态。
		// 打 Verbose 而不是 Warning，避免开发期刷屏。
		UE_LOG(LogRPG_Ability, Verbose,
			TEXT("[%s] 未配置蒙太奇，跳过动画播放（数值逻辑照常执行）"), *GetName());

		return nullptr;
	}

	// UE 5.8 没有 PlayMontageAndWaitForEvent（那是 UE4 社区插件的类），
	// 所以"播动画"和"收 GameplayEvent"必须拆成两个 Task。
	// 这里只负责播，事件监听由调用方另外建 WaitGameplayEvent。
	UAbilityTask_PlayMontageAndWait* Task =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TaskName,
			Montage,
			/*Rate*/ 1.f,
			/*StartSection*/ NAME_None,
			/*bStopWhenAbilityEnds*/ true);

	return Task;
}

void URPG_GameplayAbilityBase::ApplyDamageToTarget(
	AActor* Target,
	float DamageMultiplier,
	const FHitResult* HitResult)
{
	if (!Target)
	{
		return;
	}

	if (!DamageEffectClass)
	{
		UE_LOG(LogRPG_Ability, Error,
			TEXT("[%s] 没有配置 DamageEffectClass，伤害无法施加"), *GetName());
		return;
	}

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);

	if (!SourceASC || !TargetASC)
	{
		UE_LOG(LogRPG_Ability, Verbose,
			TEXT("[%s] 伤害施加失败：源或目标的 ASC 为空"), *GetName());
		return;
	}

	// ── 构造 EffectContext ──
	// 把命中信息塞进去，GameplayCue 才能拿到命中点来播特效。
	// 不传的话，特效只能播在角色根位置（表现上会明显不对）。
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddSourceObject(this);

	if (HitResult)
	{
		Context.AddHitResult(*HitResult);
	}

	const FGameplayEffectSpecHandle SpecHandle =
		SourceASC->MakeOutgoingSpec(DamageEffectClass, GetAbilityLevel(), Context);

	if (!SpecHandle.IsValid())
	{
		UE_LOG(LogRPG_Ability, Error,
			TEXT("[%s] 伤害 GE 的 Spec 创建失败：%s"), *GetName(), *DamageEffectClass->GetName());
		return;
	}

	// ── 用 SetByCaller 传倍率 ──
	// 这是"一个 GE 服务所有攻击段"的关键：GE_Damage 里的 Modifier 不写死数值，
	// 而是读 Data.Damage.Multiplier，具体值由调用方在运行时填入。
	SpecHandle.Data->SetSetByCallerMagnitude(RPGTags::Data_Damage_Multiplier, DamageMultiplier);

	SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);

	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("[%s] 对 %s 施加伤害，倍率 %.2f"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(Target), DamageMultiplier);
}

void URPG_GameplayAbilityBase::ConsumeStamina(float Amount)
{
	if (Amount <= 0.f)
	{
		return;
	}

	if (!StaminaCostEffectClass)
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 没有配置 StaminaCostEffectClass，耐力不会被扣除"), *GetName());
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle =
		ASC->MakeOutgoingSpec(StaminaCostEffectClass, GetAbilityLevel(), Context);

	if (!SpecHandle.IsValid())
	{
		return;
	}

	// ⚠️ 必须传**负数**。
	// GE_StaminaCost 的 Modifier Op 是 Additive，SetByCaller 的值会被直接加到
	// Stamina 上。消耗是减少，所以取负 —— 传正数会变成"攻击反而回耐力"，
	// 而且属性集那边检测不到"耐力减少"，3 秒恢复阻断也不会触发。
	// 用 -FMath::Abs() 而不是 -Amount，是为了防止调用方不小心传了负数进来
	// 变成"负负得正"。
	SpecHandle.Data->SetSetByCallerMagnitude(RPGTags::Data_Stamina_Cost, -FMath::Abs(Amount));

	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("[%s] 消耗耐力 %.1f"), *GetNameSafe(GetAvatarActorFromActorInfo()), Amount);
}

bool URPG_GameplayAbilityBase::HasEnoughStamina(float Amount) const
{
	// ⚠️ 本方法依赖 CurrentActorInfo，因此**不能在 CanActivateAbility 里调用**
	// （那个时机 this 可能是 CDO，CurrentActorInfo 为空，会拿不到属性集）。
	// 目前它只被当作"激活后"的辅助判断使用。
	// 如果将来要在 CanActivateAbility 里做耐力前置检查，需要改成接收
	// 参数里的 ActorInfo 版本。
	const URPG_AttributeSet* Attributes = GetRPGAttributeSet();
	if (!Attributes)
	{
		// 拿不到属性集时**放行**而不是拦截。
		// 理由：拿不到属性集说明初始化有问题，那是另一个 bug；
		// 在这里拦下来会让"技能全放不出来"，反而掩盖了真正的原因。
		return true;
	}

	return Attributes->GetStamina() >= Amount;
}
