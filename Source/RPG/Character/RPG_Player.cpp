// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/RPG_Player.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"

#include "AbilitySystem/RPG_AbilitySystemComponent.h"
#include "AbilitySystem/RPG_AttributeSet.h"
#include "Core/RPG_LogChannels.h"
#include "Core/RPG_PlayerState.h"

ARPG_Player::ARPG_Player()
{
	// 玩家角色不创建 ASC —— 它在 ARPG_PlayerState 上。
	// 这里有意留空并写下注释，是为了让读到这个构造函数的人不会以为"忘了写"。
}

void ARPG_Player::BeginPlay()
{
	Super::BeginPlay();

	// 兜底尝试：若 PossessedBy 已经成功初始化过，这里是空操作。
	InitializeAbilitySystem();
}

void ARPG_Player::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 单机 / 服务端的主要初始化路径：控制器占有 Pawn 时。
	// 注意这个时机 PlayerState 可能尚未就绪 —— InitializeAbilitySystem 内部会判空并推迟。
	InitializeAbilitySystem();
}

void ARPG_Player::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// 客户端路径：PlayerState 复制到达时重新建立关联。
	// 单机下这个函数不会被调用，但写上它几乎没有成本，
	// 而一旦将来接入联机，少了它就是"客户端技能全失效"级别的 bug。
	InitializeAbilitySystem();
}

// ══════════════════════════════════════════════════════════════════════
//  重生
// ══════════════════════════════════════════════════════════════════════

FTransform ARPG_Player::GetRespawnTransform() const
{
	// GetAuthGameMode 只在服务器返回非空 —— 而重生本来就只在服务器跑
	// （PerformRespawn 有 HasAuthority 判断），所以这里不需要再判一次。
	if (UWorld* World = GetWorld())
	{
		if (AGameModeBase* GameMode = World->GetAuthGameMode<AGameModeBase>())
		{
			if (AActor* PlayerStart = GameMode->FindPlayerStart(GetController()))
			{
				return PlayerStart->GetActorTransform();
			}
		}
	}

	// 没有 GameMode 的场合（比如直接拖一个 Player 进空关卡做单点测试）
	// 回落到基类的出生变换 —— 有兜底总比传送回世界原点强。
	UE_LOG(LogRPG_Combat, Warning,
		TEXT("[%s] 拿不到 PlayerStart，重生回出生位置"), *GetName());

	return Super::GetRespawnTransform();
}

