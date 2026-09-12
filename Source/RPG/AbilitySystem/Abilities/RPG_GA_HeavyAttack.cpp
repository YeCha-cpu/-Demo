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

	// 状态标签 —— 动画蓝图、UI、AI 读的是这个，而不是上面那个"能力身份"标签。
	// 轻击连段和重击（蓄力+释放）挂的是同一个 State.Attacking，
	// 动画侧因此只需要判断一次"在不在攻击"，不用管是哪一套连招。
	//
	// 注意蓄力标签（State.Attack.Charging.*）**不在这里**：
	// 它只覆盖"蓄力"那一小段时间，而本能力从蓄力起手到释放收招一直处于激活态。
	// 用 ActivationOwnedTags 表达不了"激活期间的一部分时间"，
	// 所以那部分交给 UpdateChargeTags() 手动增删。
	ActivationOwnedTags.AddTag(RPGTags::State_Attacking);

	// ══════════════════════════════════════════════════════════════════
	//  ⚠️ 这里**故意不设** CancelAbilitiesWithTag —— 取消动作挪到了 ActivateAbility
	// ══════════════════════════════════════════════════════════════════
	// 直觉上应该在这里声明 CancelAbilitiesWithTag = Ability.Attack.Light，
	// 让 GAS 自动取消轻击。**但那样切手技会永远判断不出来。**
	//
	// 原因是引擎的执行顺序（GameplayAbility.cpp:1020）：
	//
	//     CallActivateAbility()
	//        ├─ PreActivate()                          ← :1022
	//        │     └─ ApplyAbilityBlockAndCancelTags()  ← :999
	//        │           └─ CancelAbilities → 轻击的 EndAbility
	//        │                 └─ 摘掉 ActivationOwnedTags
	//        │                    （Ability.Attack.Light 就在里面）
	//        └─ ActivateAbility()                       ← :1023
	//              ↑ 轮到这里时标签已经没了
	//
	// 也就是说：**声明式取消会先于我们自己的分支判断执行**。
	// 症状是"按右键永远走蓄力，切手技像不存在一样"。
	// 最坑的是它**不报任何错** —— 因为从引擎的角度看一切正常：
	// 轻击确实被取消了，只是我们判断分支的依据也一起没了。
	//
	// 所以改成在 ActivateAbility 开头先读标签、读完再显式取消（见那里）。
	// 牺牲了声明式的优雅，换来了行为可预测。
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
	// ══════════════════════════════════════════════════════════════════
	//  ★ 分支判断必须放在最前面（详细原因见构造函数里的注释）
	// ══════════════════════════════════════════════════════════════════
	// 这是**唯一**还能看到轻击标签的时刻 —— 一旦下面执行了取消，它就没了。
	//
	// 用 GameplayTag 而不是 CombatComponent 里的连段索引，是因为索引在
	// 起手那一瞬间是 0（表示"正在打第 1 段"），无法区分"不在连段中"
	// 和"正在打第 1 段"。标签则精确表达"某个能力此刻正在激活"。
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	bTransitionBranch = ASC && ASC->HasMatchingGameplayTag(RPGTags::Ability_Attack_Light);

	// 这条日志是排查"切手技打不出来"的第一现场。
	// 用 Verbose 是因为每次按右键都会打，Info 级别会刷屏。
	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 重击分支判断：轻击标签%s → 走%s"),
		*GetName(),
		bTransitionBranch ? TEXT("存在") : TEXT("不存在"),
		bTransitionBranch ? TEXT("切手技") : TEXT("蓄力重击"));

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
	//  取消轻击
	// ══════════════════════════════════════════════════════════════════
	// 分支判断已经在函数开头做完了，现在才轮到取消 —— 顺序不能反。
	//
	// ⚠️ 匹配用的是 GA 的 **AssetTags**（GA_LightAttack 通过 SetAssetTags
	// 声明的那个），不是 ActivationOwnedTags。这是
	// UAbilitySystemComponent::CancelAbilities() 的实现细节 ——
	// 传错标签会静默地什么都不取消，又是一次"不报错但没效果"。
	//
	// Ignore 传 this，防止把自己也取消掉。
	if (ASC)
	{
		const FGameplayTagContainer CancelTags(RPGTags::Ability_Attack_Light);
		ASC->CancelAbilities(&CancelTags, nullptr, this);
	}

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

	// 兜底摘掉蓄力标签。
	// 正常路径上 ReleaseCharge() 已经摘过了，但"能力被取消 / 被打断"这条路
	// 不会经过 ReleaseCharge。漏掉这一句的话，角色会永久停在"蓄力中"——
	// 而且只在"蓄力时被敌人打断"这类特定时序下才出现，很难复现。
	ClearChargeTags();

	// 同上，"被外部取消"这条路不走 FinishHeavyAttack，蒙太奇要在这里收掉。
	// 正常结束时 FinishHeavyAttack 已经把指针清空了，这里是空操作。
	StopCurrentStageMontage();

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

	StopCurrentStageMontage();

	if (UAbilityTask_PlayMontageAndWait* MontageTask =
			PlayMontageOrSkip(Module->ComboTransitionMontage, TEXT("ComboTransition")))
	{
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();

		TrackCurrentStageMontage(MontageTask, Module->ComboTransitionMontage);
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

	// 挂上"蓄力中"标签。此刻段位还是 0（还没到第一段门槛），
	// 所以只有父标签 State.Attack.Charging —— 覆盖起手动画那段时间。
	UpdateChargeTags(0);

	// 播起手蒙太奇。
	// 注意这里**不绑 OnCompleted** —— 起手动画播完不代表蓄力结束，
	// 玩家可能还按着不放。只有 OnInterrupted 需要处理（被打断则结束能力）。
	StopCurrentStageMontage();

	if (UAbilityTask_PlayMontageAndWait* MontageTask =
			PlayMontageOrSkip(Module->HeavyAttack.ChargeStartMontage, TEXT("ChargeStart")))
	{
		// ★ 这里绑的是 OnChargeStartMontageCompleted，**不是** OnMontageCompleted。
		// 起手动画播完只意味着"摆好蓄力姿势了"，玩家很可能还按着不放。
		// 绑错的话，按下右键的瞬间整套重击就打完了 —— 蓄力机制形同虚设。
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_HeavyAttack::OnChargeStartMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();

		TrackCurrentStageMontage(MontageTask, Module->HeavyAttack.ChargeStartMontage);
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

		// 段位标签同步给 ASC —— 动画蓝图据此在起手/蓄满之间切换姿势
		UpdateChargeTags(NewLevel);

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

	// 蓄力阶段到此结束，进入释放动作 —— 摘掉蓄力标签。
	// 释放蒙太奇由 DefaultSlot 播放，动画蓝图应该切回"攻击中"而不是停在"蓄力中"。
	ClearChargeTags();

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

	// 蓄力循环动画到此为止，拆干净再播释放
	StopCurrentStageMontage();

	if (UAbilityTask_PlayMontageAndWait* MontageTask =
			PlayMontageOrSkip(Level->ReleaseMontage, TEXT("HeavyRelease")))
	{
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();

		TrackCurrentStageMontage(MontageTask, Level->ReleaseMontage);
	}
	else
	{
		// 没有释放蒙太奇 → 立刻结算伤害。
		// 重击范围比轻击大一些，所以偏移和半径都调大了。
		PerformSimulatedMeleeHit(CurrentDamageMultiplier, /*ForwardOffset*/ 180.f, /*Radius*/ 100.f);
		FinishHeavyAttack(false);
	}
}

// ══════════════════════════════════════════════════════════════════════
//  蓄力标签同步
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_HeavyAttack::UpdateChargeTags(int32 NewLevel)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	// 复制策略显式写成 CountToOwner：
	// 标签本身会复制给**所有**客户端（模拟代理也要看得出"这个敌人在蓄力"），
	// 只有计数只发给拥有者。对 HasMatchingGameplayTag 这类布尔查询没有影响。
	//
	// 为什么不用默认的 None：默认参数是不复制，
	// 结果就是"自己看得到蓄力姿势，别人看你却是站着的"——
	// 单机测试完全正常，联机才暴露。
	const EGameplayTagReplicationState RepState = EGameplayTagReplicationState::CountToOwner;

	// 先整批摘掉上一次挂的，再挂新的。
	// 直接 Add 新的而不摘旧的，会让计数越堆越高 —— 状态标签的语义是
	// "有没有"，计数堆高本身不会让判断出错，但 GameplayDebugger 里会很难看，
	// 而且一旦将来有人用 GetGameplayTagCount 做逻辑就会踩坑。
	if (!ActiveChargeTags.IsEmpty())
	{
		ASC->RemoveLooseGameplayTags(ActiveChargeTags, 1, RepState);
		ActiveChargeTags.Reset();
	}

	// 父标签：覆盖"按住右键但还没到第 1 段门槛"的起手时间。
	// 少了它，动画在起手那零点几秒里会以为"没在蓄力"。
	ActiveChargeTags.AddTag(RPGTags::State_Attack_Charging);

	// ① 固定的段位标签（Lv1 / Lv2 / Lv3）。
	// 动画蓝图查的就是这三个，所以**必须**挂上 —— 不能只依赖数据资产里配的标签，
	// 否则一旦有人在 DA 里把 ChargeLevelTag 留空，蓄力姿势就永远不显示。
	switch (NewLevel)
	{
	case 1: ActiveChargeTags.AddTag(RPGTags::State_Attack_Charging_Lv1); break;
	case 2: ActiveChargeTags.AddTag(RPGTags::State_Attack_Charging_Lv2); break;
	case 3: ActiveChargeTags.AddTag(RPGTags::State_Attack_Charging_Lv3); break;
	default: break;   // 还没到第 1 段门槛，只留父标签
	}

	// ② 数据资产里配的自定义标签（FRPG_HeavyAttackLevel::ChargeLevelTag）。
	// 这是给"想做四段蓄力"或"想让别的系统（特效、音效）按段位响应"留的口子。
	//
	// 两个都挂是刻意的：
	//   ① 保证动画侧的契约稳定 —— 动画只认固定的那三个标签
	//   ② 保留数据驱动的扩展位 —— 策划改 DA 就能新增段位，不用改动画
	// 如果只挂 ②，DA 配错了动画就瞎；只挂 ①，DA 里那个字段就成了摆设。
	if (const URPG_AttackModuleData* Module = GetAttackModule())
	{
		if (const FRPG_HeavyAttackLevel* Level = Module->GetHeavyLevel(NewLevel))
		{
			if (Level->ChargeLevelTag.IsValid())
			{
				ActiveChargeTags.AddTag(Level->ChargeLevelTag);
			}
		}
	}

	ASC->AddLooseGameplayTags(ActiveChargeTags, 1, RepState);
}

