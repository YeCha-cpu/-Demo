// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/RPG_PlayerController.h"

#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"

#include "AbilitySystem/RPG_AbilitySystemComponent.h"
#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"
#include "Input/RPG_InputConfig.h"

ARPG_PlayerController::ARPG_PlayerController()
{
	// 阶段 1 不需要每帧处理输入——增强输入是事件驱动的。
	// 将来若要做"按住攻击键持续蓄力"这类需要累积时间的逻辑，
	// 可以在 GA 里用 AbilityTask 处理，仍然不需要 PC 的 Tick。
	PrimaryActorTick.bCanEverTick = false;
}

void ARPG_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!InputConfig)
	{
		UE_LOG(LogRPG, Error,
			TEXT("[%s] 没有配置 InputConfig！输入将完全失效。"
			     "请在 BP_RPG_PlayerController 的 Details 面板里指定 DA_RPG_InputConfig"),
			*GetName());
		return;
	}

	if (!InputConfig->DefaultMappingContext)
	{
		UE_LOG(LogRPG, Error,
			TEXT("[%s] InputConfig 里没有指定 DefaultMappingContext"), *GetName());
		return;
	}

	// 把输入映射上下文注册到增强输入子系统。
	// MappingContext 才是真正决定"哪个键触发哪个 InputAction"的地方 ——
	// InputConfig 只负责"哪个 InputAction 对应哪个输入标签"。
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(InputConfig->DefaultMappingContext, InputConfig->MappingContextPriority);

			UE_LOG(LogRPG, Log, TEXT("[%s] 已添加输入映射上下文：%s（优先级 %d）"),
				*GetName(), *InputConfig->DefaultMappingContext->GetName(), InputConfig->MappingContextPriority);
		}
	}
}

void ARPG_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// ══════════════════════════════════════════════════════════════════
	//  前置检查
	// ══════════════════════════════════════════════════════════════════
	// 这里失败属于"配置错误"而非"运行异常"，所以用 Error 级别，并把修复方法
	// 直接写进日志 —— 这类问题靠猜很费时间。
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogRPG, Error,
			TEXT("[%s] InputComponent 不是 UEnhancedInputComponent。"
			     "请检查 Config/DefaultInput.ini 里的 DefaultInputComponentClass 是否指向 EnhancedInputComponent"),
			*GetName());
		return;
	}

	if (!InputConfig)
	{
		UE_LOG(LogRPG, Error, TEXT("[%s] 没有配置 InputConfig，跳过输入绑定"), *GetName());
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  移动类输入 —— 直接驱动角色，不经过 GAS
	// ══════════════════════════════════════════════════════════════════
	if (InputConfig->MoveAction)
	{
		EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this,
			&ARPG_PlayerController::OnMove);
	}

	if (InputConfig->LookAction)
	{
		EIC->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this,
			&ARPG_PlayerController::OnLook);
	}

	if (InputConfig->CrouchAction)
	{
		// 蹲伏用 Started（按下瞬间切换一次）而不是 Triggered ——
		// Triggered 每帧都会触发，会导致蹲下立刻又站起来，反复横跳。
		EIC->BindAction(InputConfig->CrouchAction, ETriggerEvent::Started, this,
			&ARPG_PlayerController::OnCrouch);
	}

	// 奔跑是"按住"型：Started 开始、Completed 结束。
	// 阶段 2 会改走 GA_Sprint（因为要持续消耗耐力），届时这两个回调
	// 只负责转成输入标签，不再直接改速度。
	if (const UInputAction* SprintAction = InputConfig->FindActionForInputTag(RPGTags::Input_Sprint))
	{
		EIC->BindAction(SprintAction, ETriggerEvent::Started, this,
			&ARPG_PlayerController::OnSprintStarted);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this,
			&ARPG_PlayerController::OnSprintCompleted);
	}

	// ══════════════════════════════════════════════════════════════════
	//  能力类输入 —— 全部走同一套转发逻辑
	// ══════════════════════════════════════════════════════════════════
	// 注意 BindAction 的最后一个参数：把这条映射的 InputTag 作为"附加参数"
	// 绑进回调。所以十几种能力输入只需要一个函数处理。
	// 新增技能时只改 DA_RPG_InputConfig，这个文件一行都不用动。
	int32 BoundCount = 0;

	for (const FRPG_InputActionMapping& Mapping : InputConfig->AbilityInputMappings)
	{
		// 跳过没配全的条目 —— 编辑资产时必然经过"填了一半"的状态，
		// 这里静默跳过；真正的配置错误会在输入失效时暴露出来。
		if (!Mapping.InputAction || !Mapping.InputTag.IsValid())
		{
			continue;
		}

		EIC->BindAction(Mapping.InputAction, ETriggerEvent::Started, this,
			&ARPG_PlayerController::OnAbilityInputPressed, Mapping.InputTag);

		// Completed 用于"按住型"能力（重击蓄力松开释放）。
		// 对瞬发技能它是空操作，不会有害。
		EIC->BindAction(Mapping.InputAction, ETriggerEvent::Completed, this,
			&ARPG_PlayerController::OnAbilityInputReleased, Mapping.InputTag);

		++BoundCount;
	}

	UE_LOG(LogRPG, Log, TEXT("[%s] 输入绑定完成：%d 个能力输入"), *GetName(), BoundCount);
}

// ══════════════════════════════════════════════════════════════════════
//  移动类回调
// ══════════════════════════════════════════════════════════════════════

void ARPG_PlayerController::OnMove(const FInputActionValue& Value)
{
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		RPGChar->Move(Value);
	}
}

