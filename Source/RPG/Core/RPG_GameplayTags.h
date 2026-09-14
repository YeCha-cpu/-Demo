// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * 项目全部原生 GameplayTag 的集中声明处。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【为什么用 C++ 声明而不是写 DefaultGameplayTags.ini】
 * ══════════════════════════════════════════════════════════════════════
 * 纯 ini 方案在 C++ 里只能这样取标签：
 *     FGameplayTag::RequestGameplayTag(FName("Input.Attack.Light"))
 * 字符串拼错了编译期不报错，运行时才崩 —— 而且崩在很远的地方，很难查。
 *
 * 用 UE_DEFINE_GAMEPLAY_TAG_COMMENT 声明的标签：
 *   · 是真正的 C++ 变量，拼错名字编译不过
 *   · IDE 能跳转、能全局查找引用
 *   · 重构重命名是安全的
 *   · 模块加载时自动向标签表注册（不需要再手写 ini）
 *
 *
 * 代价是改标签要重新编译。所以工程约定：
 *   · 影响代码逻辑的"骨架标签" → 用 C++ 声明（本文件）
 *   · 纯内容配置、策划频繁调整的标签 → 后续可另建 ini
 *
 * 用 UE_DECLARE_GAMEPLAY_TAG_EXTERN 宏用于声明一个外部定义的游戏玩法标签。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【命名规范】<域>.<类别>.<子类别>.<具体>，不超过 5 段，用单数名词
 * ══════════════════════════════════════════════════════════════════════
 *   Input.*        玩家输入意图（Controller 层用）
 *   Ability.*      能力标识（ASC 用，触发 GA）
 *   State.*        持续状态（挂在角色身上，任何系统都可查询）
 *   Event.*        瞬时事件（GameplayEvent，用 SendGameplayEventToActor 广播）
 *   Data.*         SetByCaller 传参用的数据键
 *   Cooldown.*     冷却标识
 *   Attack.Module.* 攻击模组类型
 *   GameplayCue.*  特效音效
 *
 * 【Input 和 Ability 为什么要分成两套？】
 * 这是两级解耦：
 *   按键 → Input.Attack.Light → （映射表）→ Ability.Attack.Light
 * 中间那层映射表在 URPG_InputConfig 资产里配置。好处是：
 *   · 想改按键？改 InputMappingContext，不碰代码
 *   · 想让同一个技能被两个键触发？映射表里配两条
 *   · 想做按键重绑定？只动输入层
 * 如果按键直接绑能力，这两个域就焊死了。
 */
