// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RPG_AIController.h"

#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"

#include "AI/RPG_BlackboardKeys.h"
#include "Character/RPG_BaseCharacter.h"
#include "Character/RPG_Enemy.h"
#include "Core/RPG_LogChannels.h"

ARPG_AIController::ARPG_AIController()
{
	// ══════════════════════════════════════════════════════════════════
	//  感知组件要自己建
	// ══════════════════════════════════════════════════════════════════
	// AAIController 的构造函数里**只**创建了 PathFollowingComponent，
	// PerceptionComponent 是个留给使用者填的公开成员（默认是空的）。
	// 不建它的话，后面所有 ConfigureSense 都是对空指针操作。
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));

	// ── 视觉 ──
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
	SightConfig->SetMaxAge(SightMaxAge);
	SightConfig->AutoSuccessRangeFromLastSeenLocation = AutoSuccessRangeFromLastSeenLocation;

	// ★★★ 必须显式打开阵营检测，否则 AI 什么都看不见 ★★★
	//
	// 原因链（查证自引擎源码）：
	//
	//   ① FAISenseAffiliationFilter 的三个开关**默认全是 false**
	//      （AIPerceptionTypes.h:218-224），而 UAISenseConfig 和
	//      UAISenseConfig_Sight 的构造函数都没有改过它们
	//      （AISense.cpp:118-121、175-178）。
	//
	//   ② 感知系统用 GetAsFlags() 把这三个开关压成一个位掩码，
	//      全 false → 掩码 = 0。
	//
	//   ③ 判定在 FAISenseAffiliationFilter::ShouldSenseTeam()：
	//          return AffiliationFlags == AllFlags || ((1 << 态度) & AffiliationFlags);
	//      掩码为 0 时，后面那一项永远是 0 → **任何阵营都感知不到**。
	//
	//   ④ 更绕的是"态度"怎么算。默认求解器是
	//          return A != B ? Hostile : Friendly;     // AIInterfaces.cpp:30-33
	//      而 AAIController 和玩家默认都是 FGenericTeamId::NoTeam（255），
	//      255 == 255 → 算出 **Friendly**，不是 Neutral。
	//      所以只开 bDetectNeutrals 也没用。
	//
	// 结论：在没有队伍系统的情况下，三个全开是最省事也最不会错的做法。
	// 将来真的做了阵营（比如"敌人之间会互相打"），再按需收紧。
	//
	// 症状提示：这三个开关忘了开的表现是"敌人站着不动、完全不看你"，
	// 而且**不报任何错** —— 感知系统认为一切正常，只是"没发现东西"。
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	// ── 听觉 ──
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = HearingRange;
	HearingConfig->SetMaxAge(SightMaxAge);
	// 同样要开 —— 理由同上
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	// ── 挂到感知组件上 ──
	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->ConfigureSense(*HearingConfig);

	// 主感知设为视觉：当多个感官同时报告同一个目标时，以视觉的信息为准
	// （比如"听到脚步"只知道大致方位，"看到人"知道精确位置）
	PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());

	// 感知状态变化时回调。
	// 注意这里是 AddDynamic —— 委托是动态多播，回调必须是 UFUNCTION。
	PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(
		this, &ARPG_AIController::OnTargetPerceptionUpdated);

	// AAIController 默认 bStartAILogicOnPossess = false，
	// 我们自己在 OnPossess 里显式启动行为树 —— 这样能先校验资产配好了没有，
	// 而不是让引擎静默地什么都不做。
}

// ══════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════

void ARPG_AIController::BeginPlay()
{
	Super::BeginPlay();

	ApplyPerceptionSettings();
}

