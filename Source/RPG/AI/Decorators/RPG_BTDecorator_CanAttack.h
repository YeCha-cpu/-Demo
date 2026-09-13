// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "RPG_BTDecorator_CanAttack.generated.h"

/**
 * "现在能不能打" —— 行为树的分支条件。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【装饰器（Decorator）的两种用法】
 * ══════════════════════════════════════════════════════════════════════
 *   · 挂在 Sequence 上 → 决定**要不要进入这个分支**
 *   · 挂在节点上       → 进入后持续监控，条件不满足就**打断**（Abort）
 *
 * 本装饰器用在第一种：攻击分支只在"够得着 + 活着"时才被选中，
 * 够不着就让下面的"追击"分支接手。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么直接算距离，而不从黑板读】
 * ══════════════════════════════════════════════════════════════════════
 * 距离是**每帧都在变**的。如果由服务每 0.2 秒写一次黑板，装饰器读到的
 * 就是最多 0.2 秒前的旧值 —— 玩家快速冲刺时会出现"明明已经跑到面前了
 * AI 还在往前追"（读到的还是 0.2 秒前的远距离）。
 *
 * `CalculateRawConditionValue` 每次条件评估都会被调用，当场算最准。
 * 两次减法 + 一次平方和，比查黑板还便宜。
 *
 * 而 `AttackRange` 那种**很少变**的值就适合放黑板 —— 按变化频率决定
 * 数据放哪里，是个挺有用的判断标准。
 */
UCLASS(meta = (DisplayName = "RPG 可以攻击"))
class RPG_API URPG_BTDecorator_CanAttack : public UBTDecorator
{
	GENERATED_BODY()

public:
	URPG_BTDecorator_CanAttack();

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	virtual FString GetStaticDescription() const override;

	/**
	 * 攻击距离的容差倍数。
	 *
	 * 1.0 = 必须真的进入 AttackRange 才打
	 * 1.2 = 允许在范围外 20% 就出手
	 *
	 * 稍微大于 1 是有实战意义的：攻击动作本身有前摇，等"确实进范围"
	 * 再出手，玩家往往已经走出去了，会出现"AI 一直追但永远打不到"。
	 * 留一点提前量，打起来才跟手。
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float RangeTolerance = 1.15f;
};
