// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/Tasks/RPG_BTTask_MoveBase.h"
#include "RPG_BTTask_Patrol.generated.h"

/**
 * 巡逻：依次走到敌人身上配的每一个巡逻点，走完一圈从头再来。
 *
 * 巡逻点来自 `ARPG_Enemy::PatrolPoints` —— 在关卡里摆几个 TargetPoint，
 * 然后在**这个敌人的实例上**把它们拖进数组。
 *
 * 用 `EditInstanceOnly` 配的理由：每个敌人可以有自己的巡逻路线，
 * 但不需要为每只敌人都做一个蓝图子类。摆十个敌人 = 拖十次，
 * 比"做十个蓝图"轻得多。
 */
UCLASS(meta = (DisplayName = "RPG 巡逻"))
class RPG_API URPG_BTTask_Patrol : public URPG_BTTask_MoveBase
{
	GENERATED_BODY()

public:
	URPG_BTTask_Patrol();

protected:
	virtual bool PrepareMove(
		UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor) override;

	/**
	 * 一个巡逻点都没配时怎么办。
	 *
	 * true  = 回出生点附近待着（默认）
	 * false = 任务直接失败 —— 行为树会往下走，可能变成"原地发呆"
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI")
	bool bFallbackToHomeLocation = true;
};
