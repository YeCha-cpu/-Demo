// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/Tasks/RPG_BTTask_MoveBase.h"
#include "RPG_BTTask_MoveToTarget.generated.h"

/**
 * 追击：走向目标。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【走"活的玩家"还是"最后已知位置"？】
 * ══════════════════════════════════════════════════════════════════════
 * 两种模式，由 bChaseVisibleTargetOnly 控制：
 *
 *   · 看得见时 → 直接走向玩家本体（每帧更新的活目标）
 *   · 看不见时 → 走向 LastKnownLocation（记忆里的那个点）
 *
 * 第二种才是"有脑子"的关键。如果视野一丢就放弃、直接回去巡逻，
 * 玩家绕一根柱子就能把敌人耍得团团转；而如果永远追着玩家本体跑，
 * 那就是透视挂，玩家躲在哪里都没用。
 *
 * 正确行为是：**去你最后出现的地方找一圈**，找不到才回去巡逻。
 * 这一段"找一圈"的耐心由 BTService_CombatUpdate 的脱战计时控制。
 */
UCLASS(meta = (DisplayName = "RPG 追击目标"))
class RPG_API URPG_BTTask_MoveToTarget : public URPG_BTTask_MoveBase
{
	GENERATED_BODY()

public:
	URPG_BTTask_MoveToTarget();

protected:
	virtual bool PrepareMove(
		UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor) override;

	virtual void TickTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/**
	 * 只追看得见的目标。
	 *
	 * false（默认）= 看不见时去"最后已知位置"找 —— 有记忆的追击
	 * true          = 看不见就失败，让行为树的别的分支接手
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI")
	bool bChaseVisibleTargetOnly = false;

private:
	/**
	 * 上一次发起移动时，采用的是"跟随目标"还是"去固定点"。
	 *
	 * 用来发现**中途变卦**：追着追着目标丢了（或者又看见了），
	 * 就得换一套走法。不检测的话，AI 会一路跟着你跑到 6 秒脱战为止，
	 * 哪怕你早就躲进墙后面了 —— 那等于透视。
	 */
	bool bLastMoveFollowedActor = false;

	/** 从黑板读"目标此刻可不可见" */
	bool IsTargetVisible(const UBehaviorTreeComponent& OwnerComp) const;
};
