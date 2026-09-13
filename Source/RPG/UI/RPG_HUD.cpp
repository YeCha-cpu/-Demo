// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RPG_HUD.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"

#include "Core/RPG_LogChannels.h"
#include "UI/RPG_DamageNumberWidget.h"
#include "UI/RPG_HUDWidget.h"

ARPG_HUD::ARPG_HUD()
{
	// 飘字的"跟随世界坐标"模式（bDamageNumbersTrackWorld）靠 AHUD::Tick
	// 每帧重新投影位置，所以这里显式打开 tick。
	//
	// （`AActor` 默认关 tick，但 `AHUD` 的构造函数里已经打开了 ——
	//   这一句是冗余的。留着是因为它把"本类依赖 Tick"这个事实写在了代码里，
	//   将来若有人把它关掉，至少能找到一处说明为什么不该关。）
	PrimaryActorTick.bCanEverTick = true;
}

void ARPG_HUD::BeginPlay()
{
	Super::BeginPlay();

	// 只有本地玩家的 HUD 才创建 Widget。
	//
	// 引擎本身其实已经保证了这点：`SpawnDefaultHUD` 对非 ULocalPlayer 直接返回
	// （PlayerController.cpp:1180-1185），配置的 HUD 类只会在拥有它的客户端上生成
	// （GameModeBase 的 ClientSetHUD → PlayerController::ClientSetHUD）。
	// 所以 listen server 主机上**不会**为远程玩家凭空多出几个 AHUD。
	//
	// 那这道判断是冗余的吗？是 —— 但它便宜，而且把"HUD 属于本地玩家"
	// 这个语义写在了代码里，而不是靠读者知道引擎那条实现细节。
	// 单机（Standalone）下恒为 true，不影响单人开发。
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (!PC->IsLocalController())
		{
			return;
		}
	}

	if (!HUDWidgetClass)
	{
		// 不当作错误。想临时看纯画面的场景（截图、录演示）是有用的，
		// 所以留 Warning 而不是 Error —— 但一定要报，否则表现是
		// "进游戏什么都没有"，会让人去查 Widget 是不是没显示。
		UE_LOG(LogRPG_Combat, Warning,
			TEXT("[%s] 没有配置 HUDWidgetClass —— 界面上不会显示任何东西。"
			     "请在 BP_RPG_HUD 的 Class Defaults 里指定 WBP_RPG_HUD"),
			*GetName());
		return;
	}

	HUDWidget = CreateWidget<URPG_HUDWidget>(GetOwningPlayerController(), HUDWidgetClass);
	if (!HUDWidget)
	{
		UE_LOG(LogRPG_Combat, Error, TEXT("[%s] HUD Widget 创建失败：%s"),
			*GetName(), *HUDWidgetClass->GetName());
		return;
	}

	// ZOrder = 0：HUD 在底层，将来加背包/菜单时用更大的 ZOrder 盖在上面。
	// 显式写出来是为了把"层级顺序"这件事变成代码里可见的约定。
	HUDWidget->AddToViewport(/*ZOrder=*/0);

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] HUD 已创建：%s"), *GetName(), *HUDWidgetClass->GetName());
}

void ARPG_HUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 显式摘掉 Widget。
	// 关卡切换 / 玩家退出时，Slate 那边会异步销毁这些控件，
	// 而我们的 HUDWidget 指针会变成悬垂 —— 虽然 UPROPERTY 会拦住 GC，
	// 但"先自己收拾干净"是更稳的写法。
	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}

	// 飘字同理。它们本该自己把自己移除，但关卡切换时来不及走完那一帧。
	ActiveDamageNumbers.Reset();
	ActiveDamageNumberLocations.Reset();

	Super::EndPlay(EndPlayReason);
}

void ARPG_HUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 清理已经自己结束的飘字（它们会在寿命到了之后 RemoveFromParent）。
	// 放在 Tick 开头而不是 ShowDamageNumber 里：飘字的死亡和生成是两件
	// 独立的事，只在生成时清理的话，一段时间不打架就永远留着陈旧的指针。
	PruneDamageNumbers();

	if (!bDamageNumbersTrackWorld)
	{
		return;
	}

	// ── 飘字跟随世界坐标 ──
	// 每帧重新投影。开关的取舍见头文件。
	const int32 Count = FMath::Min(ActiveDamageNumbers.Num(), ActiveDamageNumberLocations.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		URPG_DamageNumberWidget* Widget = ActiveDamageNumbers[Index];
		if (!IsValid(Widget))
		{
			continue;
		}

		FVector2D ScreenPosition;
		if (ProjectToScreen(ActiveDamageNumberLocations[Index], ScreenPosition))
		{
			Widget->UpdateScreenPosition(ScreenPosition);
		}
	}
}

// ══════════════════════════════════════════════════════════════════════
//  伤害飘字
// ══════════════════════════════════════════════════════════════════════

