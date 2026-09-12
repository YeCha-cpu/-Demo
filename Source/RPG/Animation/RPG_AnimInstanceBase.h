// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/RPG_AnimationTypes.h"
#include "Combat/RPG_CombatTypes.h"
#include "RPG_AnimInstanceBase.generated.h"

class ARPG_BaseCharacter;
class UCharacterMovementComponent;

/**
 * 玩家与敌人动画蓝图的共同父类。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它在架构里的位置：动画的"数据来源"层】
 * ══════════════════════════════════════════════════════════════════════
 *
 *      CharacterMovement / GameplayTag          ← 真相源
 *                  ↓  每帧读一次
 *      URPG_AnimInstanceBase（本类）             ← 翻译成动画能懂的布尔量
 *                  ↓
 *      ABP_RPG_Base 的 AnimGraph                ← 状态机 + 混合空间
 *
 * 关键约定：**AnimGraph 里不写任何计算**。
 * 所有"当前速度多少""在不在攻击"的判断都在 C++ 里做完，
 * 蓝图只负责连线。这么分的原因：
 *   · 蓝图里的计算没法做代码审查、没法 diff、改错了查不出来
 *   · 状态逻辑和表现连线混在一起时，"动画不对"到底是逻辑错了还是线接错了
 *     根本分不清
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么状态一律从 GameplayTag 读，而不是让 GA 推变量】
 * ══════════════════════════════════════════════════════════════════════
 * 让 GA 在激活时设置 bIsAttacking = true、结束时设回 false，看起来更直接，
 * 但那条路上有三个必踩的坑：
 *
 *   1. **被打断时不会复位**。GA 被 CancelAbilitiesWithTag 强行取消时
 *      走的是 EndAbility 的另一条分支，手写的复位语句很容易漏。
 *      症状是"角色卡在攻击姿势"，而且只在被特定招式打断时出现。
 *   2. **联机下两边不同步**。客户端预测激活、服务器拒绝，变量就永远错了。
 *   3. **两份真相**。标签已经是真相源了，再存一份 bool 就要维护一致性。
 *
 * 读标签则天然免疫这三点：标签的生命周期由 GAS 托管，
 * 能力结束、被打断、角色死亡清空能力，标签都会被自动摘干净。
 *
 * 代价是每帧要做几次标签查询 —— 都是哈希表查找，量级可忽略。
 */
UCLASS()
class RPG_API URPG_AnimInstanceBase : public UAnimInstance
{
	GENERATED_BODY()

public:
	URPG_AnimInstanceBase();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** 当前动画对象所属的角色。可能为 nullptr（编辑器预览窗口里就没有角色） */
	UFUNCTION(BlueprintPure, Category = "RPG|Animation")
	ARPG_BaseCharacter* GetRPGCharacter() const { return OwnerCharacter.Get(); }

	/** 把当前动画状态格式化成一行字符串，供日志排查用 */
	UFUNCTION(BlueprintPure, Category = "RPG|Animation")
	FString GetAnimationDebugString() const;

protected:
	// ══════════════════════════════════════════════════════════════════
	//  移动（喂给状态机与混合空间）
	// ══════════════════════════════════════════════════════════════════

	/** 水平速度（cm/s）。已剔除垂直分量 —— 详见 .cpp 里的说明 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	float Speed = 0.f;

	/** 当前姿态下的最大速度。会随冲刺 / 蹲伏变化 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	float MaxSpeed = 0.f;

	/**
	 * 归一化速度 = Speed / MaxSpeed，恒在 0~1。
	 *
	 * ★ 混合空间横轴请用这个，不要用 Speed。
	 * 因为本工程的 MaxSpeed 会变（走 300 / 冲刺 850 / 蹲 180），
	 * 用固定阈值的 Speed 轴需要为每种姿态各做一个混合空间；
	 * 用归一化轴则一个混合空间三种速度通用，而且以后改数值不用重做动画。
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	float SpeedRatio = 0.f;

	/**
	 * 移动方向（度）。
	 * 0 = 正前方，+90 = 正右，-90 = 正左，±180 = 正后。
	 *
	 * 方向轴的混合空间就是靠它做"八向移动"——
	 * 角色朝前跑但玩家按了左，Direction 就是 -90，混合出侧身跑动画。
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	float Direction = 0.f;

	/** 垂直速度（cm/s）。正数上升、负数下落，用于区分起跳 / 滞空 / 落地 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	float VerticalVelocity = 0.f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	bool bIsInAir = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	bool bIsCrouching = false;

	/** Main 状态机的切换依据。四个值分别对应四条子状态机 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Locomotion")
	ERPG_MovementState MovementState = ERPG_MovementState::Grounded;

	// ══════════════════════════════════════════════════════════════════
	//  战斗（全部来自 GameplayTag）
	// ══════════════════════════════════════════════════════════════════

	/** 攻击中。轻击连段与重击（含蓄力）都会让它为真 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	bool bIsDodging = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	bool bIsSprinting = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	bool bIsCharging = false;

	/** 蓄力段位 0~3。0 = 没在蓄力，或还在起手阶段没到第一段门槛 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	int32 ChargeLevel = 0;

	/** 无敌帧中。翻滚的无敌窗口、复活保护等都挂这个标签 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	bool bIsInvulnerable = false;

	/** 已死亡。死亡动画分支用它，而不是查血量 —— 血量可能被治疗拉回来 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	bool bIsDead = false;

	/** 当前攻击模组。决定用哪套攻击动画（徒手 / 近战 / 远程） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	ERPG_AttackModuleType AttackModuleType = ERPG_AttackModuleType::Unarmed;

	/** 当前连段索引。0 = 不在连段中，1..5 = 第几段。可用于调试显示 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "RPG|Combat")
	int32 ComboIndex = 0;

	// ══════════════════════════════════════════════════════════════════
	//  阈值配置
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 判定为"站定"的速度上限（cm/s）。
	 * 别设成 0 —— 角色实际停下时速度是逐渐衰减的，永远差一点点才到 0，
	 * 结果就是 Idle 永远进不去，脚底一直在滑。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Locomotion", meta = (ClampMin = "0.0"))
	float IdleSpeedThreshold = 10.f;

private:
	/** 所属角色。弱引用 —— AnimInstance 生命周期可能比角色长（编辑器预览） */
	TWeakObjectPtr<ARPG_BaseCharacter> OwnerCharacter;

	/** 缓存角色引用。InitializeAnimation 和 Update 都会调，拿不到就保持空 */
	void CacheOwnerCharacter();

	/** 从 CharacterMovement 读位移数据 */
	void UpdateLocomotion(float DeltaSeconds);

	/** 从 GameplayTag 与 CombatComponent 读战斗状态 */
	void UpdateCombatState();
};
