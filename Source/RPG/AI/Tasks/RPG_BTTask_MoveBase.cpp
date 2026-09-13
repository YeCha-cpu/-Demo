// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Tasks/RPG_BTTask_MoveBase.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

#include "Core/RPG_LogChannels.h"

URPG_BTTask_MoveBase::URPG_BTTask_MoveBase()
{
	// 超时判断要在 TickTask 里做，所以必须打开 tick。
	// 不打开的话 TickTask 根本不会被调用 —— 超时保护形同虚设。
	bNotifyTick = true;

	// ══════════════════════════════════════════════════════════════════
	//  ★ 每个行为树实例各要一份节点对象
	// ══════════════════════════════════════════════════════════════════
	// 行为树节点默认是**共享**的：`bCreateNodeInstance = false` 时，
	// 所有跑这棵树的 AI 用的是**同一个**节点对象（资产里那个），
	// 逐实例的状态要靠 NodeMemory 那块内存传。
	//
	// 本类在成员变量里存了"当前请求 ID"和"已等待时长"。
	// 如果节点是共享的，两只敌人同时移动就会互相覆盖这两个值 ——
	// 表现是"其中一只永远走不到目的地"或"走着走着突然停下"，
	// 而且只在**同屏出现第二只敌人**时才复现。
	//
	// 打开这个开关，引擎会为每个行为树实例克隆一份节点对象，
	// 成员变量就天然是"每只敌人各一份"。
	//
	// 代价是每实例多一个 UObject —— 对本项目这点规模可以忽略。
	// （引擎自己的节点多用 NodeMemory 方案以避免这个开销，
	//   但那套写法要自己算内存大小、自己 Placement New，复杂度高得多。）
	bCreateNodeInstance = true;
}

// ══════════════════════════════════════════════════════════════════════
//  执行
// ══════════════════════════════════════════════════════════════════════

EBTNodeResult::Type URPG_BTTask_MoveBase::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CachedOwnerComp = &OwnerComp;
	return StartMove(OwnerComp);
}

