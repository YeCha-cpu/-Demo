// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_HeavyAttack.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "AbilitySystem/Tasks/RPG_AbilityTask_WeaponTrace.h"
#include "Combat/RPG_AttackModuleData.h"
#include "Combat/RPG_AttackWindowPayload.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_HeavyAttack::URPG_GA_HeavyAttack()
{
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Attack_Heavy));

	// ══════════════════════════════════════════════════════════════════
	//  激活期间挂上自己的标签
	// ══════════════════════════════════════════════════════════════════
	// 供其他系统查询"当前是不是在重击"（比如动画、AI 决策）。
	ActivationOwnedTags.AddTag(RPGTags::Ability_Attack_Heavy);

	// ══════════════════════════════════════════════════════════════════
	//  激活时取消轻击
	// ══════════════════════════════════════════════════════════════════
	// 这是切手技的机制基础：轻击连段还在跑时玩家按右键，
	// GAS 会在本能力激活的瞬间自动取消带 Ability.Attack.Light 标签的能力。
	//
	// 用声明式的 CancelAbilitiesWithTag 而不是手写"找到轻击 GA 然后取消它"，
	// 好处是不需要知道轻击 GA 的具体类型 —— 将来加"轻击的变体"只要
	// 也带上那个标签，就会被自动取消。
	CancelAbilitiesWithTag.AddTag(RPGTags::Ability_Attack_Light);
}

// ══════════════════════════════════════════════════════════════════════
//  激活
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_HeavyAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishHeavyAttack(true);
		return;
	}

	URPG_CombatComponent* Combat = GetCombatComponent();
	URPG_AttackModuleData* Module = GetAttackModule();

	if (!Combat || !Module)
	{
		UE_LOG(LogRPG_Ability, Warning, TEXT("[%s] 无法执行重击：%s"),
			*GetName(),
			!Combat ? TEXT("角色上没有 CombatComponent") : TEXT("没有配置攻击模组"));

		FinishHeavyAttack(true);
		return;
	}

	// 消耗掉触发本次激活的输入（理由同轻击 GA）
	FGameplayTag TriggerInputTag;
	Combat->ConsumeInputTag(TriggerInputTag);

	// ══════════════════════════════════════════════════════════════════
	//  分支判断
	// ══════════════════════════════════════════════════════════════════
	// 查"轻击能力是否正在激活中"。
	//
	// 用 GameplayTag 而不是 CombatComponent 里的连段索引，是因为索引在
	// 起手那一瞬间是 0（表示"正在打第 1 段"），无法区分"不在连段中"
	// 和"正在打第 1 段"。标签则精确表达"某个能力此刻正在激活"。
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	bTransitionBranch = ASC && ASC->HasMatchingGameplayTag(RPGTags::Ability_Attack_Light);

	bReleasing = false;
	CurrentChargeLevel = 0;
	ChargeElapsed = 0.f;
	CurrentDamageMultiplier = 1.f;

	BindGameplayEventListeners();

	if (bTransitionBranch)
	{
		StartTransition();
	}
	else
	{
		StartCharge();
	}
}

void URPG_GA_HeavyAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeTimer);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ══════════════════════════════════════════════════════════════════════
//  切手技分支
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_HeavyAttack::StartTransition()
{
	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module)
	{
		FinishHeavyAttack(true);
		return;
	}

	CurrentDamageMultiplier = Module->ComboTransitionMultiplier;
	ConsumeStamina(Module->ComboTransitionStaminaCost);

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 切手技（倍率 %.2f，耐力 %.1f）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		Module->ComboTransitionMultiplier, Module->ComboTransitionStaminaCost);

	if (UAbilityTask_PlayMontageAndWait* MontageTask =
			PlayMontageOrSkip(Module->ComboTransitionMontage, TEXT("ComboTransition")))
	{
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
	else
	{
		// 没有蒙太奇 → 立刻结算一次伤害然后收招
		PerformSimulatedMeleeHit(CurrentDamageMultiplier, /*ForwardOffset*/ 180.f, /*Radius*/ 90.f);
		FinishHeavyAttack(false);
	}
}

