// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "RPG_AIController.generated.h"

class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;

/**
 * 敌人的大脑。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它负责什么，不负责什么】
 * ══════════════════════════════════════════════════════════════════════
 * 负责：
 *   · 感知配置（视觉 / 听觉）—— "能不能看到玩家"
 *   · 把感知结果写进黑板 —— "看到了谁、在哪看到的"
 *   · 启动行为树 —— "开始思考"
 *
 * 不负责：
 *   · 决策逻辑本身     → 行为树资产（BT_RPG_Enemy）
 *   · 具体动作怎么做   → 各个 BTTask
 *   · 攻击怎么打       → 复用玩家那一套 GA（这才是关键，见下）
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【★ AI 和玩家走的是同一条能力链路】
 * ══════════════════════════════════════════════════════════════════════
 * 这是本项目 AI 设计里最重要的一条：**敌人不另写一套攻击逻辑**。
 *
 *     玩家：按键 → Input.Attack.Light → ASC 映射表 → GA_LightAttack
 *     敌人：BTTask → Input.Attack.Light → ASC 映射表 → GA_LightAttack
 *                    ↑ 完全相同的入口
 *
 * 好处是"玩家能放、AI 放不出来"这类问题根本不会出现 ——
 * 因为它们根本没有两条路径可以不一致。
 *
 * 代价是 AI 也得往 CombatComponent 的输入缓存里推一个标签。
 * 这个代价其实是收益：连段、输入缓存、耐力消耗全部自动对 AI 生效。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【阵营：为什么构造函数里要把三个检测开关全打开】
 * ══════════════════════════════════════════════════════════════════════
 * 见 .cpp 里的详细说明 —— 这是"AI 站着不动不看你"的头号原因。
 */
UCLASS()
class RPG_API ARPG_AIController : public AAIController
{
	GENERATED_BODY()

public:
	ARPG_AIController();

	/** 当前锁定的目标。没在战斗时为 nullptr */
	UFUNCTION(BlueprintPure, Category = "RPG|AI")
	AActor* GetTargetActor() const;

	/**
	 * 进入 / 退出战斗状态。★ 所有改变战斗状态的路径都必须走这里。
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【为什么要有这个统一入口】
	 * ══════════════════════════════════════════════════════════════════
	 * "在不在战斗"这件事会同时影响**两处**：
	 *
	 *   ① 黑板的 `bInCombat` 键 —— 行为树的根选择器靠它选分支
	 *   ② 角色的 `MaxWalkSpeed` —— 战斗时跑起来
	 *
	 * 如果两处各改各的（比如感知回调改黑板、服务改速度），
	 * 迟早会出现"黑板说在战斗、人还在慢悠悠走"这种不一致 ——
	 * 而它看起来像寻路问题，查起来会绕很远。
	 *
	 * 收在一个入口里，两边就不可能不同步。
	 *
	 * @param bInCombat true = 发现目标，进入战斗；false = 脱战
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|AI")
	void SetCombatState(bool bInCombat);

	/** 目标最后被看到的位置 */
	UFUNCTION(BlueprintPure, Category = "RPG|AI")
	FVector GetLastKnownLocation() const;

	/**
	 * 停止思考：清目标、停寻路、停行为树。**死亡时调用。**
	 *
	 * ══════════════════════════════════════════════════════════════════
	 * 【为什么必须由"死亡"来显式调用，而不是让行为树自己发现】
	 * ══════════════════════════════════════════════════════════════════
	 * 敌人的血量归零后，行为树**不会自己停下来** —— 它没有"我死了"这个
	 * 概念，还会继续：写黑板、发起寻路、尝试激活攻击能力。
	 *
	 * 而这时角色的移动模式已经被布娃娃关掉了（MOVE_None），
	 * 于是每次寻路都失败。表现上尸体躺着不动看不太出来，但：
	 *   · 行为树会卡在一个永远不结束的 Latent Task 里
	 *   · 每帧都有失败日志
	 *   · 复活时行为树的状态是脏的，AI 会僵住
	 *
	 * 所以停 AI 这件事必须有人明确地做一次。做的人是 GA_Death ——
	 * 它是死亡流程的编排者，敌人通过 OnDeathStarted() 钩子转达。
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|AI")
	void StopAI();

	/**
	 * 恢复思考：重启行为树并把战斗状态复位。**重生时调用。**
	 *
	 * 必须和 StopAI() 配对 —— 只停不重启的话，敌人复活后会站在原地
	 * 一动不动（行为树根本没在跑），而且不报任何错。
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|AI")
	void RestartAI();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;

	/**
	 * 感知更新回调。
	 *
	 * ⚠️ 注意它的触发时机：这个函数**不是每帧调用**的，
	 * 只在"某个 Actor 的感知状态发生变化"时调用 —— 发现时调一次、
	 * 丢失时调一次。所以不能在这里做"每帧检查距离"这种事。
	 */
	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** 把蓝图里配的感知参数同步到感知组件上（理由见 .cpp） */
	void ApplyPerceptionSettings();

	/** 行为树跑起来之后，把初始值写进黑板 */
	void InitializeBlackboardValues();

	// ══════════════════════════════════════════════════════════════════
	//  组件
	// ══════════════════════════════════════════════════════════════════

	// 注意：感知组件不用在这里声明 —— AAIController 已经有一个公开的
	// PerceptionComponent 成员了，直接用继承来的那个。
	// （重复声明会在 UHT 阶段报 shadowing 错误，和 UGameplayAbility::CurrentMontage 一样）

	/** 视觉配置。挂在感知组件上 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|AI|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	/** 听觉配置 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|AI|Perception")
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	// ══════════════════════════════════════════════════════════════════
	//  配置
	// ══════════════════════════════════════════════════════════════════

	/**
	 * 行为树资产。留空的话 AI 会站着不动（并打一条 Error 日志）。
	 *
	 * 放在 AIController 上而不是敌人身上：行为树描述的是"这个大脑怎么想"，
	 * 换一个敌人模型不需要换大脑。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	// ── 视觉 ──

	/** 能看见目标的距离（厘米）。1000 = 10 米 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI|Perception", meta = (ClampMin = "0.0"))
	float SightRadius = 1500.f;

	/**
	 * 丢失视野的距离。
	 * 应该比 SightRadius 大 —— 两者相等会导致目标在边界上反复"看见/看不见"。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI|Perception", meta = (ClampMin = "0.0"))
	float LoseSightRadius = 2000.f;

	/** 视野半角（度）。45 = 总共 90 度的视野 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI|Perception", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float PeripheralVisionAngleDegrees = 45.f;

	/** 目标被挡住后，视觉信息保留多久（秒） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI|Perception", meta = (ClampMin = "0.0"))
	float SightMaxAge = 5.f;

	/** 目标消失后，在这个距离内 AI 会"自动认为还看得见"（用于绕过墙角时的容错） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI|Perception", meta = (ClampMin = "0.0"))
	float AutoSuccessRangeFromLastSeenLocation = 900.f;

	// ── 听觉 ──

	/** 听觉半径（厘米） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI|Perception", meta = (ClampMin = "0.0"))
	float HearingRange = 1200.f;

	/**
	 * 目标丢失后多久算脱战（秒）。
	 * 超过这个时间还没重新看到目标 → 清掉 TargetActor，回到巡逻。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|AI|Perception", meta = (ClampMin = "0.0"))
	float LoseTargetAfterSeconds = 6.f;
};
