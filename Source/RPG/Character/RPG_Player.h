// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/RPG_BaseCharacter.h"
#include "RPG_Player.generated.h"

/**
 * 玩家角色。
 *
 * 自己**不创建** ASC —— 那由 ARPG_PlayerState 负责。
 * 本类唯一与 GAS 相关的职责是：在正确的时机建立
 * "Owner = PlayerState / Avatar = 本角色" 的关联。
 */
UCLASS()
class RPG_API ARPG_Player : public ARPG_BaseCharacter
{
	GENERATED_BODY()

public:
	ARPG_Player();

	//~ Begin APawn interface
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	//~ End APawn interface

protected:
	virtual void BeginPlay() override;
	virtual UAbilitySystemComponent* GetASCInternal() const override;

private:
	/**
	 * 建立 GAS 的 ActorInfo 关联并授予起始能力。
	 *
	 * 为什么是幂等的而不是只调用一次？
	 * 因为它的三个调用时机（PossessedBy / OnRep_PlayerState / BeginPlay）
	 * **谁先谁后是不确定的**：
	 *   · 单机下 PossessedBy 通常最早，但那时 PlayerState 可能还是空的
	 *   · 联机下 PlayerState 的复制到达时间决定 OnRep_PlayerState 的时机
	 *   · BeginPlay 在某些初始化路径下会早于 PossessedBy
	 * 与其去研究引擎内部的调用顺序（不同版本还会变），不如写成"谁来都行、
	 * 只生效一次"，这是 UE 里处理初始化的通用做法。
	 */
	void InitializeAbilitySystem();

	/** 是否已完成初始化，保证授予能力和应用初始属性只做一次 */
	bool bAbilitySystemInitialized = false;
};
