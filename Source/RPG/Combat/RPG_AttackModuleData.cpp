// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RPG_AttackModuleData.h"

#include "Misc/DataValidation.h"

#include "Animation/AnimMontage.h"

#include "Core/RPG_LogChannels.h"

#if WITH_EDITOR
// 这几个只在编辑器校验里用到（Cast 判断蒙太奇上挂了哪些 Notify）。
// 放在 Combat/ 里引用 Animation/Notifies/ 是为了让"资产配错了当场标黄"，
// 代价是编译期多一层包含 —— 值。
#include "Animation/Notifies/RPG_AnimNotify_AttackEnd.h"
#include "Animation/Notifies/RPG_AnimNotifyState_AttackWindow.h"
#include "Animation/Notifies/RPG_AnimNotifyState_ComboWindow.h"
#include "Animation/Notifies/RPG_AnimNotifyState_Invulnerability.h"
#endif

const FRPG_AttackSegment* URPG_AttackModuleData::GetLightSegment(int32 Index) const
{
	if (!LightAttacks.IsValidIndex(Index))
	{
		return nullptr;
	}

	return &LightAttacks[Index];
}

int32 URPG_AttackModuleData::GetChargeLevelForTime(float ChargeTime) const
{
	int32 Result = 0;

	// 从低到高扫一遍，记录最后一个满足时间门槛的等级。
	// 用"最后一个满足的"而不是"第一个不满足的前一个"，是因为
	// Levels 数组的顺序由策划配置，不能假设它一定严格升序 ——
	// 这样写即使配成乱序也能得到正确结果（取满足条件的最高级）。
	for (int32 i = 0; i < HeavyAttack.Levels.Num(); ++i)
	{
		const FRPG_HeavyAttackLevel& Level = HeavyAttack.Levels[i];

		if (ChargeTime >= Level.RequiredChargeTime)
		{
			// 记下"索引+1"作为等级值（对外用 1-based，更符合直觉）
			Result = i + 1;
		}
	}

	return Result;
}

const FRPG_HeavyAttackLevel* URPG_AttackModuleData::GetHeavyLevel(int32 Level) const
{
	// 对外是 1-based，内部数组是 0-based
	const int32 Index = Level - 1;

	if (!HeavyAttack.Levels.IsValidIndex(Index))
	{
		return nullptr;
	}

	return &HeavyAttack.Levels[Index];
}

#if WITH_EDITOR

namespace
{
	/**
	 * 体检一整个攻击蒙太奇上的 Notify 布置。
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【为什么需要这个】
	 * ══════════════════════════════════════════════════════════════════
	 * "连招打不全""人物只是微微动了一下"这类问题，根因往往在蒙太奇资产上，
	 * 而**它们全都不报错**：
	 *
	 *   · `RPG 攻击结束` 放太靠前 → GA 在那儿就结束能力并停掉动画
	 *     → 连段打到一半没了 + 动作只播了个开头
	 *   · 漏了 `RPG 攻击判定窗口` → 挥刀没有伤害，但蒙太奇照样播
	 *   · 漏了 `RPG 连段衔接窗口` → 连招接不上，玩家以为"按键失灵"
	 *
	 * 光靠肉眼在时间轴上看，很难判断"这个 Notify 是不是太靠前了"。
	 * 所以这里把位置换算成百分比直接写进校验信息 ——
	 * 打开 Content Browser 就能看见，不用逐个双击进去看时间轴。
	 */
	/** 扫描一个蒙太奇上的关键 Notify 位置（百分比） */
	struct FNotifyLayout
	{
		bool  bHasAttackEnd = false;
		float AttackEndPercent = -1.f;

		bool  bHasAttackWindow = false;
		float AttackWindowBegin = -1.f, AttackWindowEnd = -1.f;

		bool  bHasComboWindow = false;
		float ComboWindowBegin = -1.f, ComboWindowEnd = -1.f;

		int32 TotalNotifyCount = 0;
	};

