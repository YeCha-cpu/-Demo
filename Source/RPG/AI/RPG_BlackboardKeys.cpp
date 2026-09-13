// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RPG_BlackboardKeys.h"

// 定义在这里（而不是头文件里 inline）是 UE 的惯例：
// FName 的构造依赖名字池，全局构造的顺序有讲究，
// 放在 .cpp 里由模块静态初始化负责，比头文件里的 inline 变量更稳妥。
// 引擎自己定义 FBlackboard::KeySelf 用的也是这个写法。

namespace RPGBlackboardKeys
{
	const FName TargetActor(TEXT("TargetActor"));
	const FName LastKnownLocation(TEXT("LastKnownLocation"));
	const FName bTargetVisible(TEXT("bTargetVisible"));

	const FName HomeLocation(TEXT("HomeLocation"));
	const FName PatrolLocation(TEXT("PatrolLocation"));
	const FName PatrolIndex(TEXT("PatrolIndex"));

	const FName AttackRange(TEXT("AttackRange"));
	const FName bInCombat(TEXT("bInCombat"));
}
