// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RPG_OverheadHealthBarComponent.h"

#include "UI/RPG_OverheadHealthBarWidget.h"

URPG_OverheadHealthBarComponent::URPG_OverheadHealthBarComponent()
{
	// Screen 空间：始终正对相机、大小不随距离变 —— 血条/名牌想要的就是这个。
	// World 空间会让血条跟着透视缩放，离远了糊成一团。
	SetWidgetSpace(EWidgetSpace::Screen);

	// 绘制尺寸（像素）。Screen 空间下这就是它在屏幕上**恒定**的大小。
	SetDrawSize(FVector2D(140.f, 20.f));

	// 每帧更新会重建 Slate 布局，白白吃 CPU。
	// 血条数值变化时自己会重绘，不需要这个开关。
	SetTickWhenOffscreen(false);

	// 默认隐藏，由 ARPG_BaseCharacter::RefreshOverheadWidgetVisibility() 在运行时决定。
	// 构造期先关掉是为了避免"生成的那一帧先闪一下血条"。
	SetVisibility(false);
}

void URPG_OverheadHealthBarComponent::InitWidget()
{
	Super::InitWidget();

	URPG_OverheadHealthBarWidget* OverheadWidget =
		Cast<URPG_OverheadHealthBarWidget>(GetUserWidgetObject());

	if (!OverheadWidget)
	{
		// Widget Class 没配是个静默失败：组件在那儿、可见性也对，就是什么都不显示。
		// 这类问题靠肉眼排查会绕很远，所以在生成时就报出来。
		//
		// ⚠️ 打 Verbose 之外的级别会有点吵（每个敌人一条），但只在**配错**时才会走
		// 到这里 —— 配好了就永远不会触发。
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 头顶血条没有设置 Widget Class（或类型不是 RPG_OverheadHealthBarWidget）—— "
			     "它不会显示任何东西。请在角色蓝图的 OverheadHealthBar 组件上指定 WBP_OverheadHealthBar"),
			*GetNameSafe(GetOwner()));
		return;
	}

	// ★ 把"我是谁的血条"显式告诉 Widget。
	// 为什么不能让它自己去问 GetOwningPlayerPawn()，见头文件里的说明 ——
	// 那条路会拿到**本地玩家**，于是所有敌人的血条都显示玩家自己的血量。
	OverheadWidget->SetOwningActor(GetOwner());
}
