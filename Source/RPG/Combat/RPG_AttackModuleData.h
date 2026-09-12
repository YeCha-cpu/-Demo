// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Combat/RPG_CombatTypes.h"
#include "Combat/RPG_AttackTypes.h"
#include "RPG_AttackModuleData.generated.h"

class UAnimMontage;

/**
 * 攻击模组数据资产 —— 一套武器的完整招式表。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【这个资产存在的意义：加一把新武器不用改代码】
 * ══════════════════════════════════════════════════════════════════════
 * 徒手 / 近战 / 远程三套模组共用同一个 GA，差异全部由本资产描述：
 *   · 5 段轻击各自的蒙太奇、伤害倍率、耐力消耗
 *   · 3 段蓄力重击的门槛时间与倍率
 *   · 切手技的蒙太奇与倍率
 *   · 伤害检测源（双手 / 刀锋 / 发射物）及其 Socket 名
 *
 * 想加一把太刀？新建一个 DA_AttackModule_Katana，填 5 个蒙太奇和倍率，
 * 挂到角色上即可 —— **一行 C++ 都不用改**。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么用 DataAsset 而不是 DataTable】
 * ══════════════════════════════════════════════════════════════════════
 *   · 需要嵌套结构（重击组里含三个等级），DataTable 表达起来很别扭
 *   · 含大量软引用（蒙太奇），DataAsset 里可以直接拖拽赋值并做引用校验
 *   · 可以在编辑器里做数据校验（IsDataValid），配错了当场提示
 *   · 数量少（三五个），不需要 DataTable 的批量编辑能力
 */
UCLASS(BlueprintType)
class RPG_API URPG_AttackModuleData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ══════════════════════════════════════════════════════════════════
	//  模组标识
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Module")
	ERPG_AttackModuleType ModuleType = ERPG_AttackModuleType::Unarmed;

	/** 模组标签，如 Attack.Module.Unarmed。用于运行时查询与日志 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Module")
	FGameplayTag ModuleTag;

	/** 用于 UI 与调试的可读名称 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Module")
	FText DisplayName;

	// ══════════════════════════════════════════════════════════════════
	//  伤害检测配置
	// ══════════════════════════════════════════════════════════════════

	/** 检测源类型。三套模组的核心差异就在这里 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace")
	ERPG_TraceSource TraceSource = ERPG_TraceSource::Hands;

	/**
	 * 检测半径（厘米）。
	 * 徒手一般 25~35（拳头大小），刀锋可以设小一点更精确（15~25）。
	 * 太大容易"隔空打人"，太小会频繁穿模漏判。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace", meta = (ClampMin = "1.0"))
	float TraceRadius = 30.f;

	// ── 双手检测（徒手模组用）──

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace|Hands")
	FName LeftHandSocket = TEXT("hand_l");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace|Hands")
	FName RightHandSocket = TEXT("hand_r");

	// ── 刀锋检测（近战武器模组用）──
	// 需要在武器 Mesh 上建这两个 Socket，或者在武器 Actor 上配好

	/** 刀锋起点（靠近刀柄） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace|Blade")
	FName BladeStartSocket = TEXT("TraceStart");

	/** 刀锋终点（刀尖） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace|Blade")
	FName BladeEndSocket = TEXT("TraceEnd");

	// ── 发射物（远程模组用）──

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace|Projectile")
	TSubclassOf<AActor> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Trace|Projectile")
	FName MuzzleSocket = TEXT("Muzzle");

	// ══════════════════════════════════════════════════════════════════
	//  攻击招式表
	// ══════════════════════════════════════════════════════════════════

	/** 5 段轻击。索引 0 是起手式 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|LightAttack")
	TArray<FRPG_AttackSegment> LightAttacks;

	/** 重击（蓄力）配置 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|HeavyAttack")
	FRPG_HeavyAttackSet HeavyAttack;

	// ══════════════════════════════════════════════════════════════════
	//  切手技（轻击连段中按右键切入）
	// ══════════════════════════════════════════════════════════════════

	/** 切手技蒙太奇。留空则切手技不可用 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Transition")
	TObjectPtr<UAnimMontage> ComboTransitionMontage = nullptr;

	/** 切手技伤害倍率。介于普通轻击和满蓄力重击之间比较合理 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Transition", meta = (ClampMin = "0.0"))
	float ComboTransitionMultiplier = 2.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Transition", meta = (ClampMin = "0.0"))
	float ComboTransitionStaminaCost = 15.f;

	// ══════════════════════════════════════════════════════════════════
	//  查询
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 取第 Index 段轻击（0-based）。越界返回 nullptr。
	 * 返回指针而不是副本 —— 段结构比较大，而且调用方只读。
	 */
	const FRPG_AttackSegment* GetLightSegment(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Attack")
	int32 GetLightSegmentCount() const { return LightAttacks.Num(); }

	/**
	 * 按已蓄力的时间取对应的蓄力等级（1-based；0 表示还没到最低门槛）。
	 * 从最高级往下找，第一个满足时间门槛的就是当前等级。
	 */
	int32 GetChargeLevelForTime(float ChargeTime) const;

	/** 取第 Level 级（1-based）的蓄力配置。越界返回 nullptr */
	const FRPG_HeavyAttackLevel* GetHeavyLevel(int32 Level) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Attack")
	int32 GetHeavyLevelCount() const { return HeavyAttack.Levels.Num(); }

#if WITH_EDITOR
	/** 编辑器数据校验：配错了在 Content Browser 里直接标黄 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