void ARPG_AIController::ApplyPerceptionSettings()
{
	if (!PerceptionComponent || !SightConfig || !HearingConfig)
	{
		return;
	}

	// ── 为什么构造函数里设了还要在这里再设一遍 ──
	// 构造函数只在**类默认对象**创建时执行一次。你在 BP_RPG_Enemy 里把
	// SightRadius 从 1500 改成 2500，**不会**重新执行构造函数 ——
	// 那时 SightConfig 上的值仍然是 1500。
	//
	// 这和 ARPG_BaseCharacter 里同步相机 / 移动参数是同一个坑，
	// 凡是"UPROPERTY 的值要影响一个子对象"就必须在运行时同步一次。
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
	SightConfig->SetMaxAge(SightMaxAge);
	SightConfig->AutoSuccessRangeFromLastSeenLocation = AutoSuccessRangeFromLastSeenLocation;

	HearingConfig->HearingRange = HearingRange;
	HearingConfig->SetMaxAge(SightMaxAge);

	// 改完配置要通知感知系统重新登记，否则改动不生效
	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->ConfigureSense(*HearingConfig);
	PerceptionComponent->RequestStimuliListenerUpdate();

	UE_LOG(LogRPG_AI, Log,
		TEXT("[%s] 感知已配置：视觉 %.0f/丢失 %.0f 半径、%.0f° 半角；听觉 %.0f"),
		*GetName(), SightRadius, LoseSightRadius, PeripheralVisionAngleDegrees, HearingRange);
}

void ARPG_AIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!BehaviorTreeAsset)
	{
		UE_LOG(LogRPG_AI, Error,
			TEXT("[%s] 没有配置 Behavior Tree Asset —— 这个 AI 不会做任何事。"
			     "在 BP_RPG_Enemy 的 AIController 类上挂 BT_RPG_Enemy"),
			*GetName());
		return;
	}

	// RunBehaviorTree 会顺便根据行为树资产里引用的黑板资产创建 Blackboard 组件。
	// 所以**必须**先跑行为树，再写黑板 —— 反过来 Blackboard 还是空的。
	if (!RunBehaviorTree(BehaviorTreeAsset))
	{
		UE_LOG(LogRPG_AI, Error,
			TEXT("[%s] 行为树启动失败。最常见的原因是行为树资产没有设置 Blackboard Asset"),
			*GetName());
		return;
	}

	InitializeBlackboardValues();

	UE_LOG(LogRPG_AI, Log, TEXT("[%s] 行为树已启动：%s"),
		*GetName(), *BehaviorTreeAsset->GetName());
}

void ARPG_AIController::InitializeBlackboardValues()
{
	if (!Blackboard || !GetPawn())
	{
		return;
	}

	// HomeLocation：脱战后回到这里。
	// 用出生点而不是"巡逻点 0" —— 有些敌人可能根本没配巡逻点。
	Blackboard->SetValueAsVector(RPGBlackboardKeys::HomeLocation, GetPawn()->GetActorLocation());

	// AttackRange：从敌人身上读出来放黑板。
	// 这样行为树节点不需要知道它服务的具体是哪个角色类，
	// 将来的"远程敌人"只要在蓝图里把这个值改大就行。
	if (const ARPG_Enemy* Enemy = Cast<ARPG_Enemy>(GetPawn()))
	{
		Blackboard->SetValueAsFloat(RPGBlackboardKeys::AttackRange, Enemy->AttackRange);
	}

	// 初始状态：不在战斗、没看到任何东西
	Blackboard->SetValueAsBool(RPGBlackboardKeys::bInCombat, false);
	Blackboard->SetValueAsBool(RPGBlackboardKeys::bTargetVisible, false);
	Blackboard->SetValueAsInt(RPGBlackboardKeys::PatrolIndex, 0);
}

// ══════════════════════════════════════════════════════════════════════
//  感知回调
// ══════════════════════════════════════════════════════════════════════

