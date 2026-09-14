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

	// ══════════════════════════════════════════════════════════════════
	//  蒙太奇生命周期诊断 ★
	// ══════════════════════════════════════════════════════════════════
	//
	//  「客户的出招动画有顿挫感」是一句**主观描述**，没法直接查。
	//  这一组回调把它变成一个数字：
	//
	//      这个蒙太奇被播了多久、它的全长是多少、结束的时候是不是"被打断"
	//
	//  如果日志里反复出现「只播了 0.4 秒 / 全长 0.8 秒 / bInterrupted=true」，
	//  那就不是"网络卡"，而是**有人在动画播完之前把它停掉了** ——
	//  接下来只要找"是谁停的"就行。
	//
	//  为什么挂在这里而不是 GA 里：GA 只知道"我请求播放了"，
	//  而蒙太奇被谁停掉（自己结束、被别的蒙太奇顶掉、能力结束、
	//  预测被服务器拒绝）**它不一定知情**。AnimInstance 是所有路径的必经之地。
	//
	//  用引擎的动态多播委托而不是自己埋点，是因为它覆盖了**全部**打断来源 ——
	//  包括引擎自己发起的（比如 `OnPredictiveMontageRejected`）。

	/**
	 * 一个蒙太奇的播放记录。
	 *
	 * ⚠️ **必须每个蒙太奇一条**，不能用一个成员变量存"当前正在播的那个"。
	 *
	 * 第一版就是那么写的，结果连段时数字全是乱的 ——
	 * 因为连段切段是"先停上一段、再播下一段"，两段在**同一帧**里重叠：
	 *     StartSegment:  StopCurrentSegmentMontage()   → 上一段开始结束
	 *                    PlayMontageOrSkip(下一段)     → 记录被覆盖
	 *     上一段的 Ended 回调           → 拿到的却是**下一段**的开始时间
	 * 于是日志里出现了"实播 0.02s""实播 8.84s""全长 0.00s"这些一看就不对的值。
	 *
	 * 教训：**做诊断工具时，"被观测对象是并发存在的"这件事必须先在数据结构上体现出来。**
	 * 用一个变量记"当前"，就是在假设"同一时刻只有一个" —— 而这个假设在连段里不成立。
	 */
	struct FMontagePlayRecord
	{
		/** 开始播放的世界时间（秒） */
		float StartTime = 0.f;

		/** 蒙太奇全长（**蒙太奇秒**，不是墙钟秒） */
		float Length = 0.f;

		/**
		 * 播放速率。
		 *
		 * ⚠️ 必须记下来，否则判据在 `Rate != 1` 时全是假警报。
		 *
		 * `PlayedFor` 是**墙钟时间**，`Length` 是**蒙太奇秒**，两者只在 Rate == 1
		 * 时可以直接比。角色上配了 `HitReactPlayRate` / `DeathMontagePlayRate`
		 * （`RPG_GA_HitReact.cpp` / `RPG_GA_Death.cpp` 会把它传给 PlayMontageOrSkip）
		 * 的时候，全长 1.77s 的死亡动画实际 0.88s 播完是**完全正常**的 ——
		 * 不记 Rate 的话会把每一次正常播完都报成"被提前结束 50%"。
		 */
		float PlayRate = 1.f;
	};

	/** 蒙太奇开始播放。按蒙太奇登记一条记录 */
	UFUNCTION()
	void HandleMontageStarted(UAnimMontage* Montage);

	/**
	 * 蒙太奇结束（自然播完 / 被打断 / 被停掉都会走到这里）。
	 *
	 * @param bInterrupted true = **没播完就被停了**。这正是"顿挫"要找的那类事件
	 */
	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/**
	 * 正在播放中的蒙太奇记录。
	 *
	 * ⚠️ 值是**数组**，不是单条 —— 同一份蒙太奇资产可以同时/连续有多个实例。
	 *
	 * 只按"资产"存一条是不够的：受击蒙太奇池里只有一两个资产时，
	 * 很容易连续两次取到同一个（`ARPG_BaseCharacter::PickHitReactMontage()` 是随机取）。
	 * `Montage_Play` 的顺序是"先停同组旧蒙太奇 → 建新实例 → 广播 OnMontageStarted"，
	 * 于是旧实例的 `OnMontageEnded` 会在 `FindOrAdd` **覆盖过记录之后**才到 ——
	 * 拿到的 StartTime 是新实例的，算出来就是"实播 0.02s"。
	 *
	 * 也就是说：按资产存单条时，被吐槽的"实播 0.02s / 全长 0.00s"只是
	 * **换了个入口重新出现**。用数组 + `Ended` 时取**最早一条**（FIFO）才对得上：
	 * 旧实例先结束、配对最早的记录。
	 *
	 * 键是弱引用，蒙太奇被卸载后键会失效，但**条目不会自动清掉** ——
	 * 会残留的路径只有"UninitializeAnimation 直接删实例、不广播 Ended"，
	 * 残留量 = 每次反初始化时活着的蒙太奇数，且同一资产再播时会追加，
	 * 不构成增长风险（`NativeUninitializeAnimation` 里会整个清空）。
	 */
	TMap<TWeakObjectPtr<UAnimMontage>, TArray<FMontagePlayRecord>> MontagePlayRecords;

	/**
	 * 反初始化：解绑委托 + 清空记录。
	 *
	 * ⚠️ 必须成对解绑。`UAnimInstance::InitializeAnimation()` 的第一句就是
	 * `UninitializeAnimation()`，而**同一个实例可以被重复初始化** ——
	 * `USkeletalMeshComponent::InitializeAnimScriptInstance()` 在
	 * "实例已存在且类相同 + bForceReinit"时会直接对现有实例再调一次
	 * （`SkeletalMeshComponent.cpp`），`bForceReinit=true` 来自重新注册路径。
	 *
	 * `AddDynamic` **不查重**（`ScriptDelegates.h` 的 `AddInternal` 是无条件
	 * `InvocationList.Add`，只有 `AddUniqueDynamic` 才查重），所以重入一次就会
	 * 绑两份：每段蒙太奇打两条日志，`ensure` 在 Development 构建里还会报红。
	 */
	virtual void NativeUninitializeAnimation() override;

	/**
	 * 只有实际播放时长短于这个比例才打 Warning。
	 *
	 * 设 0.9：正常播完（算上 BlendOut 的少量偏差）不会报警，
	 * 但被截掉 10% 以上就会。调成 1.0 会让正常的混合误差也刷屏，
	 * 反而把真正的问题淹掉。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Diagnostics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MontageCutShortWarnRatio = 0.9f;

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
