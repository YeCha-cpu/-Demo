// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/RPG_BaseCharacter.h"
#include "RPG_Enemy.generated.h"

class URPG_AbilitySystemComponent;
class URPG_AttributeSet;

/**
 * 敌人角色。**自持 ASC**。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么敌人不像玩家那样把 ASC 放 PlayerState】
 * ══════════════════════════════════════════════════════════════════════
 * 敌人没有"重生后要保留状态"的需求——死了就是销毁，冷却和 Buff 一起消失正合适。
 * 放自己身上少一层间接寻址，也少一个需要维护生命周期的外部对象。
 *
 * 这里的"不对称"（玩家放 PlayerState、敌人放自己）不是设计缺陷，而是
 * **按各自生命周期特征做的最优解**。接口层的存在让调用方感知不到这个差异。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【AI 与玩家走同一条能力触发路径】
 * ══════════════════════════════════════════════════════════════════════
 * 敌人也使用 StartupAbilities（输入标签 → 能力类）这套映射，只是触发者
 * 从"玩家按键"变成了"AI 决策"。好处是：
 *   · 同一个 GA 类玩家和敌人共用，行为绝对一致
 *   · 不会出现"玩家能打出来、AI 打不出来"这类只在一边出现的 bug
 *   · 调试时用同一套日志和 GameplayDebugger 视图
 */
UCLASS()
class RPG_API ARPG_Enemy : public ARPG_BaseCharacter
{
	GENERATED_BODY()

public:
	ARPG_Enemy();

	//~ Begin APawn interface
	virtual void PossessedBy(AController* NewController) override;
	//~ End APawn interface

	/** 拿到敌人自己的 ASC（供 AI 任务/服务使用） */
	UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem")
	URPG_AbilitySystemComponent* GetRPGAbilitySystemComponent() const { return AbilitySystemComponent; }

	// ══════════════════════════════════════════════════════════════════
	//  AI 数据（阶段 4 的行为树会读这些）
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 巡逻点。在关卡里摆几个 TargetPoint，然后在这个敌人的实例上拖进来。
	 * 用 EditInstanceOnly：每个敌人可以有不同的巡逻路线，但不需要为每只敌人都做蓝图子类。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "RPG|AI")
	TArray<TObjectPtr<AActor>> PatrolPoints;

	/** 进入这个距离（厘米）内，AI 就认为可以发起攻击 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI")
	float AttackRange = 250.f;

	/** 脱战距离：目标跑出这个距离且丢失视野一段时间后，返回巡逻 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI")
	float LoseTargetDistance = 2500.f;

protected:
	virtual void BeginPlay() override;
	virtual UAbilitySystemComponent* GetASCInternal() const override;

	/** 初始化 ASC 关联、属性与能力（幂等） */
	void InitializeAbilitySystem();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Abilities")
	TObjectPtr<URPG_AbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<URPG_AttributeSet> AttributeSet;

private:
	bool bAbilitySystemInitialized = false;
};
