// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "InputActionValue.h"
#include "RPG_PlayerController.generated.h"

class URPG_InputConfig;
class URPG_AbilitySystemComponent;
class ARPG_BaseCharacter;

/**
 * 玩家控制器 —— **所有玩家输入的入口**。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【职责边界】
 * ══════════════════════════════════════════════════════════════════════
 * 负责：绑定增强输入、把输入翻译成"输入标签"、路由到 ASC
 * 不负责：具体动作怎么执行（→ 角色）、能力内部逻辑（→ GA）
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【两类输入的走法】
 * ══════════════════════════════════════════════════════════════════════
 *
 *   移动/视角/蹲伏 —— 直接调用角色的方法
 *       按键 → PC 回调 → Character->Move() / Look() / ToggleCrouch()
 *       理由：没有冷却、没有消耗、不能被技能打断，走 GAS 是纯负担
 *
 *   能力类(攻击/闪避/跳跃/法术) —— 转成输入标签交给 ASC
 *       按键 → PC 回调 → ASC->TryActivateAbilityByInputTag(Tag) → GA
 *       理由：需要冷却、耐力消耗、前摇后摇、被状态标签阻断
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【一个回调处理所有能力输入】
 * ══════════════════════════════════════════════════════════════════════
 * 增强输入的 BindAction 支持附加参数（VarTypes...），所以：
 *
 *     for (每条映射)
 *         EIC->BindAction(Mapping.InputAction, Started, this,
 *                         &OnAbilityInputPressed, Mapping.InputTag);
 *
 * 十几种能力输入共用同一个回调，靠绑定时传入的标签区分。新增技能时
 * 只改配置资产，这个文件一行都不用动 —— 这就是两级解耦的收益。
 */
UCLASS()
class RPG_API ARPG_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARPG_PlayerController();

	// ══════════════════════════════════════════════════════════════════
	//  调试命令
	//  在游戏内控制台（~ 键）输入：
	//     RPGPrintAttributes   —— 打印玩家全部属性
	//     RPGPrintTags         —— 打印玩家当前拥有的 GameplayTag
	//  用控制台命令而不是绑定按键，是因为它不需要任何输入资产就能用，
	//  也不占用输入映射。
	// ══════════════════════════════════════════════════════════════════

	UFUNCTION(Exec)
	void RPGPrintAttributes();

	UFUNCTION(Exec)
	void RPGPrintTags();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/**
	 * 输入配置资产。在蓝图子类（BP_RPG_PlayerController）里指定。
	 * 换成另一套配置就能得到完全不同的操作方案，不需要改代码。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Input")
	TObjectPtr<URPG_InputConfig> InputConfig;

	// ── 移动类输入回调 ──

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnCrouch();
	void OnSprintStarted();
	void OnSprintCompleted();

	// ── 过渡期输入回调 ──
	// Jump 和 Sprint 最终会变成 GA（阶段 3），届时这两个回调连同绑定一起删除。
	// 详见 SetupInputComponent 里 IsInterimNativeInput 的说明。

	void OnJumpStarted();
	void OnJumpCompleted();

	// ── 能力类输入回调（所有能力共用，靠绑定时传入的标签区分）──

	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

private:
	/** 取当前控制的 RPG 角色，失败返回 nullptr */
	ARPG_BaseCharacter* GetRPGCharacter() const;

	/** 取玩家的项目扩展 ASC，失败返回 nullptr */
	URPG_AbilitySystemComponent* GetRPGAbilitySystemComponent() const;

	/** 在屏幕上同时输出一份（调试时不用切窗口看 Output Log） */
	void DebugPrint(const FString& Message, const FColor& Color = FColor::White) const;
};
