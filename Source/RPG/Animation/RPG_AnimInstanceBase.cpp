// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/RPG_AnimInstanceBase.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Character/RPG_BaseCharacter.h"
#include "Combat/RPG_AttackModuleData.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"

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

	// 这时角色可能还没 Possess 完 / 还没 BeginPlay，
	// 拿不到也不报错 —— NativeUpdateAnimation 每帧都会再试一次。
	CacheOwnerCharacter();
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
	else if (bIsSprinting)
	{
		// ★ 用标签判定，而不是 Speed > 某个阈值。
		// 阈值法在"从冲刺减速回走路"的过程中会来回抖动：
		// 速度跨过阈值 → 切回走路 → 速度又因为还没降下来而超阈值 → 又切回冲刺。
		// 标签是离散的，从按下到松开只有一次跳变，不会抖。
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
	if (!Character)
	{
		return;
	}

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