// ══════════════════════════════════════════════════════════════════════
//  蓄力分支
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_HeavyAttack::StartCharge()
{
	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module)
	{
		FinishHeavyAttack(true);
		return;
	}

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 开始蓄力（最长 %.1f 秒，每秒耗耐力 %.1f）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		MaxChargeTime, Module->HeavyAttack.ChargeStaminaDrainPerSecond);

	// 播起手蒙太奇。
	// 注意这里**不绑 OnCompleted** —— 起手动画播完不代表蓄力结束，
	// 玩家可能还按着不放。只有 OnInterrupted 需要处理（被打断则结束能力）。
	if (UAbilityTask_PlayMontageAndWait* MontageTask =
			PlayMontageOrSkip(Module->HeavyAttack.ChargeStartMontage, TEXT("ChargeStart")))
	{
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
	// 没有起手蒙太奇也照常蓄力 —— 蓄力逻辑本身不依赖动画

	// 启动蓄力计时。用循环定时器而不是 Tick：
	// 蓄力只需要按固定间隔检查"有没有升段"，不需要每帧精度。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ChargeTimer,
			this,
			&URPG_GA_HeavyAttack::TickCharge,
			ChargeTickInterval,
			/*bLoop*/ true);
	}
}

void URPG_GA_HeavyAttack::TickCharge()
{
	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module)
	{
		ReleaseCharge();
		return;
	}

	ChargeElapsed += ChargeTickInterval;

	// ── 持续消耗耐力 ──
	// 这是蓄力的代价：贪满蓄力会让耐力见底，之后连闪避都放不出来。
	ConsumeStamina(Module->HeavyAttack.ChargeStaminaDrainPerSecond * ChargeTickInterval);

	// ── 检查升段 ──
	const int32 NewLevel = Module->GetChargeLevelForTime(ChargeElapsed);
	if (NewLevel != CurrentChargeLevel)
	{
		CurrentChargeLevel = NewLevel;

		UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 蓄力升到 %d 段（%.2f 秒）"),
			*GetNameSafe(GetAvatarActorFromActorInfo()), CurrentChargeLevel, ChargeElapsed);
	}

	// ── 耐力耗尽 → 强制释放 ──
	// 不强制释放的话，玩家可以在耐力耗尽后一直保持蓄力姿势，
	// 既不能再攻击也不受惩罚，等于白嫖一个无敌的僵持状态。
	if (!HasEnoughStamina(1.f))
	{
		UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 耐力耗尽，强制释放蓄力"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));

		ReleaseCharge();
		return;
	}

	// ── 到达蓄力上限 → 自动释放 ──
	if (ChargeElapsed >= MaxChargeTime)
	{
		UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 蓄力到达上限，自动释放"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));

		ReleaseCharge();
	}
}

void URPG_GA_HeavyAttack::ReleaseCharge()
{
	// 先停掉计时，避免释放过程中 TickCharge 又被调用一次造成重复释放
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeTimer);
	}

	bReleasing = true;

	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module)
	{
		FinishHeavyAttack(false);
		return;
	}

	// ── 取当前段位的配置 ──
	const FRPG_HeavyAttackLevel* Level = Module->GetHeavyLevel(CurrentChargeLevel);

	if (!Level)
	{
		// 蓄力时间还不够最低门槛 —— 按第 1 段释放。
		// 不取消能力是因为"点一下右键"的意图很明确，取消会让玩家觉得按键失灵。
		Level = Module->GetHeavyLevel(1);
		CurrentChargeLevel = Level ? 1 : 0;
	}

	if (!Level)
	{
		// 连最低段位都没配置 —— 这是数据配置问题
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 攻击模组的 HeavyAttack.Levels 为空，无法释放蓄力攻击"), *GetName());

		FinishHeavyAttack(true);
		return;
	}

	CurrentDamageMultiplier = Level->DamageMultiplier;
	ConsumeStamina(Level->StaminaCost);

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 释放蓄力重击（%d 段，倍率 %.2f，耐力 %.1f）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		CurrentChargeLevel, Level->DamageMultiplier, Level->StaminaCost);

	if (UAbilityTask_PlayMontageAndWait* MontageTask =
			PlayMontageOrSkip(Level->ReleaseMontage, TEXT("HeavyRelease")))
	{
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
	else
	{
		// 没有释放蒙太奇 → 立刻结算伤害。
		// 重击范围比轻击大一些，所以偏移和半径都调大了。
		PerformSimulatedMeleeHit(CurrentDamageMultiplier, /*ForwardOffset*/ 180.f, /*Radius*/ 100.f);
		FinishHeavyAttack(false);
	}
}