EBTNodeResult::Type URPG_BTTask_MoveBase::StartMove(UBehaviorTreeComponent& OwnerComp)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController || !AIController->GetPawn())
	{
		return EBTNodeResult::Failed;
	}

	FVector Destination = FVector::ZeroVector;
	AActor* GoalActor = nullptr;

	if (!PrepareMove(OwnerComp, Destination, GoalActor))
	{
		// 子类说这次没地方可去（比如巡逻点一个都没配）
		return EBTNodeResult::Failed;
	}

	// ── 绑定与解绑 ──
	//
	// 先 RemoveDynamic 再 AddDynamic。
	// 不能只靠"绑一次"的假设 —— StartMove 在一轮任务里可能被调用多次
	// （追击中丢失视野要换目标）。动态多播委托重复绑定会在一次移动完成时
	// 触发 N 次回调，表现是"AI 走着走着突然跳一大步"。
	// 先解再绑是幂等的，无论调用几次都只有一个绑定。
	AIController->ReceiveMoveCompleted.RemoveDynamic(this, &URPG_BTTask_MoveBase::OnMoveCompleted);
	AIController->ReceiveMoveCompleted.AddDynamic(this, &URPG_BTTask_MoveBase::OnMoveCompleted);

	// ── 构造移动请求 ──
	//
	// ★ 用 FAIMoveRequest(GoalActor) 而不是 FAIMoveRequest(位置)：
	//
	//   传位置 → 引擎只记住那一个坐标，角色走到就停了（是"快照"，不是"跟随"）
	//   传 Actor → 引擎会调 Path->SetGoalActorObservation(Actor, 100)，
	//              **持续观察目标位置，移动超过阈值就自动重算路径**
	//              （AIController.cpp:913，NavigationData.h:362 的接口注释：
	//               "enables path observing specified AActor's location and
	//                update itself if actor changes location"）
	//
	// 追击必须用后者。用前者的话，AI 永远在走向你**几秒前**站的地方，
	// 到了之后再拍一次快照重新出发 —— 表现就是"一顿一顿地追"。
	FAIMoveRequest MoveRequest = GoalActor
		? FAIMoveRequest(GoalActor)
		: FAIMoveRequest(Destination);

	MoveRequest.SetAcceptanceRadius(AcceptanceRadius);
	MoveRequest.SetUsePathfinding(true);
	// 把角色半径算进到达判定 —— 否则"到了"是以胶囊体中心算的，
	// 视觉上角色还有半个身子在外面就被判定到达了
	MoveRequest.SetReachTestIncludesAgentRadius(true);

	// 目标会被投影到导航网格上。
	// 跟随 Actor 时由引擎自己处理目标位置，不需要这个开关。
	if (!GoalActor)
	{
		MoveRequest.SetProjectGoalLocation(true);
	}

	const FPathFollowingRequestResult MoveResult = AIController->MoveTo(MoveRequest);
	CurrentRequestID = MoveResult.MoveId;

	if (MoveResult.Code == EPathFollowingRequestResult::Failed)
	{
		// 连路径都没申请到（目标点在导航网格外、或者压根没有导航网格）
		AIController->ReceiveMoveCompleted.RemoveDynamic(
			this, &URPG_BTTask_MoveBase::OnMoveCompleted);

		UE_LOG(LogRPG_AI, Warning,
			TEXT("[%s] 移动请求失败：目标 %s 可能不在导航网格上"
			     "（关卡里放 NavMeshBoundsVolume 了吗）"),
			*GetName(),
			GoalActor ? *GoalActor->GetName() : *Destination.ToCompactString());

		return EBTNodeResult::Failed;
	}

	if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// 已经站在目标点上了 —— 不用等，直接成功。
		// 不处理这个分支的话 AI 会站在那儿干等超时。
		AIController->ReceiveMoveCompleted.RemoveDynamic(
			this, &URPG_BTTask_MoveBase::OnMoveCompleted);

		return EBTNodeResult::Succeeded;
	}

	// MoveResult.Code == RequestSuccessful —— 请求已受理，等回调
	ElapsedSeconds = 0.f;
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type URPG_BTTask_MoveBase::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* AIController = OwnerComp.GetAIOwner())
	{
		// ★ 解绑要在停移动之前还是之后？
		// 之前 —— 因为 StopMovement 可能会触发一次移动完成回调
		//（结果是 Aborted），那时我们已经被打断了，
		// 再跑一遍 OnMoveCompleted 会去 FinishLatentTask 一个已经结束的任务。
		AIController->ReceiveMoveCompleted.RemoveDynamic(
			this, &URPG_BTTask_MoveBase::OnMoveCompleted);

		AIController->StopMovement();
	}

	return EBTNodeResult::Aborted;
}

void URPG_BTTask_MoveBase::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

	ElapsedSeconds += DeltaSeconds;

	if (ElapsedSeconds >= MoveTimeoutSeconds)
	{
		// ── 超时兜底 ──
		// 走到这里说明既没收到"到达"也没收到"失败" —— 通常是寻路卡住了
		// （目标点在障碍物里、导航网格有洞、或者角色被卡住）。
		//
		// 这一步的意义不在于"处理得多好"，而在于**让 AI 恢复行动能力**：
		// 不兜底的话行为树会永远停在这个节点上，表现为"敌人突然站着不动了"。
		UE_LOG(LogRPG_AI, Warning,
			TEXT("[%s] 移动超时（%.1f 秒）—— AI 会放弃这次移动。"
			     "检查目标点是否可达、导航网格是否覆盖了那片区域"),
			*GetName(), MoveTimeoutSeconds);

		if (AAIController* AIController = OwnerComp.GetAIOwner())
		{
			AIController->ReceiveMoveCompleted.RemoveDynamic(
				this, &URPG_BTTask_MoveBase::OnMoveCompleted);
			AIController->StopMovement();
		}

		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

// ══════════════════════════════════════════════════════════════════════
//  回调
// ══════════════════════════════════════════════════════════════════════

void URPG_BTTask_MoveBase::OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result)
{
	// 只认自己那次请求的结果。
	// AI 身上可能同时有其他来源的移动请求（被击退、被别的节点挪动），
	// 不过滤的话会误判成"我到了"。
	if (CurrentRequestID.IsValid() && RequestID != CurrentRequestID)
	{
		return;
	}

	if (UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get())
	{
		FinishLatentTask(*OwnerComp,
			Result == EPathFollowingResult::Success
				? EBTNodeResult::Succeeded
				: EBTNodeResult::Failed);
	}
}
