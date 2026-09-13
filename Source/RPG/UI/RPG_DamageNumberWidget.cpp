// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RPG_DamageNumberWidget.h"

#include "Components/TextBlock.h"

void URPG_DamageNumberWidget::InitializeDamageNumber(
	float Amount, const FVector2D& ScreenPosition, float InLifetime)
{
	OriginPosition = ScreenPosition;
	TotalLifetime = InLifetime > 0.f ? InLifetime : DefaultLifetime;
	ElapsedTime = 0.f;

	if (AmountText)
	{
		// 取整显示。属性是 float（要支持"每秒掉 10 点"），
		// 但飘字上出现小数会很怪。
		AmountText->SetText(FText::AsNumber(FMath::RoundToInt(Amount)));
	}

	// 摆到命中点上。
	// bRemoveDPIScale = true：我们算出来的屏幕坐标是**像素**，
	// 而 UMG 的位置是 Slate 单位（受 DPI 缩放影响）。
	// 不除 DPI 的话，在 125%/150% 缩放的显示器上飘字会偏出去一大截 ——
	// 而且只在"别人的机器上"复现，自己开发时完全正常。
	SetPositionInViewport(ScreenPosition, /*bRemoveDPIScale=*/true);

	// 轴心摆在正中间，这样数字是"以命中点为中心"冒出来的，
	// 而不是以左上角对齐 —— 后者在数字位数变化时会左右跳。
	SetAlignmentInViewport(FVector2D(0.5f, 0.5f));

	// 初始不透明。之后由 Tick 逐渐降到 0。
	SetRenderOpacity(1.f);

	BP_OnDamageNumberInitialized(Amount);
}

void URPG_DamageNumberWidget::UpdateScreenPosition(const FVector2D& ScreenPosition)
{
	// 只有"飘字跟随世界坐标"模式下才会被调到。
	// 更新原点而不是当前位置 —— 上飘的偏移量是相对原点算的，
	// 直接改当前位置会把已经飘出去的位移吃掉。
	OriginPosition = ScreenPosition;
}

void URPG_DamageNumberWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	ElapsedTime += InDeltaTime;

	if (ElapsedTime >= TotalLifetime)
	{
		// 自己送走自己。
		//
		// 让飘字管自己的生命周期，而不是由 HUD 维护一个"飘字列表 + 定时清理"：
		// 后者要在 HUD 里额外维护容器，还得处理"角色中途死了/关卡切换了
		// 列表里的指针已经失效"这类情况。谁生的谁管，边界最清楚。
		RemoveFromParent();
		return;
	}

	const float Ratio = FMath::Clamp(ElapsedTime / TotalLifetime, 0.f, 1.f);

	// ── 上飘：缓动 ──
	// pow(Ratio, Exponent) 在 Exponent > 1 时是"先快后慢"，
	// 观感上像被击打顶出去再减速，比匀速自然得多。
	const float RiseRatio = FMath::Pow(Ratio, RiseEasingExponent);
	const float OffsetY = -RiseDistance * RiseRatio;   // 屏幕坐标 Y 向下为正，所以要取负

	// 用 RenderTranslation 而不是改 Canvas 槽位：
	// 前者是纯渲染偏移，不触发布局重算，几十个飘字同时飘也不会有性能问题。
	SetRenderTranslation(FVector2D(0.f, OffsetY));

	// ── 淡出 ──
	if (Ratio > FadeOutStartRatio)
	{
		const float FadeRatio = (Ratio - FadeOutStartRatio) / FMath::Max(1.f - FadeOutStartRatio, KINDA_SMALL_NUMBER);
		SetRenderOpacity(1.f - FadeRatio);
	}
	else
	{
		SetRenderOpacity(1.f);
	}
}
