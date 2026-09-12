// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "RPG_AnimNotifyState_ComboWindow.generated.h"

/**
 * 连段衔接窗口。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它决定连招手感，是最需要反复调的一个 Notify】
 * ══════════════════════════════════════════════════════════════════════
 * 窗口内按攻击键 → 接上下一段
 * 窗口外按攻击键 → 要么被缓存起来等窗口开（如果还在生命周期内），
 *                  要么直接作废
 *
 * 调参规律：
 *   · 窗口太窄 → 玩家必须卡在很精确的时机按，感觉"连不上"
 *   · 窗口太宽 → 玩家乱按也能连，连招失去节奏感
 *   · 通常开在后摇的前 1/3 到 1/2 处，长度约 0.3~0.5 秒
 *   · 最后一段一般不开窗口（打了就是打了，不能无限连）
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它和输入缓存的分工】
 * ══════════════════════════════════════════════════════════════════════
 *   输入缓存   —— 解决"玩家按早了"：按键记下来，等窗口开
 *   衔接窗口   —— 解决"什么时候能接"：定义规则
 *
 * 两者缺一不可。只有窗口没有缓存 → 按早了白按，不跟手；
 * 只有缓存没有窗口 → 可以无限快速连打，失去节奏。
 */
UCLASS(meta = (DisplayName = "RPG 连段衔接窗口"))
class RPG_API URPG_AnimNotifyState_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	//~ Begin UAnimNotifyState interface
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("衔接窗口");
	}
	//~ End UAnimNotifyState interface
};
