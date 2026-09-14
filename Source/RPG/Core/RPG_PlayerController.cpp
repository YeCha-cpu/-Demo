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
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"
#include "Input/RPG_InputConfig.h"

ARPG_PlayerController::ARPG_PlayerController()
{
	// ⚠️⚠️ 绝对不要在这里写 PrimaryActorTick.bCanEverTick = false; ⚠️⚠️
	//
	// 踩过这个坑：当时的理由是"增强输入是事件驱动的，不需要每帧处理"。
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
	//  关于 Jump / Sprint
	// ══════════════════════════════════════════════════════════════════
	// 阶段 1 时这两个还没有对应的 GA，曾在这里做过"过渡期原生绑定"。
	// 阶段 3 已经把 GA_Jump 和 GA_Sprint 都做出来了，所以那段临时代码已删除 ——
	// 现在它们和其他能力一样，走下面的能力循环。
	//
	// ⚠️ 前提是它们要出现在 DA_RPG_InputConfig 的 Ability Input Mappings 里：
	//      IA_RPG_Jump   → Input.Jump
	//      IA_RPG_Sprint → Input.Sprint
	// 少了映射的话，按键不会触发任何东西（启动日志里会显示能力输入数量）。

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

	// ══════════════════════════════════════════════════════════════════
	//  1. 先把输入推进缓存
	// ══════════════════════════════════════════════════════════════════
	// 这一步是连段衔接的前提。玩家在攻击动画播放期间按下的"下一段"，
	// 不会被立即执行，而是留在缓存里，等衔接窗口打开时由 GA 取走。
	//
	// 如果只调 TryActivateAbilityByInputTag 而不推缓存，那么：
	//   · 攻击进行中的按键会激活失败，然后**被彻底丢弃**
	//   · 结果就是"连按没有衔接，只能等上一段完全播完再按"
	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		// ── 死了就不推输入、也不尝试激活 ★ ──
		//
		// 不做这道判断的话，死亡期间（3 秒重生倒计时里）玩家每按一次键都会：
		//   ① 往缓存里塞一条永远没人取的输入
		//   ② 让 GA 走一遍 CanActivateAbility → 被 State.Dead 阻断 →
		//      基类打一条 Warning「CanActivateAbility 被引擎拒绝」
		// 表现是**死亡期间按键刷屏告警**，把真正有用的信息淹掉。
		//
		// 判断放在这里而不是各个 GA 里：控制器是玩家意图的入口，
		// "死人没有意图"这件事应该在最靠前的地方被挡掉。
		// （GA 侧的 ActivationBlockedTags 仍然保留 —— 那是给 AI 和
		//   其他非输入路径兜底的，两道防线管的不是同一件事。）
		if (!RPGChar->IsAlive())
		{
			return;
		}

		if (URPG_CombatComponent* Combat = RPGChar->GetCombatComponent())
		{
			Combat->PushInputTag(InputTag);

			// ── 同时把这条意图送到服务器 ★ ──
			//
			// 服务器的输入缓存不会自己填上 —— 它只在**按键那台机器**上被写。
			// 而连段推进（TryStartNextSegment）读的正是这个缓存，
			// 所以服务器那份 GA 永远连不上第二段，打完第 1 段就 EndAbility，
			// 再用 ClientEndAbility 把客户端正在播的动画**硬切**掉。
			//
			// 只在"本地控制但又不是服务器"时发：
			//   · 主机自己（Listen Server 的本机玩家）已经在服务器上了，不用发
			//   · 远端玩家的 PlayerController 在这台机器上不会收到输入事件
			// 所以这个条件正好覆盖"纯客户端"这一种情况，不会重复推。
			if (IsLocalController() && !HasAuthority())
			{
				Server_PushInputTag(InputTag);
			}
		}
	}

	// ══════════════════════════════════════════════════════════════════
	//  2. 尝试激活
	// ══════════════════════════════════════════════════════════════════
	// 如果当前没有攻击在进行，这次激活会成功，GA 内部会消耗掉刚推入的那条输入；
	// 如果正在攻击中，激活失败（无害），输入就留在缓存里等衔接窗口来取。
	//
	// 注意：这里**不做**任何"能不能放"的前置判断 ——
	// 那是 GA 的 CanActivateAbility 和 GE 的标签阻断该管的事。
	// 控制器只负责把玩家的意图送达，判断权在能力系统内部。
	ASC->TryActivateAbilityByInputTag(InputTag);
}

