// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_Dodge.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_Dodge::URPG_GA_Dodge()
{
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Dodge));

	// ══════════════════════════════════════════════════════════════════
	//  挂上状态标签 State.Dodging
	// ══════════════════════════════════════════════════════════════════
	// 动画蓝图据此判断要不要播翻滚动作，AI 据此判断"这货正在躲，先别砍"。
	//
	// 用 ActivationOwnedTags 而不是自己 AddLooseGameplayTag 有一个关键好处：
	// 它的**生命周期由 GAS 托管** —— 能力结束、被打断、被取消，
	// 标签都会被自动摘掉。手写 Add/Remove 的话，一旦有某条路径忘了 Remove
	// （比如能力被 CancelAbilitiesWithTag 强行取消），
	// 角色就会永久停在"闪避中"，而且这种 bug 只在特定时序下出现。
	//
	// 复制行为：ActivationOwnedTags 走 EGameplayTagReplicationState::CountToOwner，
	// 意味着**标签本身会复制给所有客户端**（模拟代理也看得到），
	// 只有计数只发给拥有者。对 HasMatchingGameplayTag 这种布尔查询没有影响。
	// 前提是工程设置 GameplayAbilities → ReplicateActivationOwnedTags 保持默认的开。
	ActivationOwnedTags.AddTag(RPGTags::State_Dodging);
}

bool URPG_GA_Dodge::CanActivateAbility(
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

	// ⚠️ 用参数里的 ActorInfo 取属性集。
	// 不能走基类的 GetRPGAttributeSet() —— 那个依赖 CurrentActorInfo，
	// 而 CanActivateAbility 可能在 CDO 上执行，那时 CurrentActorInfo 是空的。
	const UAbilitySystemComponent* ASC =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;

	const URPG_AttributeSet* Attributes = ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;

	if (!Attributes)
	{
		// 拿不到属性集时放行 —— 那是初始化问题，不该由闪避来背这个锅。
		// 拦下来会让"闪避按不出来"掩盖真正的初始化 bug。
		return true;
	}

	if (Attributes->GetStamina() < StaminaCost)
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 闪避失败：耐力不足（%.1f / %.1f）"),
			*GetName(), Attributes->GetStamina(), StaminaCost);
		return false;
	}

	return true;
}

void URPG_GA_Dodge::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishDodge(true);
		return;
	}

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 闪避（耐力 %.1f，冲量 %.0f）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), StaminaCost, DodgeImpulse);

	// 1. 扣耐力
	ConsumeStamina(StaminaCost);

	// 2. 建立无敌帧事件监听（由蒙太奇上的 AnimNotifyState 广播）
	BindGameplayEventListeners();

	// 3. 施加位移冲量 —— 放在播蒙太奇之前，让身体和动画同时启动
	ApplyDodgeImpulse();

	// 4. 播蒙太奇
	UAbilityTask_PlayMontageAndWait* MontageTask =
		PlayMontageOrSkip(DodgeMontage, TEXT("DodgeMontage"));

	if (MontageTask)
	{
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_Dodge::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_Dodge::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
	else
	{
		// 没有蒙太奇 → 用定时器模拟"翻滚期间无敌、之后有破绽"的时序，
		// 这样无敌帧的逻辑在动画就绪前也能验证。
		UWorld* World = GetWorld();
		if (!World)
		{
			FinishDodge(false);
			return;
		}

		// 无敌帧只覆盖翻滚的前一段，之后留出破绽期 ——
		// 全程无敌的闪避会让玩家可以无脑翻滚规避一切，战斗失去张力。
		// （真实动画里这个边界由 AnimNotifyState 的位置决定，
		//   这里用比例模拟同样的时序，让无敌的开关逻辑能被验证。）
		ApplyInvulnerability();

		World->GetTimerManager().SetTimer(
			SimulatedInvulnerabilityTimer,
			this,
			&URPG_GA_Dodge::RemoveInvulnerability,
			SimulatedDodgeDuration * SimulatedInvulnerabilityRatio,
			false);

		World->GetTimerManager().SetTimer(
			SimulatedTimer,
			this,
			&URPG_GA_Dodge::OnSimulatedDodgeFinished,
			SimulatedDodgeDuration,
			false);
	}
}

