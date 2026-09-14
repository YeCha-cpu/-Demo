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
	// 复制相关的设置分两处，这里都不需要重复写：
	//   · ASC 的 SetIsReplicated / SetReplicationMode
	//       → URPG_AbilitySystemComponent 构造函数（敌我共用，改一处即可）
	//   · 属性集的复制声明（ReplicatedUsing / GetLifetimeReplicatedProps）
	//       → URPG_AttributeSet

	AbilitySystemComponent = CreateDefaultSubobject<URPG_AbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<URPG_AttributeSet>(TEXT("AttributeSet"));

	// ══════════════════════════════════════════════════════════════════
	//  ★ 提高 PlayerState 的复制频率（默认是 1 Hz）
	// ══════════════════════════════════════════════════════════════════
	// `APlayerState` 的构造函数里硬编码了 `SetNetUpdateFrequency(1)`
	// （`PlayerState.cpp:28`）—— 对"分数、名字"这类几乎不变的数据来说，
	// 每秒发一次是合理的。
	//
	// 但**玩家的 ASC 挂在这个 PlayerState 上**，而 ASC 的
	// `RepAnimMontageInfo`（蒙太奇同步）是 ASC 的属性 ——
	// 它跟着 **PlayerState 的通道**走。于是：
	//
	//     玩家的蒙太奇同步 ≈ 每秒 1 次
	//     敌人的蒙太奇同步 ≈ 每秒 100 次（ASC 在角色身上，默认频率）
	//
	// 表现就是"看另一个玩家出招像幻灯片，看敌人却还行"。
	//
	// ⚠️ 注意这是**整个 PlayerState** 的复制频率，不只是蒙太奇 ——
	// 属性、标签、GE 全都跟着提速。所以别设太高：30 已经和引擎的
	// `NetServerMaxTickRate`（默认 30）持平，再高也发不出去，只是白算。
	SetNetUpdateFrequency(30.f);

	// ⚠️ 这一行不能省。
	// AddSpawnedAttribute 把属性集登记进 ASC 的属性表，
	// 之后 ASC->GetSet<URPG_AttributeSet>() 才能找到它。
	// 漏掉的话不会报错，但症状是"伤害没反应、血条不动"——非常难查。
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
	if (!AbilitySystemComponent) return true;   // 初始化未完成，按存活处理（详见 RPG_BaseCharacter::IsAlive 的说明）
	
	return !AbilitySystemComponent->HasMatchingGameplayTag(RPGTags::State_Dead);
}
