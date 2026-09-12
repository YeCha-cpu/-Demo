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
		return false;
	}

	// ── 死亡检查 ──
	// 虽然上面已经用 ActivationBlockedTags 加了 State.Dead，这里再判一次是因为
	// 标签阻断依赖 GE 正确授予标签 —— 如果将来有别的途径让角色"死亡但没标签"
	// （比如脚本直接扣血到 0 而没走属性集），这道检查能兜住。
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ASC->HasMatchingGameplayTag(RPGTags::State_Dead))
	{
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
