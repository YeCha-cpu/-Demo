// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Notifies/RPG_AnimNotify_AttackEnd.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

void URPG_AnimNotify_AttackEnd::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

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
	EventData.EventTag = RPGTags::Event_Combat_AttackEnd;
	EventData.Instigator = Owner;
	EventData.Target = Owner;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Owner, RPGTags::Event_Combat_AttackEnd, EventData);

	UE_LOG(LogRPG_Animation, Verbose,
		TEXT("[%s] 攻击段结束"), *Owner->GetName());
}