void URPG_GA_Dodge::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 统一清理 —— 保证任何结束路径都不会残留无敌状态。
	// 如果无敌 GE 忘记移除，角色会变成永久无敌，而且很难查
	//（表现为"敌人打不动我"，而不是报错）。
	RemoveInvulnerability();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimulatedTimer);
		World->GetTimerManager().ClearTimer(SimulatedInvulnerabilityTimer);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ══════════════════════════════════════════════════════════════════════
//  事件监听
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_Dodge::BindGameplayEventListeners()
{
	// AddDynamic 要求函数名是字面量（宏内部会字符串化它），所以只能逐条写
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGTags::Event_Character_Invulnerability_Begin);
		Task->EventReceived.AddDynamic(this, &URPG_GA_Dodge::OnInvulnerabilityBegin);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGTags::Event_Character_Invulnerability_End);
		Task->EventReceived.AddDynamic(this, &URPG_GA_Dodge::OnInvulnerabilityEnd);
		Task->ReadyForActivation();
	}
}

void URPG_GA_Dodge::OnInvulnerabilityBegin(FGameplayEventData Payload)
{
	ApplyInvulnerability();
}

void URPG_GA_Dodge::OnInvulnerabilityEnd(FGameplayEventData Payload)
{
	RemoveInvulnerability();
}

// ══════════════════════════════════════════════════════════════════════
//  位移
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_Dodge::ApplyDodgeImpulse()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	// 沿角色**当前朝向**施加冲量。
	//
	// bXYOverride = true  —— 覆盖水平速度。不覆盖的话，角色原本的移动速度
	//                        会和冲量叠加，出现"跑动中闪避能翻特别远"的问题。
	// bZOverride = false  —— **不**覆盖垂直速度。保留垂直分量，这样
	//                        从高处跳下时闪避不会让角色悬停在空中。
	const FVector Impulse = Character->GetActorForwardVector() * DodgeImpulse;

	Character->LaunchCharacter(Impulse, /*bXYOverride*/ true, /*bZOverride*/ false);
}

// ══════════════════════════════════════════════════════════════════════
//  无敌帧
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_Dodge::ApplyInvulnerability()
{
	if (InvulnerabilityHandle.IsValid())
	{
		return;   // 已经在无敌中，避免重复施加
	}

	if (!InvulnerabilityEffectClass)
	{
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 没有配置 InvulnerabilityEffectClass —— "
			     "闪避不会提供无敌帧（需要在 GA 蓝图里指定 GE_Invulnerable）"), *GetName());
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
		ASC->MakeOutgoingSpec(InvulnerabilityEffectClass, GetAbilityLevel(), Context);

	if (!SpecHandle.IsValid())
	{
		return;
	}

	// 保存句柄 —— 结束时靠它精确移除这一个 GE。
	// 用 RemoveActiveGameplayEffect(Handle) 而不是"移除所有该类 GE"，
	// 是因为将来可能有别的来源也给无敌（比如某个技能），
	// 按句柄移除不会误伤别人给的。
	InvulnerabilityHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 无敌帧开始"),
		*GetNameSafe(GetAvatarActorFromActorInfo()));
}

void URPG_GA_Dodge::RemoveInvulnerability()
{
	if (!InvulnerabilityHandle.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveActiveGameplayEffect(InvulnerabilityHandle);
	}

	InvulnerabilityHandle.Invalidate();

	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 无敌帧结束"),
		*GetNameSafe(GetAvatarActorFromActorInfo()));
}

// ══════════════════════════════════════════════════════════════════════
//  收尾
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_Dodge::FinishDodge(bool bWasCancelled)
{
	RemoveInvulnerability();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimulatedTimer);
		World->GetTimerManager().ClearTimer(SimulatedInvulnerabilityTimer);
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, bWasCancelled);
}

void URPG_GA_Dodge::OnSimulatedDodgeFinished()
{
	// 模拟模式：翻滚结束时自动收尾（没有动画事件来触发）
	FinishDodge(false);
}

void URPG_GA_Dodge::OnMontageCompleted()
{
	FinishDodge(false);
}

void URPG_GA_Dodge::OnMontageInterrupted()
{
	// 被打断时也要走到 FinishDodge → EndAbility → RemoveInvulnerability，
	// 否则会残留无敌状态。
	FinishDodge(true);
}