void URPG_GA_HeavyAttack::ClearChargeTags()
{
	// 先判空再取 ASC —— 绝大多数调用发生在"没挂过标签"的情况下
	// （比如直接走切手技分支），提前返回省掉一次组件查找。
	if (ActiveChargeTags.IsEmpty())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTags(ActiveChargeTags, 1, EGameplayTagReplicationState::CountToOwner);
	}

	// ★ 无论 ASC 是否拿得到都要清空本地记录。
	// 否则下次 UpdateChargeTags 会拿一份过期的集合去 Remove，
	// 而那份集合在 ASC 上早就不存在了 —— 摘了个寂寞，新标签也挂不上。
	ActiveChargeTags.Reset();
}

// ══════════════════════════════════════════════════════════════════
//  蒙太奇任务的清理
// ══════════════════════════════════════════════════════════════════

void URPG_GA_HeavyAttack::DetachCurrentStageMontageTask()
{
	if (!CurrentStageMontageTask)
	{
		return;
	}

	// ★ 必须先 RemoveDynamic，只调 EndTask() 拦不住迟到的回调。
	//
	// 任务广播前的守卫是 ShouldBroadcastAbilityTaskDelegates()，
	// 它判断的是 **能力** 是否激活（`Ability && Ability->IsActive()`，
	// AbilityTask.cpp:199），而不是任务是否结束。本能力从蓄力起手一路
	// 到释放收招都激活着，所以旧任务照样会把我们喊醒。
	//
	// 三个都要摘：ChargeStart 蒙太奇绑的是 OnChargeStartMontageCompleted，
	// 其余两段绑的是 OnMontageCompleted；不摘干净会漏掉一条路径。
	CurrentStageMontageTask->OnCompleted.RemoveDynamic(
		this, &URPG_GA_HeavyAttack::OnMontageCompleted);
	CurrentStageMontageTask->OnCompleted.RemoveDynamic(
		this, &URPG_GA_HeavyAttack::OnChargeStartMontageCompleted);
	CurrentStageMontageTask->OnInterrupted.RemoveDynamic(
		this, &URPG_GA_HeavyAttack::OnMontageInterrupted);

	CurrentStageMontageTask->EndTask();
	CurrentStageMontageTask = nullptr;
}

