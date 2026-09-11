// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/RPG_Enemy.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Controller.h"

#include "AbilitySystem/RPG_AbilitySystemComponent.h"
#include "AbilitySystem/RPG_AttributeSet.h"
#include "Core/RPG_LogChannels.h"

ARPG_Enemy::ARPG_Enemy()
{
	AbilitySystemComponent = CreateDefaultSubobject<URPG_AbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<URPG_AttributeSet>(TEXT("AttributeSet"));

	// 属性集的登记放在 InitializeAbilitySystem 里做，而不是构造函数。
	// 原因是构造函数可能对 CDO 执行多次，而 AddSpawnedAttribute 会改动内部数组；
	// 放到初始化函数里能保证"每个实例只登记一次"。

	// 阶段 4 会在这里补上：
	//     AIControllerClass = ARPG_AIController::StaticClass();
	//     AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	// 现在先不设，避免依赖尚不存在的 AI 类。
}

void ARPG_Enemy::BeginPlay()
{
	Super::BeginPlay();

	InitializeAbilitySystem();
}

void ARPG_Enemy::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 敌人的 ASC 与"谁在控制它"无关，所以这里只是多一个保险的初始化时机。
	// 写在 PossessedBy 里的实际价值是：AI 逻辑一旦开始跑（PossessedBy 之后），
	// 能力一定已经就位了。
	InitializeAbilitySystem();
}

UAbilitySystemComponent* ARPG_Enemy::GetASCInternal() const
{
	return AbilitySystemComponent;
}

void ARPG_Enemy::InitializeAbilitySystem()
{
	if (bAbilitySystemInitialized)
	{
		return;
	}

	if (!AbilitySystemComponent || !AttributeSet)
	{
		UE_LOG(LogRPG_Ability, Error, TEXT("[%s] ASC 或属性集为空，无法初始化 GAS"), *GetName());
		return;
	}

	// ── 登记属性集 ──
	// 先查一次，避免重复登记（重复登记会让同一个属性集在 ASC 里出现两次，
	// 修改时只改到其中一个，症状是"数值有时对有时不对"）。
	if (!AbilitySystemComponent->GetSet<URPG_AttributeSet>())
	{
		AbilitySystemComponent->AddSpawnedAttribute(AttributeSet);
	}

	// ── 建立 ActorInfo 关联 ──
	// 敌人是 Owner == Avatar == 自己。
	// 对比玩家：Owner = PlayerState、Avatar = 角色 —— 这正是不对称之处。
	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	UE_LOG(LogRPG_Ability, Log, TEXT("[%s] GAS 初始化完成（Owner 与 Avatar 均为自身，运行在%s）"),
		*GetName(), HasAuthority() ? TEXT("服务器") : TEXT("客户端"));

	// ── 以下只在服务器执行 ──
	// 敌人由 AI 驱动，所有状态都在服务器产生、再复制给各客户端。
	// 客户端上的敌人实例只需要正确的 ActorInfo（供动画和 GameplayCue 定位），
	// 不需要自己授予能力或应用初始属性 —— 理由与玩家一致，详见 RPG_Player.cpp。
	if (!HasAuthority())
	{
		bAbilitySystemInitialized = true;
		return;
	}

	// ── 初始属性（先属性后能力，理由同玩家）──
	if (InitAttributesEffect)
	{
		FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
		Context.AddSourceObject(this);

		const FGameplayEffectSpecHandle SpecHandle =
			AbilitySystemComponent->MakeOutgoingSpec(InitAttributesEffect, 1.f, Context);

		if (SpecHandle.IsValid())
		{
			AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			UE_LOG(LogRPG_Ability, Log, TEXT("[%s] 已应用初始属性：%s"),
				*GetName(), *InitAttributesEffect->GetName());
		}
	}

	// ── 起始能力 ──
	// 敌人用与玩家完全相同的映射机制，只是触发者会是 AI 而不是按键。
	AbilitySystemComponent->RegisterInputAbilities(StartupAbilities);

	bAbilitySystemInitialized = true;
}
