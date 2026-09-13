// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "RPG_OverheadHealthBarComponent.generated.h"

/**
 * 头顶血条的 WidgetComponent。比引擎自带的多做**一件**事。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【★ 为什么必须自己写一个组件】
 * ══════════════════════════════════════════════════════════════════
 * 血条 Widget 必须知道"我是谁的血条"。直觉上应该能从组件或 outer 链问出来，
 * 但两条路都是断的：
 *
 *   ① `UUserWidget::GetOwningPlayerPawn()` 拿到的是**本地玩家**，不是血条主人。
 *      因为 `UWidgetComponent::InitWidget()` 用的是
 *      `CreateWidget(World, WidgetClass)`（WidgetComponent.cpp:1761），
 *      那个重载最终走 `CreateWidgetInstance(UGameInstance&)`，
 *      里面用 `GameInstance.GetFirstGamePlayer()` 设 PlayerContext
 *      （UserWidget.cpp:2759-2762、:2813）。
 *      → 所有敌人的血条都会显示**玩家自己的**血量和名字，而且完全不报错。
 *
 *   ② `GetTypedOuter<UWidgetComponent>()` 也拿不到 ——
 *      Widget 的 outer 是 **GameInstance**，不是这个组件（同上一处 :2813）。
 *
 * 所以只能显式传。而这个传递必须在"Widget 刚被造出来"的那一刻做 ——
 * `InitWidget()` 就是那个时刻，而且它是 virtual。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【何时被调用】
 * ══════════════════════════════════════════════════════════════════
 * `UWidgetComponent::BeginPlay()` → `InitWidget()`（WidgetComponent.cpp:751）。
 * 组件的 BeginPlay 由 `AActor::BeginPlay` 派发，早于角色的 `ReceiveBeginPlay`，
 * 所以角色自己的 `BeginPlay` 里拿到 `GetUserWidgetObject()` 时它一定已经就绪。
 * 不过本类不依赖这个顺序 —— 只要 Widget 一被创建就立刻传，后面谁什么时候读都行。
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_API URPG_OverheadHealthBarComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	URPG_OverheadHealthBarComponent();

protected:
	virtual void InitWidget() override;
};