void ARPG_Player::OnRespawned()
{
	Super::OnRespawned();

	AController* Ctrl = GetController();
	if (!Ctrl)
	{
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  ★ 位置和视角都必须**显式推给拥有这个角色的客户端**
	// ══════════════════════════════════════════════════════════════════
	// 本地控制的角色在客户端是 **AutonomousProxy**，它由客户端预测驱动，
	// 服务器只负责纠偏。这意味着下面两样东西**都不会自动同步过去**：
	//
	//   ① 位置 —— `AActor::ReplicatedMovement` 的复制条件是
	//      **COND_SimulatedOnly**，根本不会发给 AutonomousProxy。
	//      走的是"客户端发 ServerMove → 服务器发现对不上 → ClientAdjustPosition"。
	//      服务器传送完，客户端要等下一次移动纠偏才知道。
	//      玩家站着不动时这个延迟尤其明显 —— 表现是"复活后还站在死亡地点，
	//      过一会儿才被拽回出生点"。
	//
	//   ② 视角 —— `AController::ControlRotation` **压根不是复制属性**
	//      （Controller.cpp:826-835 只注册了 PlayerState 和 Pawn）。
	//      服务器调 SetControlRotation 只改到自己那份，
	//      引擎给客户端送视角走的是 ClientSetRotation 这个 RPC
	//      （Controller.cpp:469-476）。
	//
	// ClientSetLocation 一次解决两件事（它的实现就是 ClientSetRotation + TeleportTo，
	// Controller.cpp:455-462）。引擎自己的 RestartPlayerAtTransform 走的是同一条思路
	// ——它干脆换一个新 Pawn 靠占有复制来解决；我们复用 Pawn，所以要显式推一次。
	//
	// 服务器侧的 SetControlRotation 仍然要调：主机自己的玩家就在这台机器上，
	// 他的视角是真需要改的。
	Ctrl->SetControlRotation(GetActorRotation());
	Ctrl->ClientSetLocation(GetActorLocation(), GetActorRotation());
}

UAbilitySystemComponent* ARPG_Player::GetASCInternal() const
{
	// ASC 在 PlayerState 上。用 Cast 而不是 UAbilitySystemBlueprintLibrary，
	// 是为了拿到 ARPG_PlayerState 的具体类型，语义更明确、也少一次接口查找。
	if (const ARPG_PlayerState* RPGPlayerState = Cast<ARPG_PlayerState>(GetPlayerState()))
	{
		return RPGPlayerState->GetAbilitySystemComponent();
	}

	return nullptr;
}

void ARPG_Player::InitializeAbilitySystem()
{
	if (bAbilitySystemInitialized) return;

	APlayerState* PS = GetPlayerState();
	if (!PS)
	{
		// PlayerState 还没生成 —— 等下一个调用时机。这是正常的时序竞争，不是错误，
		// 所以用 Verbose 而不是 Warning，避免启动时刷出误导性的警告。
		UE_LOG(LogRPG_Ability, Verbose, TEXT("[%s] PlayerState 尚未就绪，GAS 初始化推迟"), *GetName());
		return;
	}

	UAbilitySystemComponent* ASC = GetASCInternal();
	if (!ASC)
	{
		UE_LOG(LogRPG_Ability, Verbose, TEXT("[%s] 拿不到 ASC，GAS 初始化推迟"), *GetName());
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  建立 ActorInfo 关联
	// ══════════════════════════════════════════════════════════════════
	// Owner = PlayerState（能力归属）
	// Avatar = this（表现载体）
	// 这两个参数决定了 GAS 内部从哪里取"拥有者标签"、GameplayCue 挂在哪、
	// GE 的来源判定算谁 —— 传反了会导致一堆诡异问题（比如 Cue 播在 PlayerState 位置上）。
	ASC->InitAbilityActorInfo(PS, this);

	UE_LOG(LogRPG_Ability, Log, TEXT("[%s] GAS 初始化完成（Owner=%s，运行在%s）"),
		*GetName(), *PS->GetName(), HasAuthority() ? TEXT("服务器") : TEXT("客户端"));

	// ══════════════════════════════════════════════════════════════════
	//  ★ 输入标签 → 能力 的**映射表：两端都要登记**
	// ══════════════════════════════════════════════════════════════════
	// 这是一张纯配置表（来自角色上的 StartupAbilities），不是运行时状态。
	//
	// 客户端也必须拿到它 —— 因为 PlayerController 按键时走的是
	// `TryActivateAbilityByInputTag`，那个函数要靠这张表把输入标签翻译成能力。
	//
	// ⚠️ 这一步原先被放在下面的 `HasAuthority()` 分支**里面**，
	// 于是客户端的映射表永远是空的：按任何键都会走到
	// "这个输入标签没有绑定任何能力"，而且**只有在非服务器的机器上才会发生**。
	// 单机测试永远发现不了。
	//
	// 注意这里只登记、不授予 —— 授予是下一段的事，那才是只能服务器做的。
	if (URPG_AbilitySystemComponent* RPGASC = Cast<URPG_AbilitySystemComponent>(ASC))
	{
		RPGASC->RegisterInputAbilityMappings(StartupAbilities);
	}
	else
	{
		UE_LOG(LogRPG_Ability, Error,
			TEXT("[%s] ASC 不是 URPG_AbilitySystemComponent，无法登记输入能力映射。"
			     "请检查 PlayerState 的 ASC 类型"), *GetName());

		// ⚠️ 必须 return，不能让它继续走到函数末尾把 bAbilitySystemInitialized 置上。
		//
		// 不 return 的话，后面所有初始化时机都会因为那个标志变成空操作，而
		// 唯一的线索就只有上面这一行 Error —— 排查时看到的是
		// "初始化只跑了一次、之后再没动静"，很难联想到是这里提前"成功"了。
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  以下只在服务器执行 —— 客户端靠复制拿到同样的状态
	// ══════════════════════════════════════════════════════════════════
	// 为什么客户端不做这两件事：
	//
	//   · 授予能力：GiveAbility 产生的 FGameplayAbilitySpec 会由 ASC 自动复制到客户端。
	//     客户端自己也调一次的话，会和复制过来那份重复，产生两个同能力实例——
	//     症状是"放一次技能触发两遍效果"，而且**只在联机时出现**，极难查。
	//
	//   · 初始属性：GE 的修改结果会通过属性集复制同步过来，客户端不需要自己算一遍。
	//
	// 这是 GAS 联机的基本原则：**状态由服务器产生，客户端只消费复制结果**。
	// 客户端唯一要自己做的事是"预测"，那由 GA 的 NetExecutionPolicy 负责，
	// 不需要在这里重复授予能力。
	//
	// 注意 InitAbilityActorInfo 是**两边都要做**的（已经在上面执行）：
	// 客户端也需要知道自己的 Owner 是哪个 PlayerState、Avatar 是哪个角色，
	// 否则本地预测和 GameplayCue 的定位都会出错。
	if (!HasAuthority())
	{
		bAbilitySystemInitialized = true;
		return;	// 客户端就此返回
	}

	// ══════════════════════════════════════════════════════════════════
	//  应用初始属性
	// ══════════════════════════════════════════════════════════════════
	// 顺序很重要：**先属性、后能力**。
	// 因为能力激活时往往会读属性（比如耐力够不够），属性没就位会读到 0。
	if (InitAttributesEffect)
	{
		// “4步走”：创建并应用初始属性的 GameplayEffect
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(this);
		const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(InitAttributesEffect, 1.f, Context);
		// SpecHandle.Data.Get()->SetSetByCallerMagnitude( "InitAttributesEffectTag", 1.f );
		
		if (SpecHandle.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			UE_LOG(LogRPG_Ability, Log, TEXT("[%s] 已应用初始属性：%s"), *GetName(),*InitAttributesEffect->GetName());
		}
		else
		{
			UE_LOG(LogRPG_Ability, Error, TEXT("[%s] 初始属性 GE 的 Spec 创建失败：%s"),*GetName(), *InitAttributesEffect->GetName());
		}
	}
	else
	{
		// 没配 InitAttributesEffect 不是致命错误 —— 属性集构造函数里有保底默认值。
		// 但要做数值调整就必须配，所以给个警告。
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 未配置 InitAttributesEffect，将使用属性集的构造函数默认值"), *GetName());
	}

	// ══════════════════════════════════════════════════════════════════
	//  授予起始能力
	// ══════════════════════════════════════════════════════════════════
	if (URPG_AbilitySystemComponent* RPGASC = Cast<URPG_AbilitySystemComponent>(ASC))
	{
		// 按上面已登记的映射逐个授予
		RPGASC->GrantInputAbilities();

		// 被动能力（耐力恢复等）没有输入标签，走单独的入口授予
		for (const TSubclassOf<UGameplayAbility>& PassiveClass : StartupPassiveAbilities)
		{
			RPGASC->GivePassiveAbility(PassiveClass);
		}
	}

	bAbilitySystemInitialized = true;
}
