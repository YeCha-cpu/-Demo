// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Combat/RPG_CombatTypes.h"
#include "RPG_AbilityTask_WeaponTrace.generated.h"

/** 命中回调。一次广播可能包含多个目标（一刀扫到两个人） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGWeaponTraceHitDelegate, const TArray<FHitResult>&, Hits);

/**
 * 武器轨迹检测 AbilityTask。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么必须用 Sweep 而不是 Overlap】
 * ══════════════════════════════════════════════════════════════════════
 * 高速挥砍时，刀锋在**两帧之间**会整个"穿过"敌人：
 *
 *   第 N 帧：刀在敌人左边       第 N+1 帧：刀在敌人右边
 *   Overlap 检测两个离散位置 → 都检测不到 → 漏判
 *
 * 这就是"隧穿效应"。必须在两帧位置之间做**连续扫掠**（Sweep）才能抓到。
 *
 * 30fps 下这个问题尤其明显：一帧内刀尖可以移动 40cm 以上，
 * 而敌人胶囊半径可能还不到 40cm。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【去重：同一次挥砍只打一次】
 * ══════════════════════════════════════════════════════════════════════
 * 检测窗口通常持续 3~5 帧，如果不去重，同一刀会命中同一目标 3~5 次 ——
 * 伤害翻好几倍，受击特效也会叠着播。用 TSet 记录本次挥砍已命中的目标，
 * Task 销毁时清空（下次挥砍自然重新开始）。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【联机说明】
 * ══════════════════════════════════════════════════════════════════════
 * 这个 Task 会在客户端和服务器**各跑一份**（因为 GA 是 LocalPredicted）。
 *   · 客户端的检测结果只用于即时反馈（顿帧、音效）
 *   · 服务器那一份才是权威 —— 真实伤害从服务器产生并复制给所有人
 *
 * 所以这里不需要做"只在服务器跑"的限制，两份各司其职即可。
 */
UCLASS()
class RPG_API URPG_AbilityTask_WeaponTrace : public UAbilityTask
{
	GENERATED_BODY()

public:
	URPG_AbilityTask_WeaponTrace();

	/** 检测到新目标时广播。GA 在这里绑定伤害逻辑 */
	UPROPERTY(BlueprintAssignable)
	FRPGWeaponTraceHitDelegate OnHit;

	/**
	 * 创建轨迹检测任务。
	 *
	 * @param TraceSource  检测源类型（双手 / 刀锋）
	 * @param TraceRadius  检测半径（厘米）
	 * @param SocketStart  线段起点 Socket
	 * @param SocketEnd    线段终点 Socket
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks",
		meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static URPG_AbilityTask_WeaponTrace* CreateWeaponTraceTask(
		UGameplayAbility* OwningAbility,
		ERPG_TraceSource TraceSource,
		float TraceRadius,
		FName SocketStart,
		FName SocketEnd);

protected:
	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	/** 本 Task 的配置 */
	ERPG_TraceSource TraceSource = ERPG_TraceSource::Hands;
	float TraceRadius = 30.f;
	FName SocketStart;
	FName SocketEnd;

	/** 上一帧的采样线段。第一帧没有基准，只记录不检测 */
	bool bHasPreviousSample = false;
	FVector PreviousStart = FVector::ZeroVector;
	FVector PreviousEnd = FVector::ZeroVector;

	/** 本次挥砍已命中的目标（去重）。Task 销毁时清空 */
	TSet<TWeakObjectPtr<AActor>> HitActorsThisSwing;

	/** 采样当前位置，得到检测线段。拿不到骨骼/网格时返回 false */
	bool GetCurrentSample(FVector& OutStart, FVector& OutEnd) const;

	/** 每帧调用：采样 → 扫掠 → 去重 → 广播 */
	void SampleAndSweep();
};
