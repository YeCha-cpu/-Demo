// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/RPG_AbilitySystemLibrary.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AttributeSet.h"
#include "AbilitySystem/RPG_AttributeSet.h"
#include "Interfaces/RPG_AbilitySystemInterface.h"
#include "Core/RPG_GameplayTags.h"

/**
 * 本文件里多处出现 const_cast<AActor*>，原因统一说明：
 * 引擎的 UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(AActor*) 与
 * UAbilitySystemComponent::GetSetOnActor<T>(AActor*) 的参数都不是 const 指针。
 * 我们只是查询、绝不修改 Actor，所以这个转换是安全的。
 * 用 const 参数对外是为了让 C++ 调用方传 const 指针也能编译。
 */

URPG_AttributeSet* URPG_AbilitySystemLibrary::GetRPGAttributeSet(const AActor* Actor)
{
	if (!Actor) return nullptr;

	// 引擎已有 GetSetOnActor<T>()，它内部走 IAbilitySystemInterface，
	// 因此"ASC 在 PlayerState 上"（玩家）和"ASC 在自身"（敌人）都能正确解析。
	// 我们只是因为它是模板函数、蓝图不可用，才包这一层。
	return const_cast<URPG_AttributeSet*>(
		UAbilitySystemComponent::GetSetOnActor<URPG_AttributeSet>(const_cast<AActor*>(Actor)));
}

bool URPG_AbilitySystemLibrary::IsAlive(const AActor* Actor)
{
	if (!Actor) return false;

	// 优先走 RPG 接口 —— 实现者可能重写了更严格的判定
	// （比如"无敌帧中"不算可攻击状态，但仍算存活）
	if (const IRPG_AbilitySystemInterface* RPGInterface = Cast<IRPG_AbilitySystemInterface>(const_cast<AActor*>(Actor)))
	{
		return RPGInterface->IsAlive();
	}

	// 没实现接口的 Actor（可破坏物、召唤物…）退回标签判定
	return !HasGameplayTag(Actor, RPGTags::State_Dead);
}

bool URPG_AbilitySystemLibrary::HasGameplayTag(const AActor* Actor, FGameplayTag Tag)
{
	if (!Actor || !Tag.IsValid()) return false;

	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor));

	return ASC && ASC->HasMatchingGameplayTag(Tag);
}

float URPG_AbilitySystemLibrary::GetAttributeValue(const AActor* Actor, FGameplayAttribute Attribute)
{
	if (!Attribute.IsValid()) return 0.f;

	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor));

	return ASC ? ASC->GetNumericAttribute(Attribute) : 0.f;
}
