// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/RPG_BaseCharacter.h"

#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"

#include "AbilitySystem/RPG_AbilitySystemComponent.h"
#include "AbilitySystem/RPG_AttributeSet.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "UI/RPG_HUD.h"
#include "UI/RPG_OverheadHealthBarComponent.h"

ARPG_BaseCharacter::ARPG_BaseCharacter()
{
	// ══════════════════════════════════════════════════════════════════
	//  相机
	// ══════════════════════════════════════════════════════════════════
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = CameraBoomLength;
	CameraBoom->SetRelativeRotation(CameraBoomOffset);
	// 让弹簧臂跟随控制器的旋转 —— 这是"鼠标控制视角"的关键。
	// 关掉它的话，相机就只会跟着角色身体转，变成固定追尾视角。
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = CameraLagSpeed;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	// 相机自己不跟随控制器旋转——它已经挂在会旋转的弹簧臂末端了，
	// 再转一次会变成双重旋转。
	FollowCamera->bUsePawnControlRotation = false;

	// ══════════════════════════════════════════════════════════════════
	//  朝向策略
	// ══════════════════════════════════════════════════════════════════
	// 身体不跟随控制器旋转（否则鼠标一动人物原地转圈），
	// 而是由"移动方向"驱动 —— 这才是第三人称动作游戏的手感。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, RotationRateYaw, 0.f);
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->JumpZVelocity = 600.f;
	Movement->AirControl = 0.35f;

	// 没有这一句，Crouch() 会静默失败（角色蹲不下去，也不报错，很难查）
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;

	// ── 战斗组件 ──
	// 敌我共用，且不依赖 ASC，所以放在基类而不是两个子类里各建一份。
	CombatComponent = CreateDefaultSubobject<URPG_CombatComponent>(TEXT("CombatComponent"));

	// ══════════════════════════════════════════════════════════════════
	//  头顶血条
	// ══════════════════════════════════════════════════════════════════
	// 用项目自己的组件子类而不是引擎的 UWidgetComponent ——
	// 它多做一件关键的事：把"我是谁的血条"显式告诉 Widget。
	// 少了那一步，敌人的血条会去问 GetOwningPlayerPawn()，
	// 拿到的却是**本地玩家**，于是每条血条都显示玩家自己的血量。
	// 详见 RPG_OverheadHealthBarComponent.h。
	OverheadHealthBar = CreateDefaultSubobject<URPG_OverheadHealthBarComponent>(TEXT("OverheadHealthBar"));
	OverheadHealthBar->SetupAttachment(RootComponent);

	// 位置：胶囊体顶端再往上一点。
	// 90 是默认半高（胶囊总高 180），+20 留出一点缝隙。
	OverheadHealthBar->SetRelativeLocation(FVector(0.f, 0.f, 110.f));

	// 血条不投影。地面上跟着角色跑的方块阴影很出戏。
	OverheadHealthBar->CastShadow = false;

	// 碰撞 / 绘制尺寸 / 可见性都在组件自己的构造函数里设 ——
	// 那些是"这个组件是什么"的属性，不该散落在每个使用者的构造函数里。
}

void ARPG_BaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// ══════════════════════════════════════════════════════════════════
	//  把蓝图里配置的参数同步到组件上
	// ══════════════════════════════════════════════════════════════════
	// 为什么构造函数里设了还要在这里再设一遍？
	// 因为构造函数只在 CDO（类默认对象）创建时执行一次。你在蓝图子类里把
	// CameraBoomLength 从 400 改成 600，**不会**重新执行构造函数——
	// 那时组件上的值仍然是 400。必须在运行时同步一次，蓝图配置才真的生效。
	//
	// 这是 UE 里非常经典的一个坑，凡是"UPROPERTY 的值影响组件属性"都要这么处理。
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = CameraBoomLength;
		CameraBoom->SetRelativeRotation(CameraBoomOffset);
		CameraBoom->bEnableCameraLag = CameraLagSpeed > 0.f;
		CameraBoom->CameraLagSpeed = FMath::Max(CameraLagSpeed, 1.f);
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->RotationRate = FRotator(0.f, RotationRateYaw, 0.f);
		Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	}

	// 布娃娃复位要用的基准值。见头文件里为什么不能放构造函数。
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		CachedMeshRelativeTransform = MeshComp->GetRelativeTransform();
		CachedMeshCollisionProfile = MeshComp->GetCollisionProfileName();
	}
	if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		CachedCapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
	}
	CachedSpawnTransform = GetActorTransform();

	// 走统一的入口，而不是直接写 WalkSpeed ——
	// 万一将来加了"出生即在战斗中"之类的配置，这里不用再改一次。
	RefreshMaxWalkSpeed();

	// 头顶血条：先按"现在已知的控制关系"判一次。
	// 联机时这次判断可能还不准（Controller 还没复制过来），
	// 会在 OnRep_Controller 里再刷一次 —— 见 RefreshOverheadWidgetVisibility。
	RefreshOverheadWidgetVisibility();
}

