// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "RPG_BTService_CombatUpdate.generated.h"

/**
 * 定期的战斗状态维护。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【服务（Service）和任务（Task）的分工】
 * ══════════════════════════════════════════════════════════════════════
 *   · 任务：**做一件事**，有开始有结束（走过去、打一拳）
 *   · 服务：**持续维护某种状态**，挂在分支上，只要分支还活着就定期跑
 *
 * 这个服务做的事：把"感知到的事实"翻译成"行为树能用的判断依据"。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它解决的三个问题】
 * ══════════════════════════════════════════════════════════════════════
 * ① **持续更新"最后已知位置"**
 *    不能只在"刚看到"那一刻记一次 —— 玩家是移动的，记忆也要跟着更新。
 *
 * ② **丢视野 ≠ 脱战**
 *    玩家躲到柱子后面，感知系统会立刻报告"丢失"。那时就把目标清掉的话，
 *    AI 会变得极度健忘 —— 玩家绕树跑一圈敌人就回去巡逻了。
 *    所以这里做**计时**：连续 N 秒没再看到，才真的脱战。
 *
 * ③ **目标死了要立刻脱战**
 *    死亡不会触发"感知丢失"（尸体还在视野里），没人管的话 AI 会
 *    对着尸体继续挥拳。这是很常见的一个疏漏。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么不用 Tick，而是用服务自己的 Interval】
 * ══════════════════════════════════════════════════════════════════════
 * 这些判断不需要每帧做 —— 0.2 秒一次的精度对"脱战计时"完全够用，
 * 而每帧跑会白白吃 CPU（尤其敌人多的时候）。
 * `Interval` 由行为树服务框架负责调度，比自己在 Tick 里数秒干净得多。
 */
UCLASS(meta = (DisplayName = "RPG 战斗状态更新"))
class RPG_API URPG_BTService_CombatUpdate : public UBTService
{
	GENERATED_BODY()

public:
	URPG_BTService_CombatUpdate();

protected:
	virtual void TickNode(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	virtual void OnBecomeRelevant(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/**
	 * 目标"看不见"持续多久才真正脱战（秒）。
	 *
	 * 太短 → 玩家一躲起来 AI 就忘了，感觉很傻
	 * 太长 → 玩家早跑远了 AI 还在原地转圈找
	 * 3~8 秒是动作游戏里比较自然的区间
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI", meta = (ClampMin = "0.5"))
	float LoseTargetAfterSeconds = 6.f;

private:
	/** 目标已经连续不可见多久了 */
	float TimeSinceLastSeen = 0.f;

	/** 清掉目标，回到非战斗状态 */
	void ClearTarget(UBehaviorTreeComponent& OwnerComp);
};
