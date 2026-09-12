// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "RPG_AnimNotify_AttackEnd.generated.h"

/**
 * 攻击段结束。
 *
 * 放在蒙太奇的**最后一帧**，GA 收到后调用 EndAbility 结束这次能力。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么用 Notify 而不是等蒙太奇自然播完】
 * ══════════════════════════════════════════════════════════════════════
 * PlayMontageAndWait 的 OnCompleted 确实会在动画播完时触发，但：
 *   · 蒙太奇末尾可能有一段"保持姿势"的时间，那段时间玩家已经可以操作了，
 *     等它播完才结束能力会让操作感觉迟滞
 *   · 用 Notify 可以精确控制"哪一帧算结束"，把尾巴那几帧留给过渡
 *   · 有些段需要"提前结束"（比如被衔接窗口接走了），有显式的结束点更好控制
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【联机下的额外用途】
 * ══════════════════════════════════════════════════════════════════════
 * 阶段 5 的 BTTask_RPG_Attack 会监听这个事件来实现"AI 等技能播完再继续行为树"
 * —— 行为树任务必须用潜在任务（Latent Task）模式，发完攻击请求就返回
 * Succeeded 会导致 AI 在攻击动画播放期间继续移动。
 */
UCLASS(meta = (DisplayName = "RPG 攻击结束"))
class RPG_API URPG_AnimNotify_AttackEnd : public UAnimNotify
{
	GENERATED_BODY()

public:
	//~ Begin UAnimNotify interface
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("攻击结束");
	}
	//~ End UAnimNotify interface
};
