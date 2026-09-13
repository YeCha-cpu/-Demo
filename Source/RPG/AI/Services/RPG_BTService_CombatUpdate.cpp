// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Services/RPG_BTService_CombatUpdate.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "AI/RPG_AIController.h"
#include "AI/RPG_BlackboardKeys.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_LogChannels.h"

URPG_BTService_CombatUpdate::URPG_BTService_CombatUpdate()
{
	NodeName = TEXT("RPG 战斗状态更新");

	// 0.2 秒一次。加一点随机偏移是为了让多只敌人的服务不要在同一帧集中跑，
	// 避免周期性掉帧（十几只敌人每帧同时做感知查询会很明显）。
	Interval = 0.2f;
	RandomDeviation = 0.05f;

	// 有逐实例状态（连续不可见时长），每棵树各持一份
	bCreateNodeInstance = true;
}

void URPG_BTService_CombatUpdate::OnBecomeRelevant(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	// 每次进入这个分支都从头计时 —— 否则上一轮战斗残留的计时会让
	// AI 刚打完一场就立刻脱战。
	TimeSinceLastSeen = 0.f;
}

void URPG_BTService_CombatUpdate::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		return;
	}

	AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor));

	// ── 本来就没目标 ──
	if (!Target)
	{
		TimeSinceLastSeen = 0.f;

		// 兜底：确保 bInCombat 和"有没有目标"始终一致。
		// 少了这一步，一旦某条路径漏了清 bInCombat，AI 会卡在
		// "战斗分支"里但没有任何目标，两个分支都不走 —— 站着发呆。
		//
		// 同样走统一入口 —— 直接写黑板的话，速度会停在战斗速度上。
		if (Blackboard->GetValueAsBool(RPGBlackboardKeys::bInCombat))
		{
			if (ARPG_AIController* AIController = Cast<ARPG_AIController>(OwnerComp.GetAIOwner()))
			{
				AIController->SetCombatState(false);
			}
			else
			{
				Blackboard->SetValueAsBool(RPGBlackboardKeys::bInCombat, false);
			}
		}
		return;
	}

	// ── 目标死了 ──
	// 死亡**不会**触发感知丢失（尸体还在视野里），所以必须单独判。
	if (const ARPG_BaseCharacter* TargetCharacter = Cast<ARPG_BaseCharacter>(Target))
	{
		if (!TargetCharacter->IsAlive())
		{
			UE_LOG(LogRPG_AI, Log, TEXT("[%s] 目标已死亡，脱战"), *GetName());
			ClearTarget(OwnerComp);
			return;
		}
	}

	// ── 更新记忆 / 计时 ──
	if (Blackboard->GetValueAsBool(RPGBlackboardKeys::bTargetVisible))
	{
		TimeSinceLastSeen = 0.f;

		// 看得见的时候持续刷新"最后已知位置"。
		// 只在"刚看到"那一刻记一次是不够的 —— 玩家一直在动，
		// 追到最后已知位置时会停在玩家**几秒前**待过的地方。
		Blackboard->SetValueAsVector(
			RPGBlackboardKeys::LastKnownLocation, Target->GetActorLocation());
	}
	else
	{
		TimeSinceLastSeen += DeltaSeconds;

		if (TimeSinceLastSeen >= LoseTargetAfterSeconds)
		{
			UE_LOG(LogRPG_AI, Log,
				TEXT("[%s] 目标已 %.1f 秒不可见，脱战返回巡逻"), *GetName(), TimeSinceLastSeen);

			ClearTarget(OwnerComp);
		}
	}
}

void URPG_BTService_CombatUpdate::ClearTarget(UBehaviorTreeComponent& OwnerComp)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		return;
	}

	// 注意**不清 LastKnownLocation**：
	// 留着它，AI 下次进战斗时如果一开始就丢视野，至少还有个搜索目标。
	// 而且调试时能看到"上一次是在哪发现玩家的"，很有用。
	Blackboard->ClearValue(RPGBlackboardKeys::TargetActor);
	Blackboard->SetValueAsBool(RPGBlackboardKeys::bTargetVisible, false);

	// ★ 走 AIController 的统一入口，而不是直接写 bInCombat ——
	// 它还要负责把移动速度降回巡逻速度。只改黑板的话，
	// 敌人会"脱战了但还在用战斗速度走路"，看起来像脱战没生效。
	if (ARPG_AIController* AIController = Cast<ARPG_AIController>(OwnerComp.GetAIOwner()))
	{
		AIController->SetCombatState(false);
	}
	else
	{
		// 兜底：万一 AI 控制器不是我们的类型（比如关卡里误配了基类），
		// 至少把黑板清干净，行为树还能正常回到巡逻分支。
		Blackboard->SetValueAsBool(RPGBlackboardKeys::bInCombat, false);
	}

	TimeSinceLastSeen = 0.f;
}
