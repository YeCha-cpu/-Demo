// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/RPG_AnimInstanceBase.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Character/RPG_BaseCharacter.h"
#include "Combat/RPG_AttackModuleData.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_AnimInstanceBase::URPG_AnimInstanceBase()
{
	// 这里有意什么都不设。
	//
	// 特别提醒：**不要**在这里写 bUseMultiThreadedAnimationUpdate = false。
	// 网上有些教程为了"避免多线程问题"把它关掉，那是用性能换省事。
	// 本类的 NativeUpdateAnimation 只在游戏线程写成员变量，
	// AnimGraph 只在动画线程读，没有共享可变状态，天然是安全的。
}

// ══════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════

void URPG_AnimInstanceBase::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// ── 挂上蒙太奇生命周期诊断 ──
	// 这两个委托是**动态多播**，所以用 AddDynamic + UFUNCTION 回调。
	// 它们覆盖了蒙太奇的**全部**结束路径（自然播完 / 被别的蒙太奇顶掉 /
	// 能力结束 / 预测被服务器拒绝），所以不需要在 GA 那边再埋点。
	OnMontageStarted.AddDynamic(this, &URPG_AnimInstanceBase::HandleMontageStarted);
	OnMontageEnded.AddDynamic(this, &URPG_AnimInstanceBase::HandleMontageEnded);

	// 这时角色可能还没 Possess 完 / 还没 BeginPlay，
	// 拿不到也不报错 —— NativeUpdateAnimation 每帧都会再试一次。
	CacheOwnerCharacter();
}

namespace
{
	/**
	 * 蒙太奇被提前结束时是否打印调用栈。
	 *
	 * 默认 0（关）。排查"是谁把动画停掉的"时打开：
	 *     RPG.LogMontageInterruptStack 1
	 * 然后复现一次，日志里会直接给出**停它的那段代码**。
	 *
	 * 为什么默认关：调用栈很贵、而且很吵（连段时每段都会打一次）。
	 * 只在需要回答"凶手是谁"的时候开。
	 */
	static TAutoConsoleVariable<int32> CVarRPGLogMontageInterruptStack(
		TEXT("RPG.LogMontageInterruptStack"),
		0,
		TEXT("1 = 蒙太奇被提前结束时打印调用栈。排查\"谁把它停了\"用，很吵。"),
		ECVF_Default);
}

void URPG_AnimInstanceBase::HandleMontageStarted(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	// 按蒙太奇登记一条独立记录 —— 不能只存"当前那一个"，
	// 连段切段时两段会在同一帧重叠（见头文件里的说明）。
	FMontagePlayRecord& Record = MontagePlayRecords.FindOrAdd(Montage);
	Record.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Record.Length = Montage->GetPlayLength();

	// 用 Verbose：一次攻击会打好几条，正常游戏时没必要刷屏。
	// 排查手感问题时 `Log LogRPG_Animation Verbose` 打开即可。
	UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 蒙太奇开始：%s（全长 %.2fs）"),
		*GetNameSafe(GetOwningActor()), *Montage->GetName(), Record.Length);
}

