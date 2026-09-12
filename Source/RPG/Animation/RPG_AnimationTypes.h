// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RPG_AnimationTypes.generated.h"

/**
 * 动画层的公共类型定义。
 *
 * 和 Combat/RPG_CombatTypes.h 一样，这个文件刻意**不依赖任何 GAS 类** ——
 * 动画是表现层，让它依赖 ASC 会让"想单独测试动画状态计算"变得不可能。
 */

/**
 * 移动状态 —— 动画蓝图 Main 状态机的切换依据。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么是这四个，而不是 Idle / Walk / Run / Sprint】
 * ══════════════════════════════════════════════════════════════════════
 * 这个枚举回答的问题是"该走哪条子状态机"，不是"该播哪个动画"。
 * 走 / 跑 / 急停之间的过渡属于**同一个子状态机内部**的事，由
 * BlendSpace 的速度轴去混合 —— 把它们拆成平级状态的话：
 *   · Main 状态机要为"走→跑→冲刺"再画一圈转移线，和子状态机里的重复
 *   · 每个组合（蹲走→蹲跑、空中走→空中跑…）都要单独连线，数量爆炸
 * 所以这里只区分**结构性差异**（在地面 / 腾空 / 蹲着 / 冲刺），
 * 速度的连续变化交给混合空间。
 */
UENUM(BlueprintType)
enum class ERPG_MovementState : uint8
{
	/** 地面常态 —— 走、慢跑、站定都在这里，交给 SM_Locomotion_Walk 内部混合 */
	Grounded	UMETA(DisplayName = "地面常态"),

	/** 冲刺中（由 State.Sprinting 标签判定，不是靠速度阈值猜） */
	Sprinting	UMETA(DisplayName = "冲刺"),

	/** 腾空 —— 起跳 / 滞空 / 下落 */
	InAir		UMETA(DisplayName = "腾空"),

	/** 蹲伏 —— 蹲着走和蹲着不动都在这里 */
	Crouching	UMETA(DisplayName = "蹲伏"),
};
