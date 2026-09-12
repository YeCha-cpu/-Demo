// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Notifies/RPG_AnimNotifyState_AttackWindow.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

#include "Combat/RPG_AttackWindowPayload.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

void URPG_AnimNotifyState_AttackWindow::NotifyBegin(
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

	// ── 打包检测参数 ──
	// Outer 传 MeshComp 而不是 GetTransientPackage()：
	// 这样 Payload 的生命周期跟着角色走，角色销毁时一起回收，不会泄漏。
	URPG_AttackWindowPayload* Payload = NewObject<URPG_AttackWindowPayload>(MeshComp);
	Payload->AttackTag = AttackTag;
	Payload->bOverrideTrace = bOverrideTrace;
	Payload->TraceSource = TraceSource;
	Payload->TraceRadius = TraceRadius;
	Payload->SocketStart = SocketStart;
	Payload->SocketEnd = SocketEnd;

	FGameplayEventData EventData;
	EventData.EventTag = RPGTags::Event_Combat_AttackWindow_Open;
	EventData.Instigator = Owner;
	EventData.Target = Owner;
	EventData.OptionalObject = Payload;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Owner, RPGTags::Event_Combat_AttackWindow_Open, EventData);

	UE_LOG(LogRPG_Animation, Verbose,
		TEXT("[%s] 攻击判定窗口开启（%s）"),
		*Owner->GetName(),
		AttackTag.IsValid() ? *AttackTag.ToString() : TEXT("未指定标签"));
}

void URPG_AnimNotifyState_AttackWindow::NotifyEnd(
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

	// 关闭事件不需要载荷 —— GA 收到就知道该停检测了
	FGameplayEventData EventData;
	EventData.EventTag = RPGTags::Event_Combat_AttackWindow_Close;
	EventData.Instigator = Owner;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Owner, RPGTags::Event_Combat_AttackWindow_Close, EventData);

	UE_LOG(LogRPG_Animation, Verbose,
		TEXT("[%s] 攻击判定窗口关闭"), *Owner->GetName());
}

FString URPG_AnimNotifyState_AttackWindow::GetNotifyName_Implementation() const
{
	// 在蒙太奇编辑器的时间轴上直接显示标签名，不用点开 Details 就能看出是哪一段
	return AttackTag.IsValid()
		? FString::Printf(TEXT("判定窗口 [%s]"), *AttackTag.ToString())
		: TEXT("判定窗口");
}
