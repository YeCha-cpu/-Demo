// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Navigation/PathFollowingComponent.h"
#include "RPG_BTTask_MoveBase.generated.h"

/**
 * "走到某个地方并等它走到" —— 巡逻与追击的共同逻辑。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【★ 为什么必须用潜在任务（Latent Task）模式】
 * ══════════════════════════════════════════════════════════════════════
 * 行为树节点有两种写法：
 *
 *   · **瞬时完成** —— 在 ExecuteTask 里做完事，直接 return Succeeded
 *   · **潜在任务** —— return InProgress，等某件事发生后再
 *                    FinishLatentTask(Succeeded/Failed)
 *
 * "移动"必须用后者。因为 `MoveToLocation` 只是**下了一个移动请求**，
 * 它是异步的 —— 函数返回时角色才刚抬脚。
 *
 * 如果在这里 return Succeeded，行为树会以为"走完了"，
 * 立刻执行下一个节点（比如发起攻击）—— 于是你会看到 AI
 * **一边往你这边走一边挥拳头**。
 *
 * 正确做法是返回 InProgress，等 `ReceiveMoveCompleted` 回调
 * （或者我们自己判超时）再 FinishLatentTask。
 *
 * 这是 AI 里最经典的一个坑，也是面试高频题。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么抽一个基类】
 * ══════════════════════════════════════════════════════════════════════
 * 巡逻和追击的差别只有"去哪"这一件事，剩下的（下请求、绑回调、
 * 处理到达/失败/超时/中断）完全一样。让子类只实现 PrepareMove()
 * 一个函数，能保证两者的中断处理逻辑**绝对一致** ——
 * 而中断处理恰恰是最容易写漏、漏了又最难查的部分。
 */
UCLASS(Abstract)
class RPG_API URPG_BTTask_MoveBase : public UBTTaskNode
{
	GENERATED_BODY()

public:
	URPG_BTTask_MoveBase();

protected:
	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	virtual void TickTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/**
	 * 决定这次去哪。返回 false 表示"这次不走了"，任务直接失败。
	 *
	 * 子类在这里读黑板、取巡逻点、推进索引 —— 一切"目的地从哪来"的逻辑。
	 *
	 * @param OutDestination 走到这个位置。OutGoalActor 非空时忽略
	 * @param OutGoalActor   ★ 跟随这个 Actor。非空时引擎会**持续观察它的位置**，
	 *                       移动超过阈值就自动重算路径 —— 这才是"追击"该有的行为。
	 *                       用固定坐标的话只是"走到你刚才站的地方"。
	 */
	virtual bool PrepareMove(
		UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor)
		PURE_VIRTUAL(URPG_BTTask_MoveBase::PrepareMove, return false;);

	/**
	 * 发起一次移动。
	 *
	 * 抽出来是因为**追击过程中要能换目标**：跟着玩家跑的时候一旦丢了视野，
	 * 应该立刻改成去"最后已知位置"，而不是结束任务让行为树绕一圈 ——
	 * 那样会白白停一个 Wait 节点的时间，看起来就是一哆嗦。
	 *
	 * @return InProgress = 已受理，等回调；其他值 = 立刻结束
	 */
	EBTNodeResult::Type StartMove(UBehaviorTreeComponent& OwnerComp);

	/** 移动完成回调 */
	UFUNCTION()
	void OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);

	// ══════════════════════════════════════════════════════════════════
	//  配置
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 认为"到达了"的距离容差（厘米）。
	 * -1 表示用导航系统默认值（一般取角色的胶囊体半径）。
	 * 调大一点能让 AI 少做一次"微调位置"的小碎步。
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI", meta = (ClampMin = "-1.0"))
	float AcceptanceRadius = 60.f;

	/**
	 * 移动超时（秒）。
	 *
	 * 没有它的话，"寻路失败"或"目标点在导航网格外"会让 AI 永久卡在
	 * InProgress 状态 —— 行为树停在原地不动，而且不报错。
	 * 这是"AI 突然变傻站着不动"最常见的原因。
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI", meta = (ClampMin = "0.5"))
	float MoveTimeoutSeconds = 10.f;

private:
	/** 本次移动请求的 ID。用来过滤掉不是我们发起的移动完成回调 */
	FAIRequestID CurrentRequestID;

	/** 本次移动已经等了多久 */
	float ElapsedSeconds = 0.f;

	/**
	 * 发起这次移动的行为树组件。
	 *
	 * 为什么必须自己存：完成回调是在**别的时机**被调用的，
	 * 那时手上没有 OwnerComp 参数。而节点对象的 Outer 是**行为树资产**
	 * 而不是运行中的组件 —— 用 GetOuter() 会拿到错的东西。
	 */
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
};