void ARPG_AIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor || !Blackboard)
	{
		return;
	}

	// 不把自己当目标。
	// （感知系统默认会把自身也纳入检测范围，不排除掉的话 AI 会追着自己跑）
	if (Actor == GetPawn())
	{
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  ★ 死了就不再响应感知
	// ══════════════════════════════════════════════════════════════════
	// 感知组件不会因为角色死亡就停止工作 —— 玩家从尸体旁边经过，
	// 它照样会回调这里，然后：
	//   · 往黑板写"我看到目标了"
	//   · SetCombatState(true) → 给尸体挂上 State.Sprinting、改 MaxWalkSpeed
	//
	// 前者的后果最阴险：**复活时黑板上还留着"正在战斗"**，
	// AI 一起来就直奔玩家，看起来像是有前世记忆。
	// 虽然 RestartAI() 里会清一遍黑板，但那属于"事后补救"——
	// 在源头挡住更省事，也省掉死亡期间每帧一次的无效回调。
	//
	// 用 IsAlive() 而不是查 State.Dead 标签：接口层已经封装了
	// "ASC 还没初始化时按存活处理"这个边界，直接用即可。
	if (const ARPG_BaseCharacter* SelfChar = Cast<ARPG_BaseCharacter>(GetPawn()))
	{
		if (!SelfChar->IsAlive())
		{
			return;
		}
	}

	// 只对"有 GAS 的角色"起反应 —— 也就是玩家和别的敌人。
	// 地上的石头、门之类的 Actor 不该引起战斗状态。
	if (!Cast<ARPG_BaseCharacter>(Actor))
	{
		return;
	}

	if (Stimulus.WasSuccessfullySensed())
	{
		Blackboard->SetValueAsObject(RPGBlackboardKeys::TargetActor, Actor);

		// ★ 这里每次都更新"最后已知位置" —— 它和 TargetActor 是两回事：
		// 目标丢失时 TargetActor 会被清掉，但这个位置要留到最后。
		Blackboard->SetValueAsVector(
			RPGBlackboardKeys::LastKnownLocation, Actor->GetActorLocation());

		Blackboard->SetValueAsBool(RPGBlackboardKeys::bTargetVisible, true);

		// ★ 立刻进入战斗状态（黑板标记 + 移动速度一起切）。
		//
		// 这一步是"玩家一进视野敌人就追"的关键时机：
		// 感知回调在**刚看到的那一瞬间**就触发了，比行为树的任何轮询都早，
		// 也比 BTService 的 0.2 秒周期早。放在这里才叫"立即"。
		SetCombatState(true);

		// 顺带打出是哪个感官发现的 —— 排查"AI 靠听觉还是视觉找到我"时很有用
		const TSubclassOf<UAISense> SenseClass =
			UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus);

		UE_LOG(LogRPG_AI, Verbose, TEXT("[%s] 感知到目标：%s（来自 %s）"),
			*GetName(), *Actor->GetName(),
			SenseClass ? *SenseClass->GetName() : TEXT("未知感官"));
	}
	else
	{
		// ── 丢失感知 ──
		// 注意这里**不清 TargetActor**，只标记"此刻看不见"。
		//
		// 为什么：丢视野是一个瞬间事件（被柱子挡了一下），而"脱战"是一个
		// 持续判断（连续 N 秒都没再看到）。如果在这里就把目标清掉，
		// AI 会变得极度健忘 —— 玩家绕着树跑一圈敌人就回去巡逻了。
		//
		// 真正的清除交给 BTService_CombatUpdate：它盯着 bTargetVisible，
		// 连续为 false 超过阈值才清 TargetActor 和 bInCombat。
		Blackboard->SetValueAsBool(RPGBlackboardKeys::bTargetVisible, false);

		UE_LOG(LogRPG_AI, Verbose, TEXT("[%s] 丢失感知：%s（保留最后已知位置，等待超时）"),
			*GetName(), *Actor->GetName());
	}
}

void ARPG_AIController::SetCombatState(bool bInCombat)
{
	// ── ① 黑板：行为树的根选择器靠它选战斗分支还是巡逻分支 ──
	//
	// 注意这里**不提前 return**：即使黑板值没变，也要往下走把移动速度
	// 同步一遍。因为这两处可能因为某条路径的疏漏而不同步，
	// 每次调用都当成一次"校正"比省几次写入更划算。
	if (Blackboard)
	{
		Blackboard->SetValueAsBool(RPGBlackboardKeys::bInCombat, bInCombat);
	}

	// ── ② 移动速度：发现了就跑起来 ──
	//
	// 局部变量不能叫 Character —— AController 自带一个同名成员，
	// 重名会报 C4458（而且它指向的确实是同一个 Pawn，很容易误用成那个）。
	if (ARPG_BaseCharacter* RPGChar = Cast<ARPG_BaseCharacter>(GetPawn()))
	{
		RPGChar->SetCombatMovement(bInCombat);
	}

	UE_LOG(LogRPG_AI, Verbose, TEXT("[%s] %s战斗状态"),
		*GetName(), bInCombat ? TEXT("进入") : TEXT("退出"));
}

