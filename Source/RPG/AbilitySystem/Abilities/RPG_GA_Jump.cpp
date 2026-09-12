// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_Jump.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_Jump::URPG_GA_Jump()
{
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Jump));
}

bool URPG_GA_Jump::CanActivateAbility(
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

	// ⚠️ 一律走参数里的 ActorInfo
	const UAbilitySystemComponent* ASC =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;

	// ── 耐力检查 ──
	const URPG_AttributeSet* Attributes = ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;

	if (Attributes && Attributes->GetStamina() < JumpStaminaCost)
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 跳跃失败：耐力不足（%.1f / %.1f）"),
			*GetName(), Attributes->GetStamina(), JumpStaminaCost);

		return false;
	}

	// ── 能不能跳 ──
	// ACharacter::CanJump() 会检查"是否在地面上、能否起跳"。
	// 不检查的话，玩家在空中按跳跃会白白消耗耐力。
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (const ACharacter* Character = Cast<ACharacter>(Avatar))
	{
		if (!Character->CanJump())
		{
			return false;
		}
	}

	return true;
}

void URPG_GA_Jump::ActivateAbility(
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

	ConsumeStamina(JumpStaminaCost);

	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		// ACharacter::Jump() 内部会再做一次 CanJump 检查，
		// 还会处理"蹲伏中跳跃自动起立"这类细节，不需要我们重复实现。
		Character->Jump();
	}

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 跳跃（耐力 %.1f，最长按住 %.1f 秒）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), JumpStaminaCost, MaxJumpHoldTime);

	// ⚠️ 刻意**不立即结束**能力 —— 要等玩家松手来实现可变高度跳跃。
	// 用超时兜底，防止一直不松手导致能力永久激活。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			MaxJumpHoldTimer, this, &URPG_GA_Jump::FinishJump, MaxJumpHoldTime, false);
	}
}

void URPG_GA_Jump::OnInputReleased()
{
	// 松手 → 停止上升。
	// 这是平台跳跃手感的基础：短按跳得低、长按跳得高。
	// 对 ARPG 里"跳跃接闪避/接攻击"的衔接也有帮助 ——
	// 玩家可以通过短按快速落地来抢时间。
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->StopJumping();
	}

	FinishJump();
}

void URPG_GA_Jump::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MaxJumpHoldTimer);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPG_GA_Jump::FinishJump()
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
}
