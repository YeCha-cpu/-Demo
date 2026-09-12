// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_Sprint.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_Sprint::URPG_GA_Sprint()
{
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Sprint));

	// 标记"正在奔跑"，供动画和 AI 查询
	ActivationOwnedTags.AddTag(RPGTags::State_Sprinting);
}

void URPG_GA_Sprint::ActivateAbility(
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

	// 耐力已经见底时不进入奔跑 —— 直接失败比"跑一帧就停"体验更好
	const URPG_AttributeSet* Attributes = GetRPGAttributeSet();
	if (Attributes && Attributes->GetStamina() <= 0.f)
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 耐力不足，无法奔跑"), *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 切换移动速度
	if (ARPG_BaseCharacter* Character = GetRPGCharacter())
	{
		Character->StartSprint();
	}

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 开始奔跑（每秒耗耐力 %.1f）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), StaminaDrainPerSecond);

	World->GetTimerManager().SetTimer(
		DrainTimer, this, &URPG_GA_Sprint::TickStaminaDrain, DrainTickInterval, /*bLoop*/ true);
}

void URPG_GA_Sprint::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 统一清理 —— 任何结束路径（松手、耐力耗尽、被技能打断、角色死亡）
	// 都必须把移动速度恢复回去，否则角色会一直保持冲刺速度。
	//
	// 这个坑很隐蔽：GA 被外部取消时不会走 FinishSprint，
	// 如果只在 FinishSprint 里恢复速度，就会出现"被打断后永久加速"。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DrainTimer);
	}

	if (ARPG_BaseCharacter* Character = GetRPGCharacter())
	{
		Character->StopSprint();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPG_GA_Sprint::TickStaminaDrain()
{
	ConsumeStamina(StaminaDrainPerSecond * DrainTickInterval);

	const URPG_AttributeSet* Attributes = GetRPGAttributeSet();
	if (Attributes && Attributes->GetStamina() <= 0.f)
	{
		UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 耐力耗尽，停止奔跑"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));

		FinishSprint(false);
	}
}

void URPG_GA_Sprint::OnInputReleased()
{
	FinishSprint(false);
}

void URPG_GA_Sprint::FinishSprint(bool bWasCancelled)
{
	// 实际的清理在 EndAbility 里统一做，这里只是显式表达"主动收尾"的意图
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, bWasCancelled);
}
