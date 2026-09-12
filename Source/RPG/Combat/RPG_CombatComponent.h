// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat/RPG_CombatTypes.h"
#include "RPG_CombatComponent.generated.h"

class URPG_InputBuffer;
class URPG_AttackModuleData;

/**
 * 战斗状态组件。敌我共用，挂在 RPG_BaseCharacter 上。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它管什么，不管什么】
 * ══════════════════════════════════════════════════════════════════════
 * 管：
 *   · 输入缓存（URPG_InputBuffer 的持有者与生命周期管理）
 *   · 连段索引（当前打到第几段）
 *   · 当前攻击模组（徒手 / 近战 / 远程）
 *
 * 不管：
 *   · 能力怎么触发、动画怎么播     → GA
 *   · 伤害怎么算                   → DamageExecution
 *   · "是否正在攻击"这个状态       → GameplayTag（State.Attacking）
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么"是否攻击中"不放这里，而用标签】
 * ══════════════════════════════════════════════════════════════════════
 * 如果组件里存一个 bAttacking，那么它和 GameplayTag 就有了两份真相：
 *   · GA 被外部打断时，标签会被 GAS 自动清理，但 bAttacking 不会
 *   · 联机下标签会复制，bAttacking 不会
 * 结果就是"有时判断为攻击中、有时判断为不攻击"，而且只在特定时序下出现。
 *
 * 所以本组件只保存**纯战斗逻辑状态**（打到第几段），
 * 凡是"角色当前处于什么状态"一律以 GameplayTag 为准。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【连段索引的生命周期】
 * ══════════════════════════════════════════════════════════════════════
 *   Idle → 轻击输入 → ComboIndex = 1
 *        → 衔接窗口开启 + 缓存有货 → ComboIndex = 2
 *        → 衔接窗口关闭仍未输入 → ComboIndex = 0（重置）
 *
 * 重置时机是最容易做错的地方：如果在 GA 结束时就重置，玩家永远连不上第二段；
 * 如果一直不重置，玩家停手后下一击会直接从第 5 段开始。
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_API URPG_CombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_CombatComponent();

	// ══════════════════════════════════════════════════════════════════
	//  输入缓存
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 把一个输入意图压入缓存。
	 * 由 PlayerController（玩家）或 AI 任务（敌人）调用。
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
	void PushInputTag(FGameplayTag InputTag);

	/**
	 * 取出一个缓存输入。
	 * @return 容器为空或全部过期时返回 false
	 */
	bool ConsumeInputTag(FGameplayTag& OutTag);

	UFUNCTION(BlueprintPure, Category = "RPG|Combat")
	int32 GetBufferedInputCount() const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
	void ClearInputBuffer();

	// ══════════════════════════════════════════════════════════════════
	//  连段状态
	// ══════════════════════════════════════════════════════════════════

	/** 当前连段索引（0 表示不在连段中，1-based 对应第 N 段） */
	UFUNCTION(BlueprintPure, Category = "RPG|Combat")
	int32 GetComboIndex() const { return ComboIndex; }

	/** 推进到指定段 */
	void SetComboIndex(int32 NewIndex);

	/** 重置连段（衔接窗口关闭后仍未消耗到输入时调用） */
	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
	void ResetCombo();

	// ══════════════════════════════════════════════════════════════════
	//  攻击模组
	// ══════════════════════════════════════════════════════════════════

	/** 当前生效的攻击模组。可能为 nullptr（角色没配） */
	UFUNCTION(BlueprintPure, Category = "RPG|Combat")
	URPG_AttackModuleData* GetAttackModule() const { return CurrentModule; }

	/** 切换攻击模组（换武器时调用）。传 nullptr 会回落到默认模组 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
	void SetAttackModule(URPG_AttackModuleData* NewModule);

	// ══════════════════════════════════════════════════════════════════
	//  调试
	// ══════════════════════════════════════════════════════════════════

	/** 把当前战斗状态格式化成一行字符串（连段索引 + 缓存内容） */
	UFUNCTION(BlueprintPure, Category = "RPG|Combat")
	FString GetCombatDebugString() const;

protected:
	virtual void BeginPlay() override;

	/** 默认攻击模组。在角色蓝图里配 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Combat")
	TObjectPtr<URPG_AttackModuleData> DefaultModule;

	/** 输入缓存策略。默认栈（尊重最新意图），可改成队列对比手感 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Combat")
	ERPG_InputBufferMode BufferMode = ERPG_InputBufferMode::Stack;

	/**
	 * 缓存条目的生命周期（秒）。
	 * 太短 → 玩家在动画后半段按的下一段会丢，感觉"不跟手"
	 * 太长 → 玩家乱按后角色会自己动，感觉"失控"
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Combat", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float InputLifeTime = 0.5f;

private:
	/** 输入缓存容器。BeginPlay 时创建 */
	UPROPERTY()
	TObjectPtr<URPG_InputBuffer> InputBuffer;

	UPROPERTY()
	TObjectPtr<URPG_AttackModuleData> CurrentModule;

	/** 0 = 不在连段中；1..N = 当前处于第 N 段 */
	int32 ComboIndex = 0;

	/** 取当前世界时间。没有 World 时（CDO）返回 0 */
	float GetNow() const;
};
