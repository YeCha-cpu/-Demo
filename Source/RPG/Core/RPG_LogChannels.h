// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * 项目日志类别 —— 按功能域拆分。
 *
 * 【为什么不用单一 LogRPG】
 * GAS 调试时日志量极大（一次能力激活可能刷十几条）。如果全部混在一个类别里，
 * 想看战斗判定就得在几百行里翻。拆开之后可以单独把某一类开到 VeryVerbose：
 *
 *     Log LogRPG_Combat VeryVerbose      // 只看战斗判定
 *     Log LogRPG_Ability VeryVerbose     // 只看能力激活/结束
 *     Log LogRPG_Ability Log             // 关回去
 *     Log LogRPG_Combat Off              // 完全静音
 *
 * 也可以写进 Config/DefaultEngine.ini 让它启动就生效：
 *     [Core.Log]
 *     LogRPG_Combat=VeryVerbose
 *
 * 【默认级别为什么是 Log 而不是 Verbose】
 * 交付版本不希望刷屏。开发期用上面的控制台命令临时打开即可。
 * 若某类需要长期开 Verbose（比如 Ability），把下面的 Log 改成 Verbose 即可。
 */

/** 通用 / 未分类 */
DECLARE_LOG_CATEGORY_EXTERN(LogRPG, Log, All);

/** GAS 层：ASC 初始化、能力激活/结束、GE 施加、标签变化 */
DECLARE_LOG_CATEGORY_EXTERN(LogRPG_Ability, Log, All);

/** 战斗层：输入缓存、连段推进、检测框命中、伤害结算 */
DECLARE_LOG_CATEGORY_EXTERN(LogRPG_Combat, Log, All);

/** AI 层：感知更新、行为树节点执行 */
DECLARE_LOG_CATEGORY_EXTERN(LogRPG_AI, Log, All);

/** 动画层：AnimNotify 广播、状态机变量更新 */
DECLARE_LOG_CATEGORY_EXTERN(LogRPG_Animation, Log, All);
