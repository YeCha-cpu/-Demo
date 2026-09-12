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

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Owner, RPGTags::Event_Combat_ComboWindow_Close, EventData);

	// 窗口关闭是**连段重置的触发点**：
	// 如果到这里缓存里还是没有输入，说明玩家停手了，连段应该归零。
	// 否则玩家停手后下一击会直接从第 N 段开始 —— 这是连段系统最经典的 bug。
	UE_LOG(LogRPG_Animation, Verbose,
		TEXT("[%s] 连段衔接窗口关闭"), *Owner->GetName());
}
