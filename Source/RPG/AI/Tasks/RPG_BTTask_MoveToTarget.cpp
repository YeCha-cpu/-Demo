// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Tasks/RPG_BTTask_MoveToTarget.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "AI/RPG_BlackboardKeys.h"
#include "Core/RPG_LogChannels.h"

URPG_BTTask_MoveToTarget::URPG_BTTask_MoveToTarget()
{
	NodeName = TEXT("RPG 追击目标");
}

bool URPG_BTTask_MoveToTarget::PrepareMove(
	UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor)
{
	OutGoalActor = nullptr;
	bLastMoveFollowedActor = false;

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		return false;
	}

	AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor));

	if (Blackboard->GetValueAsBool(RPGBlackboardKeys::bTargetVisible) && Target)
	{
		// ══════════════════════════════════════════════════════════════
		//  看得见 → **跟随目标 Actor**，不是"走到它现在的位置"
		// ══════════════════════════════════════════════════════════════
		// 这两种写法的差别是"追击手感"的分水岭：
		//
		//   OutDestination = Target->GetActorLocation()   ← 一个坐标快照
		//   OutGoalActor   = Target                       ← 跟随这个 Actor
		//
		// 传坐标的话，引擎只知道"走到那个点为止"。角色走到时玩家早就走了，
		// 于是到了 → 停 → 重新拍快照 → 再走 —— 表现就是一顿一顿的，
		// 而且永远在追你**几秒前**站的地方。
		//
		// 传 Actor 则会让引擎调用 Path->SetGoalActorObservation(Actor, 100)
		// （AIController.cpp:913），**持续观察目标位置，移动超过阈值就
		// 自动重算路径** —— 真正的"时刻确认玩家在哪"。
		OutGoalActor = Target;
		bLastMoveFollowedActor = true;
		return true;
	}

	if (bChaseVisibleTargetOnly)
	{
		// 看不见就认输，交给别的分支
		return false;
	}

	// ── 看不见目标 → 去最后已知位置找 ──
	//
	// 这是"有记忆的追击"：不是立刻放弃，而是去你最后出现的地方找一圈。
	// 注意 LastKnownLocation **不是零向量**时才有效 —— 从没看到过目标时
	// 它是 (0,0,0)，直接走过去会让 AI 冲向世界原点。
	const FVector LastKnown = Blackboard->GetValueAsVector(RPGBlackboardKeys::LastKnownLocation);

	if (LastKnown.IsNearlyZero())
	{
		return false;
	}

	OutDestination = LastKnown;

	UE_LOG(LogRPG_AI, Verbose,
		TEXT("[%s] 目标不可见，前往最后已知位置 %s 搜索"),
		*GetName(), *LastKnown.ToCompactString());

	return true;
}

bool URPG_BTTask_MoveToTarget::IsTargetVisible(const UBehaviorTreeComponent& OwnerComp) const
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	return Blackboard && Blackboard->GetValueAsBool(RPGBlackboardKeys::bTargetVisible);
}

void URPG_BTTask_MoveToTarget::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// ══════════════════════════════════════════════════════════════════
	//  中途变卦就换一套走法 —— 而不是结束任务让行为树绕一圈
	// ══════════════════════════════════════════════════════════════════
	// 追击过程中"看得见"和"看不见"会来回切换。切换时必须换走法：
	//
	//   看得见 → 看不见：不能再跟着 Actor 跑了（那等于透视），
	//                    改成去最后已知位置
	//   看不见 → 看得见：重新锁定，跟着真人跑
	//
	// 为什么在 TickTask 里就地换、而不是 return Succeeded 让行为树重来：
	// 行为树的巡逻/追击分支后面跟着一个 Wait 节点，绕一圈就意味着
	// **原地站 0.2 秒**再出发 —— 那正是要消掉的"一顿"。
	if (IsTargetVisible(OwnerComp) != bLastMoveFollowedActor)
	{
		const EBTNodeResult::Type RestartResult = StartMove(OwnerComp);

		if (RestartResult != EBTNodeResult::InProgress)
		{
			FinishLatentTask(OwnerComp, RestartResult);
			return;
		}

		// StartMove 已经把计时清零了，这一帧不用再累加超时
		return;
	}

	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);
}
