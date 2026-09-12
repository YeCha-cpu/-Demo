// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_Heal.h"

#include "AbilitySystemComponent.h"

#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_Heal::URPG_GA_Heal()
{
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Heal));

	// 治疗是数值逻辑，服务器算一次复制给客户端即可。
	// 用 LocalPredicted 的话两端各算一次，治疗量会翻倍。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void URPG_GA_Heal::ActivateAbility(
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

	if (!ASC || !HealEffectClass)
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 无法治疗：%s"),
			*GetName(),
			!ASC ? TEXT("拿不到 ASC") : TEXT("没有配置 HealEffectClass"));

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle =
		ASC->MakeOutgoingSpec(HealEffectClass, GetAbilityLevel(), Context);

	if (!SpecHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 通过 SetByCaller 传治疗量 —— 同一个 GE_Heal 服务所有强度的治疗
	SpecHandle.Data->SetSetByCallerMagnitude(RPGTags::Data_Heal_Amount, HealAmount);

	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 治疗 %.1f"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), HealAmount);

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
}
