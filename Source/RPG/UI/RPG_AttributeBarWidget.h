// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPG_AttributeBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;

/**
 * 一条属性条（血量 / 法力 / 耐力共用）。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【它只做三件事】
 * ══════════════════════════════════════════════════════════════════
 *   ① 把"当前值 / 最大值"换算成一个 0~1 的比例填进进度条
 *   ② 顺带把数字文本刷成 "72 / 100"
 *   ③ 给蓝图一个回调，用来做"掉血时闪一下"这类表现
 *
 * 它**不知道**这个值是血量还是耐力，也不知道值是从哪来的 ——
 * 那由 URPG_HUDWidget 去接 ASC 的属性委托。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【为什么要有这个类，而不是在 WBP 里各画三条】
 * ══════════════════════════════════════════════════════════════════
 * 三条属性条的"数值 → 进度 + 文本"换算完全一样，只有颜色和图标不同。
 * 各写三遍的话，将来要加"数值平滑过渡"就得改三处 —— 而且必然漏一处。
 * 抽成基类后，WBP_HealthBar / WBP_ManaBar / WBP_StaminaBar 只是三个
 * **皮肤不同、行为相同**的子类蓝图。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【★ BindWidget 的坑：名字对不上不会报错，只是那个控件永远不更新】
 * ══════════════════════════════════════════════════════════════════
 * 标了 `BindWidget` 的成员，会在 Widget 构造时按**变量名**去 WBP 里找同名控件。
 *   · 找不到           → 编译期报错（这个还好）
 *   · 名字对但类型错   → 编译期报错
 *   · **没标 BindWidget → 运行时永远 nullptr，不报错**
 *
 * 所以下面 `Bar` / `ValueText` / `Icon` 这三个名字就是**契约**：
 * 你在 WBP 里必须建同名控件。具体见 PHASE7_UI_SETUP.md 的控件命名表。
 */
UCLASS(Abstract)
class RPG_API URPG_AttributeBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 更新这条属性条。由 URPG_HUDWidget 在属性委托触发时调用。
	 *
	 * @param Current 当前值
	 * @param Max     最大值。传 0 或负数时按"满"处理 —— 属性集还没初始化完
	 *                的那一帧 Max 可能是 0，不挡的话会除零出 NaN，
	 *                进度条会变成一条诡异的白线或者干脆不显示
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void SetAttributeValues(float Current, float Max);

	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	float GetPercent() const { return CachedPercent; }

	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	float GetCurrentValue() const { return CachedCurrent; }

	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	float GetMaxValue() const { return CachedMax; }

protected:
	/**
	 * 数值变化的通知点。**在 C++ 里改完控件之后调用**，给 WBP 补做表现的余地。
	 *
	 * 典型用法：掉血时让整条闪一下红、血量低于 30% 时把填充色换成红色、
	 * 播放"数值滚动"的动画（用 UMG 的动画 + 这里的 Current 做插值起点/终点）。
	 *
	 * 做成 BlueprintImplementableEvent 而不是让 WBP 去覆写 SetAttributeValues：
	 * 覆写的话父类的赋值逻辑就有被跳过或重复执行的风险（UMG 里没有"调 Super"的强制）。
	 * 拆成一个纯通知，父类的职责边界才守得住。
	 *
	 * @param bIncreased true = 这次是涨（法力恢复、回血）；false = 掉了
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|UI", meta = (DisplayName = "On Attribute Values Changed"))
	void BP_OnAttributeValuesChanged(float Current, float Max, bool bIncreased);

	// ══════════════════════════════════════════════════════════════════
	//  绑定控件（WBP 里必须建同名控件，名字对不上编译不过）
	// ══════════════════════════════════════════════════════════════════

	/** 填充条。必填 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> Bar;

	/**
	 * 数值文本，显示 "72 / 100"。
	 * 可选：标 OptionalBindWidget 时 WBP 里没有这个控件也能通过编译，指针为 nullptr。
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	/** 图标。可选 —— 三条属性条的图标不同，由 WBP 自己指定图片 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Icon;

	/** 数值文本的显示格式。{0} = 当前值，{1} = 最大值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI")
	FText ValueFormat = NSLOCTEXT("RPG", "AttributeValueFormat", "{0} / {1}");

private:
	float CachedCurrent = 0.f;
	float CachedMax = 0.f;
	float CachedPercent = 0.f;
};