namespace RPGTags
{
	// ══════════════════════════════════════════════════════════════════
	//  Input —— 玩家输入意图
	//  由 RPG_PlayerController 产生，推入 CombatComponent 的输入缓存容器
	// ══════════════════════════════════════════════════════════════════
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Light);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Heavy);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Dodge);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Sprint);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Jump);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Crouch);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Spell_1);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Spell_2);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Spell_3);

	// ══════════════════════════════════════════════════════════════════
	//  Ability —— 能力标识
	//  用 ASC->TryActivateAbilitiesByTag() 触发；也用作 GA 的 AssetTags
	// ══════════════════════════════════════════════════════════════════
	/**
	 * 攻击类能力的**父标签**（本身不挂在任何能力上）。
	 *
	 * 用途：给 `CancelAbilities()` 当"取消组"用 ——
	 * 它的匹配是**层级式**的，传这个父标签就能一次命中
	 * `Ability.Attack.Light` 和 `Ability.Attack.Heavy`。
	 *
	 * 好处是加新的攻击类型（比如 `Ability.Attack.Special`）时，
	 * "受击要打断攻击"那处代码不用回来改。
	 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light_01);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light_02);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light_03);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light_04);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light_05);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Heavy);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Dodge);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Sprint);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Jump);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Heal);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Buff_AttackUp);
	/**
	 * 通用"施加效果"能力标签。
	 *
	 * ⚠️ 增益和减益**共用这一个**，不要按效果种类各建一个。
	 * 区分具体是哪种效果靠 **GE 的 GrantedTags**（比如 State.Buff.AttackUp /
	 * State.Debuff.DefenseDown），那是给动画、UI、驱散逻辑查的；
	 * 能力标签只是"这个 GA 是干什么的"这个身份，给 GAS 机制（Block / Cancel /
	 * 调试显示）用。
	 *
	 * 原来这里是 Ability_Buff_AttackUp 硬编码 —— 那样一个减防的 GE 会顶着
	 * "AttackUp" 的标签，日志和调试面板全是误导。
	 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ApplyEffect);
	/** 被动能力：常驻激活，负责耐力恢复（恢复开关由 State.Stamina.Blocked 控制） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_StaminaRegen);
	/** 被动能力：死亡表现 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Death);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Spell_1);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Spell_2);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Spell_3);

	// ══════════════════════════════════════════════════════════════════
	//  State —— 持续状态标签
	//  由 GE 的 GrantedTags 授予。任何系统都可以查询（动画、AI、伤害计算）
	// ══════════════════════════════════════════════════════════════════
	/** 攻击中（总括标签，动画蓝图用它判断是否走战斗分支） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attacking);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Windup);
	/** 伤害判定窗口开启中 —— WeaponTrace 只在此标签存在时采样检测 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Active);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Recovery);
	/** 连段衔接窗口开启中 —— 缓存容器只在此标签存在时被消耗 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_ComboWindow);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Charging);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Charging_Lv1);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Charging_Lv2);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Charging_Lv3);
	/**
	 * 切手技进行中（轻击连段中按右键切入的那一招）。
	 *
	 * 为什么要单独一个标签，而不是让 UI 去猜：
	 * 切手技和蓄力重击**共用同一个 GA、同一个输入**（见 URPG_GA_HeavyAttack），
	 * 走哪条分支只由"激活瞬间在不在轻击连段里"决定，是 GA 的私有状态。
	 * 没有这个标签的话，HUD 想显示"当前在放什么招"就只能：
	 *   · 要么 Cast 到 GA 去读私有成员（GA 实例还会随能力结束被回收）
	 *   · 要么靠蒙太奇名字猜（改名就坏）
	 * 挂成标签之后，"当前招式"这件事对所有系统都是可查的公共状态。
	 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Attack_Transition);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dodging);
	/** ★ 无敌帧。伤害 GE 检查此标签决定是否生效 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Invulnerable);
	/**
	 * 以高于行走的速度移动。
	 *
	 * 玩家的冲刺（GA_Sprint）和 AI 追击时切到的战斗速度都挂这个标签 ——
	 * 它们对动画的要求是一样的："别再播走路了"。
	 *
	 * 动画蓝图靠它决定用哪条移动状态机
	 * （见 URPG_AnimInstanceBase::UpdateLocomotion 的 MovementState）。
	 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Sprinting);
	/** 格挡（下期内容，先占位，避免将来改标签名） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Blocking);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Hit);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	/** ★ 耐力恢复阻断。存在时 GE_StaminaRegen 被抑制（OngoingTagRequirements） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Stamina_Blocked);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_InCombat);

	// ══════════════════════════════════════════════════════════════════
	//  Event —— 瞬时事件（GameplayEvent）
	//  用 UAbilitySystemBlueprintLibrary::SendGameplayEventToActor 广播
	//  用 UAbilityTask_WaitGameplayEvent 监听
	//
	//  这是动画 ↔ GAS 之间唯一的通信通道：
	//     AnimNotify → SendGameplayEventToActor → GA 的 WaitGameplayEvent 回调
	// ══════════════════════════════════════════════════════════════════
	/** 伤害判定窗口开启。Payload 携带单次攻击信息（倍率、检测源、半径） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_AttackWindow_Open);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_AttackWindow_Close);
	/** 连段衔接窗口开启。GA 收到后检查缓存容器，决定是否续段 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_ComboWindow_Open);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_ComboWindow_Close);
	/** 攻击段结束。GA 收到后 EndAbility；BTTask_RPG_Attack 也监听它来结束潜在任务 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_AttackEnd);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_Hit);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_Death);
	/** AI 请求攻击。用事件而不是直接激活能力，是为了能携带"用哪个模组/第几段"参数 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_AttackRequest);
	/** 蓄力：开始 / 升段 / 松手释放 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_ChargeStart);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_ChargeLevelUp);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_ChargeRelease);
	/** 无敌帧开始/结束。由 AnimNotifyState 广播，GA 据此上/下无敌 GE */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Character_Invulnerability_Begin);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Character_Invulnerability_End);
	/** 动画驱动的耐力消耗（攻击段在某一帧扣耐力） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Character_StaminaCost);

	// ══════════════════════════════════════════════════════════════════
	//  Item —— 场景里的效果触发器（拾取物 / 治疗泉 / 毒池 / 陷阱）发出的
	// ══════════════════════════════════════════════════════════════════
	// 这些事件由 World/RPG_EffectVolume 在服务器上发给碰到的角色，
	// 对应的 GA 用 AbilityTriggers 响应（见 GA_Heal / GA_ApplyBuff）。
	//
	// ⚠️ 拾取物**不直接施加 GE**，而是发事件让 GA 去施加 ——
	// 这是项目定的链路：ASC 初始化 → 触发 GA → GE 上 Buff/标签 → GC 特效。
	// 直接施加的话，消耗、冷却、动画、打断这些就全都没地方放了。
	//
	// 拾取物具体发哪个标签由它自己的 TriggerEvent 属性配（BP 里可见），
	// 所以**新增一种效果不需要改代码**，做一个新 GE + 配一个拾取物就行。

	/** 治疗（回血） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Heal);
	/** 增益（加攻 / 加防 / 加速……） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Buff);
	/** 减益（减防 / 减速 / 持续掉血……） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Debuff);

	// ══════════════════════════════════════════════════════════════════
	//  Data —— SetByCaller 传参键
	//  用法：Spec->SetSetByCallerMagnitude(RPGTags::Data_Damage_Multiplier, 1.15f)
	// ══════════════════════════════════════════════════════════════════
	/** 本次攻击的伤害倍率（轻击 1.0/1.15/1.4/1.6/2.0，重击 3.0/4.5/6.5） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage_Multiplier);
	/** 固定基础伤害（不依赖攻击力时使用，如陷阱） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage_Base);
	/** 单次耐力消耗量 */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Stamina_Cost);
	/** 持续耐力消耗速率（每秒） */
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Stamina_Rate);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Heal_Amount);

	// ══════════════════════════════════════════════════════════════════
	//  Cooldown —— 冷却标识
	//  用作 GE_Cooldown 的 GrantedTags，查询用 ASC->GetCooldownRemainingForTag
	// ══════════════════════════════════════════════════════════════════
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack_Light);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack_Heavy);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Dodge);

	// ══════════════════════════════════════════════════════════════════
	//  Attack.Module —— 攻击模组类型
	//  挂在 AttackModuleData 资产上；GA 据它决定用哪套蒙太奇与检测源
	// ══════════════════════════════════════════════════════════════════
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attack_Module_Unarmed);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attack_Module_Melee);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attack_Module_Ranged);

	// ══════════════════════════════════════════════════════════════════
	//  GameplayCue —— 特效音效
	//  由 GE 的 GameplayCues 字段触发，或手动 ExecuteGameplayCue
	//  命名必须以 GameplayCue. 开头，否则 GC 系统不会识别
	// ══════════════════════════════════════════════════════════════════
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_Hit);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HeavyHit);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_Death);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Character_Dodge);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Character_Heal);
	RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Character_Buff);
}
