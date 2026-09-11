// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/RPG_AbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "Core/RPG_LogChannels.h"

URPG_AbilitySystemComponent::URPG_AbilitySystemComponent()
{
	// ── 开启复制 ──
	// 少这一句，联机下整个 GAS 等于失效：
	//   · 能力激活不会同步到服务器（客户端放技能，服务器不知道）
	//   · GE 不会复制到其他客户端（别人看不到你身上的 Buff）
	//   · GameplayCue 不会在其他客户端播放（特效只有自己看得见）
	// 而且这三件事都不会报错，只是"没反应"。
	SetIsReplicated(true);

	// ── 复制模式 ──
	// Mixed = 自己控制的角色走 Full，其他角色走 Minimal：
	//   Full    —— 客户端拿到完整的 GE 信息，这样才能做本地预测
	//   Minimal —— 只同步 GameplayCue 与标签，省带宽
	//             （别人身上的 Buff 具体是几层、还剩几秒，我不需要知道）
	// 这是玩家类 ASC 的标准配置。
	//
	// 敌人（AI 控制）在 Mixed 下走 Minimal，对 MVP 够用。
	// 若将来要让客户端对敌人做预测（比如"附身"机制），再单独调成 Full。
	SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

bool URPG_AbilitySystemComponent::RegisterInputAbility(
	FGameplayTag InputTag,
	TSubclassOf<UGameplayAbility> AbilityClass,
	int32 Level)
{
	if (!InputTag.IsValid())
	{
		UE_LOG(LogRPG_Ability, Warning, TEXT("RegisterInputAbility 失败：输入标签无效"));
		return false;
	}

	if (!AbilityClass)
	{
		UE_LOG(LogRPG_Ability, Warning, TEXT("RegisterInputAbility 失败：%s 没有指定能力类"),
			*InputTag.ToString());
		return false;
	}

	// 重复注册同一个输入标签时，先撤销旧能力。
	// 不这么做的话，同一个标签会挂上多个 GA 实例，激活时可能同时触发两个，很难查。
	if (const FGameplayAbilitySpecHandle* OldHandle = InputTagToSpecHandle.Find(InputTag))
	{
		if (OldHandle->IsValid())
		{
			ClearAbility(*OldHandle);
			UE_LOG(LogRPG_Ability, Verbose, TEXT("输入标签 %s 重复注册，已撤销旧能力"),
				*InputTag.ToString());
		}
		InputTagToSpecHandle.Remove(InputTag);
	}

	// “2步走”创建并授予 GA 实例
	// Level 会传给 GA，进而影响它施加的 GE 的 Level——
	// 这是 GAS 里做"技能等级影响数值"的机制，现在固定 1 级，架构先留好。
	const FGameplayAbilitySpec NewSpec(AbilityClass, Level);
	const FGameplayAbilitySpecHandle Handle = GiveAbility(NewSpec);

	if (!Handle.IsValid())
	{
		UE_LOG(LogRPG_Ability, Error, TEXT("RegisterInputAbility 失败：授予能力 %s 未成功"), *AbilityClass->GetName());
		return false;
	}

	// 存储两个映射关系表，方便后续通过输入标签快速找到对应的 GA 类与 Handle
	InputTagToAbilityClass.Add(InputTag, AbilityClass);
	InputTagToSpecHandle.Add(InputTag, Handle);

	UE_LOG(LogRPG_Ability, Log, TEXT("注册输入能力：%s → %s"), *InputTag.ToString(), *AbilityClass->GetName());

	return true;
}

void URPG_AbilitySystemComponent::RegisterInputAbilities(const TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>& InMappings)
{
	for (const TPair<FGameplayTag, TSubclassOf<UGameplayAbility>>& Pair : InMappings)
	{
		RegisterInputAbility(Pair.Key, Pair.Value);
	}

	UE_LOG(LogRPG_Ability, Log, TEXT("批量注册输入能力完成，共 %d 条"), InMappings.Num());
}

bool URPG_AbilitySystemComponent::TryActivateAbilityByInputTag(FGameplayTag InputTag)
{
	const FGameplayAbilitySpecHandle* HandlePtr = InputTagToSpecHandle.Find(InputTag);

	if (!HandlePtr || !HandlePtr->IsValid())
	{
		UE_LOG(LogRPG_Ability, Verbose, TEXT("输入标签 %s 未绑定任何能力，忽略"),
			*InputTag.ToString());
		return false;
	}

	// 用 Handle 而不是类去激活：同一个 GA 类可能被授予多次（不同来源/等级），
	// Handle 精确指向"这一次授予"，不会误激活另一个实例。
	const bool bActivated = TryActivateAbility(*HandlePtr);

	// 激活失败是常态而非异常——冷却中、耐力不足、被 State.Dying 阻断都会失败。
	// 所以用 Verbose 级别，只有单独打开这个日志类别时才输出，避免刷屏。
	UE_LOG(LogRPG_Ability, Verbose, TEXT("按输入标签激活 %s：%s"),
		*InputTag.ToString(), bActivated ? TEXT("成功") : TEXT("失败（冷却/资源不足/被标签阻断）"));

	return bActivated;
}

bool URPG_AbilitySystemComponent::HasAbilityForInputTag(FGameplayTag InputTag) const
{
	return InputTagToSpecHandle.Contains(InputTag);
}
