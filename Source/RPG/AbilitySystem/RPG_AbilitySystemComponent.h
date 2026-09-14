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
 * UE 5.8 的 FGameplayAbilitySpec 只有 int32 `InputID`，**没有** `FGameplayTag InputTag`。
 *
 * ⚠️ 措辞要准：不是"引擎早期有、后来移除了" —— 引擎**从来没提供过** InputTag 版本。
 * 网上教程里那种写法来自 Lyra 自己的 `FLyraAbilitySet_GameplayAbility`，
 * 那是示例项目的扩展，不是引擎接口。
 *
 * 基于 int32 的编号方案有两个问题：
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

	// ══════════════════════════════════════════════════════════════════
	//  输入标签 → 能力，分成**两步**
	// ══════════════════════════════════════════════════════════════════
	//
	//  ★ 为什么必须是两步：客户端也要拿到这张映射表。
	//
	//  原先只有一个 RegisterInputAbilities（登记 + 授予一起做），而它只在
	//  `HasAuthority()` 分支里被调用 —— 于是**客户端的映射表永远是空的**，
	//  按任何键都会走到"这个输入标签没有绑定任何能力"。
	//
	//  而这张表其实是**纯配置数据**（来自角色上的 StartupAbilities），
	//  不是运行时状态，两端都应该有。真正只能服务器做的是"授予"那一步。
	//
	//  拆成两步之后，角色初始化代码里的分工一眼可见：
	//      两端都调：RegisterInputAbilityMappings()
	//      仅服务器：GrantInputAbilities()
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 登记"输入标签 → 能力类"的映射。**服务器和客户端都要调。**
	 *
	 * 它不授予任何能力，只是把配置记下来 —— 客户端之后要靠它
	 * 反查复制过来的能力 Spec 属于哪个输入标签（见 RebuildInputTagHandleMap）。
	 */
	void RegisterInputAbilityMappings(const TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>& InMappings);

	/**
	 * 按已登记的映射逐个授予能力。**只能服务器调。**
	 *
	 * 客户端的 AbilitySpec 是复制过来的，自己再授予一次会变成两份 ——
	 * 症状是"放一次技能触发两遍效果"，而且只在联机时出现。
	 */
	void GrantInputAbilities(int32 Level = 1);

	/**
	 * 注册一个输入标签对应的GA，并立即授予。
	 *
	 * @param InputTag     输入标签，如 Input.Attack.Light
	 * @param AbilityClass 要授予的能力类
	 * @param Level        能力等级（影响 GE 的 Level 与 SetByCaller 缩放）
	 * @return 是否注册成功
	 */
	bool RegisterInputAbility(FGameplayTag InputTag, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);

	/**
	 * 从**已复制的**能力 Spec 重建"输入标签 → SpecHandle"缓存。
	 *
	 * 服务器上这张缓存由 `RegisterInputAbility` 在授予时直接填。
	 * 客户端拿不到那个时机（能力是复制过来的），只能在 `OnRep_ActivateAbilities`
	 * 里按类反查着重建。
	 *
	 * @return 是否至少重建出一条（映射表为空或能力还没复制到时会返回 false）
	 */
	bool RebuildInputTagHandleMap();

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
	 * 能力列表复制到达时重建输入映射缓存。**客户端走这条路。**
	 *
	 * 为什么不能用别的时机：客户端的能力 Spec 是 `COND_ReplayOrOwner`
	 * 复制过来的，到达时间不确定。`OnRep_ActivateAbilities` 是引擎唯一
	 * 保证"Spec 列表变了"的通知点，而且基类实现里已经处理了
	 * "Spec 还没复制好就 0.5 秒后重试"这种情况。
	 *
	 * ⚠️ 必须调 `Super::` —— 基类那里还负责跑"服务器已激活但客户端
	 * 当时还没收到能力"的补激活队列，漏掉它会让那类激活永远丢失。
	 */
	// ⚠️ 这里**不能**再写 `UFUNCTION()` —— 实测 UHT 会直接报错：
	//   "Override of UFUNCTION 'OnRep_ActivateAbilities' in parent
	//    'UAbilitySystemComponent' cannot have a UFUNCTION() declaration
	//    above it; it will use the same parameters as the original declaration."
	// 覆盖父类的反射函数时，反射信息沿用父类那份，子类只写 virtual ... override。
	virtual void OnRep_ActivateAbilities() override;

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
	 * 已经警告过"**配置里没有**这个输入标签"的键。
	 *
	 * 这个查询会在每次按键时发生。"没绑能力"属于配置错误而不是运行时异常，
	 * 值得用 Warning 级别报出来 —— 之前用 Verbose，导致按键没反应时日志里
	 * 一片空白，完全无从下手。但玩家连打时会反复触发，所以用这个集合去重。
	 */
	TSet<FGameplayTag> WarnedUnmappedTags;

	/**
	 * 已经警告过"**配置里有**但拿不到能力实例"的键。
	 *
	 * ★ 和上面那个**必须分开**。
	 *
	 * 两种情况的排查方向完全相反（改配置 vs 等复制/查授权），
	 * 但如果共用一个去重集合，就会出现：
	 * 客户端进场时先因为"能力还没复制到"报了一次 → 这个键被永久标记 →
	 * 之后**真的配置错了**也再也不会报出来。
	 * 那正好把这次修复特意做的诊断拆分又抹掉了。
	 */
	TSet<FGameplayTag> WarnedUnresolvedTags;
};