	/** 把 FNotifyLayout 渲染成一行便于比对的文字 */
	FString FormatNotifyLayout(const FNotifyLayout& Layout)
	{
		auto RangeText = [](bool bHas, float Begin, float End)
		{
			return bHas
				? FString::Printf(TEXT("%d%%→%d%%"),
					FMath::RoundToInt(Begin * 100.f), FMath::RoundToInt(End * 100.f))
				: FString(TEXT("无"));
		};

		return FString::Printf(
			TEXT("判定窗口 %s｜衔接窗口 %s｜攻击结束 %s"),
			*RangeText(Layout.bHasAttackWindow, Layout.AttackWindowBegin, Layout.AttackWindowEnd),
			*RangeText(Layout.bHasComboWindow, Layout.ComboWindowBegin, Layout.ComboWindowEnd),
			Layout.bHasAttackEnd
				? *FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Layout.AttackEndPercent * 100.f))
				: TEXT("无"));
	}

	FNotifyLayout ScanMontageNotifies(const UAnimMontage* Montage, float Length)
	{
		FNotifyLayout Layout;

		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			++Layout.TotalNotifyCount;

			// NotifyState 取两端时刻，单点 Notify 只有触发时刻
			const float Begin = Event.GetTriggerTime() / Length;
			const float End = Event.GetDuration() > 0.f
				? (Event.GetTriggerTime() + Event.GetDuration()) / Length
				: Begin;

			if (Cast<URPG_AnimNotify_AttackEnd>(Event.Notify))
			{
				Layout.bHasAttackEnd = true;
				Layout.AttackEndPercent = Begin;
			}
			else if (Cast<URPG_AnimNotifyState_AttackWindow>(Event.NotifyStateClass))
			{
				Layout.bHasAttackWindow = true;
				Layout.AttackWindowBegin = Begin;
				Layout.AttackWindowEnd = End;
			}
			else if (Cast<URPG_AnimNotifyState_ComboWindow>(Event.NotifyStateClass))
			{
				Layout.bHasComboWindow = true;
				Layout.ComboWindowBegin = Begin;
				Layout.ComboWindowEnd = End;
			}
		}

		return Layout;
	}

	/**
	 * 体检一个**攻击蒙太奇**：期望它有判定窗口，按需有衔接窗口，
	 * 且「攻击结束」要靠后。
	 */
	void ValidateAttackMontage(
		FDataValidationContext& Context,
		const UAnimMontage* Montage,
		const FText& Label,
		bool bExpectComboWindow,
		bool bWarnOnUnexpectedComboWindow)
	{
		if (!Montage)
		{
			// 蒙太奇留空是合法的（GA 会走模拟时序分支），不算问题。
			return;
		}

		const float Length = Montage->GetPlayLength();
		if (Length <= 0.f)
		{
			Context.AddWarning(FText::Format(
				NSLOCTEXT("RPG", "ZeroLengthMontage", "{0}：蒙太奇 {1} 长度为 0，动画不会播放"),
				Label, FText::FromString(Montage->GetName())));
			return;
		}

		const FNotifyLayout Layout = ScanMontageNotifies(Montage, Length);

		// ── 收集问题 ──
		// 先收集、后输出，是为了让每个蒙太奇**最多只标一条黄**，
		// 并且把位置表一并带上：一次就能看全，不用点开时间轴对数字。
		TArray<FString> Problems;

		// ① `RPG 攻击结束` 的位置 —— 连段能不能打全就看它
		if (!Layout.bHasAttackEnd)
		{
			Problems.Add(TEXT("缺少 `RPG 攻击结束` 通知 → GA 退化成「蒙太奇播完才结束」，"
				"后摇偏长。建议在 95% 处加一个"));
		}
		else if (Layout.AttackEndPercent < 0.7f)
		{
			// ★ 这就是"连招打不全 + 人物只微微动了一下"的常见元凶
			Problems.Add(FString::Printf(
				TEXT("`RPG 攻击结束` 在 %d%% 处，**太靠前** → GA 会在这一帧结束能力"
					"并停掉动画，表现为连段中途断掉、动作只播了个开头。拖到 95%% 附近"),
				FMath::RoundToInt(Layout.AttackEndPercent * 100.f)));
		}

		// ② 判定窗口 —— 没有它挥刀不掉血
		if (!Layout.bHasAttackWindow)
		{
			Problems.Add(TEXT("缺少 `RPG 攻击判定窗口` → 这一刀不会造成任何伤害"));
		}

		// ③ 衔接窗口 —— 没有它连招接不上
		if (bExpectComboWindow && !Layout.bHasComboWindow)
		{
			Problems.Add(TEXT("不是最后一段却没有 `RPG 连段衔接窗口` → 这一招接不下去"));
		}
		else if (bWarnOnUnexpectedComboWindow && Layout.bHasComboWindow)
		{
			Problems.Add(TEXT("是最后一段却有 `RPG 连段衔接窗口` → 可以无限连下去，"
				"终结技一般不该开"));
		}

		if (Problems.Num() == 0)
		{
			// 没问题的蒙太奇不输出任何东西 —— 否则每个资产都常年标黄，
			// 真正的警告就被淹没了。
			return;
		}

		FString Report = FString::Printf(TEXT("%s（%s，全长 %.2f 秒）"),
			*Label.ToString(), *Montage->GetName(), Length);

		for (const FString& Problem : Problems)
		{
			Report += FString::Printf(TEXT("\n  ✗ %s"), *Problem);
		}

		Report += FString::Printf(TEXT("\n  Notify 实际位置：%s"), *FormatNotifyLayout(Layout));

		Context.AddWarning(FText::FromString(Report));
	}

	/**
	 * 体检一个**"保持姿势"蒙太奇**（蓄力起手 / 蓄力循环）。
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【★ 它和攻击蒙太奇的规则是相反的】
	 * ══════════════════════════════════════════════════════════════════
	 * 这类蒙太奇只是"摆个姿势然后停住等玩家松手"，**一个 Notify 都不该有**。
	 *
	 * 如果误加了通知，后果不是"少打一下"而是"机制直接坏掉"：
	 *   · 加 `RPG 攻击结束` → 起手姿势刚摆好能力就结束，蓄力形同虚设
	 *   · 加 `RPG 攻击判定窗口` → 每次摆姿势都白送一次伤害
	 *
	 * 所以这里**反过来查**：发现任何本项目的通知就报问题。
	 * 早先的版本把它丢进了通用的攻击规则里，结果是"资产配对了反而被标黄"，
	 * 而照着提示改恰恰会把蓄力搞坏 —— 比不检查更糟。
	 */
	void ValidateHoldMontage(
		FDataValidationContext& Context,
		const UAnimMontage* Montage,
		const FText& Label)
	{
		if (!Montage)
		{
			// 留空合法：没有起手动画时角色直接保持当前姿势
			return;
		}

		const float Length = Montage->GetPlayLength();
		if (Length <= 0.f)
		{
			Context.AddWarning(FText::Format(
				NSLOCTEXT("RPG", "ZeroLengthHoldMontage", "{0}：蒙太奇 {1} 长度为 0，动画不会播放"),
				Label, FText::FromString(Montage->GetName())));
			return;
		}

		const FNotifyLayout Layout = ScanMontageNotifies(Montage, Length);

		TArray<FString> Problems;

		if (Layout.bHasAttackEnd)
		{
			Problems.Add(TEXT("有 `RPG 攻击结束` → 起手姿势刚摆好能力就结束了，"
				"蓄力机制形同虚设。这类蒙太奇不该有任何通知"));
		}
		if (Layout.bHasAttackWindow)
		{
			Problems.Add(TEXT("有 `RPG 攻击判定窗口` → 每次摆蓄力姿势都会白送一次伤害。"
				"这类蒙太奇不该有任何通知"));
		}
		if (Layout.bHasComboWindow)
		{
			Problems.Add(TEXT("有 `RPG 连段衔接窗口` → 蓄力中会意外接出下一段"));
		}

		if (Problems.Num() == 0)
		{
			return;
		}

		FString Report = FString::Printf(TEXT("%s（%s，全长 %.2f 秒）"),
			*Label.ToString(), *Montage->GetName(), Length);

		for (const FString& Problem : Problems)
		{
			Report += FString::Printf(TEXT("\n  ✗ %s"), *Problem);
		}

		Report += FString::Printf(TEXT("\n  该蒙太奇共 %d 个通知，正确的做法是 0 个"),
			Layout.TotalNotifyCount);

		Context.AddWarning(FText::FromString(Report));
	}
}