void URPG_GA_HeavyAttack::OnInputReleased()
{
	// 切手技是瞬发招式，不响应松手
	if (bTransitionBranch)
	{
		return;
	}

	// 已经在释放中就不要再触发一次
	if (bReleasing)
	{
		return;
	}

	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 松手，按 %.2f 秒的蓄力释放"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), ChargeElapsed);

	ReleaseCharge();
}

// ══════════════════════════════════════════════════════════════════════
//  事件监听
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_HeavyAttack::BindGameplayEventListeners()
{
	// AddDynamic 要求函数名字面量，只能逐条写
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGTags::Event_Combat_AttackWindow_Open);
		Task->EventReceived.AddDynamic(this, &URPG_GA_HeavyAttack::OnAttackWindowOpen);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGTags::Event_Combat_AttackWindow_Close);
		Task->EventReceived.AddDynamic(this, &URPG_GA_HeavyAttack::OnAttackWindowClose);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGTags::Event_Combat_AttackEnd);
		Task->EventReceived.AddDynamic(this, &URPG_GA_HeavyAttack::OnAttackEndEvent);
		Task->ReadyForActivation();
	}
}

void URPG_GA_HeavyAttack::OnAttackWindowOpen(FGameplayEventData Payload)
{
	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module)
	{
		return;
	}

	ERPG_TraceSource Source = Module->TraceSource;
	float Radius = Module->TraceRadius;
	FName SocketStart = Module->LeftHandSocket;
	FName SocketEnd = Module->RightHandSocket;

	if (const URPG_AttackWindowPayload* WindowPayload =
			Cast<URPG_AttackWindowPayload>(Payload.OptionalObject.Get()))
	{
		if (WindowPayload->bOverrideTrace)
		{
			Source = WindowPayload->TraceSource;
			Radius = WindowPayload->TraceRadius;
			SocketStart = WindowPayload->SocketStart;
			SocketEnd = WindowPayload->SocketEnd;
		}
	}

	if (Source == ERPG_TraceSource::WeaponBlade)
	{
		SocketStart = Module->BladeStartSocket;
		SocketEnd = Module->BladeEndSocket;
	}

	if (Source == ERPG_TraceSource::Projectile)
	{
		// 远程模组的发射物逻辑属于阶段 6
		return;
	}

	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}

	TraceTask = URPG_AbilityTask_WeaponTrace::CreateWeaponTraceTask(
		this, Source, Radius, SocketStart, SocketEnd);

	if (TraceTask)
	{
		TraceTask->OnHit.AddDynamic(this, &URPG_GA_HeavyAttack::OnWeaponTraceHit);
		TraceTask->ReadyForActivation();
	}
}

void URPG_GA_HeavyAttack::OnAttackWindowClose(FGameplayEventData Payload)
{
	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}
}

void URPG_GA_HeavyAttack::OnAttackEndEvent(FGameplayEventData Payload)
{
	FinishHeavyAttack(false);
}

void URPG_GA_HeavyAttack::OnMontageCompleted()
{
	FinishHeavyAttack(false);
}

void URPG_GA_HeavyAttack::OnMontageInterrupted()
{
	// ⚠️ 这里必须排除"释放时打断蓄力起手动画"这种正常情况。
	// 释放阶段会播新的蒙太奇，那必然打断仍在播放的起手蒙太奇 ——
	// 如果没有 bReleasing 这个判断，会把正常的蓄力→释放误判成"被打断"，
	// 于是能力在刚放出招的瞬间就被结束了。
	if (bReleasing)
	{
		return;
	}

	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 蓄力被打断"), *GetName());

	FinishHeavyAttack(true);
}

void URPG_GA_HeavyAttack::OnWeaponTraceHit(const TArray<FHitResult>& Hits)
{
	for (const FHitResult& Hit : Hits)
	{
		ApplyDamageToTarget(Hit.GetActor(), CurrentDamageMultiplier, &Hit);
	}
}

// ══════════════════════════════════════════════════════════════════════
//  收尾
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_HeavyAttack::FinishHeavyAttack(bool bWasCancelled)
{
	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeTimer);
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, bWasCancelled);
}
