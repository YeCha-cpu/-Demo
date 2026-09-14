// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RPG_EffectVolume.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UGameplayEffect;
class ARPG_BaseCharacter;

/**
 * 触发模式。
 */
UENUM(BlueprintType)
enum class ERPG_EffectTriggerMode : uint8
{
	/** 进入范围时触发一次，然后按 bConsumeOnTrigger 决定是否用掉。适合拾取物（药水、卷轴） */
	OnEnter       UMETA(DisplayName = "进入时触发一次（拾取物）"),

	/** 只要在范围内就按 RepeatInterval 重复触发。适合治疗泉 / 毒池 / 光环 */
	WhileInside   UMETA(DisplayName = "在范围内按周期重复（治疗泉 / 毒池）"),
};

/**
 * 作用对象。
 */
UENUM(BlueprintType)
enum class ERPG_EffectTargetMode : uint8
{
	/** 只作用于**触发的那一个**角色。适合拾取物 —— 谁捡到谁受益 */
	TriggeringActor  UMETA(DisplayName = "只作用于触发者"),

	/** 作用于范围内**全部**符合条件的角色。适合治疗泉 / 毒池 / 范围陷阱 */
	AllOverlapping   UMETA(DisplayName = "作用于范围内全部角色"),
};

/**
 * 效果触发器 —— 会碰撞的、往角色身上施加效果的场景物体。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它能当什么用】
 * ══════════════════════════════════════════════════════════════════════
 * 同一个 C++ 类，靠配置就能覆盖这些玩法：
 *
 *   · 治疗药水   —— OnEnter + TriggeringActor + 用完消失 + Event.Item.Heal
 *   · 加攻卷轴   —— OnEnter + TriggeringActor + 用完消失 + Event.Item.Buff
 *   · 减防毒瓶   —— OnEnter + TriggeringActor + 用完消失 + Event.Item.Debuff
 *   · 治疗泉     —— WhileInside + AllOverlapping + Event.Item.Heal
 *   · 毒池 / 熔岩 —— WhileInside + AllOverlapping + Event.Item.Debuff
 *   · 一次性陷阱 —— OnEnter + AllOverlapping + 用完消失 + Event.Item.Debuff
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【★ 它为什么不直接施加 GameplayEffect】
 * ══════════════════════════════════════════════════════════════════════
 * 它只做一件事：**在服务器上，往碰到的角色发一个 GameplayEvent**。
 * 真正的效果由对应的 GA 去施加（`GA_Heal` / `GA_ApplyBuff`）。
 *
 * 这是项目定死的链路：**ASC 初始化 → 触发 GA → GE 上 Buff/标签 → GC 特效**。
 *
 * 如果这里图省事直接 `ApplyGameplayEffectToTarget`，短期能跑，但会一次性丢掉：
 *   · 消耗（这个能力要不要花耐力 / 有冷却）
 *   · 动画（喝药有个抬手的动作）
 *   · 打断与阻挡（死了 / 眩晕中不能捡）
 *   · 能力层的标签（`State.UsingItem` 之类，动画和 AI 要查）
 *
 * 而且**新增一种效果不需要改 C++**：做一个新 GE，配一个拾取物就行。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【联机】
 * ══════════════════════════════════════════════════════════════════════
 * · 效果**只在服务器施加**（`HasAuthority()` 守卫）—— 和项目里所有状态变更一致
 * · "用掉了"这个事实通过复制 `bConsumed` 同步，客户端在 `OnRep` 里隐藏自己
 * · 碰撞在两端都会触发，所以守卫必须写在最前面，不能靠"客户端不会触发"来省
 */
UCLASS(Blueprintable)
class RPG_API ARPG_EffectVolume : public AActor
{
	GENERATED_BODY()

public:
	ARPG_EffectVolume();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ── 碰撞回调 ──

	UFUNCTION()
	void OnSphereBeginOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	// ── 核心逻辑 ──

	/**
	 * 对单个角色施加效果 —— 往他身上发一个 `TriggerEvent` 事件。
	 *
	 * **只在服务器上真正做事**，客户端调用会被守卫挡掉。
	 */
	void ApplyEffectTo(ARPG_BaseCharacter* Character);

	/** 这个角色现在该不该被作用（存活 / 已死的是否也吃效果） */
	bool CanAffect(const ARPG_BaseCharacter* Character) const;

	/** `WhileInside` 模式的周期回调：给范围内所有符合条件的人来一次 */
	void ApplyToAllOverlapping();

	/** 用掉了：标记复制 + 隐藏 + 关碰撞；配了重生时间的话挂上重生定时器 */
	void ConsumeVolume();