EDataValidationResult URPG_AttackModuleData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// ── 校验：轻击段数 ──
	// 不强制要求正好 5 段（设计上允许变体武器只有 3 段），
	// 但超过 5 段时给个提示 —— 因为输入缓存的容量按 5 段连击设计的。
	if (LightAttacks.Num() > 5)
	{
		Context.AddWarning(FText::Format(
			NSLOCTEXT("RPG", "TooManyLightSegments", "轻击段数为 {0}，超过 5 段。输入缓存容量按 5 段连击设计，多余的段可能永远连不上。"),
			FText::AsNumber(LightAttacks.Num())));
	}

	// ── 校验：倍率是否递增 ──
	// 连段的意义在于"越往后越强"，倍率不递增会让玩家没有打完整套的动力。
	// 只警告不报错 —— 也许策划就是想要一个特殊的递减套路。
	for (int32 i = 1; i < LightAttacks.Num(); ++i)
	{
		if (LightAttacks[i].DamageMultiplier < LightAttacks[i - 1].DamageMultiplier)
		{
			Context.AddWarning(FText::Format(
				NSLOCTEXT("RPG", "NonIncreasingMultiplier", "第 {0} 段的伤害倍率低于前一段 —— 连段通常应该递增倍率"),
				FText::AsNumber(i + 1)));
		}
	}

	// ── 校验：蓄力等级的时间门槛 ──
	for (int32 i = 1; i < HeavyAttack.Levels.Num(); ++i)
	{
		if (HeavyAttack.Levels[i].RequiredChargeTime <= HeavyAttack.Levels[i - 1].RequiredChargeTime)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("RPG", "BadChargeTime", "第 {0} 级的蓄力时间门槛没有比上一级更高，这一级永远达不到"),
				FText::AsNumber(i + 1)));
		}
	}

	// ── 校验：检测源与 Socket 配置是否匹配 ──
	if (TraceSource == ERPG_TraceSource::WeaponBlade)
	{
		if (BladeStartSocket.IsNone() || BladeEndSocket.IsNone())
		{
			Context.AddError(NSLOCTEXT("RPG", "MissingBladeSocket",
				"检测源是刀锋，但没有配置 BladeStartSocket / BladeEndSocket —— 轨迹检测拿不到位置"));
		}
	}
	else if (TraceSource == ERPG_TraceSource::Projectile && !ProjectileClass)
	{
		Context.AddError(NSLOCTEXT("RPG", "MissingProjectile",
			"检测源是发射物，但没有配置 ProjectileClass —— 攻击不会产生任何效果"));
	}

	// ══════════════════════════════════════════════════════════════════
	//  校验：蒙太奇上的 Notify 布置 ★
	// ══════════════════════════════════════════════════════════════════
	// 这一组检查是为了把"连招打不全""人物只是微微动了一下"这类
	// 只能靠手感描述的问题，变成打开资产就能看见的具体百分比。
	//
	// 详见文件上方 ValidateAttackMontage() 的说明。

	// ── 轻击：非最后一段必须有衔接窗口；最后一段不该有 ──
	const int32 LastLightIndex = LightAttacks.Num() - 1;
	for (int32 i = 0; i < LightAttacks.Num(); ++i)
	{
		ValidateAttackMontage(
			Context,
			LightAttacks[i].Montage,
			FText::Format(NSLOCTEXT("RPG", "LightSegmentLabel", "第 {0} 段轻击"), FText::AsNumber(i + 1)),
			/*bExpectComboWindow*/            i < LastLightIndex,
			/*bWarnOnUnexpectedComboWindow*/  i == LastLightIndex);
	}

	// ── 重击起手 / 循环：★ 用"保持姿势"规则 —— 它们不该有任何通知 ──
	// 用错规则比不检查更糟：把起手动画按攻击动作来查，会提示"缺少攻击结束"，
	// 而照着加恰恰会让蓄力机制失效。
	ValidateHoldMontage(Context, HeavyAttack.ChargeStartMontage,
		NSLOCTEXT("RPG", "ChargeStartLabel", "重击·蓄力起手"));

	ValidateHoldMontage(Context, HeavyAttack.ChargeLoopMontage,
		NSLOCTEXT("RPG", "ChargeLoopLabel", "重击·蓄力循环"));

	// ── 重击释放：是攻击动作，但没有衔接窗口（终结技）──
	for (int32 i = 0; i < HeavyAttack.Levels.Num(); ++i)
	{
		ValidateAttackMontage(
			Context,
			HeavyAttack.Levels[i].ReleaseMontage,
			FText::Format(NSLOCTEXT("RPG", "HeavyReleaseLabel", "重击·第 {0} 段释放"), FText::AsNumber(i + 1)),
			/*bExpectComboWindow*/ false, /*bWarnOnUnexpectedComboWindow*/ false);
	}

	// ── 切手技：一次性变招，同样不该有衔接窗口 ──
	ValidateAttackMontage(Context, ComboTransitionMontage,
		NSLOCTEXT("RPG", "ComboTransitionLabel", "切手技"),
		/*bExpectComboWindow*/ false, /*bWarnOnUnexpectedComboWindow*/ false);

	return Result;
}
#endif // WITH_EDITOR
