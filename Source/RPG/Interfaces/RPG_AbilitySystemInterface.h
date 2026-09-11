// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RPG_AbilitySystemInterface.generated.h"

class URPG_AttributeSet;

UINTERFACE(MinimalAPI, BlueprintType)
class URPG_AbilitySystemInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * RPG 角色的能力系统接口。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么还需要这个接口？引擎不是已经有 IAbilitySystemInterface 了吗】
 * ══════════════════════════════════════════════════════════════════════
 * 两者职责不同，不重复：
 *
 *   IAbilitySystemInterface（引擎）
 *     · 只有一个 GetAbilitySystemComponent()
 *     · GAS 内部机制依赖它（UAbilitySystemGlobals::GetAbilitySystemComponentFromActor 会 Cast 它）
 *     · 所以：**必须实现**，不实现的话 GAS 根本找不到你的 ASC
 *
 *   IRPG_AbilitySystemInterface（本接口）
 *     · 只补充游戏层需要的查询：属性集、是否存活
 *     · **不重复提供 GetAbilitySystemComponent()** —— 那是引擎接口的职责，
 *       再定义一个同功能的函数只会让调用方纠结"该调哪个"
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【ASC 挂在哪是一个必须被封装掉的差异】
 * ══════════════════════════════════════════════════════════════════════
 *   玩家：ASC 在 RPG_PlayerState 上（角色死亡销毁后，冷却/Buff/属性要延续）
 *   敌人：ASC 在自己身上（生命周期与角色完全一致，没有重生需求）
 *
 * 战斗代码（比如"给目标上一个减防 Debuff"）不该关心这个差异——
 * 那是实现者的责任。接口就是这层封装。
 */
class RPG_API IRPG_AbilitySystemInterface
{
	GENERATED_BODY()

public:
	/**
	 * 获取 RPG 属性集。
	 *
	 * 实现者负责处理"ASC 可能不在自己身上"的情况：
	 *   玩家角色  → 转发到 PlayerState 的 AttributeSet
	 *   敌人      → 返回自己的 AttributeSet
	 *
	 * 返回裸指针，可能为 nullptr（ASC 尚未初始化时）。调用方必须判空。
	 */
	virtual URPG_AttributeSet* GetRPGAttributeSet() const = 0;

	/**
	 * 是否存活。
	 *
	 * 判定依据是 State.Dead 标签而不是 Health 数值——
	 * 因为标签是"状态"的唯一真相源，而 Health 可能被各种 Buff 短暂改写。
	 * AI 选目标、伤害前置检查、UI 刷新都用它。
	 */
	virtual bool IsAlive() const = 0;
};