void ARPG_PlayerController::OnLook(const FInputActionValue& Value)
{
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		RPGChar->Look(Value);
	}
}

void ARPG_PlayerController::OnCrouch()
{
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		RPGChar->ToggleCrouch();
	}
}

void ARPG_PlayerController::OnSprintStarted()
{
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		RPGChar->StartSprint();
	}
}

void ARPG_PlayerController::OnSprintCompleted()
{
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		RPGChar->StopSprint();
	}
}

// ══════════════════════════════════════════════════════════════════════
//  能力类回调
// ══════════════════════════════════════════════════════════════════════

void ARPG_PlayerController::OnAbilityInputPressed(FGameplayTag InputTag)
{
	if (URPG_AbilitySystemComponent* ASC = GetRPGAbilitySystemComponent())
	{
		// 注意：这里**不做**任何"能不能放"的前置判断 ——
		// 那是 GA 的 CanActivateAbility 和 GE 的标签阻断该管的事。
		// 控制器只负责把玩家的意图送达，判断权在能力系统内部。
		// 这样将来加新规则（耐力不足、状态禁止）不用回来改这里。
		ASC->TryActivateAbilityByInputTag(InputTag);
	}
}

void ARPG_PlayerController::OnAbilityInputReleased(FGameplayTag InputTag)
{
	// 阶段 2 会在这里通知"按住型"能力（重击蓄力）执行释放逻辑。
	// 现在留空是有意的：瞬发技能不需要释放处理，而按住型能力尚未实现。
	UE_LOG(LogRPG_Ability, VeryVerbose, TEXT("输入释放：%s"), *InputTag.ToString());
}

// ══════════════════════════════════════════════════════════════════════
//  辅助
// ══════════════════════════════════════════════════════════════════════

ARPG_BaseCharacter* ARPG_PlayerController::GetRPGCharacter() const
{
	// AController 已经维护了一个 Character 成员，用 GetCharacter() 访问，
	// 它返回"当前控制的 ACharacter"，控制的不是角色时返回 nullptr。
	// 我们只是把它转成本项目的角色类型，不需要自己再存一份。
	//
	// ⚠️ 注意：正因为 AController 有这个成员，在 Controller 的成员函数里
	// **不要**用 Character 当局部变量名 —— 会遮蔽它，而且 UE 把 C4458
	// （变量遮蔽类成员）当作编译错误而不是警告。
	return Cast<ARPG_BaseCharacter>(GetCharacter());
}

URPG_AbilitySystemComponent* ARPG_PlayerController::GetRPGAbilitySystemComponent() const
{
	const ARPG_BaseCharacter* RPGChar = GetRPGCharacter();
	if (!RPGChar)
	{
		return nullptr;
	}

	// 角色的 ASC 可能在它自己身上（敌人），也可能在 PlayerState 上（玩家）。
	// 走接口拿，不要在这里假设 —— 这正是接口存在的意义。
	return Cast<URPG_AbilitySystemComponent>(RPGChar->GetAbilitySystemComponent());
}

void ARPG_PlayerController::DebugPrint(const FString& Message, const FColor& Color) const
{
	// 双通道输出：屏幕上一份（PIE 时不用切窗口），Output Log 一份（能回溯）。
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, Color, Message);
	}
	UE_LOG(LogRPG, Log, TEXT("%s"), *Message);
}

// ══════════════════════════════════════════════════════════════════════
//  调试命令
// ══════════════════════════════════════════════════════════════════════

void ARPG_PlayerController::RPGPrintAttributes()
{
	const ARPG_BaseCharacter* RPGChar = GetRPGCharacter();
	const URPG_AttributeSet* Attributes = RPGChar ? RPGChar->GetRPGAttributeSet() : nullptr;

	if (!Attributes)
	{
		DebugPrint(TEXT("⚠ 拿不到属性集 —— ASC 可能尚未初始化，"
		                "或角色的 GetASCInternal() 没有正确实现"), FColor::Red);
		return;
	}

	DebugPrint(TEXT("════════ 玩家属性 ════════"), FColor::Cyan);
	DebugPrint(FString::Printf(TEXT("  生命  %6.1f / %.1f"), Attributes->GetHealth(), Attributes->GetMaxHealth()));
	DebugPrint(FString::Printf(TEXT("  攻击  %6.1f"), Attributes->GetAttack()));
	DebugPrint(FString::Printf(TEXT("  防御  %6.1f"), Attributes->GetDefense()));
	DebugPrint(FString::Printf(TEXT("  法力  %6.1f / %.1f"), Attributes->GetMana(), Attributes->GetMaxMana()));
	DebugPrint(FString::Printf(TEXT("  耐力  %6.1f / %.1f"), Attributes->GetStamina(), Attributes->GetMaxStamina()));
	DebugPrint(TEXT("══════════════════════════"), FColor::Cyan);
}

void ARPG_PlayerController::RPGPrintTags()
{
	UAbilitySystemComponent* ASC = GetRPGAbilitySystemComponent();
	if (!ASC)
	{
		DebugPrint(TEXT("⚠ 拿不到 ASC"), FColor::Red);
		return;
	}

	FGameplayTagContainer OwnedTags;
	ASC->GetOwnedGameplayTags(OwnedTags);

	if (OwnedTags.IsEmpty())
	{
		DebugPrint(TEXT("当前没有任何 GameplayTag"), FColor::Yellow);
		return;
	}

	DebugPrint(FString::Printf(TEXT("════ 当前标签（%d 个）════"), OwnedTags.Num()), FColor::Cyan);
	for (const FGameplayTag& Tag : OwnedTags)
	{
		DebugPrint(FString::Printf(TEXT("  %s"), *Tag.ToString()));
	}
}
