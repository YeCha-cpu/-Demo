// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/RPG_GameplayTags.h"

/**
 * 原生 GameplayTag 的实际定义。
 *
 * 这些变量在模块加载时自动向 GameplayTag 表注册（通过 FNativeGameplayTag 的构造函数），
 * 所以不需要在 Config/DefaultGameplayTags.ini 里重复声明。
 *
 * 【调试技巧】在编辑器控制台里执行：
 *     GameplayTags.List              // 列出全部已注册标签
 *     GameplayTags.List Input        // 用前缀过滤
 * 能看到这里定义的每一个标签及其来源（会显示所属模块 RPG）。
 */
namespace RPGTags
{
	// ══════════════════════════════════════════════════════════════════
	//  Input
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Attack_Light, "Input.Attack.Light", "轻击输入（鼠标左键）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Attack_Heavy, "Input.Attack.Heavy", "重击输入（鼠标右键）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Dodge,        "Input.Dodge",        "闪避输入");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Sprint,       "Input.Sprint",       "奔跑输入（按住）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Jump,         "Input.Jump",         "跳跃输入");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Crouch,       "Input.Crouch",       "蹲伏输入");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Spell_1,      "Input.Spell.1",      "法术 1");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Spell_2,      "Input.Spell.2",      "法术 2");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Spell_3,      "Input.Spell.3",      "法术 3");

	// ══════════════════════════════════════════════════════════════════
	//  Ability
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Light, "Ability.Attack.Light", "轻击能力（5 段连段）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Light_01, "Ability.Attack.Light.01", "轻击能力（5 段连段）01");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Light_02, "Ability.Attack.Light.02", "轻击能力（5 段连段）02");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Light_03, "Ability.Attack.Light.03", "轻击能力（5 段连段）03");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Light_04, "Ability.Attack.Light.04", "轻击能力（5 段连段）04");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Light_05, "Ability.Attack.Light.05", "轻击能力（5 段连段）05");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Heavy, "Ability.Attack.Heavy", "重击能力（3 段蓄力 + 切手技）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Dodge,        "Ability.Dodge",        "闪避能力（含无敌帧）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Sprint,       "Ability.Sprint",       "奔跑能力（持续消耗耐力）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Jump,         "Ability.Jump",         "跳跃能力（一次性消耗耐力）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Heal,         "Ability.Heal",         "治疗能力");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Buff_AttackUp,"Ability.Buff.AttackUp","加攻 Buff 能力");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_StaminaRegen, "Ability.StaminaRegen", "耐力恢复（被动，常驻）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Death,        "Ability.Death",        "死亡（被动）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Spell_1,      "Ability.Spell.1",      "法术 1 能力");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Spell_2,      "Ability.Spell.2",      "法术 2 能力");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Spell_3,      "Ability.Spell.3",      "法术 3 能力");

	// ══════════════════════════════════════════════════════════════════
	//  State
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attacking,          "State.Attacking",           "攻击中（总括）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_Windup,      "State.Attack.Windup",       "攻击前摇");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_Active,      "State.Attack.Active",       "伤害判定窗口开启中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_Recovery,    "State.Attack.Recovery",     "攻击后摇");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_ComboWindow, "State.Attack.ComboWindow",  "连段衔接窗口开启中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_Charging,    "State.Attack.Charging",     "蓄力中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_Charging_Lv1,"State.Attack.Charging.Lv1", "蓄力一段");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_Charging_Lv2,"State.Attack.Charging.Lv2", "蓄力二段");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attack_Charging_Lv3,"State.Attack.Charging.Lv3", "蓄力三段");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dodging,            "State.Dodging",             "闪避中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Invulnerable,       "State.Invulnerable",        "无敌帧中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Sprinting,          "State.Sprinting",           "奔跑中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Blocking,           "State.Blocking",            "格挡中（下期）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Hit,                "State.Hit",                 "受击中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead,               "State.Dead",                "已死亡");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Stamina_Blocked,    "State.Stamina.Blocked",     "耐力恢复阻断中");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Combat_InCombat,    "State.Combat.InCombat",     "战斗中");

	// ══════════════════════════════════════════════════════════════════
	//  Event
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_AttackWindow_Open,  "Event.Combat.AttackWindow.Open",  "伤害判定窗口开启");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_AttackWindow_Close, "Event.Combat.AttackWindow.Close", "伤害判定窗口关闭");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_ComboWindow_Open,   "Event.Combat.ComboWindow.Open",   "连段衔接窗口开启");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_ComboWindow_Close,  "Event.Combat.ComboWindow.Close",  "连段衔接窗口关闭");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_AttackEnd,          "Event.Combat.AttackEnd",          "攻击段结束");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_Hit,                "Event.Combat.Hit",                "命中发生");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_Death,              "Event.Combat.Death",              "死亡");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_AttackRequest,      "Event.Combat.AttackRequest",      "AI 请求攻击");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_ChargeStart,        "Event.Combat.ChargeStart",        "开始蓄力");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_ChargeLevelUp,      "Event.Combat.ChargeLevelUp",      "蓄力升段");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Combat_ChargeRelease,      "Event.Combat.ChargeRelease",      "蓄力释放");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Character_Invulnerability_Begin, "Event.Character.Invulnerability.Begin", "无敌帧开始");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Character_Invulnerability_End,   "Event.Character.Invulnerability.End",   "无敌帧结束");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Character_StaminaCost,     "Event.Character.StaminaCost",     "动画驱动的耐力消耗");

	// ══════════════════════════════════════════════════════════════════
	//  Data（SetByCaller）
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage_Multiplier, "Data.Damage.Multiplier", "本次攻击伤害倍率");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage_Base,       "Data.Damage.Base",       "固定基础伤害");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Stamina_Cost,      "Data.Stamina.Cost",      "单次耐力消耗量");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Stamina_Rate,      "Data.Stamina.Rate",      "持续耐力消耗速率（每秒）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Heal_Amount,       "Data.Heal.Amount",       "治疗量");

	// ══════════════════════════════════════════════════════════════════
	//  Cooldown
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Attack_Light, "Cooldown.Attack.Light", "轻击冷却");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Attack_Heavy, "Cooldown.Attack.Heavy", "重击冷却");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Dodge,        "Cooldown.Dodge",        "闪避冷却");

	// ══════════════════════════════════════════════════════════════════
	//  Attack.Module
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Module_Unarmed, "Attack.Module.Unarmed", "徒手模组（双手检测）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Module_Melee,   "Attack.Module.Melee",   "近战武器模组（刀锋检测）");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Attack_Module_Ranged,  "Attack.Module.Ranged",  "远程武器模组（发射物）");

	// ══════════════════════════════════════════════════════════════════
	//  GameplayCue
	// ══════════════════════════════════════════════════════════════════
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Combat_Hit,        "GameplayCue.Combat.Hit",        "普通命中特效音效");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Combat_HeavyHit,   "GameplayCue.Combat.HeavyHit",   "重击命中特效音效");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Combat_Death,      "GameplayCue.Combat.Death",      "死亡特效音效");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Character_Dodge,   "GameplayCue.Character.Dodge",   "闪避特效音效");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Character_Heal,    "GameplayCue.Character.Heal",    "治疗特效音效");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Character_Buff,    "GameplayCue.Character.Buff",    "Buff 特效音效");
}
