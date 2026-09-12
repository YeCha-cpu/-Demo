// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RPG_AttackModuleData.h"

#include "Misc/DataValidation.h"
#include "Core/RPG_LogChannels.h"

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

	return Result;
}
#endif // WITH_EDITOR