void URPG_GA_HeavyAttack::StopCurrentStageMontage()
{
	// 三件事：摘回调、停蒙太奇、结束任务。原因同轻击 GA 的
	// StopCurrentSegmentMontage()：
	//   · 不停蒙太奇 → 它的 Notify 会继续广播（判定窗口、攻击结束）
	//   · 不摘回调   → 它播完/被打断时会广播 OnInterrupted，
	//                  把"蓄力起手播完切循环"这种正常切换误判成"蓄力被打断"
	DetachCurrentStageMontageTask();

	if (CurrentStageMontage)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->StopMontageIfCurrent(*CurrentStageMontage, 0.f);
		}

		CurrentStageMontage = nullptr;
	}
}

void URPG_GA_HeavyAttack::TrackCurrentStageMontage(
	UAbilityTask_PlayMontageAndWait* Task, UAnimMontage* Montage)
{
	CurrentStageMontageTask = Task;
	CurrentStageMontage = Montage;
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

void URPG_GA_HeavyAttack::OnChargeStartMontageCompleted()
{
	// 已经进入释放阶段 —— 别再往上盖循环动画了。
	// ReleaseCharge 会打断起手动画，那次打断同样会走到这里。
	if (bReleasing)
	{
		return;
	}

	const URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module || !Module->HeavyAttack.ChargeLoopMontage)
	{
		// 没配循环动画是完全正常的做法：
		// 角色停在起手动画的最后一帧，保持一个"蓄势待发"的张力姿势。
		// 很多动作游戏就是这么做的，不循环反而更有力量感。
		return;
	}

	// ★ 起手蒙太奇到此为止 —— 必须先把它拆干净再播循环。
	// 不拆的话，播放循环动画会打断起手动画，起手任务的 OnInterrupted
	// 立刻广播 → 被误判成"蓄力被打断"，蓄力刚摆好姿势就结束了。
	StopCurrentStageMontage();

	if (UAbilityTask_PlayMontageAndWait* LoopTask =
			PlayMontageOrSkip(Module->HeavyAttack.ChargeLoopMontage, TEXT("ChargeLoop")))
	{
		// 这里**不绑 OnCompleted**。
		// 循环蒙太奇应该在资产里把最后一节指回自己（Section 的 Next Section 设成自身），
		// 那样它永远不会"播完"，只会被打断 —— 打断的处理已经在 OnMontageInterrupted 里。
		//
		// 如果忘了配循环，OnCompleted 会触发一次但没人接，蒙太奇自然结束、
		// 角色停在最后一帧，效果和"没配循环动画"一样，不会出错。
		LoopTask->OnInterrupted.AddDynamic(this, &URPG_GA_HeavyAttack::OnMontageInterrupted);
		LoopTask->ReadyForActivation();

		TrackCurrentStageMontage(LoopTask, Module->HeavyAttack.ChargeLoopMontage);
	}
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

	// ── 蒙太奇收尾 ──
	// 两种路径的处理不一样：
	//   · 正常打完（OnAttackEnd 通知 / 蒙太奇播完）→ 摘掉回调但**不停动画**，
	//     让最后几帧的后摇自然播完
	//   · 被打断（闪避取消、切手技取消等）→ 连动画一起停掉，
	//     否则它的判定窗口 Notify 会在别人的动作上再结算一次伤害
	if (bWasCancelled)
	{
		StopCurrentStageMontage();
	}
	else
	{
		DetachCurrentStageMontageTask();
		CurrentStageMontage = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeTimer);
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, bWasCancelled);
}