void ARPG_BaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// ⚠️ 这个函数是**必需**的，不能只靠 BeginPlay。
	//
	// 引擎的生成顺序是：GameMode 先 SpawnActor（→ PostActorConstruction
	//   → DispatchBeginPlay），然后才调 FinishRestartPlayer → Possess。
	// 也就是说 **BeginPlay 跑在 Possess 之前** ——
	// 那一刻 Controller 还是空的，判据必然返回 false，
	// 结果就是"本地玩家自己的头顶血条被显示出来"。
	RefreshOverheadWidgetVisibility();
}

void ARPG_BaseCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	// 客户端侧：Controller 是通过复制才到达的，在那之前判据一直返回 false。
	// 少了这一处，客户端上每个玩家的头顶血条都会显示出来 —— 包括自己那个。
	RefreshOverheadWidgetVisibility();
}

bool ARPG_BaseCharacter::IsLocallyControlledPlayer() const
{
	const AController* Ctrl = GetController();

	// ① 必须是**玩家**控制器。
	//    这一条是关键 —— 见头文件里对 AController::IsLocalController()
	//    那两条 return true 的说明。少了它，单机下所有敌人都算"本地玩家"。
	if (!Ctrl || !Ctrl->IsA<APlayerController>())
	{
		return false;
	}

	// ② 而且必须是"我这台机器上的"那一个。
	//    listen server 主机上有多个 PlayerController，只有一个是自己的；
	//    远端玩家的 PlayerController 在主机上也会存在，但它不是本地的。
	return Ctrl->IsLocalController();
}

void ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()
{
	if (!OverheadHealthBar)
	{
		return;
	}

	// ── 本地玩家自己不显示头顶血条 ──
	// 需求："除本地玩家外的其他玩家、AI 要有头顶血条"。
	//
	// ★ 判据必须是"**被本地 PlayerController 占有**"，不能直接用
	//   `IsLocallyControlled()` —— 那个函数在敌人身上**恒为 true**。
	//
	//   原因在 AController::IsLocalController()（Controller.cpp:90-113）：
	//     · 第 94-98 行：NM_Standalone 下**无脑 return true**
	//     · 第 106-110 行：GetLocalRole() == ROLE_Authority 且
	//       RemoteRole 不是 AutonomousProxy 时 return true
	//   服务器上的 AIController 正好满足第二条（它是权威，且不是谁的
	//   AutonomousProxy）。Standalone 下连第一条都直接命中。
	//
	//   结果：拿 IsLocallyControlled() 当判据，**所有敌人在单机下都会被判定成
	//   "本地玩家"，血条全被藏起来**。而且它在客户端上是对的（客户端上
	//   复制过来的 AIController 不是权威），所以这个 bug 只在单机/主机上出现。
	const bool bAmLocalPlayer = IsLocallyControlledPlayer();
	OverheadHealthBar->SetVisibility(!bAmLocalPlayer);

	// 打一条日志说明这次判定的结果。
	//
	// 为什么值得占用 Log 级别："敌人头顶没血条"有好几种成因 ——
	// WBP 没建、组件上没配 Widget Class、可见性判反了 ——
	// 而它们的现象**一模一样**。这条日志能把最后一种直接排除掉，
	// 省掉一轮"改代码加日志再编译"的往返。
	// 频率是每个角色每次控制关系变化一条，不会刷屏。
	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 头顶血条：%s（控制器 %s）"),
		*GetName(),
		bAmLocalPlayer ? TEXT("隐藏（我是本地玩家自己）") : TEXT("显示"),
		*GetNameSafe(GetController()));

	// Widget Class 没配是个静默失败：组件在那儿、可见性也对，就是什么都不显示。
	// 值得在生成时报一次 —— 这类问题靠肉眼排查会绕很远。
	if (!bAmLocalPlayer && !OverheadHealthBar->GetWidgetClass())
	{
		UE_LOG(LogRPG_Combat, Warning,
			TEXT("[%s] 头顶血条没有设置 Widget Class —— 它不会显示任何东西。"
			     "请在角色蓝图的 OverheadHealthBar 组件上指定 WBP_OverheadHealthBar"),
			*GetName());
	}
}

