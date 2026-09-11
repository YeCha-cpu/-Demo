// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/RPG_PlayerState.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/RPG_AbilitySystemComponent.h"
#include "AbilitySystem/RPG_AttributeSet.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

ARPG_PlayerState::ARPG_PlayerState()
{
	// PlayerState 默认不 Tick。ASC 和属性集都不需要每帧更新，
	// 保持默认即可 —— 属性变化是通过 GE 的委托驱动的，不是轮询。
	//
	// 本项目不做联机，所以没有调用 SetIsReplicated / SetReplicationMode。
	// 若将来接入联机，这里需要补：
	//     AbilitySystemComponent->SetIsReplicated(true);
	//     AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AbilitySystemComponent = CreateDefaultSubobject<URPG_AbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<URPG_AttributeSet>(TEXT("AttributeSet"));

	// ⚠️ 这一行不能省。
	// AddSpawnedAttribute 把属性集登记进 ASC 的属性表，之后
	// ASC->GetSet<URPG_AttributeSet>() 才能找到它。漏掉的话不会报错，
	// 症状是"伤害没反应、血条不动"——非常难查。
	AbilitySystemComponent->AddSpawnedAttribute(AttributeSet);
}

UAbilitySystemComponent* ARPG_PlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

URPG_AttributeSet* ARPG_PlayerState::GetRPGAttributeSet() const
{
	// 直接用成员而不是 ASC->GetSet<>()：这里我们明确知道属性集就是自己创建的那个，
	// 少一次查表，也避免 ASC 尚未注册完成时返回 nullptr。
	return AttributeSet;
}

bool ARPG_PlayerState::IsAlive() const
{
	if (!AbilitySystemComponent)
	{
		return true;   // 初始化未完成，按存活处理（详见 RPG_BaseCharacter::IsAlive 的说明）
	}

	return !AbilitySystemComponent->HasMatchingGameplayTag(RPGTags::State_Dead);
}
