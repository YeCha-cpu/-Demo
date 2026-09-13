// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPG_DamageNumberWidget.generated.h"

class UTextBlock;

/**
 * 一个伤害飘字。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【它不是 WidgetComponent】
 * ══════════════════════════════════════════════════════════════════
 * 头顶血条是每个角色一个、长期存在的，所以用 WidgetComponent 挂在角色身上。
 * 飘字正相反：**短命、数量多、每帧都在动**。给每个飘字造一个
 * WidgetComponent 意味着给每次命中都生成一个场景组件 —— 群怪混战时
 * 开销很难看，而且组件注册/注销本身就比排版贵。
 *
 * 所以飘字走的是纯屏幕空间：
 *   1. HUD 把世界坐标投影成屏幕坐标
 *   2. 造一个本控件，用 SetPositionInViewport 摆在那个点上
 *   3. 控件自己 Tick 往上飘 + 淡出，时间到了自己 RemoveFromParent
 *
 * ══════════════════════════════════════════════════════════════════
 * 【代价：飘字不会跟着角色走】
 * ══════════════════════════════════════════════════════════════════
 * 屏幕位置只在**生成的那一瞬间**算一次。之后镜头一转，数字不会跟着
 * 原来那个世界坐标跑 —— 它会留在屏幕上原地飘完。
 *
 * 对短命（不到 1 秒）的飘字来说这个误差看不出来，是业界普遍做法。
 * 如果要做"钉在世界坐标上"的版本，得在 Tick 里每帧重新投影一次，
 * 代价是每帧 N 次 ProjectWorldToScreen。项目里留了这个口子 ——
 * 见 ARPG_HUD::bDamageNumbersTrackWorld（默认关）。
 */
UCLASS(Abstract)
class RPG_API URPG_DamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 初始化一个飘字。由 URPG_HUDWidget 在创建之后立刻调用。
	 *
	 * @param Amount        伤害数值（只用于显示）
	 * @param ScreenPosition 生成时的屏幕坐标（像素）
	 * @param InLifetime    存活时长（秒）。<=0 时用类里配的默认值
	 */
	void InitializeDamageNumber(float Amount, const FVector2D& ScreenPosition, float InLifetime);

	/** 更新屏幕坐标。仅在"飘字跟随世界坐标"模式下由 HUD 调用 */
	void UpdateScreenPosition(const FVector2D& ScreenPosition);

	/** 这次飘字已经活了多久 */
	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	float GetElapsedRatio() const { return TotalLifetime > 0.f ? FMath::Clamp(ElapsedTime / TotalLifetime, 0.f, 1.f) : 1.f; }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * 飘字刚初始化完的通知点。
	 *
	 * 在这里做 WBP 那边的表现：按数值大小换字号/颜色、
	 * 播"弹出来再缩回去"的动画、按暴击播不同的音效……
	 *
	 * @param Amount 伤害数值。WBP 可以据此决定"这个数字算大还是小"
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|UI", meta = (DisplayName = "On Damage Number Initialized"))
	void BP_OnDamageNumberInitialized(float Amount);

	// ── 绑定控件 ──

	/** 数字文本。必填 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> AmountText;

	// ══════════════════════════════════════════════════════════════════
	//  运动参数（C++ 驱动，所以 WBP 不用做动画也能看到效果）
	// ══════════════════════════════════════════════════════════════════
	// 为什么把"往上飘 + 淡出"写在 C++ 而不是让 WBP 做 UMG 动画：
	//   · 每个飘字的生命周期由 C++ 管（自己 RemoveFromParent），
	//     动画时长和生命周期分散在两个地方，改一个忘一个就会出现
	//     "数字淡完了还挂在屏幕上"或者"还没飘完就消失"
	//   · WBP 的 UMG 动画在 Widget 被销毁时不会自动停，容易留下残留状态
	// 需要更花哨的表现（缩放回弹、抖动）时，WBP 里再加动画叠加即可。

	/** 飘字总共往上飘多少像素 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Motion", meta = (ClampMin = "0.0"))
	float RiseDistance = 70.f;

	/**
	 * 上飘的缓动指数。
	 * 1 = 匀速；2~3 = 先快后慢（更像"被顶出去然后减速"）；<1 = 先慢后快。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Motion", meta = (ClampMin = "0.1"))
	float RiseEasingExponent = 2.f;

	/** 从生命的百分之几开始淡出（0.5 = 后一半时间在淡出） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadeOutStartRatio = 0.45f;

	/** 默认存活时长（秒） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI|Motion", meta = (ClampMin = "0.05"))
	float DefaultLifetime = 0.9f;

private:
	/** 生成时的屏幕坐标 —— 上飘是相对它算的 */
	FVector2D OriginPosition = FVector2D::ZeroVector;

	float ElapsedTime = 0.f;
	float TotalLifetime = 0.9f;
};
