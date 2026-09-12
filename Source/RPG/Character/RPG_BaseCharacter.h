// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "InputActionValue.h"
#include "Interfaces/RPG_AbilitySystemInterface.h"
#include "RPG_BaseCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;
class URPG_AttributeSet;
class URPG_CombatComponent;
class UGameplayAbility;
class UGameplayEffect;

/**
 * 玩家与敌人的共同基类。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【职责边界】
 * ══════════════════════════════════════════════════════════════════════
 * 负责：相机与弹簧臂、移动参数、接口实现、"角色能做的动作"（Move/Look/Sprint/Crouch）
 *
 * 不负责：
 *   · 输入绑定        → RPG_PlayerController（玩家输入一律走 PC，这是项目硬性约定）
 *   · ASC 的创建      → RPG_PlayerState（玩家）/ RPG_Enemy（敌人）
 *   · 能力的具体逻辑  → 各个 GA
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么基类实现接口，而 ASC 由子类提供】
 * ══════════════════════════════════════════════════════════════════════
 * 接口要求"能拿到 ASC"，但 ASC 在哪对玩家和敌人是不一样的：
 *     玩家 → PlayerState 上     敌人 → 自己身上
 *
 * 与其让两个子类各写一遍接口实现，不如基类实现接口、把"取 ASC"抽成一个虚函数
 * GetASCInternal()，子类只实现这一个函数。这样：
 *   · 接口实现只有一份，行为绝对一致
 *   · 新增角色类型（比如召唤物）只需实现 GetASCInternal()
 *   · IsAlive / GetRPGAttributeSet 这类派生查询自动可用
 */
UCLASS(Abstract)
class RPG_API ARPG_BaseCharacter : public ACharacter,
                                    public IAbilitySystemInterface,
                                    public IRPG_AbilitySystemInterface
{
	GENERATED_BODY()

public:
	ARPG_BaseCharacter();

	//~ Begin IAbilitySystemInterface
	/** 引擎接口：GAS 内部机制依赖它，必须实现 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ Begin IRPG_AbilitySystemInterface
	virtual URPG_AttributeSet* GetRPGAttributeSet() const override;
	virtual bool IsAlive() const override;
	//~ End IRPG_AbilitySystemInterface

	// ══════════════════════════════════════════════════════════════════
	//  角色动作
	//  由 RPG_PlayerController 绑定输入后调用。放在这里而不是 PC 里，是因为
	//  这些是"角色会做什么"，而不是"哪个键触发"——后者才是 PC 的职责。
	// ══════════════════════════════════════════════════════════════════

	/** 移动。Value 是 2D 向量：X = 左右，Y = 前后 */
	void Move(const FInputActionValue& Value);

	/** 视角。Value 是 2D 向量：X = 水平旋转，Y = 俯仰 */
	void Look(const FInputActionValue& Value);

	/**
	 * 开始奔跑（切到冲刺速度）。
	 * 阶段 3 起由 GA_Sprint 调用它并负责耐力消耗，输入不再直接调这里。
	 */
	void StartSprint();

	/** 停止奔跑，速度回落到当前姿态对应的值 */
	void StopSprint();

	/** 蹲伏/起立切换 */
	void ToggleCrouch();

	/** 角色当前的基础移动速度（行走） */
	UFUNCTION(BlueprintPure, Category = "RPG|Movement")
	float GetWalkSpeed() const { return WalkSpeed; }

	/**
	 * 战斗组件：输入缓存、连段索引、当前攻击模组。
	 * GA 通过它读取"当前该打第几段"。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|Combat")
	URPG_CombatComponent* GetCombatComponent() const { return CombatComponent; }

protected:
	virtual void BeginPlay() override;

	// ══════════════════════════════════════════════════════════════════
	//  子类必须实现的 GAS 接入点
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 返回本角色的 ASC 的虚函数。
	 *   玩家：return PlayerState->GetAbilitySystemComponent()
	 *   敌人：return 自己的 AbilitySystemComponent
	 * 允许返回 nullptr（ASC 尚未初始化时）。
	 */
	virtual UAbilitySystemComponent* GetASCInternal() const
		PURE_VIRTUAL(ARPG_BaseCharacter::GetASCInternal, return nullptr;);

	// ══════════════════════════════════════════════════════════════════
	//  组件
	// ══════════════════════════════════════════════════════════════════

	/** 弹簧臂。bUsePawnControlRotation = true，所以它跟随控制器旋转（鼠标控制视角） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** 跟随相机。挂在弹簧臂末端，自己不旋转，由弹簧臂带动 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/**
	 * 战斗状态组件（敌我共用）。
	 *
	 * 它**不依赖任何 GAS 类** —— 只持有输入缓存、连段索引、当前攻击模组。
	 * 这样设计的好处是：战斗逻辑可以脱离 GAS 单独测试，
	 * 而且将来加召唤物、可破坏物之类没有 ASC 的 Actor 也能直接复用。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Combat")
	TObjectPtr<URPG_CombatComponent> CombatComponent;

	// ══════════════════════════════════════════════════════════════════
	//  相机参数
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Camera")
	float CameraBoomLength = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Camera")
	FRotator CameraBoomOffset = FRotator(-10.f, 0.f, 0.f);

	/** 相机滞后速度。越大越"跟手"，越小越有重量感。设 0 关闭滞后 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Camera")
	float CameraLagSpeed = 15.f;

	// ══════════════════════════════════════════════════════════════════
	//  移动参数
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float WalkSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float SprintSpeed = 850.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float CrouchSpeed = 180.f;

	/** 角色转向速率（度/秒）。越大转身越快，越小越"重" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float RotationRateYaw = 540.f;

	// ══════════════════════════════════════════════════════════════════
	//  起始能力与属性
	//  在蓝图子类里配置，玩家和敌人各配各的。
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 输入标签 → 起始能力。
	 * 例：Input.Attack.Light → GA_LightAttack
	 * 敌人在自己的蓝图里配成 AI 用的能力（可以复用同一个 GA 类）。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Abilities")
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> StartupAbilities;

	/**
	 * 起始被动能力（没有输入触发，授予后自动激活并常驻）。
	 *
	 * 典型成员：GA_StaminaRegen（耐力恢复）。
	 * 它们不出现在 StartupAbilities 里，因为那张表是"输入标签 → 能力"的映射，
	 * 而被动能力没有任何输入可以映射。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupPassiveAbilities;

	/**
	 * 初始属性 GE。角色初始化时应用一次，用来设定生命/攻击/防御等数值。
	 * 放在 GE 而不是 C++ 构造函数里，是为了让数值可以被策划直接调整而不必重新编译。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Abilities")
	TSubclassOf<UGameplayEffect> InitAttributesEffect;
};
