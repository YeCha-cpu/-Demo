// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "RPG_AbilitySystemComponent.generated.h"

class UGameplayAbility;

/**
 * 项目扩展的 ASC。敌我共用同一个类。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它解决什么问题：输入 → 能力的两级解耦】
 * ══════════════════════════════════════════════════════════════════════
 * 很多项目的写法是：按键事件里直接 Cast 到具体的 GA 类去激活。结果是：
 *   · 想改键位 → 要动代码
 *   · 想让一个技能被两个键触发 → 要写两遍
 *   · 想做按键重绑定 → 无从下手
 *
 * 这里改成两级：
 *
 *     物理按键  ──①──▶  Input.* 标签  ──②──▶  Ability.* 能力
 *              (IMC 配置)         (本类的映射表)
 *
 * ① 在 InputMappingContext 资产里配，改键位不碰代码。
 * ② 在角色初始化时注册，改技能配置不碰输入代码。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么不用引擎自带的 InputID 机制】
 * ══════════════════════════════════════════════════════════════════════
 * UE 5.8 的 FGameplayAbilitySpec 只有 int32 InputID，**没有** FGameplayTag InputTag
 * （早期版本有，后来移除了）。基于 int32 的编号方案有两个问题：
 *   · 编号和能力的对应关系藏在配置里，加一个技能要小心不要撞号
 *   · 调试时日志里只有 "InputID 3"，看不出是什么
 * 用 GameplayTag 则自带语义，日志里直接显示 "Input.Attack.Light"。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么用 FGameplayAbilitySpecHandle 而不是 AbilityClass 来激活】
 * ══════════════════════════════════════════════════════════════════════
 * 同一个 GA 类可能被授予多次（不同等级、不同来源）。用 Handle 精确指向
 * "这一次授予"，不会误激活另一个实例。
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_API URPG_AbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	URPG_AbilitySystemComponent();

	/**
	 * 注册一个输入标签对应的GA，并立即授予。
	 *
	 * @param InputTag     输入标签，如 Input.Attack.Light
	 * @param AbilityClass 要授予的能力类
	 * @param Level        能力等级（影响 GE 的 Level 与 SetByCaller 缩放）
	 * @return 是否注册成功
	 */
	bool RegisterInputAbility(FGameplayTag InputTag, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);

	/** 批量注册GA（角色初始化时调用一次） */
	void RegisterInputAbilities(const TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>& InMappings);

	/**
	 * 按输入标签尝试激活能力。
	 *
	 * 这是 PlayerController 和 AI 共用的入口——两边都走同一条路径，
	 * 行为完全一致，不会出现"玩家能放、AI 放不出来"的诡异问题。
	 *
	 * @return 是否成功激活（失败原因通常是：未注册、冷却中、被标签阻断、正在被其他能力占用）
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|AbilitySystem")
	bool TryActivateAbilityByInputTag(FGameplayTag InputTag);

	/** 查询某个输入标签当前是否绑定了能力 */
	UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem")
	bool HasAbilityForInputTag(FGameplayTag InputTag) const;

protected:
	/**
	 * 输入标签 → 能力类 的映射表。只用于注册阶段，运行期激活走下面的 Handle 表。
	 */
	UPROPERTY()
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> InputTagToAbilityClass;

	/**
	 * 输入标签 → 已授予能力的 SpecHandle 的映射表。
	 * 注册后立即填充，是运行期查找的唯一依据。
	 */
	TMap<FGameplayTag, FGameplayAbilitySpecHandle> InputTagToSpecHandle;
};