void ARPG_PlayerController::Server_PushInputTag_Implementation(FGameplayTag InputTag)
{
	// 服务器端的落地：把玩家的意图推进**这台机器上那份**缓存。
	//
	// 走的是和本地按键完全相同的入口 `PushInputTag` ——
	// 和项目"A I 和玩家共用一条输入链路"的约定一致，
	// 服务器这边不需要知道这条输入是玩家按的还是 RPC 送来的。
	//
	// 不在这里调 TryActivateAbilityByInputTag：
	//   · 服务器侧的能力激活由 `ServerTryActivateAbility`（GA 的 LocalPredicted 机制）
	//     自己负责，那条路已经通了
	//   · 这里重复激活会让服务器上出现两份激活
	// 这个 RPC 只解决"缓存里有没有货"这一件事。
	//
	// ══════════════════════════════════════════════════════════════════
	//  服务器侧的两道门 ★
	// ══════════════════════════════════════════════════════════════════
	// 这个 RPC 的**参数来自客户端**，服务器不能无条件相信。
	//
	// 先澄清它**不是**什么：它不需要额外的"权威判断"。
	// `UNetDriver::ShouldCallRemoteFunction` 保证只有 Owner 能对自己的
	// Actor 发 Server RPC，加上 `APlayerController` 的
	// `bOnlyRelevantToOwner`，别的客户端根本看不到你也调不到你 ——
	// 下面两道门防的是"**改过的客户端**"，不是"别的玩家"。
	//
	// 也别把这里说成安全防线。真正的安全来自"服务器有权重算一切"
	// （命中判定、伤害数值、状态变更全在服务器算）。
	// 这里做的只是"别让明显的垃圾进缓存"。

	// 门 ①：标签白名单。
	// 服务器会照单全收任意 GameplayTag —— 不在映射表里的标签推进去也没用
	// （连段的消费只认自己那几个），但会白占一个缓存槽（容量 4，满了挤掉最旧的），
	// 把**合法**的连段输入挤出去。
	if (!GetRPGAbilitySystemComponent()
		|| !GetRPGAbilitySystemComponent()->HasAbilityForInputTag(InputTag))
	{
		UE_LOG(LogRPG_Ability, VeryVerbose,
			TEXT("[%s] 服务器丢弃未登记的输入标签：%s"), *GetName(), *InputTag.ToString());
		return;
	}

	if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())
	{
		// 门 ②：存活判断。
		// 客户端在 `OnAbilityInputPressed` 里已经判过 `IsAlive`，但那是在**客户端** ——
		// 服务器不能依赖客户端的判断。不判的话，改过的客户端可以在死亡期间
		// 往服务器缓存里塞条目（危害有限，重生时 `ClearInputBuffer` 会兜住，
		// 但"死人没有意图"这条规则在服务器侧不该是缺失的）。
		if (!RPGChar->IsAlive())
		{
			return;
		}

		if (URPG_CombatComponent* Combat = RPGChar->GetCombatComponent())
		{
			Combat->PushInputTag(InputTag);

			UE_LOG(LogRPG_Ability, VeryVerbose,
				TEXT("[%s] 服务器收到客户端输入：%s（缓存现有 %d 条）"),
				*GetName(), *InputTag.ToString(), Combat->GetBufferedInputCount());
		}
	}
}

void ARPG_PlayerController::OnAbilityInputReleased(FGameplayTag InputTag)
{
	UE_LOG(LogRPG_Ability, Verbose, TEXT("[%s] 输入释放：%s"), *GetName(), *InputTag.ToString());

	if (URPG_AbilitySystemComponent* ASC = GetRPGAbilitySystemComponent())
	{
		// 交给 ASC 去找"这个输入标签对应的、当前正在激活的能力"，
		// 然后调用它的 OnInputReleased()。
		//
		// 瞬发能力对这个调用无感（基类默认空实现）；
		// 按住型能力（重击蓄力）靠它知道玩家松手了。
		ASC->NotifyInputReleased(InputTag);
	}
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