void ARPG_BaseCharacter::Multicast_ShowDamageNumber_Implementation(
	float Amount, FVector_NetQuantize Location)
{
	// 谁负责把它变成屏幕上的一个数字？—— HUD。
	//
	// 角色不该知道"飘字 widget 长什么样、放在哪、用什么动画"，
	// 那是表现层的事。这里只做"广播事实"，由 HUD 决定怎么画。
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ⚠️ 必须挑**本地控制**的那个 PlayerController，不能用
	// `GetFirstPlayerController()` —— 那个函数返回的是 PlayerControllerList[0]，
	// **没有任何 IsLocalController 过滤**（World.cpp:6515-6533）。
	// listen server 主机上列表里的第一个恰好是主机自己，看起来能用；
	// 但那是"碰巧"而不是"保证"，换个创建顺序就会静默失效。
	//
	// 挑本地那个才能保证"这一端的屏幕上冒一次数字"：
	//   · 服务器：只有主机自己那一个 PC 是本地的
	//   · 客户端：只有自己那一个 PC 是本地的
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}

		if (ARPG_HUD* HUD = PC->GetHUD<ARPG_HUD>())
		{
			HUD->ShowDamageNumber(Amount, Location);
		}

		// 一个客户端只有一个本地 PC，找到就够了。
		break;
	}
}

// ══════════════════════════════════════════════════════════════════════
//  接口实现
// ══════════════════════════════════════════════════════════════════════

UAbilitySystemComponent* ARPG_BaseCharacter::GetAbilitySystemComponent() const
{
	return GetASCInternal();
}

URPG_AttributeSet* ARPG_BaseCharacter::GetRPGAttributeSet() const
{
	const UAbilitySystemComponent* ASC = GetASCInternal();
	if (!ASC)
	{
		return nullptr;
	}

	// GetSet 返回 const 指针——因为它只是"找到并返回"，不承诺可写。
	// 但属性集本身是需要被 GE 修改的（GE 内部持有非 const 指针），
	// 所以这里去掉 const 限定是安全且必要的。
	return const_cast<URPG_AttributeSet*>(ASC->GetSet<URPG_AttributeSet>());
}

bool ARPG_BaseCharacter::IsAlive() const
{
	const UAbilitySystemComponent* ASC = GetASCInternal();

	if (!ASC)
	{
		// ASC 尚未初始化完（BeginPlay 之前会被调用到）——按"存活"处理。
		// 如果这里返回 false，刚生成的角色会在第一帧被 AI 当成尸体忽略掉。
		return true;
	}

	return !ASC->HasMatchingGameplayTag(RPGTags::State_Dead);
}

// ══════════════════════════════════════════════════════════════════════
//  角色动作
// ══════════════════════════════════════════════════════════════════════

void ARPG_BaseCharacter::Move(const FInputActionValue& Value)
{
	// ── 死了就不接受移动输入 ★ ──
	// 这是**唯一**一处挡住"死亡期间还能跑"的地方，因为：
	//   · 移动不是能力，ActivationBlockedTags 拦不住它
	//   · 从血归零到 GA_Death 把移动模式关掉之间有一段蒙太奇时间，
	//     没有这道判断，玩家会在弥留之际满场跑
	//
	// 放在 Move() 而不是用 Controller->SetIgnoreMoveInput()：
	// 后者不复制，服务器上设了客户端不知道，联机下等于没设。
	// 而 State.Dead 标签是复制的，两端行为天然一致。
	if (!IsAlive()) return;

	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (!Controller || MovementVector.IsNearlyZero()) return;

	// 用**控制器的** Yaw 而不是角色自身的 Yaw 作为参考系。
	// 这样"按 W"永远是"朝屏幕前方走"，而不是"朝角色面朝方向走"——
	// 后者在角色背对镜头时会变成往镜头方向跑，手感很差。
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// 增强输入的 2D 轴约定：X = 左右，Y = 前后。
	// 注意这里和第二行是交叉的（前后用 Y、左右用 X），别写反。
	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void ARPG_BaseCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (!Controller) return;

	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

void ARPG_BaseCharacter::StartSprint()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = SprintSpeed;
	}
}

