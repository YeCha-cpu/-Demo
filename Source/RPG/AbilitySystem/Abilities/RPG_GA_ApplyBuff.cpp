// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_ApplyBuff.h"

#include "AbilitySystemComponent.h"

#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_ApplyBuff::URPG_GA_ApplyBuff()
{
	// ⚠️ 用**通用**标签，不要按效果种类各建一个。
	//
	// 原来这里是硬编码的 `Ability.Buff.AttackUp` —— 那样一个减防的 GE 会顶着
	// "AttackUp" 的标签，日志、调试面板、`CancelAbilitiesByTag` 全是误导。
	//
	// 区分具体是哪种效果靠 **GE 的 GrantedTags**（`State.Buff.AttackUp` /
	// `State.Debuff.DefenseDown` 之类），那是给动画 / UI / 驱散逻辑查的。
	// 能力标签只表达"这个 GA 是干什么的"，给 GAS 机制用。
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_ApplyEffect));

	// 施加效果是状态变更，由服务器决定、复制给客户端。
	// LocalPredicted 会让两端各挂一次 GE，叠加层数翻倍。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// ── 事件触发：场景里的效果触发器会发这两个事件 ──
	//
	// 增益和减益**共用这一个能力** —— 区别只在施加的 GE 是正的还是负的。
	// 那两个事件都接，是因为触发器可以选择发 `Event.Item.Buff`（卷轴、图腾）
	// 或者 `Event.Item.Debuff`（毒瓶、陷阱），而它们该由同一个通用施加者处理。
	//
	// ⚠️ 这也意味着：这个能力在角色身上**只能授予一次**。
	// 授予两份的话，一次事件会把两份都激活 —— 效果施加两次。
	// 需要多个不同效果的 Buff，靠的是"不同 GE + 不同拾取物"，
	// 不是"不同 GA 蓝图"。
	{
		FAbilityTriggerData BuffTrigger;
		BuffTrigger.TriggerTag = RPGTags::Event_Item_Buff;
		BuffTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(BuffTrigger);

		FAbilityTriggerData DebuffTrigger;
		DebuffTrigger.TriggerTag = RPGTags::Event_Item_Debuff;
		DebuffTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(DebuffTrigger);
	}
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

	// ── 参数解析：事件载荷优先，能力自己的配置兜底 ★ ──
	//
	// 效果触发器（卷轴 / 毒瓶 / 陷阱）在载荷里带上"用哪个 GE"，
	// 于是**新增一种 Buff 不需要改 C++，也不需要新建 GA 蓝图** ——
	// 做一个新 GE 资产，在拾取物上指过去就行了。
	//
	// 手动激活（没有载荷）时回落到本能力配的 BuffEffectClass，老用法不受影响。
	const TSubclassOf<UGameplayEffect> EffectToApply =
		ResolveEffectClassFromEvent(TriggerEventData, BuffEffectClass);

	if (!ASC || !EffectToApply)
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 无法施加效果：%s"),
			*GetName(),
			!ASC ? TEXT("拿不到 ASC") : TEXT("没有配置 BuffEffectClass（事件载荷里也没有）"));

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

	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 施加效果：%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), *EffectToApply->GetName());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
}