void URPG_AnimInstanceBase::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!Montage)
	{
		return;
	}

	// 取出这条蒙太奇**自己**的记录。取不到说明我们没见到它的开始
	// （比如它在本 AnimInstance 初始化之前就在播了）——
	// 这种情况下老实说"没记录到"，而不是拿别人的数字硬凑。
	FMontagePlayRecord Record;
	const bool bHasRecord = MontagePlayRecords.RemoveAndCopyValue(Montage, Record);

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float PlayedFor = bHasRecord ? (Now - Record.StartTime) : -1.f;

	// ══════════════════════════════════════════════════════════════════
	//  ★ 这一条是给"出招动画有顿挫感"准备的
	// ══════════════════════════════════════════════════════════════════
	//  "顿挫"没法直接查，但可以变成数字：**它实际播了多久 vs 它本该播多久**。
	//
	//  正常播完时 PlayedFor ≈ 全长（差一点点是混合时间的正常误差）。
	//  如果明显短于全长，说明**有人在动画播完之前把它停掉了** ——
	//  那就不是"网络卡"，而是要找"是谁停的"。
	//
	//  ⚠️ 注意区分两类"被停"：
	//    · **设计如此**：轻击连段换段时，上一段就是被下一段顶掉的
	//      （`GA_LightAttack::StartSegment` 里的 StopCurrentSegmentMontage）
	//    · **可疑**：最后一段（AM_Light_05）没有下一段可接，
	//      它被打断就说明有别的东西在动手
	//  所以这条日志的价值在"看第几段被打断、打断了多少"，不在"有没有被打断"。
	//
	//  bInterrupted 是引擎直接给的判据，不是我们算的 —— 它永远可信。
	//
	//  ⚠️ 但**不能只看 bInterrupted**。`PlayMontageOrSkip` 建任务时用的是
	//  `bStopWhenAbilityEnds = true`，能力一结束它就会停掉蒙太奇 ——
	//  哪怕这段动画**已经播完了**，引擎也会报 `bInterrupted=true`。
	//  第一版日志里那些"实播 1.77s / 全长 1.77s（100%）但 bInterrupted=true"
	//  就是这么来的：**假警报**，把真正的问题淹掉了。
	//
	//  所以判据是"**实际播的时长明显短于全长**"，bInterrupted 只作为补充说明。
	const bool bPlayedMostOfIt = bHasRecord && Record.Length > KINDA_SMALL_NUMBER
		&& PlayedFor >= Record.Length * MontageCutShortWarnRatio;
	const bool bCutShort = bHasRecord && !bPlayedMostOfIt;

	if (bCutShort)
	{
		UE_LOG(LogRPG_Animation, Warning,
			TEXT("[%s] 蒙太奇被提前结束：%s —— 实播 %.2fs / 全长 %.2fs（%.0f%%），bInterrupted=%s"),
			*GetNameSafe(GetOwningActor()), *Montage->GetName(),
			PlayedFor, Record.Length,
			Record.Length > KINDA_SMALL_NUMBER ? (PlayedFor / Record.Length * 100.f) : 0.f,
			bInterrupted ? TEXT("true") : TEXT("false"));

		// 想知道"是谁停的"就把这个 CVar 打开：
		//     RPG.LogMontageInterruptStack 1
		if (CVarRPGLogMontageInterruptStack.GetValueOnGameThread() != 0)
		{
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
	}
	else
	{
		// 这里包含两种情况，都是**正常**的：
		//   · 自然播完
		//   · 播完了，随后被"能力结束"顺手停掉（引擎仍报 bInterrupted=true）
		UE_LOG(LogRPG_Animation, Verbose,
			TEXT("[%s] 蒙太奇结束：%s（实播 %.2fs / 全长 %.2fs，bInterrupted=%s）"),
			*GetNameSafe(GetOwningActor()), *Montage->GetName(),
			bHasRecord ? PlayedFor : -1.f, bHasRecord ? Record.Length : -1.f,
			bInterrupted ? TEXT("true") : TEXT("false"));
	}
}

void URPG_AnimInstanceBase::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// 兜底重取。AnimInstance 的初始化时机和角色初始化时机**不保证有先后**：
	// 蓝图里换 Mesh、运行时换 SkeletalMesh、编辑器预览，
	// 都会让 InitializeAnimation 早于角色就绪。
	if (!OwnerCharacter.IsValid())
	{
		CacheOwnerCharacter();
		if (!OwnerCharacter.IsValid())
		{
			return;
		}
	}

	// ★ 顺序有讲究：先战斗、后移动。
	// UpdateLocomotion 里判定 MovementState 时要读 bIsSprinting，
	// 而那个值在 UpdateCombatState 里才算出来。
	// 反过来的话，冲刺状态永远慢一帧 —— 表现为"起步瞬间播的是跑步动画"。
	UpdateCombatState();
	UpdateLocomotion(DeltaSeconds);

	// DeltaSeconds 目前用不到，但保留参数是为了将来做速度插值
	// （比如"停下时 Direction 缓慢归零"这种平滑处理）。
}

void URPG_AnimInstanceBase::CacheOwnerCharacter()
{
	// TryGetPawnOwner 在编辑器预览窗口（没有真实 Pawn）会返回 nullptr，
	// 所以这里判空是正常路径，不是错误。
	OwnerCharacter = Cast<ARPG_BaseCharacter>(TryGetPawnOwner());
}

// ══════════════════════════════════════════════════════════════════════
//  移动状态
// ══════════════════════════════════════════════════════════════════════

