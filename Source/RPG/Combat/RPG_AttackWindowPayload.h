// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Combat/RPG_CombatTypes.h"
#include "RPG_AttackWindowPayload.generated.h"

/**
 * AnimNotify 广播"伤害判定窗口开启"时携带的数据。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么要用 UObject 包装】
 * ══════════════════════════════════════════════════════════════════════
 * GameplayEvent 的载荷类型 FGameplayEventData 是引擎定义的固定结构，
 * 只提供 OptionalObject / OptionalObject2 这两个通用槽位来放自定义数据。
 * 想传结构化的信息就只能包成 UObject 塞进 OptionalObject。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么这里只有"检测参数"，没有伤害倍率】
 * ══════════════════════════════════════════════════════════════════════
 * 检测参数（半径、Socket 名）配在动画通知上，是因为它们**本质上由动画决定**：
 * 这一刀挥出去手在哪个位置、扫过多大范围，看的是动画本身。
 *
 * 伤害倍率则相反，它属于**数值设计**，应该集中在 AttackModuleData 里管理 ——
 * 改个倍率不该需要打开动画资产。所以倍率由 GA 从攻击模组读，不从这里传。
 */
UCLASS()
class RPG_API URPG_AttackWindowPayload : public UObject
{
	GENERATED_BODY()

public:
	/** 这一段攻击的标签，用于日志区分与事件过滤 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attack")
	FGameplayTag AttackTag;

	/** 是否用下面的参数覆盖攻击模组里的检测配置 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attack")
	bool bOverrideTrace = false;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attack")
	ERPG_TraceSource TraceSource = ERPG_TraceSource::Hands;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attack")
	float TraceRadius = 30.f;

	/** 检测线段的起点 Socket（双手检测时为左手，刀锋检测时为刀柄） */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attack")
	FName SocketStart;

	/** 检测线段的终点 Socket（双手检测时为右手，刀锋检测时为刀尖） */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Attack")
	FName SocketEnd;
};
