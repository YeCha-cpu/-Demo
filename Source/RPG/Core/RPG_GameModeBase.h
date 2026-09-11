// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RPG_GameModeBase.generated.h"

/**
 * 项目默认 GameMode。
 *
 * 目前只做一件事：把各类默认类型指向本项目的 C++ 类。
 * 实际运行时通常用一个蓝图子类（BP_RPG_GameModeBase）覆盖 DefaultPawnClass
 * 指向 BP_RPG_Player —— 因为蓝图子类才能在编辑器里配置组件和资产引用。
 */
UCLASS()
class RPG_API ARPG_GameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARPG_GameModeBase();
};
