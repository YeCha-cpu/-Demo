// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GameplayTagContainer.h"
#include "RPG_BTTask_Attack.generated.h"

/**
 * 发动一次攻击，并等它播完。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【★ 这个节点为什么不能"发完指令就返回成功"】
 * ══════════════════════════════════════════════════════════════════════
 * 攻击动画要播 0.5~1 秒。如果 ExecuteTask 里发动完攻击就 return Succeeded，
 * 行为树会立刻执行下一个节点 —— 于是 AI 会在挥拳的同时开始走向玩家，
 * 看起来像在"滑步出拳"。
 *
 * 正确做法是返回 **InProgress**，让行为树停在这个节点上，
 * 等攻击真正结束了再 FinishLatentTask。这就是**潜在任务（Latent Task）**模式，
 * 也是 AI + 技能系统结合最经典的一个坑。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【★ AI 和玩家走的是同一条能力链路】
 * ══════════════════════════════════════════════════════════════════════
 * 这个节点做的事情，和 `RPG_PlayerController::OnAbilityInputPressed` **一模一样**：
 *
 *     1. 把 InputTag 推进 CombatComponent 的输入缓存
 *     2. ASC->TryActivateAbilityByInputTag(InputTag)
 *
 *     玩家：按键 → ─┐
 *     敌人：本节点 → ┴→ Input.Attack.Light → ASC 映射表 → GA_LightAttack
 *
 * 为什么要这么绕，而不直接 `TryActivateAbilityByTag(Ability.Attack.Light)`？
 *   · 直接调能力会**绕过输入缓存** → AI 打不出连段（连段靠缓存推进）
 *   · 两条路径分开写，早晚会不一致 → "玩家能放、AI 放不出来"这类问题
 *   · 现在这个写法下，将来给攻击加"前摇期间不能转身"之类规则，
 *     玩家和 AI 自动同时生效
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【怎么知道攻击结束了】
 * ══════════════════════════════════════════════════════════════════════
 * 两条路都用过，最后选了**轮询标签**：
 *
 *   · 监听 `Event.Combat.AttackEnd` 事件 —— 精确，但只覆盖"动画播到
 *     AttackEnd 通知"这一条结束路径。攻击被取消、耐力不足直接失败、
 *     蒙太奇没配走模拟时序……这些情况下事件不会来，AI 会一直等下去。
 *   · 轮询 `State.Attacking` 标签 —— 不管因为什么原因结束的，
 *     能力一结束这个标签就会被 GAS 自动摘掉，**覆盖所有路径**。
 *
 * 代价是每帧一次标签查询（哈希表查找，可忽略）。
 * 用"状态标签"而不是"结束事件"当判据，正是本项目"标签是唯一真相源"
 * 这条原则带来的直接好处。
 */
UCLASS(meta = (DisplayName = "RPG 攻击"))
class RPG_API URPG_BTTask_Attack : public UBTTaskNode
{
	GENERATED_BODY()

public:
	URPG_BTTask_Attack();

protected:
	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	virtual void TickTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	// ══════════════════════════════════════════════════════════════════
	//  配置
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 用哪个输入标签触发攻击。
	 *
	 * 用 Input 域的标签（而不是 Ability 域的），因为这个入口是
	 * "模拟一次按键"，和玩家走的是同一个。
	 *
	 * 想细分"轻击 / 重击 / 技能"，就在这里配不同的 Input.* 标签。
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI", meta = (Categories = "Input"))
	FGameplayTag InputTag;

	/**
	 * 攻击最长允许多久（秒）。超时则判失败，让行为树继续往下走。
	 *
	 * 没有它的话，万一 `State.Attacking` 因为某个 bug 没被摘掉，
	 * AI 会永远卡在这个节点上 —— 表现是"敌人打完一拳之后就不动了"。
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|AI", meta = (ClampMin = "0.5"))
	float AttackTimeoutSeconds = 3.f;

private:
	/** 本次攻击已经等了多久 */
	float ElapsedSeconds = 0.f;

	/** 发起攻击的行为树组件（完成时要靠它调 FinishLatentTask） */
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
};
