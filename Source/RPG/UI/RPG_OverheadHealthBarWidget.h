// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPG_OverheadHealthBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UAbilitySystemComponent;
struct FOnAttributeChangeData;

/**
 * 头顶血条 —— 挂在其他玩家和 AI 头上。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【本地玩家自己不显示这个】
 * ══════════════════════════════════════════════════════════════════
 * 显示与否由 **ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()** 决定，
 * 判据是 `IsLocallyControlledPlayer()`（**不是** `IsLocallyControlled()` ——
 * 那个在敌人身上恒为 true，理由见那个函数的注释）。
 * 那件事放在角色上而不是这里，是因为"谁在控制我"是 Pawn 的状态，
 * 而且要在 Controller 复制到达的**第一时间**就纠正 ——
 * 放在 Widget 里会晚一帧（Widget 是在组件可见之后才被创建的），
 * 表现是"复活/进场时自己的血条闪一下"。
 *
 * ══════════════════════════════════════════════════════════════════
 * 【它怎么拿到数据：每个 Widget 自己订阅自己主人的属性】
 * ══════════════════════════════════════════════════════════════════
 * 头顶血条是**每个角色一个**的，所以每条血条订阅自己那个角色的 ASC 就够了，
 * 不需要中心化的 HUD 转发。这样：
 *   · 敌人多了也不会在 HUD 里排队
 *   · 敌人销毁时 Widget 跟着销毁，订阅自然断开
 *
 * ══════════════════════════════════════════════════════════════════
 * 【联机下它是怎么工作的】
 * ══════════════════════════════════════════════════════════════════
 * 敌人的 ASC 在客户端走的是 Minimal 那一路（项目用的是 Mixed：
 * 自己控制的角色 Full、其他角色 Minimal），但**属性值本身照样复制** ——
 * `SpawnedAttributes` 是 `COND_None`，且由 `ReplicateSubobjects` 无条件复制，
 * 跟 GE 的复制模式无关。RPG_AttributeSet 里 Health 也是 `COND_None`，
 * 注释写得很清楚："血条、队友状态、敌人血量都需要被别人看到"。
 * 所以客户端的血条读的是复制过来的值 —— 不需要服务器推任何额外的东西。
 */
UCLASS(Abstract)
class RPG_API URPG_OverheadHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * ★ 告诉这个血条"你是谁的血条"。
	 *
	 * **由 URPG_OverheadHealthBarComponent::InitWidget() 在 Widget 一被创建时调用。**
	 *
	 * 必须显式传，不能让 Widget 自己去问 —— `GetOwningPlayerPawn()` 会返回
	 * **本地玩家**而不是血条主人（原因见 RPG_OverheadHealthBarComponent.h 里的说明）。
	 * 那条路不报任何错，只是所有敌人的血条都显示玩家自己的血量。
	 */
	void SetOwningActor(AActor* InOwner);

	/** 立刻按当前属性值刷一次（构建完就能显示正确的血量，不用等下一次变化） */
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void RefreshFromOwner();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * 属性变化的回调（C++ 非动态委托，所以不是 UFUNCTION）。
	 *
	 * ⚠️ 用 `AddUObject` 绑，就得用**对应的** `Remove(Handle)` 解 ——
	 * 解绑时要拿出绑定时返回的那个 FDelegateHandle，所以下面把它存了起来。
	 * （没有叫 RemoveUObject 的 API，只有 `Remove(FDelegateHandle)` 和
	 *   `RemoveAll(对象)`；后者会误伤同一个属性上别人的订阅。）
	 */
	void HandleHealthChanged(const FOnAttributeChangeData& Data);

	/**
	 * 按属性集当前值重画血条 + 残血边沿检测。
	 *
	 * 抽出来是因为有两个入口：属性委托触发时、以及"刚接上/刚设置主人"时
	 * 主动读一次。两条路径各写一遍比例换算的话迟早会不一致。
	 */
	void RefreshBarValues();

	/**
	 * 拿到"我的主人"的 ASC。
	 *
	 * 返回 nullptr 是常态而非异常：Widget 可能在角色的 GAS 初始化之前
	 * 就被造出来了，那时 ASC 还没就位。所以 NativeTick 里会一直重试到拿到为止。
	 *
	 * ⚠️ 只认 SetOwningActor() 传进来的那个 Actor，
	 * **不用 GetOwningPlayerPawn()** —— 那是本地玩家，不是血条主人。
	 */
	UAbilitySystemComponent* ResolveOwnerASC() const;

	// ── 绑定控件 ──

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	/** 名字文本。可选 —— 敌人可以不显示名字 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	/** 低血量阈值：低于它会调 BP_OnLowHealth(true)，用来做"残血闪烁" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|UI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthThreshold = 0.3f;

	/** 残血状态变化时通知蓝图（进入/退出各一次，不是每帧调） */
	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|UI", meta = (DisplayName = "On Low Health Changed"))
	void BP_OnLowHealth(bool bIsLow);

private:
	/**
	 * 这个血条的主人。由 URPG_OverheadHealthBarComponent::InitWidget() 塞进来。
	 *
	 * WeakPtr 而不是裸指针：角色比 Widget 先死是完全可能的
	 * （敌人被销毁、关卡切换），裸指针会变成悬垂。
	 */
	TWeakObjectPtr<AActor> OwningActor;

	/** 当前订阅的 ASC。角色销毁 / ASC 换了（比如玩家 Pawn 重生成）时要能察觉 */
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;

	/** 订阅 Health 的凭据。解绑要用 —— 没有它就只能 RemoveAll，会误伤别的订阅者 */
	FDelegateHandle HealthChangedHandle;

	/** 订阅 MaxHealth 的凭据。**健康上限变化时血条也必须重画**，理由见 .cpp */
	FDelegateHandle MaxHealthChangedHandle;

	/** 上一次的残血状态，用来做边沿检测（只在跨越阈值时通知蓝图） */
	bool bWasLowHealth = false;
};
