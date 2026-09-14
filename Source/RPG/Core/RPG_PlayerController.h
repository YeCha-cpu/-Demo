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
 * 玩家控制器 —— **所有玩家输入的入口**：
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

	// ── 能力类输入回调（所有能力共用，靠绑定时传入的标签区分）──

	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

	/**
	 * 把玩家的输入意图送到**服务器**的输入缓存里。★ 联机连段的关键
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【为什么必须有这个 RPC】
	 * ══════════════════════════════════════════════════════════════════
	 * `URPG_CombatComponent::InputBuffer` 是**本机运行时对象**，从来不复制
	 * （`NewObject` 建的，组件也没调 `SetIsReplicated`）。
	 * 也就是说：**只有按键那台机器的缓存里有东西**。
	 *
	 * 而连段推进靠的正是这个缓存：
	 *     `TryStartNextSegment()` → `Combat->ConsumeInputTag(...)`
	 *
	 * 于是服务器上那份轻击 GA **永远连不上第二段** ——
	 * 它的衔接窗口开的时候，缓存是空的。
	 * 第 1 段一结束服务器就 `EndAbility`，然后把这个"结束"复制给客户端：
	 *     `ClientEndAbility` → `EndAbility` → `StopCurrentSegmentMontage()`
	 *     → `Montage_Stop(0.f)`   ← **零混合时间的硬切**
	 *
	 * 表现就是"客户端出招一顿一顿的、连段打不全"，而**主机完全正常** ——
	 * 因为引擎只在 `!IsLocallyControlled()` 时才发 `ClientEndAbility`，
	 * 主机自己控制的 Pawn 收不到这条 RPC。
	 *
	 * 所以修法是：**让服务器的缓存和客户端保持一致**。
	 * 客户端按键时额外发一条 RPC，服务器收到后往**同一个缓存**里推一条。
	 * 之后服务器的连段推进就和客户端走完全相同的逻辑了。
	 *
	 * ⚠️ 用 `Reliable` 而不是 `Unreliable`：
	 * 丢一条输入就是"这一段被吞了"，表现是连段断掉 —— 玩家会感觉按键失灵。
	 * 输入事件的频率很低（人手按键），Reliable 的代价可以忽略。
	 */
	UFUNCTION(Server, Reliable)
	void Server_PushInputTag(FGameplayTag InputTag);

private:
	/** 取当前控制的 RPG 角色，失败返回 nullptr */
	ARPG_BaseCharacter* GetRPGCharacter() const;

	/** 取玩家的项目扩展 ASC，失败返回 nullptr */
	URPG_AbilitySystemComponent* GetRPGAbilitySystemComponent() const;

	/** 在屏幕上同时输出一份（调试时不用切窗口看 Output Log） */
	void DebugPrint(const FString& Message, const FColor& Color = FColor::White) const;
};
