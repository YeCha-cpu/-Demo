// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "Interfaces/RPG_AbilitySystemInterface.h"
#include "RPG_PlayerState.generated.h"

class URPG_AbilitySystemComponent;
class URPG_AttributeSet;

/**
 * 玩家状态。**玩家 ASC 的宿主**。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么玩家的 ASC 放这里，而不是放角色身上】
 * ══════════════════════════════════════════════════════════════════════
 * 三个理由，从最实际到最理论：
 *
 * 1. 死亡与重生
 *    角色死亡时 Character 会被销毁。ASC 挂在它身上的话，技能冷却、Buff 剩余
 *    时间、属性数值会一并消失。挂在 PlayerState 上，重生后状态自然延续。
 *    动作游戏里"死亡惩罚是清空 Buff 还是保留"是设计决策，而把 ASC 放
 *    PlayerState 让你**有得选**；放 Character 上你根本没得选。
 *
 * 2. Owner 与 Avatar 的语义分离
 *    GAS 里这两个概念是独立的：
 *      OwnerActor  = 这个能力属于谁（逻辑归属，用于 GE 的来源判定、团队判定）
 *      AvatarActor = 这个能力通过什么身体表现（用于动画、GameplayCue 定位）
 *    玩家死后"魂还在、身体没了"——Owner 仍然有效，Avatar 为空。
 *    放在 PlayerState 上天然表达这个关系。
 *
 * 3. 复制正确
 *    PlayerState 是网络复制中天然跟随玩家的 Actor，ASC 挂在它下面，
 *    重生时属性、冷却、Buff 会自动同步给客户端。
 *    反过来，ASC 放 Character 上会导致"角色重生后客户端拿不到正确的 ASC 引用"
 *    这类经典 bug —— 这是联机项目里最常见的架构错误之一。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【敌人为什么反过来放自己身上】
 * ══════════════════════════════════════════════════════════════════════
 * 敌人的 ASC 生命周期和它自己完全一致——死了一起销毁，没有重生需求，
 * 没有跨 Actor 的状态延续问题。放自己身上最简单，也少一层间接寻址。
 */
UCLASS()
class RPG_API ARPG_PlayerState : public APlayerState,
                                  public IAbilitySystemInterface,
                                  public IRPG_AbilitySystemInterface
{
	GENERATED_BODY()

public:
	ARPG_PlayerState();

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ Begin IRPG_AbilitySystemInterface
	virtual URPG_AttributeSet* GetRPGAttributeSet() const override;
	virtual bool IsAlive() const override;
	//~ End IRPG_AbilitySystemInterface

	/** 供 UI / 调试使用：拿到项目扩展的 ASC 类型，省去每次 Cast */
	UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem")
	URPG_AbilitySystemComponent* GetRPGAbilitySystemComponent() const { return AbilitySystemComponent; }

protected:
	/**
	 * 玩家的 ASC。
	 * 注意它挂在 PlayerState 上，所以 ActorInfo 的 Owner = PlayerState、Avatar = 玩家角色，
	 * 这个关联由 ARPG_Player::InitializeAbilitySystem() 建立。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Abilities")
	TObjectPtr<URPG_AbilitySystemComponent> AbilitySystemComponent;

	/** 玩家属性集。敌我用的是同一个类 */
	UPROPERTY()
	TObjectPtr<URPG_AttributeSet> AttributeSet;
};
