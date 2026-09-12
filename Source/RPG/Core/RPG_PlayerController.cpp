// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/RPG_PlayerController.h"

#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

#include "AbilitySystem/RPG_AbilitySystemComponent.h"
#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"
#include "Input/RPG_InputConfig.h"

ARPG_PlayerController::ARPG_PlayerController()
{
	// ⚠️⚠️ 绝对不要在这里写 PrimaryActorTick.bCanEverTick = false; ⚠️⚠️
	//
	// 我踩过这个坑：当时的理由是"增强输入是事件驱动的，不需要每帧处理"。
	// 这个判断是**错的**。增强输入的 Triggered / Started / Completed 事件，
	// 恰恰是在每帧的输入处理里评估 InputMappingContext 的 Trigger 才产生的：
	//
	//     APlayerController::TickActor()     ← 由 PrimaryActorTick 驱动
	//       └─ TickPlayerInput()
	//            └─ UPlayerInput::Tick()      ← 在这里评估所有 IMC 的 Trigger
	//                 └─ 产生 Triggered / Started / Completed 事件
	//
	// 把 bCanEverTick 设为 false，等于掐断整条输入管线。症状是
	// **所有按键都没反应，而且不报任何错**——极难查。
	//
	// AController 的构造函数里把它设成 true（Controller.cpp:62）正是为了让
	// 这条链路能跑起来，子类保持默认即可，不要动它。
	//
	// （将来做"按住攻击键蓄力"这类需要累积时间的逻辑时，用 GA 里的
	//   AbilityTask 处理；即使那样，PlayerController 的 Tick 也不能关。）
}