void ARPG_BaseCharacter::StopSprint()
{
	// 回落到"当前状态对应"的速度，而不是无条件回 WalkSpeed——
	// 否则蹲着跑完松开按键，角色会突然站起来以行走速度移动。
	// 交给统一的入口算，战斗/蹲伏/常态三种状态的优先级只有一份定义。
	RefreshMaxWalkSpeed();
}

void ARPG_BaseCharacter::SetCombatMovement(bool bInCombat)
{
	if (bInCombat == bInCombatMovement)
	{
		return;
	}

	bInCombatMovement = bInCombat;
	RefreshMaxWalkSpeed();

	// ══════════════════════════════════════════════════════════════════
	//  让动画也跟着切到"跑" —— 只改 MaxWalkSpeed 是不够的
	// ══════════════════════════════════════════════════════════════════
	// 动画蓝图靠 **State.Sprinting 标签**决定用哪条移动状态机
	// （见 URPG_AnimInstanceBase::UpdateLocomotion），而那个标签
	// 原本只有 GA_Sprint 会挂。
	//
	// 敌人没有 GA_Sprint，所以它的 MovementState 一直停在 Grounded ——
	// 走的是"走路"那条状态机。表现就是"人明明变快了，看着还在走"。
	//
	// 补上标签后，MovementState 变成 Sprinting，动画切换到跑步状态机。
	//
	// 标签的语义在这里要放宽理解：它表示"**以高于行走的速度移动**"，
	// 玩家的冲刺和 AI 的追击都算 —— 它们对动画的要求是一样的。
	if (UAbilitySystemComponent* ASC = GetASCInternal())
	{
		const FGameplayTagContainer SprintTag(RPGTags::State_Sprinting);

		// 用 CountToOwner 而不是默认的"不复制"：
		// 敌人的动画在**每个客户端**上都要各自求值，标签必须复制过去，
		// 否则会出现"服务器上在跑、客户端上在走"。
		//
		// CountToOwner 的语义是"标签复制给所有人，只有计数只发给拥有者"
		// （EGameplayTagReplicationState，GameplayEffectTypes.h:1050-1057），
		// 对 HasMatchingGameplayTag 这类布尔查询完全够用。
		const EGameplayTagReplicationState RepState = EGameplayTagReplicationState::CountToOwner;

		if (bInCombat)
		{
			ASC->AddLooseGameplayTags(SprintTag, 1, RepState);
		}
		else
		{
			ASC->RemoveLooseGameplayTags(SprintTag, 1, RepState);
		}
	}

	// ══════════════════════════════════════════════════════════════════
	//  ⚠️ 为什么这里不需要处理"客户端也要知道"
	// ══════════════════════════════════════════════════════════════════
	// `UCharacterMovementComponent::MaxWalkSpeed` 在引擎里**不是复制属性**
	// （CharacterMovementComponent.h:274，UPROPERTY 上没有 replicated 标记）。
	// 所以这个改动只影响服务器 —— 客户端的 MaxWalkSpeed 会保持原值。
	//
	// 对本项目来说这不是问题，原因有两层：
	//
	//   ① **移动本身是复制的**。敌人的位置由 CharacterMovement 的
	//      复制通道同步给客户端，客户端看到的就是正确的移动速度，
	//      不需要知道 MaxWalkSpeed 是多少。
	//
	//   ② **AI 的移动不做客户端预测**。客户端的 MaxWalkSpeed 只被
	//      动画蓝图用来算 SpeedRatio，而 SpeedRatio 在算出来之后会被
	//      Clamp 到 0~1 —— 即使分母是旧的 300，追人时的比值也只是
	//      被封顶到 1.0，表现上仍然是"全速跑"。方向是对的，只是失去
	//      了中间的过渡区间。
	//
	// 如果将来要做"敌人被减速 50%"这类会让 SpeedRatio 变得不准的效果，
	// 就得把 bInCombatMovement / 速度倍率做成 ReplicatedUsing 的属性，
	// 在 OnRep 里重新算一遍。目前不需要。
	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] %s战斗移动（MaxWalkSpeed = %.0f）"),
		*GetName(), bInCombat ? TEXT("进入") : TEXT("退出"),
		GetCharacterMovement() ? GetCharacterMovement()->MaxWalkSpeed : 0.f);
}