void URPG_AnimInstanceBase::UpdateLocomotion(float /*DeltaSeconds*/)
{
	ARPG_BaseCharacter* Character = OwnerCharacter.Get();
	if (!Character)
	{
		return;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	const FVector Velocity = Movement->Velocity;

	// ── 水平速度 ──
	// 用 Size2D 而不是 Size：跳跃上升时 Velocity.Z 有几百，
	// 混进去会让"边跳边前进"被判成超高速移动，混合空间直接跳到冲刺段。
	Speed = Velocity.Size2D();

	// ── 归一化速度的分母 ──
	// ★ 蹲伏时 UE 用的是另一个字段（MaxWalkSpeedCrouched），
	// 直接拿 MaxWalkSpeed 会让"蹲着走"的比值停在 180/300 = 0.6，
	// 混合空间卡在半速位置，看着像"蹲着慢跑"。
	const float CurrentMaxSpeed = Character->bIsCrouched
		? Movement->MaxWalkSpeedCrouched
		: Movement->MaxWalkSpeed;

	MaxSpeed = CurrentMaxSpeed;

	// 除零保护。MaxWalkSpeed 理论上不会为 0，但蓝图里手滑改成 0 是常见操作，
	// 而 0 除法的结果是 NaN —— NaN 进混合空间会让角色整个消失（顶点全变 NaN），
	// 现象非常吓人且极难定位到"原来是速度设成 0 了"。
	SpeedRatio = (CurrentMaxSpeed > KINDA_SMALL_NUMBER)
		? FMath::Clamp(Speed / CurrentMaxSpeed, 0.f, 1.f)
		: 0.f;

	// ── 移动方向 ──
	// 把世界坐标的速度转成"相对角色朝向"的局部向量，再取角度。
	// 约定与引擎的 UKismetAnimationLibrary::CalculateDirection 完全一致：
	//     正前 0° / 正右 +90° / 正左 -90° / 正后 ±180°
	// 保持一致很重要 —— 这样从任何教程里抄来的方向混合空间都能直接对得上，
	// 不用反着配一遍角度。
	//
	// 自己算而不是直接调引擎函数，是为了不额外依赖 AnimGraphRuntime 模块；
	// 数学只有三行，不值得为它加一个模块依赖。
	const FVector Velocity2D(Velocity.X, Velocity.Y, 0.f);

	if (!Velocity2D.IsNearlyZero())
	{
		// InverseTransformVector 只旋转不平移 —— 我们要的是方向不是位置，
		// 用 InverseTransformPosition 会把角色坐标也减进去，结果完全错。
		const FVector LocalVelocity =
			FRotationMatrix(Character->GetActorRotation()).InverseTransformVector(Velocity2D);

		Direction = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
	}
	// 速度为 0 时**保留上一帧的 Direction**，不归零。
	// 归零的话，角色停下的一瞬间方向会"啪"地弹回正前方，
	// 而这时混合空间还在往 Idle 淡出 —— 表现为停步时腿抽一下。

	VerticalVelocity = Velocity.Z;
	bIsInAir = Movement->IsFalling();
	bIsCrouching = Character->bIsCrouched;

	// ── Main 状态机的切换依据 ──
	// 判定顺序 = 优先级。腾空优先于一切：从空中落下时就算还按着冲刺键，
	// 也该播下落动画；蹲着从平台边缘掉下去同理。
	if (bIsInAir)
	{
		MovementState = ERPG_MovementState::InAir;
	}
	else if (bIsCrouching)
	{
		MovementState = ERPG_MovementState::Crouching;
	}
	else if (bIsSprinting && Speed > IdleSpeedThreshold)
	{
		// ══════════════════════════════════════════════════════════════
		//  ★ 标签**且**真的在动，两个条件都要
		// ══════════════════════════════════════════════════════════════
		// 光看标签会出一个很显眼的问题：
		//
		//   State.Sprinting 在"冲刺/追击期间"是**常驻**的，人站着不动它也在。
		//   这时 MovementState 会是 Sprinting → 动画蓝图用「跑步」那条状态机
		//   → 那条状态机的混合空间拿 SpeedRatio 当横轴，而站着时 SpeedRatio = 0
		//   → 播的是混合空间**最慢的那个采样点**（通常是走路）
		//   → 表现就是**原地踏步**。
		//
		// 两个具体症状（同一个根因）：
		//   · 敌人攻击后站在 Wait 里 → 走两步
		//   · 玩家站着按 Shift → 进入"行走"，松开才回 idle
		//
		// 补上速度判断就好了：站着不动就该待在 Grounded 分支，
		// 那条状态机里才有 Idle。
		//
		// 为什么阈值用 IdleSpeedThreshold（而不是另设一个大的）：
		// 它表达的是"算不算停下来了"，和速度归零的判断用同一把尺子，
		// 免得出现"动画说没停、但 SpeedRatio 已经是 0"这种前后不一致。
		//
		// 至于"会不会因为阈值抖动"——不会：
		//   进入跑步仍然由**标签**决定（离散事件，按 Shift 才跳变），
		//   速度条件只负责"停下来时退出"这一个方向。
		MovementState = ERPG_MovementState::Sprinting;
	}
	else
	{
		MovementState = ERPG_MovementState::Grounded;
	}
}

// ══════════════════════════════════════════════════════════════════════
//  战斗状态
// ══════════════════════════════════════════════════════════════════════

void URPG_AnimInstanceBase::UpdateCombatState()
{
	ARPG_BaseCharacter* Character = OwnerCharacter.Get();
	if (!Character) return;

	// ══════════════════════════════════════════════════════════════════
	//  每帧重新取 ASC，不做缓存
	// ══════════════════════════════════════════════════════════════════
	// 玩家的 ASC 挂在 PlayerState 上，而 PlayerState 会随重生、关卡切换、
	// 联机重连被整个替换掉。缓存 WeakObjectPtr 的话必须自己处理失效时机 ——
	// 漏一个就是"复活之后动画再也不对了"，而且只在特定流程下复现。
	//
	// 代价只是一次 Cast + 一次虚函数调用，一帧一次完全可忽略。
	// 拿一点点性能换掉一整类时序 bug，是划算的。
	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!ASC)
	{
		// ASC 还没初始化完（BeginPlay 之前会走到这里）——按"非战斗"处理。
		// 注意不能提前 return 而不赋值：成员变量会保留上一次的值，
		// 角色池复用（比如重生）时就会带着上个角色的攻击姿态出现。
		bIsAttacking = false;
		bIsDodging = false;
		bIsSprinting = false;
		bIsCharging = false;
		bIsInvulnerable = false;
		bIsDead = false;
		ChargeLevel = 0;
		return;
	}

	// ── 全部从 GameplayTag 读 ──
	// HasMatchingGameplayTag 是**层级匹配**：容器里有某个子标签时，
	// 查它的父标签同样返回 true。所以：
	//   · 查 State.Attacking 对 State.Attack.Windup / Active / Recovery 都成立
	//   · 蓄力挂的是 State.Attack.Charging.Lv2，查 State.Attack.Charging 也是 true
	// 这让我们能用"粗粒度"的查询做动画分支，不必逐个枚举子标签。
	bIsAttacking    = ASC->HasMatchingGameplayTag(RPGTags::State_Attacking);
	bIsDodging      = ASC->HasMatchingGameplayTag(RPGTags::State_Dodging);
	bIsSprinting    = ASC->HasMatchingGameplayTag(RPGTags::State_Sprinting);
	bIsCharging     = ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Charging);
	bIsInvulnerable = ASC->HasMatchingGameplayTag(RPGTags::State_Invulnerable);
	bIsDead         = ASC->HasMatchingGameplayTag(RPGTags::State_Dead);

	// 蓄力段位：从高往低查，第一个命中的就是当前段位。
	// 高段位标签存在时低段位一定不存在（GA 每次升段会先摘掉上一段的标签），
	// 所以这里不需要考虑"同时挂着 Lv1 和 Lv3"的情况。
	if (ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Charging_Lv3))
	{
		ChargeLevel = 3;
	}
	else if (ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Charging_Lv2))
	{
		ChargeLevel = 2;
	}
	else if (ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Charging_Lv1))
	{
		ChargeLevel = 1;
	}
	else
	{
		ChargeLevel = 0;
	}

	// ── 攻击模组与连段索引 ──
	// 这两个**不是** GameplayTag，因为它们不是"状态"而是"配置"和"进度"：
	//   · 当前拿什么武器 —— 是配置，换武器时才变
	//   · 打到第几段 —— 是进度，0~5 的连续量，标签表达不了
	// 所以走 CombatComponent 而不是硬塞进标签体系。
	if (const URPG_CombatComponent* Combat = Character->GetCombatComponent())
	{
		ComboIndex = Combat->GetComboIndex();

		if (const URPG_AttackModuleData* Module = Combat->GetAttackModule())
		{
			AttackModuleType = Module->ModuleType;
		}
	}
}

// ══════════════════════════════════════════════════════════════════════
//  调试
// ══════════════════════════════════════════════════════════════════════

FString URPG_AnimInstanceBase::GetAnimationDebugString() const
{
	static const UEnum* MoveStateEnum = StaticEnum<ERPG_MovementState>();

	const FString MoveStateName = MoveStateEnum
		? MoveStateEnum->GetNameStringByValue(static_cast<int64>(MovementState))
		: TEXT("?");

	return FString::Printf(
		TEXT("移动[%s] 速度 %.0f/%.0f (%.2f) 方向 %.0f° 垂直 %.0f | ")
		TEXT("攻击 %d 闪避 %d 冲刺 %d 蓄力 %d(段 %d) 无敌 %d 死亡 %d | 模组 %d 连段 %d"),
		*MoveStateName,
		Speed, MaxSpeed, SpeedRatio, Direction, VerticalVelocity,
		bIsAttacking ? 1 : 0,
		bIsDodging ? 1 : 0,
		bIsSprinting ? 1 : 0,
		bIsCharging ? 1 : 0, ChargeLevel,
		bIsInvulnerable ? 1 : 0,
		bIsDead ? 1 : 0,
		static_cast<int32>(AttackModuleType), ComboIndex);
}
