// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "InputActionValue.h"
#include "Interfaces/RPG_AbilitySystemInterface.h"
#include "RPG_BaseCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;
class URPG_AttributeSet;
class URPG_CombatComponent;
class UGameplayAbility;
class UGameplayEffect;
class URPG_OverheadHealthBarComponent;

/**
 * 玩家与敌人的共同基类：
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【职责边界】
 * ══════════════════════════════════════════════════════════════════════
 * 负责：相机与弹簧臂、移动参数、接口实现、"角色能做的动作"（Move/Look/Sprint/Crouch）
 *
 * 不负责：
 *   · 输入绑定        → RPG_PlayerController（玩家输入一律走 PC，这是项目硬性约定）
 *   · ASC 的创建      → RPG_PlayerState（玩家）/ RPG_Enemy（敌人）
 *   · 能力的具体逻辑  → 各个 GA
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么基类实现接口，而 ASC 由子类提供】
 * ══════════════════════════════════════════════════════════════════════
 * 接口要求"能拿到 ASC"，但 ASC 在哪对玩家和敌人是不一样的：
 *     玩家 → PlayerState 上		敌人 → 自己身上		可能还会有更多角色类型......
 *
 * 与其让两个子类各写一遍接口实现，不如基类实现接口、把"取 ASC"抽成一个虚函数
 * GetASCInternal()，子类只实现这一个函数。这样：
 *   · 接口实现只有一份，行为绝对一致
 *   · 新增角色类型（比如召唤物）只需实现 GetASCInternal()
 *   · IsAlive / GetRPGAttributeSet 这类派生查询自动可用
 */
