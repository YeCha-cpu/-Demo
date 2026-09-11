// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "RPG_AbilitySystemLibrary.generated.h"

class URPG_AttributeSet;
class UAbilitySystemComponent;

/**
 * GAS 相关的静态查询工具。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么需要它，引擎不是已经有 UAbilitySystemBlueprintLibrary 了吗】
 * ══════════════════════════════════════════════════════════════════════
 * 引擎的库提供了 GetAbilitySystemComponent(AActor*) —— 我们**直接用它的**，
 * 不重复造。本库只补两件引擎没有的事：
 *
 *   1. GetRPGAttributeSet()：引擎有 UAbilitySystemComponent::GetSetOnActor<T>()，
 *      但那是 C++ 模板函数，**无法暴露给蓝图**（UFUNCTION 不支持模板）。
 *      想在蓝图里拿属性集读血条，就需要这个包装。
 *
 *   2. IsAlive() / HasGameplayTag()：游戏层语义的便捷查询，避免每个调用点
 *      都写一遍 "取 ASC → 判空 → 查标签" 三件套。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它解决的真正痛点】
 * ══════════════════════════════════════════════════════════════════════
 * 战斗代码里最烦的是："我想给目标上 Debuff，但它的 ASC 可能在 PlayerState 上，
 * 也可能在自己身上"。如果每个调用点都写：
 *
 *     UAbilitySystemComponent* ASC = nullptr;
 *     if (auto* Player = Cast<ARPG_Player>(Target)) ASC = Player->GetPlayerState()->GetASC();
 *     else if (auto* Enemy = Cast<ARPG_Enemy>(Target)) ASC = Enemy->GetASC();
 *
 * 那就是灾难。这里收口一次，调用方永远不用关心 ASC 挂在哪。
 */
UCLASS()
class RPG_API URPG_AbilitySystemLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 获取任意 Actor 的 RPG 属性集，取不到返回 nullptr。
	 *
	 * 内部转发给引擎的 GetSetOnActor，它会通过 IAbilitySystemInterface 找到 ASC，
	 * 因此玩家（ASC 在 PlayerState）和敌人（ASC 在自己身上）都能正确处理——
	 * 前提是角色的接口实现写对了。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem", meta = (DefaultToSelf = "Actor"))
	static URPG_AttributeSet* GetRPGAttributeSet(const AActor* Actor);

	/** 角色是否存活（以 State.Dead 标签为判据，不是看血量数值） */
	UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem", meta = (DefaultToSelf = "Actor"))
	static bool IsAlive(const AActor* Actor);

	/** Actor 是否拥有指定标签。内部处理 ASC 为空的情况 */
	UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem", meta = (DefaultToSelf = "Actor"))
	static bool HasGameplayTag(const AActor* Actor, FGameplayTag Tag);

	/**
	 * 取 Actor 当前的某个属性值（原始 float）。
	 * 蓝图里读属性要经过 FGameplayAttributeData，比较啰嗦，这里包一层。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem", meta = (DefaultToSelf = "Actor"))
	static float GetAttributeValue(const AActor* Actor, FGameplayAttribute Attribute);
};
