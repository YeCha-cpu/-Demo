// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_StaminaRegen.h"

#include "AbilitySystemComponent.h"

#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_StaminaRegen::URPG_GA_StaminaRegen()
{
	// 授予后自动激活（由 URPG_AbilitySystemComponent::RegisterInputAbility 处理）
	bActivateOnGranted = true;

	// 恢复是纯数值逻辑，客户端不需要自己算 —— 属性复制会把结果同步过去。
	// 用 ServerOnly 而不是 LocalPredicted 还有一个好处：
	// 避免客户端和服务器各自跑一份周期 GE 导致恢复速度翻倍。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// 只允许服务器请求激活，客户端无权干预
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;

	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_StaminaRegen));

	// 标记为被动能力 —— 其他系统（比如"死亡时清空所有能力"）可以据此识别
	ActivationOwnedTags.AddTag(RPGTags::Ability_StaminaRegen);
}

void URPG_GA_StaminaRegen::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	if (!ASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ true);
		return;
	}

	if (!RegenEffectClass)
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 没有配置 RegenEffectClass —— 耐力不会自动恢复。"
			     "请在 GA_StaminaRegen 蓝图里指定 GE_StaminaRegen"), *GetName());

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle =
		ASC->MakeOutgoingSpec(RegenEffectClass, GetAbilityLevel(), Context);

	if (!SpecHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 常驻挂上。
	// 注意：这里**不做任何"现在该不该恢复"的判断** ——
	// 那完全由 GE 自己的 OngoingTagRequirements 决定。
	// 我们只负责把它挂上去，剩下的交给 GAS。
	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	UE_LOG(LogRPG_Ability, Log,
		TEXT("[%s] 耐力恢复已挂载（受 State.Stamina.Blocked 标签阻断）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()));

	// ⚠️ 刻意**不调用 EndAbility**：这个能力要保持激活状态，
	// 它施加的 Infinite GE 才会一直挂在角色身上。
	//
	// 什么时候结束？角色死亡时由 State.Dead 标签阻断，
	// 或者角色销毁时随 ASC 一起清理。
}