void ARPG_BaseCharacter::RefreshMaxWalkSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// 优先级：蹲伏 > 战斗 > 常态。
	//
	// 为什么蹲伏排在最前：蹲着移动时 UE 用的是另一个字段
	// （MaxWalkSpeedCrouched），但我们的蹲伏和战斗是有可能叠加的
	// （敌人蹲着巡逻时发现玩家）。这时"蹲着"是更强的约束。
	//
	// 冲刺不参与这个优先级 —— 它由 StartSprint() 直接写 SprintSpeed，
	// 只在能力激活期间生效，结束时调 StopSprint() 回到这里重新算。
	if (bIsCrouched)
	{
		Movement->MaxWalkSpeed = CrouchSpeed;
	}
	else if (bInCombatMovement)
	{
		Movement->MaxWalkSpeed = CombatMoveSpeed;
	}
	else
	{
		Movement->MaxWalkSpeed = WalkSpeed;
	}
}

void ARPG_BaseCharacter::ToggleCrouch()
{
	if (bIsCrouched) UnCrouch();
	else  Crouch();
}

// ══════════════════════════════════════════════════════════════════════
//  受击表现
// ══════════════════════════════════════════════════════════════════════

UAnimMontage* ARPG_BaseCharacter::PickHitReactMontage() const
{
	if (HitReactMontages.Num() == 0)
	{
		return nullptr;
	}

	// ── 从池子里挑一个非空的 ──
	// 逐个判空而不是直接随机取值：数组里留了 None 槽（美术删了蒙太奇、
	// 或配置时手滑加了一行）不该变成运行时崩溃。
	TArray<UAnimMontage*, TInlineAllocator<8>> ValidMontages;
	ValidMontages.Reserve(HitReactMontages.Num());
	for (const TObjectPtr<UAnimMontage>& Montage : HitReactMontages)
	{
		if (Montage)
		{
			ValidMontages.Add(Montage);
		}
	}

	if (ValidMontages.Num() == 0)
	{
		return nullptr;
	}

	// ⚠️ 随机只在**服务器**上发生一次，客户端不参与挑选 ——
	// 蒙太奇信息（RepAnimMontageInfo）是复制属性，服务器播了哪个，
	// 客户端就跟着播哪个。两端各自随机的话，会随机到不同的蒙太奇。
	return ValidMontages[FMath::RandRange(0, ValidMontages.Num() - 1)];
}

// ══════════════════════════════════════════════════════════════════════
//  布娃娃
// ══════════════════════════════════════════════════════════════════════

void ARPG_BaseCharacter::EnterRagdoll()
{
	if (bRagdollEnabled)
	{
		return;
	}

	bRagdollEnabled = true;

	// ⚠️ OnRep 在**服务器上不会被自动调用** —— 它只在属性复制到客户端时触发。
	// 所以这里手动调一次，让服务器本体的表现和客户端一致。
	// 少了这一句，表现是"客户端上敌人倒了，服务器（也就是 PIE 里的主机自己）看着还站着"。
	OnRep_RagdollEnabled();
}

void ARPG_BaseCharacter::ExitRagdoll()
{
	// ⚠️ 幂等守卫是**必须**的，不是优化：
	// 属性初次复制到客户端时，引擎会为所有复制属性调用一次 OnRep，
	// 包括值等于默认值的那些。也就是说活着的角色也会收到一次
	// OnRep_RagdollEnabled(false)，没有这个守卫就会白白跑一遍"站起来"的流程
	// （把移动模式重设成 Walking、把网格变换拨回去……在跳跃中触发时尤其明显）。
	if (!bRagdollEnabled)
	{
		return;
	}

	bRagdollEnabled = false;
	OnRep_RagdollEnabled();
}

