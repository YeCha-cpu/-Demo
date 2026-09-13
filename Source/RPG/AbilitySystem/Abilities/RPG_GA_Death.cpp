// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_Death.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTriggerType.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"

#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_Death::URPG_GA_Death()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 死亡只在服务器判定 —— 血归零、事件广播、AI 停止、重生计时
	// 全都是服务器的事。客户端靠复制看到结果。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// 不重入。挂上 State.Dead 之后新的触发本来就会被 ActivationBlockedTags 挡住，
	// 这里再显式写一次，是为了让"死亡只能发生一次"这条规则在代码里可见。
	bRetriggerInstancedAbility = false;

	// ══════════════════════════════════════════════════════════════════
	//  ★ 声明式触发：整个"伤害 → 死亡表现"链路唯一的连接点
	// ══════════════════════════════════════════════════════════════════
	// 这一句等价于说"当 Event.Combat.Death 事件到达时，激活我"。
	// 之后不需要任何地方去 `Cast` 出 GA_Death 并手动激活 ——
	// 属性集只管广播事件，谁关心谁自己声明。
	//
	// 对比另一种写法（属性集里直接调用角色的死亡函数）：
	//   · 那样属性集就要知道"角色有死亡表现"这件事，分层被打破
	//   · 加"死亡时掉装备""死亡时给击杀者经验"都得回头改属性集
	//   · 而声明式触发下，这些各自是独立的能力或监听者，互不知道对方存在
	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = RPGTags::Event_Combat_Death;
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);

	// ⚠️ 这里**不要**动 ActivationBlockedTags。
	// 基类已经加了 State.Dead —— 那正是我们要的：State.Dead 一挂上，
	// 第二次死亡触发就再也进不来。不用额外写"防重复死亡"的代码。
}

void URPG_GA_Death::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bDeathFinished = false;

	ARPG_BaseCharacter* Character = GetRPGCharacter();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	if (!Character || !ASC)
	{
		UE_LOG(LogRPG_Combat, Error,
			TEXT("[%s] 死亡能力拿不到角色或 ASC，死亡流程中止（角色会停在血量为 0 但还活着的状态）"),
			*GetName());

		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  ① 挂 State.Dead —— 必须最先做
	// ══════════════════════════════════════════════════════════════════
	// 从这一行开始，角色的所有能力激活都会被基类的 ActivationBlockedTags 挡掉。
	// 详见头文件里"为什么 ① 必须排在 ④ 前面"。
	//
	// CountToOwner 的语义：标签本身复制给所有人（客户端才知道这个人死了），
	// 只有计数只发给拥有者。对 HasMatchingGameplayTag 这种布尔查询完全够用。
	ASC->AddLooseGameplayTags(
		FGameplayTagContainer(RPGTags::State_Dead),
		/*Count=*/1,
		EGameplayTagReplicationState::CountToOwner);

	// ══════════════════════════════════════════════════════════════════
	//  ② 取消所有还在跑的能力
	// ══════════════════════════════════════════════════════════════════
	// 目的是**清理它们挂着的 ActivationOwnedTags**：
	// 不停掉的话，一个"死在攻击中"的角色会一直带着 State.Attacking，
	// 动画蓝图（每帧查 ASC 标签）会让他保持战斗姿态，而不是瘫下去。
	//
	// 用 CancelAllAbilities 而不是 CancelAbilitiesWithTag：
	//   · 前者把耐力恢复、疾跑、Buff 之类**全部**收掉，语义就是"人都死了"
	//   · 后者要枚举标签，漏一个就是一个不显眼的残留状态
	//
	// 传 this 是为了把自己排除在外 —— 否则会取消自己，后面的蒙太奇就没得播了。
	ASC->CancelAllAbilities(this);

	// ③ 通知角色层：敌人的 AI 在这里停大脑，玩家在这里停输入
	Character->OnDeathStarted();

	// ══════════════════════════════════════════════════════════════════
	//  ④ 播死亡蒙太奇
	// ══════════════════════════════════════════════════════════════════
	UAnimMontage* Montage = Character->GetDeathMontage();
	if (Montage)
	{
		MontageTask = PlayMontageOrSkip(Montage, TEXT("DeathMontage"), Character->GetDeathMontagePlayRate());
		if (MontageTask)
		{
			MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_Death::OnMontageCompleted);
			MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_Death::OnMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &URPG_GA_Death::OnMontageInterrupted);
			MontageTask->ReadyForActivation();
			return;
		}
	}

	// 没配死亡蒙太奇 → 直接倒地。
	// 这不是错误配置：测试布娃娃链路时最快的方式就是留空，
	// 表现上是"人直接瘫下去"，反而比等一段动画更省事。
	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("[%s] 角色没有配置死亡蒙太奇，直接进入布娃娃"), *GetName());

	FinishDeath();
}

void URPG_GA_Death::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	DetachMontageTask();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPG_GA_Death::DetachMontageTask()
{
	if (!MontageTask)
	{
		return;
	}

	// 先 RemoveDynamic 再 EndTask —— 只 EndTask 拦不住迟到的回调，
	// 理由见 RPG_GA_HitReact::DetachMontageTask 的注释。
	MontageTask->OnCompleted.RemoveDynamic(this, &URPG_GA_Death::OnMontageCompleted);
	MontageTask->OnInterrupted.RemoveDynamic(this, &URPG_GA_Death::OnMontageInterrupted);
	MontageTask->OnCancelled.RemoveDynamic(this, &URPG_GA_Death::OnMontageInterrupted);

	MontageTask->EndTask();
	MontageTask = nullptr;
}

void URPG_GA_Death::OnMontageCompleted()
{
	FinishDeath();
}

void URPG_GA_Death::OnMontageInterrupted()
{
	// 死亡动画被打断也要倒地 —— 躺下这件事不该依赖动画播完。
	// （正常情况下没人能打断它：State.Dead 挡掉了所有新能力，
	//   而 CancelAllAbilities 又跳过了自己。这里是兜底。）
	FinishDeath();
}

void URPG_GA_Death::FinishDeath()
{
	if (bDeathFinished)
	{
		return;
	}
	bDeathFinished = true;

	ARPG_BaseCharacter* Character = GetRPGCharacter();
	if (Character)
	{
		// ⑤ 交给物理：角色失去控制，网格由 Chaos 模拟。
		//    物理状态本身不复制，各端自己算 —— 见 RPG_BaseCharacter::OnRep_RagdollEnabled。
		Character->EnterRagdoll();

		// 该不该重生由角色上的 RespawnDelay 决定：
		// 玩家通常配 3~5 秒，敌人配 0（死了就躺着）。
		Character->StartRespawnCountdown();
	}

	// 结束能力。注意**不能**在这里摘 State.Dead ——
	// 那个标签就是"这个人死了"的定义，要一直挂到复活时才摘。
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}