UCLASS(Abstract)
class RPG_API ARPG_BaseCharacter : public ACharacter,
                                    public IAbilitySystemInterface,
                                    public IRPG_AbilitySystemInterface
{
	GENERATED_BODY()

public:
	ARPG_BaseCharacter();

	//~ Begin IAbilitySystemInterface
	/** 引擎接口：GAS 内部机制依赖它，必须实现 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ Begin IRPG_AbilitySystemInterface
	virtual URPG_AttributeSet* GetRPGAttributeSet() const override;
	virtual bool IsAlive() const override;
	//~ End IRPG_AbilitySystemInterface

	// ══════════════════════════════════════════════════════════════════
	//  角色动作
	//  由 RPG_PlayerController 绑定输入后调用。放在这里而不是 PC 里，是因为
	//  这些是"角色会做什么"，而不是"哪个键触发"——后者才是 PC 的职责。
	// ══════════════════════════════════════════════════════════════════

	/** 移动。Value 是 2D 向量：X = 左右，Y = 前后 */
	void Move(const FInputActionValue& Value);

	/** 视角。Value 是 2D 向量：X = 水平旋转，Y = 俯仰 */
	void Look(const FInputActionValue& Value);

	/**
	 * 开始奔跑（切到冲刺速度）。
	 * 阶段 3 起由 GA_Sprint 调用它并负责耐力消耗，输入不再直接调这里。
	 */
	void StartSprint();

	/** 停止奔跑，速度回落到当前姿态对应的值 */
	void StopSprint();

	/** 蹲伏/起立切换 */
	void ToggleCrouch();

	/**
	 * 切换"战斗移动"状态。
	 *
	 * 进入战斗时用 CombatMoveSpeed 跑起来，脱离战斗回落到常态速度。
	 * 由 AI 在感知到目标时调用 —— 见 ARPG_AIController::SetCombatState()。
	 *
	 * **幂等**：传入值和当前状态相同就直接返回。感知回调在"进入/离开视野"
	 * 时各调一次，但服务超时、行为树中断等路径也可能重复调，
	 * 加个判断省掉无谓的属性写入（写 MaxWalkSpeed 会触发寻路参数更新）。
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Movement")
	void SetCombatMovement(bool bInCombat);

	UFUNCTION(BlueprintPure, Category = "RPG|Movement")
	bool IsInCombatMovement() const { return bInCombatMovement; }

	/** 角色当前的基础移动速度（行走） */
	UFUNCTION(BlueprintPure, Category = "RPG|Movement")
	float GetWalkSpeed() const { return WalkSpeed; }

	/**
	 * 战斗组件：输入缓存、连段索引、当前攻击模组。
	 * GA 通过它读取"当前该打第几段"。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|Combat")
	URPG_CombatComponent* GetCombatComponent() const { return CombatComponent; }

	// ══════════════════════════════════════════════════════════════════
	//  死亡与重生
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 进入布娃娃状态：角色失去控制，网格交给物理引擎。
	 *
	 * 由 GA_Death 在死亡蒙太奇播完后调用。**不要直接调它来表示"死亡"** ——
	 * 死亡是一个过程（蒙太奇 → 倒地），布娃娃只是最后一步。
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Death")
	void EnterRagdoll();

	/** 退出布娃娃，把网格挂回胶囊体并恢复移动 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Death")
	void ExitRagdoll();

	UFUNCTION(BlueprintPure, Category = "RPG|Death")
	bool IsRagdoll() const { return bRagdollEnabled; }

	/**
	 * 复活前的状态复位：退出布娃娃、摘掉死亡标记、属性回满、清空输入缓存。
	 *
	 * **不含传送** —— 传送到哪由重生逻辑决定（玩家去 PlayerStart、
	 * 敌人回出生点），这里只负责"把这个人恢复成能动的状态"。
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Death")
	void ResetForRespawn();

	/**
	 * 从受击蒙太奇池里随机挑一个。**只挑不播。**
	 *
	 * 播放交给 GA 的 AbilityTask —— 那是唯一能正确跟踪"蒙太奇什么时候结束"
	 * 的地方，而受击/死亡的表现时序完全依赖这个信号。
	 * 角色这边只负责回答"我该播哪个"。
	 *
	 * @return 没配、或配的槽位全是 None 时返回 nullptr
	 */
	UAnimMontage* PickHitReactMontage() const;

	/** 死亡蒙太奇（没配返回 nullptr） */
	UAnimMontage* GetDeathMontage() const { return DeathMontage; }

	UFUNCTION(BlueprintPure, Category = "RPG|Animation|Hit")
	float GetHitReactPlayRate() const { return HitReactPlayRate; }

	UFUNCTION(BlueprintPure, Category = "RPG|Animation|Hit")
	float GetDeathMontagePlayRate() const { return DeathMontagePlayRate; }

	// ══════════════════════════════════════════════════════════════════
	//  死亡 / 重生的子类扩展点
	//
	//  这两个函数由 GA_Death 在流程中的固定时机调用，**不是**给自己写的
	//  —— 基类实现是空的，只有需要额外处理的子类才重写。
	//
	//  它们放在 public 而不是 protected，是因为调用方是另一个类（GA_Death）。
	//  语义上它们是"通知"，和 EnterRagdoll() 一样属于角色的公开能力。
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 死亡开始（在挂上 State.Dead、取消完其它能力之后调用）。
	 *
	 * 敌人重写它来让 AI 停止思考 —— 见 ARPG_Enemy，那里解释了
	 * "为什么必须有人明确地停 AI"。
	 * 只有服务器会走到这里（GA_Death 是 ServerOnly）。
	 */
	virtual void OnDeathStarted() {}

	/** 重生完成（复位 + 传送都做完之后调用）。敌人重写它来重启 AI */
	virtual void OnRespawned() {}

	// ══════════════════════════════════════════════════════════════════
	//  伤害飘字
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 让**所有客户端**在这个角色的某个位置冒一个伤害数字。
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【为什么不能用现成的 Event.Combat.Hit】
	 * ══════════════════════════════════════════════════════════════════
	 * 那个 GameplayEvent 只在**服务器**广播（见 RPG_AttributeSet 里的权威判断），
	 * 而飘字是每个客户端都要各自画的东西 —— 服务器上画了没人看得见。
	 *
	 * NetMulticast 就是干这个的：服务器调一次，所有客户端各自执行一份。
	 *
	 * 用 Unreliable 而不是 Reliable：飘字是纯表现，丢一个数字不影响任何逻辑，
	 * 而 Reliable RPC 的确认与重发机制在挨打密集时会白白吃掉带宽。
	 * 这是"表现类 RPC 一律 Unreliable"这条通用规则的实例。
	 *
	 * @param Amount   伤害数值（显示用，不做任何计算）
	 * @param Location 世界坐标。用 FVector_NetQuantize 而不是 FVector ——
	 *                 飘字位置精确到厘米就够了，量化后每个坐标只占几个字节
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ShowDamageNumber(float Amount, FVector_NetQuantize Location);

	/** 头顶血条组件。本地玩家自己看不到它 —— 见 RefreshOverheadWidgetVisibility */
	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	URPG_OverheadHealthBarComponent* GetOverheadHealthBar() const { return OverheadHealthBar; }

	/**
	 * 死后多久重生（秒）。0 表示不重生。
	 *
	 * 公开出来是给 HUD 做倒计时用的：
	 * 死亡状态（State.Dead）是复制的，重生时长是个配置常量，
	 * 两者客户端都能拿到 —— 所以**客户端可以自己数这个秒数**，
	 * 不需要服务器再复制一个"还剩几秒"。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|Death")
	float GetRespawnDelay() const { return RespawnDelay; }

	/**
	 * 根据"我是不是本地玩家自己"决定头顶血条显示不显示。
	 *
	 * 三处调用：BeginPlay / PossessedBy / OnRep_Controller —— 缺一不可，
	 * 理由见 .cpp 里的说明。
	 */
	void RefreshOverheadWidgetVisibility();

	/**
	 * 这个角色是不是"**我这台机器上那个玩家**所控制的"。
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【★ 为什么不能直接用 IsLocallyControlled()】
	 * ══════════════════════════════════════════════════════════════════
	 * `APawn::IsLocallyControlled()` 就是 `Controller->IsLocalController()`，
	 * 而 `AController::IsLocalController()`（Controller.cpp:90-113）有这么两条：
	 *
	 *   · 第 94-98 行：`NetMode == NM_Standalone` → **无脑 return true**
	 *   · 第 106-110 行：本地角色是权威、且远端角色不是 AutonomousProxy
	 *                    → return true（注释写的就是 "Local authority in control"）
	 *
	 * 服务器上的 AIController 两条都沾（它是权威，也不是谁的 AutonomousProxy），
	 * 所以**敌人身上 IsLocallyControlled() 恒为 true**。
	 *
	 * 拿它当"是不是本地玩家"的判据，后果是单机下所有敌人都被当成玩家自己，
	 * 头顶血条全被藏起来 —— 而且因为客户端上复制来的 AIController 不是权威、
	 * 判定反而是对的，所以这个 bug **只在单机/主机上出现**。
	 *
	 * 正确的定义要额外加一条"必须是 PlayerController"。
	 */
	UFUNCTION(BlueprintPure, Category = "RPG|UI")
	bool IsLocallyControlledPlayer() const;

	/**
	 * 启动重生倒计时。由 GA_Death 在布娃娃之后调用（**仅服务器**）。
	 *
	 * RespawnDelay <= 0 表示"不重生"（敌人默认就是这样：死了就躺着，
	 * 免得玩家刚打赢又冒出来一个）。
	 */
	void StartRespawnCountdown();

protected:
	virtual void BeginPlay() override;

	/** 被控制器占有时刷新头顶血条 —— 它的可见性取决于"谁在控制我" */
	virtual void PossessedBy(AController* NewController) override;

	/** 客户端侧 Controller 复制到达时刷新（联机时"我是不是本地玩家"要等它才准） */
	virtual void OnRep_Controller() override;

	/**
	 * 倒计时到点后真正执行重生：复位状态 → 传送回出生点。
	 *
	 * ⚠️ 顺序不能反。必须先 ResetForRespawn() 再传送 ——
	 * 反过来的话，传送触发的物理/碰撞更新会在"角色还带着死亡状态"时跑一遍，
	 * 布娃娃的网格会和刚落地的胶囊体打架，表现是重生瞬间人被弹飞。
	 */
	virtual void PerformRespawn();

	/**
	 * 重生时把角色放回哪个位置。
	 * 基类默认返回 BeginPlay 时记下的出生变换（敌人用这个）。
	 * 玩家覆写成"去找 PlayerStart"——见 ARPG_Player。
	 */
	virtual FTransform GetRespawnTransform() const;

	/** 布娃娃开关的复制回调 —— 物理状态本身不复制，靠这个标记让各端自己模拟 */
	UFUNCTION()
	void OnRep_RagdollEnabled();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ══════════════════════════════════════════════════════════════════
	//  子类必须实现的 GAS 接入点
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 返回本角色的 ASC 的虚函数。
	 *   玩家：return PlayerState->GetAbilitySystemComponent()
	 *   敌人：return 自己的 AbilitySystemComponent
	 * 允许返回 nullptr（ASC 尚未初始化时）。
	 */
	virtual UAbilitySystemComponent* GetASCInternal() const
		PURE_VIRTUAL(ARPG_BaseCharacter::GetASCInternal, return nullptr;);

	// ══════════════════════════════════════════════════════════════════
	//  组件
	// ══════════════════════════════════════════════════════════════════

	/** 弹簧臂。bUsePawnControlRotation = true，所以它跟随控制器旋转（鼠标控制视角） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** 跟随相机。挂在弹簧臂末端，自己不旋转，由弹簧臂带动 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/**
	 * 战斗状态组件（敌我共用）。
	 *
	 * 它**不依赖任何 GAS 类** —— 只持有【输入缓存】、【连段索引】、【当前攻击模组】。
	 * 这样设计的好处是：战斗逻辑可以脱离 GAS 单独测试，
	 * 而且将来加召唤物、可破坏物之类没有 ASC 的 Actor 也能直接复用。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Combat")
	TObjectPtr<URPG_CombatComponent> CombatComponent;

	/**
	 * 头顶血条。世界空间里"贴"在角色头顶，但用 **Screen** 空间渲染。
	 *
	 * ── 为什么是 Screen 而不是 World ──
	 * `EWidgetSpace::Screen` 的 WidgetComponent 会始终正对相机，且**大小不随距离变化** ——
	 * 这正是血条/名牌想要的行为：远处的小怪和贴脸的大怪，血条一样清楚。
	 * World 空间会让血条跟着透视缩放，离远了就糊成一团。
	 *
	 * ── 为什么放在基类而不是敌人独占 ──
	 * 需求是"**除本地玩家外**的其他玩家、AI 都要有"。
	 * 判定条件是"谁在控制我"，不是"我是玩家还是敌人" ——
	 * 玩家的血条在**队友视角**里也是要显示的。放在基类里，两种角色共用一套逻辑。
	 *
	 * Widget Class 在角色蓝图里设（见 PHASE7_UI_SETUP.md）。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|UI")
	TObjectPtr<URPG_OverheadHealthBarComponent> OverheadHealthBar;

	// ══════════════════════════════════════════════════════════════════
	//  相机参数
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Camera")
	float CameraBoomLength = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Camera")
	FRotator CameraBoomOffset = FRotator(-10.f, 0.f, 0.f);

	/** 相机滞后速度。越大越"跟手"，越小越有重量感。设 0 关闭滞后 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Camera")
	float CameraLagSpeed = 15.f;

	// ══════════════════════════════════════════════════════════════════
	//  移动参数
	// ══════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float WalkSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float SprintSpeed = 850.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float CrouchSpeed = 180.f;

	/**
	 * 战斗移动速度（厘米/秒）。
	 *
	 * 发现目标后由 AI 切换到这个速度。比巡逻慢走快，但比玩家冲刺（850）慢 ——
	 * 敌人的定位是"跟得上你，但你还能甩掉它"，跑得和玩家一样快会让追逐失去张力。
	 *
	 * ⚠️ 改这个值只影响**服务器**。`MaxWalkSpeed` 在引擎里不是复制属性，
	 * 客户端不会跟着变。详见 SetCombatMovement() 里的说明。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float CombatMoveSpeed = 600.f;

	/** 角色转向速率（度/秒）。越大转身越快，越小越"重" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Movement")
	float RotationRateYaw = 540.f;

	// ══════════════════════════════════════════════════════════════════
	//  起始能力与属性
	//  在蓝图子类里配置，玩家和敌人各配各的。
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 输入标签 → 起始能力。
	 * 例：Input.Attack.Light → GA_LightAttack
	 * 敌人在自己的蓝图里配成 AI 用的能力（可以复用同一个 GA 类）。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Abilities")
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> StartupAbilities;

	/**
	 * 起始能力：**没有输入触发**的那些。授予后是否立刻激活，由能力自己的
	 * bActivateOnGranted 决定 —— 所以这张表其实装了两类能力：
	 *
	 *   ① 常驻被动（bActivateOnGranted = true）
	 *      典型成员：GA_StaminaRegen（耐力恢复）。
	 *      授予后立刻激活并一直保持激活。
	 *
	 *   ② 事件驱动（bActivateOnGranted = false）
	 *      典型成员：GA_Death、GA_HitReact。
	 *      授予后只是**待命**，靠自己在构造函数里声明的 AbilityTriggers
	 *      等 GameplayEvent。它们没有输入标签，所以也进不了 StartupAbilities。
	 *
	 * 为什么不像 StartupAbilities 那样按"触发方式"分成两张表：
	 * 因为"怎么触发"这件事的能力差异是**能力自己的属性**（AbilityTriggers /
	 * bActivateOnGranted），不是角色的属性。角色只负责回答
	 * "我会哪些能力"，分成两张表反而多一层需要维护的对应关系。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupPassiveAbilities;

	/**
	 * 初始属性集 GE。角色初始化时应用一次，用来设定生命/攻击/防御等数值。
	 * 放在 GE 而不是 C++ 构造函数里，是为了让数值可以被策划直接调整而不必重新编译。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Abilities")
	TSubclassOf<UGameplayEffect> InitAttributesEffect;

	// ══════════════════════════════════════════════════════════════════
	//  受击 / 死亡表现
	//  资产配置放这里（而不是放 GA 里），因为这是"这个角色长什么样"的属性 ——
	//  同一个 GA_HitReact 挂在玩家和敌人身上，播出来的蒙太奇应该各是各的。
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 受击蒙太奇池。每次受击随机抽一个。
	 *
	 * 池子（而不是单个）是为了避免连续挨打时反复播同一个动作 ——
	 * 打击感的一半来自"每次反馈略有不同"。只配一个也能跑，随机范围就是它自己。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Animation|Hit")
	TArray<TObjectPtr<UAnimMontage>> HitReactMontages;

	/**
	 * 受击蒙太奇的播放速率。小于 1 会放慢，配合略长的蒙太奇能做出"被打得踉跄"的沉重感。
	 * 玩家和敌人可以配不同值（玩家 1.0 保持响应性，敌人 0.9 显得笨重）。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Animation|Hit")
	float HitReactPlayRate = 1.f;

	/**
	 * 死亡蒙太奇。播完（或被打断）后进入布娃娃。
	 *
	 * 留空也可以：GA_Death 会跳过等待直接倒地，
	 * 表现上就是"人直接瘫下去"，测试布娃娃链路时反而更快。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Animation|Hit")
	TObjectPtr<UAnimMontage> DeathMontage;

	/**
	 * 死亡蒙太奇的播放速率。一般大于 1（快放），
	 * 因为死亡蒙太奇末尾通常已经躺到地上，再慢放会显得拖沓。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Animation|Hit")
	float DeathMontagePlayRate = 1.f;

	/**
	 * 死后多久重生（秒）。
	 *
	 * 0 = **不重生**，尸体原地留着。敌人默认是这个值 ——
	 * 打死一只立刻又站起来会让战斗失去意义；等关卡重载或加"尸体清理"再说。
	 *
	 * 玩家建议 3~5 秒：够看清自己是怎么死的，又不至于干等。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Death", meta = (ClampMin = "0.0"))
	float RespawnDelay = 0.f;

	/**
	 * 按当前姿态重算 MaxWalkSpeed。
	 *
	 * 抽出来是因为有**三个**地方会改这个值（启动同步 / 蹲伏 / 战斗切换），
	 * 各写各的迟早会出现"蹲着脱战之后站起来用走路速度跑"这类组合 bug ——
	 * 优先级规则只有一份，才不会各处不一致。
	 *
	 * 优先级：蹲伏 > 战斗 > 常态。
	 * （冲刺不在这里 —— 它由 StartSprint/StopSprint 单独管，
	 *   而且只在能力激活期间生效，天然盖过其他状态。）
	 */
	void RefreshMaxWalkSpeed();

private:
	/** 当前是否处于"战斗移动"状态 */
	bool bInCombatMovement = false;

	/**
	 * 布娃娃开关。
	 *
	 * ⚠️ 复制的是这个**开关**，不是物理状态本身。
	 * 骨骼的位置/速度/碰撞由 Chaos 在各端**各自**模拟，引擎压根不复制它们。
	 * 所以做法只能是：服务器改开关 → 各端收到 OnRep → 各端跑同一套 API。
	 * 视觉上会有细微差异（两台机器算出来的倒地姿势不可能逐帧一致），
	 * 要精确同步得用 Network Physics 那一套 —— 那属于 L3，本项目不做。
	 *
	 * ⚠️ OnRep 的触发条件比想象中严格：**值真的变了才会调**
	 * （RepLayout.cpp:3372-3392，值相同会打一条 "Skipping RepNotify" 的 Verbose 日志）。
	 * 所以活着的角色初次复制到客户端时**不会**收到 OnRep_RagdollEnabled(false) ——
	 * 本地初值就是 false，没变化。
	 *
	 * 而"客户端开始观察一个**已经倒地**的角色"这种情况会收到 OnRep(true)，
	 * 那正是我们要的：新进来的客户端也能看到尸体躺在地上。
	 *
	 * ExitRagdoll 里那道幂等守卫因此不是"必须的"，留着是因为它便宜，
	 * 而且能挡住将来可能出现的其他调用路径。
	 */
	UPROPERTY(ReplicatedUsing = OnRep_RagdollEnabled)
	bool bRagdollEnabled = false;

	/**
	 * 出生时的网格相对变换（相对胶囊体）。
	 *
	 * 布娃娃结束时必须把网格拨回这个值 —— 物理仿真停下后，网格会保持着
	 * "最后被模拟到的世界姿势"和它的相对变换，不还原的话：
	 * 胶囊体站在原地，网格躺在几米外。
	 *
	 * 在 BeginPlay 里记而不是构造函数里：蓝图可以覆盖网格的相对位置，
	 * 构造函数拿到的只是 CDO 的默认值。
	 */
	TOptional<FTransform> CachedMeshRelativeTransform;

	/**
	 * 出生时的碰撞设置。布娃娃会临时改掉它们（网格换 "Ragdoll" 预设、
	 * 胶囊体整个关掉），退出时必须还原成**原来那个**，而不是还原成
	 * "引擎默认值" —— 蓝图里可能给网格换了自定义的碰撞预设，
	 * 硬写 "CharacterMesh" 会把它悄悄改掉，而且要过很久才会有人发现。
	 */
	FName CachedMeshCollisionProfile;
	ECollisionEnabled::Type CachedCapsuleCollisionEnabled = ECollisionEnabled::QueryAndPhysics;

	/** 出生位置。敌人（和不走 PlayerStart 的情况）重生时回到这里 */
	FTransform CachedSpawnTransform;

	FTimerHandle RespawnTimerHandle;
};