// ══════════════════════════════════════════════════════════════════════
//  查询
// ══════════════════════════════════════════════════════════════════════

AActor* ARPG_AIController::GetTargetActor() const
{
	return Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor))
		: nullptr;
}

FVector ARPG_AIController::GetLastKnownLocation() const
{
	return Blackboard
		? Blackboard->GetValueAsVector(RPGBlackboardKeys::LastKnownLocation)
		: FVector::ZeroVector;
}

// ══════════════════════════════════════════════════════════════════════
//  死亡 / 重生时的 AI 启停
// ══════════════════════════════════════════════════════════════════════

void ARPG_AIController::StopAI()
{
	// ① 停寻路。
	// 注意 StopMovement 只清掉**当前的**移动请求；如果不清，PathFollowingComponent
	// 会在角色已经进入布娃娃（移动模式 = MOVE_None）之后继续尝试重算路径，每帧失败。
	StopMovement();

	// ② 清焦点。不清的话，AI 的"注视"会继续把尸体往目标方向扭。
	ClearFocus(EAIFocusPriority::Gameplay);

	// ③ 退出战斗状态。
	//
	// 不做这一步的话，尸体身上会一直挂着 State.Sprinting 这个 loose tag，
	// MaxWalkSpeed 也停在 CombatMoveSpeed —— 因为死亡流程里没有人会去清它。
	// 默认 RespawnDelay = 0 的敌人更是永远不清。
	//
	// 对布娃娃来说这看着无害（网格由物理驱动，不读速度），
	// 但任何**靠 State.Sprinting 判断状态**的东西都会看到假信息：
	// 动画蓝图的 MovementState、将来可能有的 UI、调试面板。
	// 让它在源头停下来，比让下游各自判断"这个是不是尸体"便宜得多。
	SetCombatState(false);

	// ④ 停行为树。
	// StopLogic(Safe) 会让当前正在跑的分支走 Abort 流程 ——
	// 那些 Latent Task（比如 BTTask_Attack）会收到 AbortTask 回调，
	// 有机会清理自己。这正是 StopLogic 比"直接不管"好的地方。
	if (BrainComponent)
	{
		BrainComponent->StopLogic(TEXT("角色死亡"));
	}

	UE_LOG(LogRPG_AI, Log, TEXT("[%s] AI 已停止（角色死亡）"), *GetName());
}

void ARPG_AIController::RestartAI()
{
	// ① 清掉上一条命留下的记忆。
	// 不清的话，复活瞬间 AI 会带着"我上次在追玩家"的状态醒过来 ——
	// 表现是刚站起来就直奔玩家的旧位置，像是有前世记忆。
	if (Blackboard)
	{
		Blackboard->ClearValue(RPGBlackboardKeys::TargetActor);
		Blackboard->ClearValue(RPGBlackboardKeys::bTargetVisible);
		Blackboard->SetValueAsVector(RPGBlackboardKeys::LastKnownLocation, FVector::ZeroVector);
	}

	// ② 走统一入口复位战斗状态（黑板 bInCombat + 移动速度一起）
	SetCombatState(false);

	// ③ 重启行为树。
	// RestartLogic 内部会处理"树曾被 StopLogic 停过"这种情况
	// （BehaviorTreeComponent.cpp:448 的 RestartTree 会看 bRequestedStop 标志），
	// 不需要我们手动 RunBehaviorTree 重建一遍。
	if (BrainComponent)
	{
		BrainComponent->RestartLogic();
	}

	// ④ 把出生点和攻击距离重新写一遍 —— 复活位置可能和最初不同
	InitializeBlackboardValues();

	UE_LOG(LogRPG_AI, Log, TEXT("[%s] AI 已恢复（角色重生）"), *GetName());
}
