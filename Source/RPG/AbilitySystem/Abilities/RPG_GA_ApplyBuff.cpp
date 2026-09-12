// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_ApplyBuff.h"

#include "AbilitySystemComponent.h"

#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_ApplyBuff::URPG_GA_ApplyBuff()
{
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Buff_AttackUp));

	// Buff 的施加是状态变更，由服务器决定、复制给客户端。
	// LocalPredicted 会让两端各挂一次 GE，叠加层数翻倍。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void URPG_GA_ApplyBuff::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	if (!ASC || !BuffEffectClass)
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 无法施加增益：%s"),
			*GetName(),
			!ASC ? TEXT("拿不到 ASC") : TEXT("没有配置 BuffEffectClass"));

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle =
		ASC->MakeOutgoingSpec(BuffEffectClass, GetAbilityLevel(), Context);

	if (!SpecHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 施加增益：%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), *BuffEffectClass->GetName());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
}
