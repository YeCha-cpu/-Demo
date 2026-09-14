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

	// ── 事件触发：场景里的效果触发器会发这个事件 ──
	//
	// 来源是 `ARPG_EffectVolume`（药水 / 治疗泉 / 拾取物），见 World/RPG_EffectVolume.h。
	// 触发器**不直接施加 GE**，而是发 GameplayEvent 让这里去施加 ——
	// 这样消耗、动画、打断、能力层标签才都有地方放。
	//
	// ⚠️ 触发源是 `GameplayEvent`，所以发送方必须走
	// `UAbilitySystemBlueprintLibrary::SendGameplayEventToActor`。
	// 直接 `ApplyGameplayEffectToTarget` 是走不到这里的（那也绕开了 GA 层）。
	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = RPGTags::Event_Item_Heal;
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);
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

	// ── 参数解析：事件载荷优先，能力自己的配置兜底 ★ ──
	//
	// 效果触发器（药水 / 治疗泉）可以在载荷里带上"用哪个 GE、治多少"，
	// 于是**同一个 GA 就能服务所有强度的治疗**，不需要为每个强度做一个 GA 蓝图。
	//
	// 手动激活（没有载荷）时两个都回落到本能力配的 HealEffectClass / HealAmount，
	// 所以老用法完全不受影响。
	const TSubclassOf<UGameplayEffect> EffectToApply =
		ResolveEffectClassFromEvent(TriggerEventData, HealEffectClass);
	const float AmountToHeal = ResolveMagnitudeFromEvent(TriggerEventData, HealAmount);

	if (!ASC || !EffectToApply)
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 无法治疗：%s"),
			*GetName(),
			!ASC ? TEXT("拿不到 ASC") : TEXT("没有配置 HealEffectClass（事件载荷里也没有）"));

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle =
		ASC->MakeOutgoingSpec(EffectToApply, GetAbilityLevel(), Context);

	if (!SpecHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 通过 SetByCaller 传治疗量 —— 同一个 GE_Heal 服务所有强度的治疗
	SpecHandle.Data->SetSetByCallerMagnitude(RPGTags::Data_Heal_Amount, AmountToHeal);

	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 治疗 %.1f（GE=%s）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), AmountToHeal, *GetNameSafe(EffectToApply));

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
}
