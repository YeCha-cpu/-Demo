// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Tasks/RPG_BTTask_Attack.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

#include "AbilitySystem/RPG_AbilitySystemComponent.h"
#include "Character/RPG_BaseCharacter.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_BTTask_Attack::URPG_BTTask_Attack()
{
	NodeName = TEXT("RPG 攻击");

	// 默认轻击 —— 敌人在阶段 5 用的是同一套徒手连招
	InputTag = RPGTags::Input_Attack_Light;

	// 超时判断在 TickTask 里做
	bNotifyTick = true;

	// 本节点有逐实例的状态（已等待时长），必须每棵行为树各持一份。
	// 共享节点对象的话，两只敌人同时攻击会互相覆盖计时。
	// 详见 RPG_BTTask_MoveBase 构造函数里的说明。
	bCreateNodeInstance = true;

	// 失败时不要把整棵子树标成"失败需要重新规划" ——
	// 攻击失败（比如耐力不够）是正常情况，让行为树按 Selector 继续试别的分支就好
	bIgnoreRestartSelf = false;
}

// ══════════════════════════════════════════════════════════════════════
//  执行
// ══════════════════════════════════════════════════════════════════════

EBTNodeResult::Type URPG_BTTask_Attack::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	ARPG_BaseCharacter* Character = Cast<ARPG_BaseCharacter>(AIController->GetPawn());
	if (!Character)
	{
		return EBTNodeResult::Failed;
	}

	// 走基类的 IAbilitySystemInterface 拿 ASC，再 Cast 成项目扩展的类型 ——
	// 这样本节点对玩家和敌人**一视同仁**：谁的 ASC 都行，
	// 只要它支持"按输入标签激活"这个能力。
	// 用 GetRPGAbilitySystemComponent()（只在 RPG_Enemy 上有）的话，
	// 这段代码就写死成"只能给敌人用"了。
	URPG_AbilitySystemComponent* ASC = Cast<URPG_AbilitySystemComponent>(Character->GetAbilitySystemComponent());

	if (!ASC)
	{
		UE_LOG(LogRPG_AI, Warning,
			TEXT("[%s] 角色上没有 URPG_AbilitySystemComponent —— AI 无法发动攻击"), *GetName());
		return EBTNodeResult::Failed;
	}

	if (!InputTag.IsValid())
	{
		UE_LOG(LogRPG_AI, Error,
			TEXT("[%s] 没有配置 InputTag —— 这个攻击节点永远不会生效"), *GetName());
		return EBTNodeResult::Failed;
	}

	// ══════════════════════════════════════════════════════════════════
	//  和玩家按键完全相同的两步
	// ══════════════════════════════════════════════════════════════════

	// ① 推进输入缓存。
	//
	// 这一句看着多余（反正马上要激活了），但它决定了**连段能不能推进** ——
	// GA_LightAttack 的连段靠"在衔接窗口开启时从缓存里取出下一条输入"来推进，
	// 不推缓存的话 AI 永远只能打第 1 段。
	//
	// 也就是说：AI 想打 3 连击，就得让行为树连续三次执行本节点。
	// 这正好是行为树擅长的事 —— 在 Sequence 里排三个攻击节点即可。
	if (URPG_CombatComponent* Combat = Character->GetCombatComponent())
	{
		Combat->PushInputTag(InputTag);
	}

	// ② 走 ASC 的映射表激活能力
	if (!ASC->TryActivateAbilityByInputTag(InputTag))
	{
		// 失败是很正常的（耐力不够、还在冷却、被 State.Dead 阻断……）。
		// 用 Verbose 而不是 Warning —— 否则 AI 每次想打但打不出来都刷一条警告。
		// 真正的失败原因由 ASC 的 AbilityFailedCallbacks 打出来。
		UE_LOG(LogRPG_AI, Verbose,
			TEXT("[%s] 攻击能力激活失败（%s）—— 交给行为树尝试别的分支"),
			*GetName(), *InputTag.ToString());
		return EBTNodeResult::Failed;
	}

	UE_LOG(LogRPG_AI, Verbose, TEXT("[%s] 发动攻击（%s）"), *GetName(), *InputTag.ToString());

	// ★ 返回 InProgress 而不是 Succeeded —— 见头文件里的说明。
	// 行为树会停在这个节点上，等我们 FinishLatentTask。
	CachedOwnerComp = &OwnerComp;
	ElapsedSeconds = 0.f;

	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type URPG_BTTask_Attack::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// ── 被打断时故意**不取消**攻击能力 ──
	//
	// 被打断的原因通常是"条件不再满足"（比如目标死了、或者距离变了）。
	// 但招式已经挥出去了，中途硬停下来会看到动作卡在半空 ——
	// 而且伤害判定窗口可能已经开过了，取消能力会造成"看起来打中了却没伤害"。
	//
	// 让这一招自然打完（通常不到 1 秒），行为树下一轮自然会重新决策。
	// 如果将来要做"被打断就收招"，在这里调 ASC->CancelAbilities 即可。
	UE_LOG(LogRPG_AI, Verbose, TEXT("[%s] 攻击被行为树打断 —— 让这一招自然播完"), *GetName());

	return EBTNodeResult::Aborted;
}

void URPG_BTTask_Attack::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

	ElapsedSeconds += DeltaSeconds;

	AAIController* AIController = OwnerComp.GetAIOwner();
	ARPG_BaseCharacter* Character = AIController
		? Cast<ARPG_BaseCharacter>(AIController->GetPawn())
		: nullptr;

	const UAbilitySystemComponent* ASC = Character
		? Character->GetAbilitySystemComponent()
		: nullptr;

	// ── 攻击结束的判据：State.Attacking 标签没了 ──
	//
	// 用标签而不是等 Event.Combat.AttackEnd 事件，是因为标签覆盖**所有**结束路径：
	//   · 蒙太奇播到 AttackEnd 通知 → 标签摘掉 ✅
	//   · 蒙太奇自然播完（没配通知）→ 标签摘掉 ✅
	//   · 能力被取消 / 被打断 → 标签摘掉 ✅
	//   · 能力压根没激活成功 → 标签本来就没有 ✅
	// 而事件只覆盖第一条。
	if (ASC && !ASC->HasMatchingGameplayTag(RPGTags::State_Attacking))
	{
		if (UBehaviorTreeComponent* BTComp = CachedOwnerComp.Get())
		{
			FinishLatentTask(*BTComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	// ── 超时兜底 ──
	if (ElapsedSeconds >= AttackTimeoutSeconds)
	{
		UE_LOG(LogRPG_AI, Warning,
			TEXT("[%s] 攻击超时（%.1f 秒）仍未结束 —— 检查 State.Attacking 是否被正常清理，"
			     "或者敌人是不是卡在了一个不会结束的能力里"),
			*GetName(), AttackTimeoutSeconds);

		if (UBehaviorTreeComponent* BTComp = CachedOwnerComp.Get())
		{
			FinishLatentTask(*BTComp, EBTNodeResult::Failed);
		}
	}
}