void ARPG_BaseCharacter::OnRep_RagdollEnabled()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	if (bRagdollEnabled)
	{
		// ── 倒下去 ──

		// ① 先停移动。
		// 不停的话，CharacterMovement 下一帧还会按输入方向推胶囊体，
		// 而网格已经在自己模拟物理了 —— 表现是"尸体在地上滑行"。
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}

		// ② 胶囊体让位。
		// 还开着碰撞的话，物理网格会和"套着自己的那个胶囊体"顶在一起：
		// 尸体要么悬在半空，要么原地高频抖动。
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		// ③ 交给物理。
		// "Ragdoll" 是引擎自带的碰撞预设（PhysicsBody + 忽略 Pawn 通道），
		// 直接用它比自己拼一串响应设置更不容易错。
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		MeshComp->SetAllBodiesSimulatePhysics(true);
		MeshComp->SetSimulatePhysics(true);
		MeshComp->WakeAllRigidBodies();

		// bBlendPhysics 让"动画姿势"平滑过渡到"物理模拟"。
		// （`SetSimulatePhysics(true)` 内部已经置过这个标志了
		//   —— SkeletalMeshComponentPhysics.cpp:362-372，
		//   这里显式写一遍是为了让"为什么要混合"这件事在代码里可见。）
		MeshComp->bBlendPhysics = true;
	}
	else
	{
		// ── 站起来 ──

		// 先清速度再关模拟。
		// 不清的话，各骨骼会保留倒地时的线速度/角速度 ——
		// 下一次再进布娃娃时这些陈旧速度会被直接拿来用，
		// 表现是"第二次死亡时尸体猛地弹一下"。
		// 注意参数是 FVector（UE 5.8 里没有接受 float 的那版重载了）
		MeshComp->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		MeshComp->SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

		MeshComp->bBlendPhysics = false;
		MeshComp->SetAllBodiesSimulatePhysics(false);
		MeshComp->SetSimulatePhysics(false);

		// 物理一关，网格会停在"最后被模拟到的那个姿势"上。必须显式拨回去，
		// 否则胶囊体在原地、网格却躺在三米外 —— 而且它**不会自己恢复**。
		if (CachedMeshRelativeTransform.IsSet())
		{
			MeshComp->SetRelativeTransform(CachedMeshRelativeTransform.GetValue());
		}

		// 还原成**出生时那个**预设，而不是硬写 "CharacterMesh" ——
		// 蓝图里可能配了自定义的碰撞预设，硬写会把它悄悄改掉。
		// 拿不到缓存时（BeginPlay 之前就被调用）才回落。
		MeshComp->SetCollisionProfileName(
			CachedMeshCollisionProfile.IsNone() ? FName(TEXT("CharacterMesh")) : CachedMeshCollisionProfile);

		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(CachedCapsuleCollisionEnabled);
		}

		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			// Walking 而不是 None：复活后本来就该是站着的。
			// 至于"脚下有没有地、要不要转成 Falling"，CharacterMovement 下一帧自己会纠正。
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
}

// ══════════════════════════════════════════════════════════════════════
//  重生
// ══════════════════════════════════════════════════════════════════════

void ARPG_BaseCharacter::StartRespawnCountdown()
{
	// 计时只在服务器跑。客户端上这个 TimerHandle 永远是空的，
	// 它看到的"复活"是服务器传送 + 属性复制过来的结果。
	if (!HasAuthority())
	{
		return;
	}

	if (RespawnDelay <= 0.f)
	{
		UE_LOG(LogRPG_Combat, Log, TEXT("[%s] RespawnDelay 为 0，不重生（尸体保留）"), *GetName());
		return;
	}

	GetWorldTimerManager().SetTimer(
		RespawnTimerHandle, this, &ARPG_BaseCharacter::PerformRespawn, RespawnDelay, /*bLoop=*/false);

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] %.1f 秒后重生"), *GetName(), RespawnDelay);
}

