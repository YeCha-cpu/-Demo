// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/RPG_GameModeBase.h"

#include "Character/RPG_Player.h"
#include "Core/RPG_LogChannels.h"
#include "Core/RPG_PlayerController.h"
#include "Core/RPG_PlayerState.h"

ARPG_GameModeBase::ARPG_GameModeBase()
{
	// ══════════════════════════════════════════════════════════════════
	//  默认类型指定
	// ══════════════════════════════════════════════════════════════════
	// 这里给的是 C++ 层面的默认值，保证"即使不做任何蓝图子类也能跑起来"。
	//
	// 实际项目几乎总会在蓝图子类里覆盖 DefaultPawnClass 指向 BP_RPG_Player：
	// 因为角色上要配 InputConfig、StartupAbilities、InitAttributesEffect
	// 这些资产引用，而这些只有在编辑器里才能可视化地指定。
	//
	// 值得一提：PlayerStateClass 必须是 ARPG_PlayerState，否则玩家的 ASC
	// 无处安放，GetASCInternal() 会一直返回 nullptr，症状是"技能全失效
	// 但不报任何错"。这是本项目里最隐蔽的一个配置陷阱。

	DefaultPawnClass = ARPG_Player::StaticClass();
	PlayerStateClass = ARPG_PlayerState::StaticClass();
	PlayerControllerClass = ARPG_PlayerController::StaticClass();

	UE_LOG(LogRPG, Verbose, TEXT("ARPG_GameModeBase 构造完成，默认类已指定"));
}
