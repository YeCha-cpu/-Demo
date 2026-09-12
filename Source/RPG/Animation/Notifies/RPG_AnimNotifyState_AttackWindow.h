// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Combat/RPG_CombatTypes.h"
#include "RPG_AnimNotifyState_AttackWindow.generated.h"

/**
 * 伤害判定窗口。
 *
 * 放在蒙太奇里覆盖"这一刀真正能打到人"的那段时间。窗口之外挥刀不产生伤害 ——
 * 这直接决定了战斗的手感：窗口开得太早会"还没挥到就掉血"，太晚会"明明打中了没反应"。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它怎么和 GA 通信】
 * ══════════════════════════════════════════════════════════════════════
 *   NotifyBegin → SendGameplayEventToActor(Event.Combat.AttackWindow.Open)
 *   NotifyEnd   → SendGameplayEventToActor(Event.Combat.AttackWindow.Close)
 *
 * GA 侧用 UAbilityTask_WaitGameplayEvent 监听这两个事件，
 * 开启时启动武器轨迹检测，关闭时停止。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么不在 Notify 里直接做伤害判定】
 * ══════════════════════════════════════════════════════════════════════
 * 这是本项目的一条硬性原则：**表现层不做逻辑**。
 *   · Notify 里做判定 → 数值和动画焊死，改数值要动动画资产
 *   · 无法单元测试，也无法在没有动画的情况下验证数值链路
 *   · 联机下 Notify 的播放时机在客户端和服务器可能有细微差异，
 *     把权威逻辑放在这里会引入难以复现的 bug
 *
 * 所以 Notify 只负责"广播一个事实"，怎么响应是 GA 的事。
 */
UCLASS(meta = (DisplayName = "RPG 攻击判定窗口"))
class RPG_API URPG_AnimNotifyState_AttackWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** 这一段攻击的标签，便于在日志里区分是第几段 */
	UPROPERTY(EditAnywhere, Category = "RPG|Attack")
	FGameplayTag AttackTag;

	/**
	 * 是否用下面的参数覆盖攻击模组里的检测配置。
	 * 一般不需要开 —— 除非这一段的攻击范围明显和其他段不同
	 * （比如某个大范围的横扫招式）。
	 */
	UPROPERTY(EditAnywhere, Category = "RPG|Attack")
	bool bOverrideTrace = false;

	UPROPERTY(EditAnywhere, Category = "RPG|Attack", meta = (EditCondition = "bOverrideTrace"))
	ERPG_TraceSource TraceSource = ERPG_TraceSource::Hands;

	UPROPERTY(EditAnywhere, Category = "RPG|Attack", meta = (EditCondition = "bOverrideTrace", ClampMin = "1.0"))
	float TraceRadius = 30.f;

	UPROPERTY(EditAnywhere, Category = "RPG|Attack", meta = (EditCondition = "bOverrideTrace"))
	FName SocketStart;

	UPROPERTY(EditAnywhere, Category = "RPG|Attack", meta = (EditCondition = "bOverrideTrace"))
	FName SocketEnd;

	//~ Begin UAnimNotifyState interface
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
	//~ End UAnimNotifyState interface
};