void ARPG_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	// ══════════════════════════════════════════════════════════════════
	//  联机适配：只有"本机控制"的 PlayerController 才处理输入
	// ══════════════════════════════════════════════════════════════════
	// 联机下，服务器上会存在代表**远程玩家**的 PlayerController。它们没有
	// LocalPlayer（GetLocalPlayer() 返回 nullptr），在上面绑定输入、添加 IMC
	// 没有任何意义。
	//
	// 单机（Standalone）下 IsLocalController() 恒为 true，这个判断等于不存在，
	// 所以它**不会**影响单人 PIE 的迭代速度 —— 这是"保留单机自动降级"的体现。
	if (!IsLocalController())
	{
		UE_LOG(LogRPG, Verbose, TEXT("[%s] 不是本机控制器（远程玩家），跳过输入初始化"), *GetName());
		return;
	}

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
	// InputConfig 只负责"哪个 InputAction 对应哪个输入标签（起映射作用）"。
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(InputConfig->DefaultMappingContext, InputConfig->MappingContextPriority);

			UE_LOG(LogRPG, Log, TEXT("[%s] 已添加输入映射上下文：%s（优先级 %d）"),
				*GetName(), *InputConfig->DefaultMappingContext->GetName(), InputConfig->MappingContextPriority);
		}
	}

	// ── 诊断：IMC 里到底配了几条按键映射 ──
	// ⚠️ "IMC 添加成功" 不等于 "IMC 里有内容"。一个空的 IMC 照样能添加成功，
	// 但按什么键都不会有反应。少了这个检查，这两种情况的日志长得一模一样。
	const int32 MappingCount = InputConfig->DefaultMappingContext->GetMappings().Num();
	if (MappingCount == 0)
	{
		UE_LOG(LogRPG, Error,
			TEXT("[%s] IMC「%s」里一条按键映射都没有！"
			     "请打开这个 IMC 资产，在 Mappings 数组里添加 W/A/S/D 等映射"),
			*GetName(), *InputConfig->DefaultMappingContext->GetName());
	}
	else
	{
		UE_LOG(LogRPG, Log, TEXT("[%s] IMC「%s」中共有 %d 条按键映射"),
			*GetName(), *InputConfig->DefaultMappingContext->GetName(), MappingCount);
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
	// 每个分支都打印结果。之前这里是静默的，导致"InputConfig 里没填 Move Action"
	// 和"IMC 里没配按键"这两种完全不同的故障，在日志上完全分辨不出来。
	if (InputConfig->MoveAction)
	{
		EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this,
			&ARPG_PlayerController::OnMove);
		UE_LOG(LogRPG, Log, TEXT("  ├─ Move    → %s"), *InputConfig->MoveAction->GetName());

		// 移动的 Value Type 必须是 Axis2D。如果配成 Digital 或 Axis1D，
		// Value.Get<FVector2D>() 会静默返回零向量——按了键，代码也收到了事件，
		// 但移动向量是 (0,0)，表现为"按键没反应"。
		if (InputConfig->MoveAction->ValueType != EInputActionValueType::Axis2D)
		{
			UE_LOG(LogRPG, Error,
				TEXT("  │   ⚠ IA_RPG_Move 的 Value Type 应该是 Axis2D (Vector2D)，当前不是 —— "
				     "移动向量会永远是零，角色不会动"));
		}
	}
	else
	{
		UE_LOG(LogRPG, Error,
			TEXT("  ├─ Move    ✗ 未绑定！DA_RPG_InputConfig 的 Locomotion 分类下 "
			     "Move Action 是空的 —— WASD 不会有任何反应"));
	}

	if (InputConfig->LookAction)
	{
		EIC->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this,
			&ARPG_PlayerController::OnLook);
		UE_LOG(LogRPG, Log, TEXT("  ├─ Look    → %s"), *InputConfig->LookAction->GetName());

		if (InputConfig->LookAction->ValueType != EInputActionValueType::Axis2D)
		{
			UE_LOG(LogRPG, Error,
				TEXT("  │   ⚠ IA_RPG_Look 的 Value Type 应该是 Axis2D (Vector2D)，当前不是"));
		}
	}
	else
	{
		UE_LOG(LogRPG, Error,
			TEXT("  ├─ Look    ✗ 未绑定！InputConfig 里 Look Action 是空的 —— 鼠标转不了视角"));
	}

	if (InputConfig->CrouchAction)
	{
		// 蹲伏用 Started（按下瞬间切换一次）而不是 Triggered ——
		// Triggered 每帧都会触发，会导致蹲下立刻又站起来，反复横跳。
		EIC->BindAction(InputConfig->CrouchAction, ETriggerEvent::Started, this,
			&ARPG_PlayerController::OnCrouch);
		UE_LOG(LogRPG, Log, TEXT("  ├─ Crouch  → %s"), *InputConfig->CrouchAction->GetName());
	}
	else
	{
		UE_LOG(LogRPG, Warning, TEXT("  ├─ Crouch  ✗ 未绑定（InputConfig 里 Crouch Action 为空）"));
	}

	// ══════════════════════════════════════════════════════════════════
	//  过渡期输入 —— 最终会走 GAS，但对应的 GA 还没写出来
	// ══════════════════════════════════════════════════════════════════
	// Jump 和 Sprint 在最终设计里都是 GA（要消耗耐力、有前后摇），但那属于阶段 3。
	// 在那之前，如果只把它们留在下面的能力循环里，按键会**静默失败** ——
	// ASC 找不到对应能力，TryActivateAbilityByInputTag 直接返回 false，不报任何错。
	//
	// 所以这里临时绑到角色的原生行为上，让阶段 1 能验证"能跑能跳"。
	// 每做好一个 GA，把它从这个判断里删掉即可 —— 它会自动回到能力循环那条
	// 正规路径，不需要改动其他代码。
	const auto IsInterimNativeInput = [](const FGameplayTag& Tag)
	{
		return Tag == RPGTags::Input_Jump || Tag == RPGTags::Input_Sprint;
	};

	// ── Jump ──
	// 走 ACharacter 的原生跳跃。联机下同样可用：CharacterMovementComponent
	// 内部通过 ServerMove 机制同步，不需要我们额外处理。
	if (const UInputAction* JumpAction = InputConfig->FindActionForInputTag(RPGTags::Input_Jump))
	{
		EIC->BindAction(JumpAction, ETriggerEvent::Started, this,
			&ARPG_PlayerController::OnJumpStarted);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this,
			&ARPG_PlayerController::OnJumpCompleted);
		UE_LOG(LogRPG, Log, TEXT("  ├─ Jump    → %s（过渡期：原生跳跃，阶段 3 换 GA_Jump）"),
			*JumpAction->GetName());
	}
	else
	{
		UE_LOG(LogRPG, Warning,
			TEXT("  ├─ Jump    ✗ 未绑定（DA_RPG_InputConfig 里没有映射到 Input.Jump 的条目）"));
	}

	// ── Sprint ──
	// "按住"型：Started 开始、Completed 结束。
	if (const UInputAction* SprintAction = InputConfig->FindActionForInputTag(RPGTags::Input_Sprint))
	{
		EIC->BindAction(SprintAction, ETriggerEvent::Started, this,
			&ARPG_PlayerController::OnSprintStarted);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this,
			&ARPG_PlayerController::OnSprintCompleted);
		UE_LOG(LogRPG, Log, TEXT("  ├─ Sprint  → %s（过渡期：直接改速度，阶段 3 换 GA_Sprint）"),
			*SprintAction->GetName());
	}
	else
	{
		UE_LOG(LogRPG, Warning,
			TEXT("  ├─ Sprint  ✗ 未绑定（DA_RPG_InputConfig 里没有映射到 Input.Sprint 的条目）"));
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
		// 跳过没配全的条目 —— 编辑资产时必然有"填了一半"的状态，
		// 这里静默跳过；真正的配置错误会在输入失效时暴露出来。
		if (!Mapping.InputAction || !Mapping.InputTag.IsValid())
			continue;

		// 过渡期输入已经在上面的原生绑定里处理过了。
		// **必须跳过** —— 否则按一次键会触发两遍（原生行为 + 尝试激活能力）。
		if (IsInterimNativeInput(Mapping.InputTag))
			continue;

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

void ARPG_PlayerController::OnJumpStarted()
{
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		// ACharacter::Jump() 内部会做 CanJump() 检查（是否在空中、能否起跳），
		// 不需要我们再判断一次。
		RPGChar->Jump();
	}
}

