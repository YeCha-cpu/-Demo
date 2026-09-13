// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPG_CombatTypes.generated.h"

/**
 * 战斗层的公共类型定义。
 *
 * 这个文件不依赖任何 GAS 类（只有 GameplayTag 这种轻量类型），
 * 所以 Combat/ 目录下的东西可以独立编译、独立测试 ——
 * 输入缓存容器甚至不需要 Actor 就能跑单元测试。
 */

/** 攻击模组类型。三套模组共用同一套 GA，靠这个枚举区分行为 */
UENUM(BlueprintType)
enum class ERPG_AttackModuleType : uint8
{
	/** 徒手：检测源是双手骨骼 */
	Unarmed		UMETA(DisplayName = "徒手"),

	/** 近战武器：检测源是武器上的刀锋 Socket */
	Melee		UMETA(DisplayName = "近战武器"),

	/** 远程武器：不走轨迹检测，生成发射物 */
	Ranged		UMETA(DisplayName = "远程武器"),
};

/**
 * 伤害检测源 —— 决定 WeaponTrace 采样什么位置。
 *
 * 前两种走"连续 Sweep"（防止高速挥砍时穿透目标），第三种交给发射物自己处理。
 */
UENUM(BlueprintType)
enum class ERPG_TraceSource : uint8
{
	/** 双手骨骼：采样 hand_l / hand_r 的位置，两帧之间连成线段做 Sweep */
	Hands		UMETA(DisplayName = "双手骨骼"),

	/** 武器刀锋：采样武器 Mesh 上两个 Socket 之间的线段 */
	WeaponBlade	UMETA(DisplayName = "武器刀锋"),

	/** 发射物：生成 Projectile，由它自己的碰撞回调施加伤害 */
	Projectile	UMETA(DisplayName = "发射物"),
};

/**
 * 输入缓存的取出策略。
 *
 * 单键连打时两者完全等价（都是同一个标签）；差异只在混合输入
 * （比如连按"左左右"）时体现。
 */
UENUM(BlueprintType)
enum class ERPG_InputBufferMode : uint8
{
	/** 栈：移除最后压入的 —— 尊重玩家最新的意图 */
	Stack	UMETA(DisplayName = "栈 LIFO（尊重最新意图）"),

	/** 队列：移除最早压入的 —— 按输入顺序依次执行 */
	Queue	UMETA(DisplayName = "队列 FIFO（按顺序执行）"),
};

/**
 * 一条被缓存的输入意图。
 *
 * 带时间戳是因为"输入缓存"必须有过期机制：玩家在硬直里乱按的按键，
 * 如果无限期保留，等硬直结束会突然打出一串招式，手感非常糟。
 * 生命周期太短又会"不跟手"——玩家在动画后半段按的下一段会丢。
 * 0.4~0.6 秒是动作游戏里比较舒服的区间。
 */
USTRUCT(BlueprintType)
struct FRPG_BufferedInput
{
	GENERATED_BODY()

	/** 输入标签，形如 Input.Attack.Light */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Input")
	FGameplayTag InputTag;

	/** 压入时的世界时间 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Input")
	float Timestamp = 0.f;

	/** 生命周期（秒），超过就作废 */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Input")
	float LifeTime = 0.5f;

	/** 是否过期 */
	bool IsExpired(float Now) const
	{
		return (Now - Timestamp) > LifeTime;
	}
};
