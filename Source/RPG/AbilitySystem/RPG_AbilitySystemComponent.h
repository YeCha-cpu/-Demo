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
	 * 授予一个**被动能力**（不绑定任何输入标签）。
	 *
	 * 为什么需要单独的入口：RegisterInputAbility 的映射表是
	 * "输入标签 → 能力"，而耐力恢复这类被动能力根本没有输入触发 ——
	 * 硬塞一个假的输入标签进去只会让配置表变得莫名其妙。
	 *
	 * 授予后如果该能力标记了 bActivateOnGranted，会立刻激活并一直保持激活。
	 */
	bool GivePassiveAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);

	/**
	 * 把所有"授予即激活"的被动能力重新拉起来。**重生时必调。**
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【为什么会有这个需求】
	 * ══════════════════════════════════════════════════════════════════
	 * 死亡流程会 CancelAllAbilities()，把包括 GA_StaminaRegen 在内的
	 * 所有常驻能力一起停掉。但**授予关系还在** —— 能力只是不再激活。
	 *
	 * 复活时如果不管它，就会出现一个非常隐蔽的问题：
	 * "复活后耐力永远不再恢复"，而且不报任何错。
	 * （分配点数时尤其难查：你会以为是数值配错了。）
	 *
	 * 判定条件是"CDO 上标记了 bActivateOnGranted 且当前没在激活"，
	 * 所以重复调用是安全的 —— 正在跑的被动能力不会被重启。
	 */
	void ReactivatePassiveAbilities();

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

	/**
	 * 通知"某个输入被松开"。
	 *
	 * 按住型能力（重击蓄力）靠它知道玩家什么时候松手。
	 * 实现是：从输入标签找到对应的已激活能力实例，调用它的 OnInputReleased()。
	 *
	 * 为什么不让 GA 自己监听输入？因为那要求能力知道输入层的存在，
	 * 破坏"能力不关心按键"的分层。由 ASC 充当这个翻译官更合适 ——
	 * 能力只需要知道自己"被松开了"，不需要知道是哪个键、更不需要知道
	 * 是键盘还是手柄。
	 */
	void NotifyInputReleased(FGameplayTag InputTag);

protected:
	virtual void BeginPlay() override;

	/**
	 * 能力激活失败的回调。
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【为什么必须自己接这个回调】
	 * ══════════════════════════════════════════════════════════════════
	 * GAS 的 TryActivateAbility 失败时**默认不打印任何日志** ——
	 * 它只在 CanActivateAbility 里往 OptionalRelevantTags 填一个原因标签，
	 * 然后就返回 false 了。结果就是"按键没反应，日志一片空白"
	 *
	 * AbilityFailedCallbacks 专门用来补这个盲区：它把失败原因标签带出来，
	 * 让我们能直接告诉开发者是冷却、资源不足、还是被标签阻断。
	 */
	void OnAbilityActivationFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags);

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

	/**
	 * 已经警告过"没有绑定能力"的输入标签。
	 *
	 * 这个查询会在每次按键时发生。"没绑能力"属于配置错误而不是运行时异常，
	 * 值得用 Warning 级别报出来 —— 之前用 Verbose，导致按键没反应时日志里
	 * 一片空白，完全无从下手。但玩家连打时会反复触发，所以用这个集合去重。
	 */
	TSet<FGameplayTag> WarnedInputTags;
};
