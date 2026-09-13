// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RPG_AttributeBarWidget.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Internationalization/Text.h"

void URPG_AttributeBarWidget::SetAttributeValues(float Current, float Max)
{
	// ══════════════════════════════════════════════════════════════════
	//  ⚠️ Max 可能是 0 —— 不挡的话进度条会拿到 NaN
	// ══════════════════════════════════════════════════════════════════
	// 属性集在角色初始化完成之前，MaxHealth / MaxStamina 都是构造函数里的
	// 默认值（100），但**如果 InitAttributesEffect 还没应用**，
	// 或者某个属性根本没有配 Max，就会出现 Max = 0 的窗口期。
	//
	// 除以 0 得到的 NaN 塞进 ProgressBar，表现是进度条**完全不显示**
	// 或者变成一条细白线 —— 看起来像"UI 没接上"，很容易往错的方向查。
	//
	// 按"满"处理是有意的：宁可短暂显示满血，也不要显示 NaN。
	const bool bValidMax = Max > KINDA_SMALL_NUMBER;

	// ⚠️ 先记下旧值再覆盖 —— 顺序反了的话 bIncreased 永远是 false，
	// 蓝图那边的"掉血闪红"就永远不会触发，而且不报错。
	const float PreviousValue = CachedCurrent;

	CachedCurrent = Current;
	CachedMax = Max;
	CachedPercent = bValidMax ? FMath::Clamp(Current / Max, 0.f, 1.f) : 1.f;

	const bool bIncreased = Current > PreviousValue;

	if (Bar)
	{
		// SetPercent 只接受 0~1，超范围会被引擎静默 Clamp（SProgressBar 里夹了一下，不打日志），
		// 但上面还是显式 Clamp 过一次 —— 那样 CachedPercent 这个对外暴露的读数
		// 才和条上真正显示的一致，不然 WBP 里拿它做逻辑会和眼睛看到的不符。
		Bar->SetPercent(CachedPercent);
	}

	if (ValueText)
	{
		// 数字取整显示。属性是 float（因为要支持"每秒掉 10 点"这种连续消耗），
		// 但玩家不需要看到 72.35 / 100 这种精度。
		const FText DisplayText = FText::Format(
			ValueFormat,
			FText::AsNumber(FMath::RoundToInt(Current)),
			FText::AsNumber(FMath::RoundToInt(Max)));

		ValueText->SetText(DisplayText);
	}

	// 交给蓝图补表现（闪红、低血警告、数值滚动动画……）
	BP_OnAttributeValuesChanged(Current, Max, bIncreased);
}
