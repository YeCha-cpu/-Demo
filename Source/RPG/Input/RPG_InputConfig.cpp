// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/RPG_InputConfig.h"

#include "InputAction.h"

bool URPG_InputConfig::FindInputTagForAction(const UInputAction* Action, FGameplayTag& OutTag) const
{
	if (!Action) return false;

	for (const FRPG_InputActionMapping& Mapping : AbilityInputMappings)
	{
		// 用指针相等判断，而不是名字比较——InputAction 是资产，指针就是它的身份。
		// 名字比较会在有同名资产时出错，而且慢。
		if (Mapping.InputAction == Action && Mapping.InputTag.IsValid())
		{
			OutTag = Mapping.InputTag;
			return true;
		}
	}

	return false;
}

UInputAction* URPG_InputConfig::FindActionForInputTag(FGameplayTag InputTag) const
{
	if (!InputTag.IsValid()) return nullptr;

	for (const FRPG_InputActionMapping& Mapping : AbilityInputMappings)
	{
		if (Mapping.InputTag == InputTag)
		{
			return Mapping.InputAction;
		}
	}

	return nullptr;
}

void URPG_InputConfig::GetAllAbilityActions(TArray<const UInputAction*>& OutActions) const
{
	OutActions.Reset();

	for (const FRPG_InputActionMapping& Mapping : AbilityInputMappings)
	{
		// 只收集"动作和标签都配好了"的条目。
		// 配了一半的条目是配置错误，但在这里静默跳过而不是报错——
		// 因为编辑资产的过程中必然会经过"填了一半"的状态，报错会刷屏。
		if (Mapping.InputAction && Mapping.InputTag.IsValid())
		{
			OutActions.Add(Mapping.InputAction);
		}
	}
}
