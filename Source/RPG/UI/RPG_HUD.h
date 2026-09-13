// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RPG_HUD.generated.h"

class URPG_HUDWidget;
class URPG_DamageNumberWidget;

/**
 * 本地玩家的 HUD。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【为什么是 AHUD，而不是在 PlayerController 里 CreateWidget】
 * ══════════════════════════════════════════════════════════════════
 * 两种写法都能跑，选 AHUD 是因为职责边界更干净：
 *
 *   · **HUD 是"本地表现"**。AHUD 天生就是"每个本地玩家一份"的语义，
 *     引擎负责在正确的时机创建和销毁它。放在 PlayerController 里的话，
 *     那片代码要自己处理"这台机器上哪些 PlayerController 是本地的"
 *     （listen server 主机上会有多个 PlayerController，只有一个是自己的）。
 *
 *   · **PlayerController 已经有一件正事要做**：把输入翻译成能力激活。
 *     再塞进 UI 的创建、销毁、输入模式切换，那个类会变成什么都往里装的抽屉。
 *
 *   · 引擎自带的 `HUDClass` 配置位本来就为此准备着 —— 在 GameMode 里指定，
 *     零代码切换。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【它同时是飘字的落点】
 * ══════════════════════════════════════════════════════════════════
 * 角色那边把"谁挨了多少伤害、在哪个世界坐标"通过 NetMulticast 广播到各端，
 * 各端再由**自己那个** AHUD 把它变成屏幕上的数字。
 * 这一层中转是必要的：世界坐标 → 屏幕坐标要相机信息，而相机是每个客户端各自的。
 */
UCLASS()
class RPG_API ARPG_HUD : public AHUD
{
	GENERATED_BODY()

public:
	ARPG_HUD();

	/**
	 * 在指定的世界坐标冒一个伤害数字。
	 *
	 * 由 ARPG_BaseCharacter::Multicast_ShowDamageNumber 在每个客户端上调用。
	 *
	 * @param Amount         伤害数值
	 * @param WorldLocation  冒出来的位置（通常是命中点）
	 */
	void ShowDamageNumber(float Amount, const FVector& WorldLocation);

	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	URPG_HUDWidget* GetHUDWidget() const { return HUDWidget; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 主 HUD 的 Widget 类。
	 *
	 * 在 BP_RPG_HUD 的 Class Defaults 里指定 WBP_RPG_HUD。
	 * 留空的话 HUD 什么都不会显示（会打一条 Warning）——
	 * 保留这个空状态是有用的：想单独看画面的时候不用改代码。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI")
	TSubclassOf<URPG_HUDWidget> HUDWidgetClass;

	/** 伤害飘字的 Widget 类 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI")
	TSubclassOf<URPG_DamageNumberWidget> DamageNumberWidgetClass;

	/**
	 * 飘字是否每帧跟随世界坐标。
	 *
	 * false（默认）：屏幕坐标只在生成时算一次，之后数字在屏幕上原地飘完。
	 *   这是业界普遍做法 —— 飘字活不到 1 秒，镜头造成的偏移看不出来，
	 *   而且省掉每帧 N 次投影。
	 *
	 * true：每帧重新投影。镜头快速转动时数字会"钉"在敌人身上，
	 *   代价是同时存在几十个飘字时每帧几十次 ProjectWorldToScreen。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI")
	bool bDamageNumbersTrackWorld = false;

	/** 飘字最多同时存在多少个。超过时**最旧的先消失**，防止群怪混战刷屏 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI", meta = (ClampMin = "1"))
	int32 MaxDamageNumbers = 32;

	/** 同一帧内多个飘字的位置散开半径（像素），避免数字完全重叠看不清 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI", meta = (ClampMin = "0.0"))
	float DamageNumberScatterRadius = 22.f;

private:
	/** 把世界坐标投影成屏幕坐标。在相机背后时返回 false */
	bool ProjectToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const;

	void PruneDamageNumbers();

	UPROPERTY()
	TObjectPtr<URPG_HUDWidget> HUDWidget;

	/**
	 * 活着的飘字。
	 *
	 * ⚠️ 用 TObjectPtr 的数组而不是裸指针：飘字是**自己**在 Tick 末尾
	 * 调 RemoveFromParent 的，裸指针会在下一帧变成悬垂。
	 *
	 * ⚠️ 但**不能**靠 `IsValid()` 判断它还在不在 ——
	 * RemoveFromParent 不会把对象标记成待回收，而 UPROPERTY 数组持的是强引用，
	 * GC 也永远收不走它。判据必须是 `IsInViewport()`，
	 * 详见 RPG_HUD.cpp 里 PruneDamageNumbers 的说明。
	 */
	UPROPERTY()
	TArray<TObjectPtr<URPG_DamageNumberWidget>> ActiveDamageNumbers;

	/** 和 ActiveDamageNumbers 一一对应的世界坐标，仅在 bDamageNumbersTrackWorld 时用得上 */
	TArray<FVector> ActiveDamageNumberLocations;
};
