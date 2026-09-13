// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayEffectTypes.h"
#include "RPG_HUDWidget.generated.h"

class UAbilitySystemComponent;
class URPG_AttributeBarWidget;
class URPG_CombatComponent;
class ARPG_BaseCharacter;
class UImage;
class UProgressBar;
class UTextBlock;
class UWidget;

/**
 * 主 HUD。布局：
 *
 * ```
 *  ┌────────────────────────────────────────────────────────────┐
 *  │                                                            │
 *  │                                                            │
 *  │                                                            │
 *  │  ┌──────────────┐                          ┌────────────┐  │
 *  │  │ ▮▮▮▮▮▮ 72/100│  血                      │ 招式名      │  │
 *  │  │ ▮▮▮▮▮▮ 40/100│  蓝                      │ ▮▮▮▮▯ 蓄力  │  │
 *  │  │ ▮▮▮▮▮▮ 88/100│  耐力                    └────────────┘  │
 *  │  │ 攻 25   防 18│                                          │
 *  │  └──────────────┘                                          │
 *  └────────────────────────────────────────────────────────────┘
 * ```
 *
 * ══════════════════════════════════════════════════════════════════
 * 【★ 两套数据源，因为这是两类不同的问题】
 * ══════════════════════════════════════════════════════════════════
 *
 *   属性条 / 攻防数值  →  **委托驱动**（GetGameplayAttributeValueChangeDelegate）
 *       属性有"值"，会变。委托能在变化的那一刻给出新旧值，
 *       而且客户端上属性复制到达时引擎也会自动广播 —— 正好是我们需要的时机。
 *       每帧轮询属性等于每帧读 8 个 float 再比对，纯浪费。
 *
 *   蓄力条 / 招式名 / 闪避图标 / 死亡面板  →  **每帧读 GameplayTag**
 *       这些是**布尔状态**（"现在是不是在蓄力"），不是连续值。
 *       用 RegisterGameplayTagEvent 监听也能做，但要注册 6~8 个标签、
 *       各自维护解绑，代码量翻倍；而且标签"有没有"这个查询本身就是
 *       一次哈希表查找，每帧做几次的开销可以忽略。
 *
 *       这套写法和 URPG_AnimInstanceBase 里 UpdateCombatState() 完全一致 ——
 *       项目里"C++ 读 GAS 状态再暴露给表现层"已经是这个惯例。
 *
 *   **一句话**：连续量用委托，布尔状态用轮询。这个取舍本身就是个面试话题。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【为什么要有"惰性绑定"】
 * ══════════════════════════════════════════════════════════════════
 * HUD 是在 AHUD::BeginPlay 里创建的，那一刻玩家的 PlayerState（也就是 ASC 的宿主）
 * 可能还没复制过来 —— 联机时尤其如此。绑不上**不会报错**，
 * 表现是"整条 HUD 永远不动"，非常难查。
 * 所以 NativeTick 里会一直重试到接上为止，代价只是一个空指针比较。
 */
