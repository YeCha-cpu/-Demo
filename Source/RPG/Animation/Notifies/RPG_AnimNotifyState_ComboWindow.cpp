// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Notifies/RPG_AnimNotifyState_ComboWindow.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

void URPG_AnimNotifyState_ComboWindow::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	FGameplayEventData EventData;
	EventData.EventTag = RPGTags::Event_Combat_ComboWindow_Open;
	EventData.Instigator = Owner;
	EventData.OptionalObject2 = Animation;   // 来源蒙太奇，见下方说明

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Owner, RPGTags::Event_Combat_ComboWindow_Open, EventData);

	UE_LOG(LogRPG_Animation, Verbose,
		TEXT("[%s] 连段衔接窗口开启"), *Owner->GetName());
}

void URPG_AnimNotifyState_ComboWindow::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	FGameplayEventData EventData;
	EventData.EventTag = RPGTags::Event_Combat_ComboWindow_Close;
	EventData.Instigator = Owner;

	// ★ 把来源蒙太奇带上 —— 这是"迟到事件"能被识别出来的唯一依据。
	//
	// 引擎在**蒙太奇被停掉时，会主动给所有还活着的 NotifyState 补发 NotifyEnd**
	// （UAnimInstance::TriggerMontageEndedEvent，AnimInstance.cpp:2507，
	//  注释就写着 "Send end notifications for anim notify state when we are stopped"）。
	//
	// 而连段接下一段的做法正是"停掉上一段的蒙太奇"，所以：
	//   第 N 段衔接窗口打开 → 接上第 N+1 段 → 第 N 段的蒙太奇被停
	//     → 引擎补发第 N 段的「衔接窗口关闭」→ 一帧后到达 GA
	//     → 如果 GA 不辨来源，会拿它当成"当前这一段的窗口关闭"
	//     → 顺手把缓存里的下一次按键吃掉，凭空多跳一段
	//
	// 表现是"连招打不全/跳段"，而且只在快速连打时出现。
	// 带上来源之后，GA 侧一比就知道这条是上一段的，直接忽略。
	//
	// 用 OptionalObject2 而不是 OptionalObject：后者留给攻击判定窗口传载荷用。
	EventData.OptionalObject2 = Animation;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Owner, RPGTags::Event_Combat_ComboWindow_Close, EventData);

	// 窗口关闭是**连段重置的触发点**：
	// 如果到这里缓存里还是没有输入，说明玩家停手了，连段应该归零。
	// 否则玩家停手后下一击会直接从第 N 段开始 —— 这是连段系统最经典的 bug。
	UE_LOG(LogRPG_Animation, Verbose,
		TEXT("[%s] 连段衔接窗口关闭"), *Owner->GetName());
}