	/** 重生：恢复可见 + 恢复碰撞 */
	void RespawnVolume();

	UFUNCTION()
	void OnRep_bConsumed();

	/** 按当前 bConsumed 状态刷新表现与碰撞。两处复用，避免不一致 */
	void RefreshVisualState();

	// ══════════════════════════════════════════════════════════════════
	//  组件
	// ══════════════════════════════════════════════════════════════════

	/** 碰撞球。半径在 BP 里调 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Effect")
	TObjectPtr<USphereComponent> TriggerSphere;

	/**
	 * 外观。BP 里换个 Static Mesh / 材质就是一个新道具。
	 *
	 * ⚠️ **根组件是 `TriggerSphere` 而不是它**，它挂在碰撞球下面。
	 * 反过来（把 Mesh 当根）的话，在编辑器里缩放模型会**同时缩放碰撞范围** ——
	 * 调外观顺手改了玩法数值，而且没有任何提示。
	 * 现在是"调碰撞球的大小"和"调模型的大小"两件独立的事。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Effect")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// ══════════════════════════════════════════════════════════════════
	//  配置（全部在 BP 的 Class Defaults 里改）
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 发给角色的 GameplayEvent 标签。
	 *
	 * 可选的四个（见 `Core/RPG_GameplayTags.h`）：
	 *   · `Event.Item.Heal`    治疗
	 *   · `Event.Item.Buff`    增益
	 *   · `Event.Item.Debuff`  减益
	 *
	 * ⚠️ 这个标签决定"由哪个 GA 响应" —— 目标角色必须已经**授予**了
	 * 带对应 `AbilityTriggers` 的能力，否则事件发出去没有人接，**而且不报错**。
	 * （诊断日志会打一条，见 `.cpp` 的 ApplyEffectTo）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect")
	FGameplayTag TriggerEvent;

	/**
	 * 这次效果要施加的 GE。
	 *
	 * 通过 `FGameplayEventData::OptionalObject` 传给 GA，GA 会优先用它、
	 * 拿不到才回落到自己配的默认 GE。
	 *
	 * ★ **这就是"新增一种效果不用改代码"的地方** ——
	 * 做一个新 GE 资产，在这里指过去就完事。
	 *
	 * 留空 = 用响应它的那个 GA 自己配的 GE。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect")
	TSubclassOf<UGameplayEffect> EffectClass;

	/**
	 * 数值（治疗量 / 强度）。
	 *
	 * 走 `FGameplayEventData::EventMagnitude` 传给 GA。
	 * **0 表示"用 GA 自己配的默认值"** —— 所以 0 不是一个合法的"治疗 0 点"。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect")
	float Magnitude = 0.f;

	/** 什么时候触发 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect")
	ERPG_EffectTriggerMode TriggerMode = ERPG_EffectTriggerMode::OnEnter;

	/** 作用对象 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect")
	ERPG_EffectTargetMode TargetMode = ERPG_EffectTargetMode::TriggeringActor;

	/** `WhileInside` 的触发间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect",
		meta = (ClampMin = "0.05", EditCondition = "TriggerMode == ERPG_EffectTriggerMode::WhileInside"))
	float RepeatInterval = 1.f;

	/**
	 * 触发后是否用掉（隐藏 + 关碰撞）。
	 *
	 * `OnEnter` 模式下：药水 = true，治疗泉 = false。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect",
		meta = (EditCondition = "TriggerMode == ERPG_EffectTriggerMode::OnEnter"))
	bool bConsumeOnTrigger = true;

	/**
	 * 用掉之后多久重生（秒）。<= 0 表示不重生。
	 *
	 * 只在 `bConsumeOnTrigger` 为真时有意义。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect",
		meta = (ClampMin = "0.0", EditCondition = "bConsumeOnTrigger"))
	float RespawnDelay = 0.f;

	/** 死了的角色还吃不吃效果。毒池一般希望吃（尸体也泡在里面），药水一般不希望 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Effect")
	bool bAffectDead = false;

	// ══════════════════════════════════════════════════════════════════
	//  运行时状态
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 用掉了没有。
	 *
	 * 复制给客户端 —— 客户端靠它隐藏外观、关掉碰撞。
	 * 效果本身**不需要**复制：那是服务器的权威变更，走属性/GE 复制就够了。
	 */
	UPROPERTY(ReplicatedUsing = OnRep_bConsumed, BlueprintReadOnly, Category = "RPG|Effect")
	bool bConsumed = false;

	/** `WhileInside` 的周期定时器 */
	FTimerHandle RepeatTimerHandle;

	/** 重生定时器 */
	FTimerHandle RespawnTimerHandle;

};
