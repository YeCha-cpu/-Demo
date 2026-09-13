// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Decorators/RPG_BTDecorator_CanAttack.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "AI/RPG_BlackboardKeys.h"
#include "Character/RPG_BaseCharacter.h"

URPG_BTDecorator_CanAttack::URPG_BTDecorator_CanAttack()
{
	NodeName = TEXT("RPG 可以攻击");

	// ══════════════════════════════════════════════════════════════════
	//  ★ 默认就要能打断"追击"，否则敌人会贴着你走却不打
	// ══════════════════════════════════════════════════════════════════
	// 行为树的 Selector 从左往右选：攻击分支在前、追击分支在后。
	// 敌人正在追击时，Selector 就"卡"在追击上 ——
	// **不会主动回头重新检查攻击分支的条件**。
	//
	// 结果是：追到玩家面前了也不停下来挥拳，一路贴着走。
	// 这和"玩家进视野敌人不切换战斗分支"是同一类问题，
	// 修法也一样：把装饰器设成观察者。
	//
	// 区别在于这条**不用你在编辑器里配** —— 这是我们自己的装饰器，
	// 可以在构造函数里把默认值定好。（引擎自带的 Blackboard 装饰器
	// 就只能在 Details 里手动设 Flow Abort Mode。）
	//
	// FlowAbortMode 是 UBTDecorator 的 protected 成员，子类可以设。
	// bAllowAbortLowerPri 默认为 true，所以引擎允许这个值。
	FlowAbortMode = EBTFlowAbortMode::LowerPriority;
}

bool URPG_BTDecorator_CanAttack::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();

	if (!AIController || !Blackboard)
	{
		return false;
	}

	APawn* SelfPawn = AIController->GetPawn();
	if (!SelfPawn)
	{
		return false;
	}

	const AActor* Target =
		Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor));

	if (!Target)
	{
		return false;
	}

	// ── 双方都得活着 ──
	//
	// 自己死了不该继续挥拳；目标死了更不该（虽然服务会清目标，
	// 但那是 0.2 秒一次 —— 这一层是更快的兜底）。
	if (const ARPG_BaseCharacter* SelfCharacter = Cast<ARPG_BaseCharacter>(SelfPawn))
	{
		if (!SelfCharacter->IsAlive())
		{
			return false;
		}
	}

	if (const ARPG_BaseCharacter* TargetCharacter = Cast<ARPG_BaseCharacter>(Target))
	{
		if (!TargetCharacter->IsAlive())
		{
			return false;
		}
	}

	// ── 距离判定 ──
	//
	// 用**表面的距离**而不是两个原点的距离：角色的原点在脚底，
	// 而胶囊体有半径。直接用原点距离的话，两个 34 半径的胶囊体贴在一起时
	// 距离仍有 0，而稍微分开一点就"超范围"了 —— 手感上会觉得
	// "明明挨着却打不到"。
	const float CenterDistance = FVector::Dist(SelfPawn->GetActorLocation(), Target->GetActorLocation());
	const float SurfaceDistance = CenterDistance
		- SelfPawn->GetSimpleCollisionRadius()
		- Target->GetSimpleCollisionRadius();

	const float AttackRange = Blackboard->GetValueAsFloat(RPGBlackboardKeys::AttackRange);

	// 注意 SurfaceDistance 可能是负数（两个胶囊体重叠）——
	// 那正好说明绝对够得着，和正数比较的结果自然就是 true。
	return SurfaceDistance <= AttackRange * RangeTolerance;
}

FString URPG_BTDecorator_CanAttack::GetStaticDescription() const
{
	// 显示在行为树节点上，一眼看出容差配成了多少 ——
	// 调试"AI 为什么不出手"时不用点开 Details 面板
	return FString::Printf(TEXT("距离 ≤ 攻击范围 × %.2f，且双方存活"), RangeTolerance);
}