FTransform ARPG_BaseCharacter::GetRespawnTransform() const
{
	// 基类默认回出生点。敌人用这个 —— 它在关卡里摆在哪就在哪复活。
	// 玩家会覆写成"找 PlayerStart"（见 ARPG_Player）。
	return CachedSpawnTransform;
}

void ARPG_BaseCharacter::PerformRespawn()
{
	if (!HasAuthority())
	{
		return;
	}

	// ① 先复位状态，再传送。顺序反了的话见头文件里的说明。
	ResetForRespawn();

	// ② 挪回出生点。
	// TeleportPhysics 而不是默认的 TeleportNone —— 布娃娃刚关掉、
	// 物理场景里还留着网格的旧位置，不通知物理线程的话它会把角色拽回去。
	const FTransform RespawnTransform = GetRespawnTransform();
	SetActorTransform(RespawnTransform, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// ③ 通知子类（敌人在这里重启 AI，玩家在那里同步视角）
	OnRespawned();

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 已重生到 %s"), *GetName(),
		*RespawnTransform.GetLocation().ToCompactString());
}

void ARPG_BaseCharacter::ResetForRespawn()
{
	// ── ① 从地上爬起来 ──
	if (bRagdollEnabled)
	{
		ExitRagdoll();
	}

	// ── ② 清掉身上的 GAS 状态 ──
	if (UAbilitySystemComponent* ASC = GetASCInternal())
	{
		// 死亡标记是 loose tag，不是 GE —— RemoveActiveEffects 清不掉它，得单独摘。
		// 不摘的话新角色一出生就带着 State.Dead，所有能力被 ActivationBlockedTags 挡死，
		// 表现是"复活了但一个键都按不动"，且不会有任何报错。
		ASC->RemoveLooseGameplayTags(
			FGameplayTagContainer(RPGTags::State_Dead), 1, EGameplayTagReplicationState::CountToOwner);

		// 把还挂着的 GE 全清掉：流血、Buff、冷却、无敌帧……
		// 留着它们会出现"复活后还带着上辈子的减速 debuff"这种超自然现象。
		ASC->RemoveActiveEffects(FGameplayEffectQuery());

		// ── ③ 属性回满 ──
		// 重新应用一次初始化 GE，而不是 SetHealth(GetMaxHealth())：
		// 走 GE 才能保证"属性怎么初始化"这件事只有一份定义 ——
		// 哪天初始 GE 里加了护盾、加了初始耐力，重生逻辑不用跟着改。
		if (InitAttributesEffect)
		{
			FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
			Context.AddSourceObject(this);

			ASC->ApplyGameplayEffectToSelf(
				InitAttributesEffect->GetDefaultObject<UGameplayEffect>(), 1.f, Context);
		}
		else
		{
			UE_LOG(LogRPG_Ability, Warning,
				TEXT("[%s] 没配 InitAttributesEffect，重生后属性不会恢复"), *GetName());
		}

		// ── ③.5 把常驻被动能力重新拉起来 ★ ──
		// 死亡时 CancelAllAbilities() 把 GA_StaminaRegen 也停掉了，
		// 不重启的话"复活后耐力永远不恢复"，而且不报错。
		// 详见 URPG_AbilitySystemComponent::ReactivatePassiveAbilities。
		if (URPG_AbilitySystemComponent* RPGASC = Cast<URPG_AbilitySystemComponent>(ASC))
		{
			RPGASC->ReactivatePassiveAbilities();
		}
	}

	// ── ④ 清空战斗状态 ──
	if (CombatComponent)
	{
		// 不清的话，死亡瞬间按下的那一堆输入会被带进新一条命：
		// 复活后角色自己就动起来了 —— 玩家会觉得"角色不受控制"。
		CombatComponent->ClearInputBuffer();
		CombatComponent->ResetCombo();
	}
}

// ══════════════════════════════════════════════════════════════════════
//  网络复制
// ══════════════════════════════════════════════════════════════════════

void ARPG_BaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 只复制"倒没倒"这个开关，物理本身各端各自模拟 —— 理由见头文件。
	// COND_None：要复制给所有客户端，包括"看着这个敌人"的那几个。
	DOREPLIFETIME_CONDITION(ARPG_BaseCharacter, bRagdollEnabled, COND_None);
}
