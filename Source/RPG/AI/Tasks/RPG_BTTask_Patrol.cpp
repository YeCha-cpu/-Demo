// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Tasks/RPG_BTTask_Patrol.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "AI/RPG_BlackboardKeys.h"
#include "Character/RPG_Enemy.h"
#include "Core/RPG_LogChannels.h"

URPG_BTTask_Patrol::URPG_BTTask_Patrol()
{
	// 名字会显示在行为树节点的标题上，Debug 时一眼认出是哪个节点
	NodeName = TEXT("RPG 巡逻");
}

bool URPG_BTTask_Patrol::PrepareMove(
	UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor)
{
	// 巡逻永远是"走到某个固定点"，不跟随任何 Actor
	OutGoalActor = nullptr;

	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();

	if (!AIController || !Blackboard)return false;

	const ARPG_Enemy* Enemy = Cast<ARPG_Enemy>(AIController->GetPawn());
	if (!Enemy)return false;

	// ── 一个巡逻点都没配 ──
	if (Enemy->PatrolPoints.IsEmpty())
	{
		if (!bFallbackToHomeLocation) return false;

		// 回出生点待着。
		// 用 LastKnownLocation 而不是 PatrolLocation 传目的地 —— 后者是
		// "巡逻点的位置"，把它写成一个固定点会误导调试时看到的值。
		OutDestination = Blackboard->GetValueAsVector(RPGBlackboardKeys::HomeLocation);
		return true;
	}

	// ── 取下一个巡逻点 ──
	// 索引存在黑板上而不是本节点里，是为了**能在编辑器里实时看到**
	// AI 巡逻到第几个点了（运行时选中 AI，看它的黑板即可）。
	// 存在节点成员里的话，调试时只能靠日志。
	const int32 Count = Enemy->PatrolPoints.Num();
	const int32 CurrentIndex = Blackboard->GetValueAsInt(RPGBlackboardKeys::PatrolIndex);
	const int32 NextIndex = (CurrentIndex + 1) % Count;   // 环形，走完一圈从头开始

	Blackboard->SetValueAsInt(RPGBlackboardKeys::PatrolIndex, NextIndex);

	// 巡逻点可能被配成了空的（数组里留了个 None），跳过它
	const AActor* Point = Enemy->PatrolPoints[NextIndex];
	if (!Point)
	{
		UE_LOG(LogRPG_AI, Warning,
			TEXT("[%s] 巡逻点数组第 %d 项是空的 —— 检查一下敌人的 PatrolPoints 配置"),
			*GetName(), NextIndex);
		return false;
	}

	OutDestination = Point->GetActorLocation();
	Blackboard->SetValueAsVector(RPGBlackboardKeys::PatrolLocation, OutDestination);

	return true;
}
