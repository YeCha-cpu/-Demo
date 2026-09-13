// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_HitReact.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTriggerType.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_HitReact::URPG_GA_HitReact()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// ── 只在服务器执行 ──
	// 触发的源头（属性集的伤害结算）就在服务器，客户端收不到事件；
	// 就算收到了也不该执行 —— 见头文件里的说明。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// ══════════════════════════════════════════════════════════════════
	//  ★ 允许重入：连挨两下应该刷新硬直，而不是第二下没反应
	// ══════════════════════════════════════════════════════════════════
	// 默认值 false 时，能力激活期间再来一次激活请求会被直接拒绝
	// （AbilitySystemComponent_Abilities.cpp:1848 "Can't activate instanced
	// per actor ability ... already a currently active instance"）。
	//
	// 表现上就是：被打第一下会踉跄，紧接着被打第二下**完全没反应**。
	// 敌人连击的时候这个现象特别明显，而且不报错。
	//
	// 打开之后引擎会先 EndAbility(bWasCancelled=false) 旧实例再重新激活
	// （同文件 :1836-1844），所以 EndAbility 里必须把旧任务清理干净。
	bRetriggerInstancedAbility = true;

	// ── 打断正在进行的攻击和闪避 ──
	// 用 Ability.Attack 父标签一次覆盖轻击和重击，加新攻击类型不用改这里。
	// 闪避虽然大部分时间无敌（那时压根不会走到受击），但无敌帧结束后
	// 的后摇阶段是能被抓的，那时候应该被打断。
	CancelAbilitiesWithTag.AddTag(RPGTags::Ability_Attack);
	CancelAbilitiesWithTag.AddTag(RPGTags::Ability_Dodge);

	// ── 状态标签 ──
	// State.Hit 在能力激活期间由 GAS 自动挂上/摘掉。
	// 动画蓝图可以据此整体切到"受击"姿态，AI 也可以用它判断目标在挨打。
	ActivationOwnedTags.AddTag(RPGTags::State_Hit);

	// ══════════════════════════════════════════════════════════════════
	//  ★ 声明式触发 —— 少了这一段的后果是"整个功能是死代码"
	// ══════════════════════════════════════════════════════════════════
	// 事件触发的 GA 不是在每次收到事件时去遍历所有能力找"谁想接"，
	// 而是在**授予能力时**就把能接事件的能力登记进一张表：
	//     OnGiveAbility → RegisterAbilityTriggers(Spec, AbilityTriggers)
	//     （AbilitySystemComponent_Abilities.cpp:578）
	// 之后 HandleGameplayEvent 只遍历这张表（同文件 :2571）。
	//
	// 也就是说：**没有 AbilityTriggers 的能力，事件系统根本看不见它** ——
	// 它会被正常授予、正常出现在 ActivatableAbilities 里、日志一切正常，
	// 但永远等不到触发。不报错，只是"挨打了没反应"。
	//
	// ⚠️ 这段和下面那段注释是配套的：构造函数里没有它，
	// 前面所有关于"事件驱动"的说明就都只是文档而不是事实。
	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = RPGTags::Event_Combat_Hit;
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);
}

void URPG_GA_HitReact::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	// 旧任务必须先摘干净再建新任务，否则它的回调会冒充这一次的结果 ——
	// 详见 DetachMontageTask 的说明。
	DetachMontageTask();

	ARPG_BaseCharacter* Character = GetRPGCharacter();
	UAnimMontage* Montage = Character ? Character->PickHitReactMontage() : nullptr;

	if (Montage)
	{
		// 播放速率从角色上读 —— 同一个 GA 挂在玩家和敌人身上时，
		// "这个角色挨打是什么节奏"是角色的属性，不是能力的属性。
		const float Rate = Character->GetHitReactPlayRate();

		MontageTask = PlayMontageOrSkip(Montage, TEXT("HitReactMontage"), Rate);
		if (MontageTask)
		{
			MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_HitReact::OnMontageCompleted);
			MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_HitReact::OnMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &URPG_GA_HitReact::OnMontageInterrupted);
			MontageTask->ReadyForActivation();

			UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 受击反应：播放 %s（%.2fx）"),
				*GetName(), *Montage->GetName(), Rate);
			return;
		}
	}

	// 没配蒙太奇 → 用定时器模拟一段硬直。
	// 这样"挨打会被打断、会有一小段不能动"这条逻辑不会因为美术没做动画而失效。
	StartSimulatedHitReact();
}

void URPG_GA_HitReact::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// ⚠️ 顺序：先清任务再调 Super。
	// Super::EndAbility 会摘掉 ActivationOwnedTags 并广播结束事件，
	// 那之后我们还去动任务是安全的，但保持"自己申请的资源自己先释放"
	// 这个顺序，读起来更不容易出错。
	DetachMontageTask();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimulatedTimer);
	}

	// 最后一段动画故意留着播完 —— 受击被打断（比如又挨了一下）时，
	// 新的一次激活会立刻播新蒙太奇把它顶掉，不需要在这里停。
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPG_GA_HitReact::DetachMontageTask()
{
	if (!MontageTask)
	{
		return;
	}

	// ★ 先 RemoveDynamic，再 EndTask —— 两件事缺一不可。
	// 只 EndTask 拦不住回调（见头文件里的引用）。
	MontageTask->OnCompleted.RemoveDynamic(this, &URPG_GA_HitReact::OnMontageCompleted);
	MontageTask->OnInterrupted.RemoveDynamic(this, &URPG_GA_HitReact::OnMontageInterrupted);
	MontageTask->OnCancelled.RemoveDynamic(this, &URPG_GA_HitReact::OnMontageInterrupted);

	MontageTask->EndTask();
	MontageTask = nullptr;
}

void URPG_GA_HitReact::OnMontageCompleted()
{
	FinishHitReact(/*bWasCancelled=*/false);
}

void URPG_GA_HitReact::OnMontageInterrupted()
{
	FinishHitReact(/*bWasCancelled=*/true);
}

void URPG_GA_HitReact::FinishHitReact(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility=*/true, bWasCancelled);
}

void URPG_GA_HitReact::StartSimulatedHitReact()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		FinishHitReact(false);
		return;
	}

	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("[%s] 角色没有配置受击蒙太奇，改用 %.2f 秒的模拟硬直"),
		*GetName(), SimulatedHitDuration);

	World->GetTimerManager().SetTimer(
		SimulatedTimer, this, &URPG_GA_HitReact::OnSimulatedHitReactFinished, SimulatedHitDuration, false);
}

void URPG_GA_HitReact::OnSimulatedHitReactFinished()
{
	FinishHitReact(/*bWasCancelled=*/false);
}