void ARPG_PlayerController::OnJumpCompleted()
{
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		// 松开跳跃键时通知，实现"可变高度跳跃"——短按跳得低、长按跳得高。
		// 这是平台跳跃手感的基础，对 ARPG 里"跳跃接闪避/接攻击"的衔接也有帮助。
		RPGChar->StopJumping();
	}
}

// ══════════════════════════════════════════════════════════════════════
//  能力类回调
// ══════════════════════════════════════════════════════════════════════

void ARPG_PlayerController::OnAbilityInputPressed(FGameplayTag InputTag)
{
	// 这条日志是排查"按键没反应"的**第一个检查点**：
	// 能看到它 → 说明按键、IMC、InputConfig 三层的映射都是通的，问题在 GAS 侧；
	// 看不到它 → 说明上面三层里有一层断了（绝大多数情况是 IMC 里没配这个按键）。
	UE_LOG(LogRPG_Ability, Log, TEXT("[%s] 收到能力输入：%s"), *GetName(), *InputTag.ToString());

	URPG_AbilitySystemComponent* ASC = GetRPGAbilitySystemComponent();

	if (!ASC)
	{
		// 拿不到 ASC 几乎总是因为 PlayerStateClass 没指向 RPG_PlayerState 的蓝图子类
		// —— 玩家的 ASC 挂在 PlayerState 上，那一环断了这里就永远是空。
		UE_LOG(LogRPG_Ability, Warning,
			TEXT("[%s] 拿不到 ASC，输入 %s 无法处理 —— "
			     "请检查 GameMode 的 PlayerStateClass 是否指向 RPG_PlayerState 的蓝图子类"),
			*GetName(), *InputTag.ToString());
		return;
	}

	// 注意：这里**不做**任何"能不能放"的前置判断 ——
	// 那是 GA 的 CanActivateAbility 和 GE 的标签阻断该管的事。
	// 控制器只负责把玩家的意图送达，判断权在能力系统内部。
	// 这样将来加新规则（耐力不足、状态禁止）不用回来改这里。
	ASC->TryActivateAbilityByInputTag(InputTag);
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