bool ARPG_HUD::ProjectToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const
{
	if (!PlayerOwner)
	{
		return false;
	}

	// ProjectWorldLocationToScreen 的返回值含义是"这个点在不在相机前面"。
	// ⚠️ 一定要判它。不判的话，角色背后的点会被投影成一组**镜像的合法坐标**，
	// 表现是"敌人在我身后挨打，屏幕另一侧冒出数字"。
	// 这个 bug 在第三人称里特别容易被忽略（因为玩家很少注意身后）。
	return PlayerOwner->ProjectWorldLocationToScreen(WorldLocation, OutScreenPosition, /*bPlayerViewportRelative=*/false);
}

void ARPG_HUD::ShowDamageNumber(float Amount, const FVector& WorldLocation)
{
	if (!DamageNumberWidgetClass)
	{
		// 没配就当没有这个功能，静默返回。
		// 和 HUDWidgetClass 不同，飘字是纯锦上添花，缺了不影响任何玩法验证 ——
		// 每次挨打都打一条 Warning 只会刷屏。
		return;
	}

	// 相机背后不显示。理由见 ProjectToScreen。
	FVector2D ScreenPosition;
	if (!ProjectToScreen(WorldLocation, ScreenPosition))
	{
		return;
	}

	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	// ── 散开 ──
	// 同一帧多个伤害（AOE、连段多段判定）会投影到几乎同一个点上，
	// 数字完全重叠就只剩最上面那个看得见。
	// 用随机偏移散开 —— 这里不需要确定性，纯观感。
	if (DamageNumberScatterRadius > 0.f)
	{
		ScreenPosition.X += FMath::FRandRange(-DamageNumberScatterRadius, DamageNumberScatterRadius);
		ScreenPosition.Y += FMath::FRandRange(-DamageNumberScatterRadius * 0.4f, DamageNumberScatterRadius * 0.4f);
	}

	URPG_DamageNumberWidget* Widget = CreateWidget<URPG_DamageNumberWidget>(PC, DamageNumberWidgetClass);
	if (!Widget)
	{
		return;
	}

	// 先 AddToViewport 再 InitializeDamageNumber。
	//
	// 这两个顺序其实都可以（位置和对齐走的是 GameViewportSubsystem，
	// 槽位信息在控件还没上屏时也会被存下来，之后 AddToViewport 会重新捡起它），
	// 但"先入屏再摆位置"更符合直觉，也避免依赖那套内部行为。
	Widget->AddToViewport(/*ZOrder=*/10);
	Widget->InitializeDamageNumber(Amount, ScreenPosition, /*InLifetime=*/0.f);

	ActiveDamageNumbers.Add(Widget);
	ActiveDamageNumberLocations.Add(WorldLocation);

	// 超出上限就丢最旧的。
	// 群怪混战时飘字能轻松堆到上百个，每个都是一份 Slate 控件 ——
	// 不设上限的话，一场大乱斗能让帧率掉一截，而且屏幕上糊成一片也看不清。
	if (ActiveDamageNumbers.Num() > MaxDamageNumbers)
	{
		const int32 Overflow = ActiveDamageNumbers.Num() - MaxDamageNumbers;
		for (int32 Index = 0; Index < Overflow; ++Index)
		{
			if (IsValid(ActiveDamageNumbers[Index]))
			{
				ActiveDamageNumbers[Index]->RemoveFromParent();
			}
		}
		ActiveDamageNumbers.RemoveAt(0, Overflow, EAllowShrinking::No);
		ActiveDamageNumberLocations.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

void ARPG_HUD::PruneDamageNumbers()
{
	// 从后往前遍历，因为 RemoveAt 会挪动后面的元素。
	// 正序遍历 + RemoveAt 是经典的漏删 bug（相邻两个元素里会跳过一个）。
	for (int32 Index = ActiveDamageNumbers.Num() - 1; Index >= 0; --Index)
	{
		URPG_DamageNumberWidget* Widget = ActiveDamageNumbers[Index];

		// ⚠️ 判"还活着"不能只靠 IsValid()。
		//
		// 飘字寿命到了会调 RemoveFromParent() —— 但那个函数**不会**把对象标记成
		// 待回收（它只是把自己从 GameViewportSubsystem 里摘掉，
		// 见 GameViewportSubsystem 的 RemoveWidget）。
		// 而 ActiveDamageNumbers 是个 UPROPERTY 数组，持的是**强引用** ——
		// GC 永远不会回收它们。
		//
		// 两个因素加起来：`!IsValid(...)` 永远为 false，这个清理循环一个都删不掉，
		// 数组里会长期挂着几十个已经不在屏幕上的死控件，
		// 而且 bDamageNumbersTrackWorld 那条路径还会继续对它们调 UpdateScreenPosition。
		//
		// 正确的判据是"还在不在 viewport 里" —— 那才是 RemoveFromParent 真正改变的状态。
		const bool bStillAlive = IsValid(Widget) && Widget->IsInViewport();

		if (!bStillAlive)
		{
			ActiveDamageNumbers.RemoveAt(Index, 1, EAllowShrinking::No);
			ActiveDamageNumberLocations.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
}