UCLASS(Abstract)
class RPG_API URPG_HUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URPG_HUDWidget(const FObjectInitializer& ObjectInitializer);

	/** 按当前属性值整表刷一次（接上 ASC 之后立刻调用，不用等下一次变化） */
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void RefreshAllAttributes();

	/** 当前是否已经接上本地玩家的 ASC */
	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	bool IsBound() const { return BoundASC.IsValid(); }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ══════════════════════════════════════════════════════════════════
	//  属性
	// ══════════════════════════════════════════════════════════════════

	/** 8 个属性共用一个回调，在内部按 Data.Attribute 分派 */
	void HandleAttributeChanged(const FOnAttributeChangeData& Data);

	/** 读一次某个属性的当前值并刷新对应的控件 */
	void RefreshAttribute(const FGameplayAttribute& Attribute);

	/** 绑定/解绑本地玩家的 ASC。返回是否已绑定成功 */
	bool TryBindToLocalPlayer();

	void UnbindFromASC();

	// ══════════════════════════════════════════════════════════════════
	//  战斗状态（每帧读标签）
	// ══════════════════════════════════════════════════════════════════

	void UpdateCombatState(float InDeltaTime);

	/**
	 * 刷新右下角招式区。
	 *
	 * @param bCharging 本帧是否在蓄力。**作为参数传进来而不是读 bWasCharging** ——
	 *                  后者是个"必须在 UpdateCombatState 之后调用"的隐式约定，
	 *                  将来有人调整调用顺序就会静默出错（蓄力条永远不显示）。
	 */
	void UpdateSkillPanel(bool bCharging);

	/** 取本地玩家角色。取不到返回 nullptr */
	ARPG_BaseCharacter* GetLocalCharacter() const;

	/** 当前玩家的战斗组件（连段索引用）。取不到返回 nullptr */
	URPG_CombatComponent* GetLocalCombatComponent() const;

	// ══════════════════════════════════════════════════════════════════
	//  绑定控件
	//  名字即契约 —— WBP 里必须建同名控件。详见 PHASE7_UI_SETUP.md
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<URPG_AttributeBarWidget> HealthBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<URPG_AttributeBarWidget> ManaBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<URPG_AttributeBarWidget> StaminaBar;

	/** 攻击力数值 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AttackText;

	/** 防御力数值 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DefenseText;

	/** 闪避图标。只在 State.Dodging 存在时显示 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> DodgeIcon;

	// ── 右下角招式区 ──

	/** 招式区的整块容器。不在出招时整块隐藏 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SkillPanel;

	/** 蓄力进度条 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ChargeBar;

	/** 招式名文本："轻击 · 第 3 段" / "重击 · 蓄力二段" / "切手技" */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MoveNameText;

	// ── 死亡 ──

	/** 死亡面板。State.Dead 存在时显示 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DeathPanel;

	/** 重生倒计时文本 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RespawnCountdownText;

	// ══════════════════════════════════════════════════════════════════
	//  文案（在 WBP 里改，不用重新编译）
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Text")
	FText LightAttackFormat = NSLOCTEXT("RPG", "HudLightAttack", "轻击 · 第 {0} 段");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Text")
	FText HeavyAttackText = NSLOCTEXT("RPG", "HudHeavyAttack", "重击 · 蓄力中");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Text")
	FText TransitionAttackText = NSLOCTEXT("RPG", "HudTransitionAttack", "切手技");

	/** 死亡面板的倒计时格式。{0} = 剩余秒数 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Text")
	FText RespawnCountdownFormat = NSLOCTEXT("RPG", "HudRespawnCountdown", "{0} 秒后重生");

	/** 攻防数值的格式。{0} = 数值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Text")
	FText AttackValueFormat = NSLOCTEXT("RPG", "HudAttackValue", "攻 {0}");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Text")
	FText DefenseValueFormat = NSLOCTEXT("RPG", "HudDefenseValue", "防 {0}");

private:
	// ══════════════════════════════════════════════════════════════════
	//  绑定状态
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY()
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;

	/** 8 个属性的委托凭据。解绑时要用 —— RemoveAll 会误伤别人的订阅 */
	TArray<FDelegateHandle> AttributeDelegateHandles;

	/** 已经绑过的那 8 个属性，RefreshAllAttributes 按它遍历 */
	TArray<FGameplayAttribute> BoundAttributes;

	// ══════════════════════════════════════════════════════════════════
	//  战斗状态缓存
	// ══════════════════════════════════════════════════════════════════

	/**
	 * UI 侧自己累计的蓄力时间。
	 *
	 * ⚠️ 为什么不从 GA 里读：`ChargeElapsed` 是 URPG_GA_HeavyAttack 的私有成员，
	 * 而 GA 还需要 Cast 才能拿到、实例还会随能力结束被回收。
	 * 更要紧的是它每 0.1 秒才更新一次（ChargeTickInterval），
	 * 直接拿来画进度条会看出明显的台阶。
	 *
	 * 所以这里用**标签当节拍器 + UI 自己计时**：
	 *   · State.Attack.Charging 出现 → 从这里开始累计
	 *   · 标签消失（松手/被打断）    → 归零
	 *   · 段位（第几段）仍然读权威的 Lv1/Lv2/Lv3 标签，不由 UI 猜
	 *
	 * 误差只可能是一帧，而且只影响"填充条过没过刻度线"，
	 * 不会出现"UI 说二段、实际打了三段"这种事 —— 段位是标签说了算的。
	 */
	float ChargeDisplayTime = 0.f;

	/** 上一帧 State.Attack.Charging 是否存在，用来做边沿检测（判断"刚刚开始蓄力"） */
	bool bWasCharging = false;

	/** 上一帧是否已死，用来在"刚死"那一刻启动重生倒计时 */
	bool bWasDead = false;

	/** UI 侧的重生倒计时剩余秒数。由 State.Dead 出现时的 RespawnDelay 初始化 */
	float RespawnCountdown = 0.f;

	/** 上一次显示的招式名，用来避免每帧写 TextBlock（会触发排版） */
	FText LastMoveName;
};
