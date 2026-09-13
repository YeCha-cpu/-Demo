// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RPG_HUDWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"
#include "Combat/RPG_AttackModuleData.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"
#include "UI/RPG_AttributeBarWidget.h"

// ══════════════════════════════════════════════════════════════════════
//  ⚠️ 这个类依赖 NativeTick，有两个前提条件
// ══════════════════════════════════════════════════════════════════════
// UMG 不会无条件给 Widget 开 tick。`UUserWidget::UpdateCanTick()`
// （UserWidget.cpp:2347-2381）只有在下面几条里**任意一条**成立时才开：
//
//   · 蓝图侧实现了 Tick 事件（bHasScriptImplementedTick）
//   · 派生它的 WBP 里有动画、或有 Latent 节点
//   · **本类没有被标记 `DisableNativeTick`**
//       —— 编译器读的就是这个 meta 标签
//       （WidgetBlueprint.cpp:1562-1563：
//          bClassRequiresNativeTick = !NativeParent->HasMetaData("DisableNativeTick")）
//   · WBP 的 Class Defaults → Tick Frequency 是 `Auto`（默认值）
//
// 我们是靠第三条拿到 tick 的。所以：
//   ① 不要给 URPG_HUDWidget 加 `UCLASS(meta = (DisableNativeTick))`
//   ② 不要在 WBP 里把 Tick Frequency 改成 `Never`
//
// 这两个都能编译通过、也能正常显示，只是**永远停在初始状态**：
// 血量不动、蓄力条不走、死亡面板不弹 —— 而且一条日志都没有。
// 这里没有在构造函数据做什么，写下这段是为了让这个隐式依赖是可见的。
URPG_HUDWidget::URPG_HUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ══════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════

void URPG_HUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 尝试绑一次。绑不上也没关系 —— NativeTick 会一直重试。
	TryBindToLocalPlayer();
}

void URPG_HUDWidget::NativeDestruct()
{
	UnbindFromASC();

	Super::NativeDestruct();
}

void URPG_HUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 惰性绑定：ASC 还没就位就继续试。
	// 每次失败只是一个 WeakPtr 判空 + 一次 Cast，开销可以忽略。
	if (!BoundASC.IsValid())
	{
		TryBindToLocalPlayer();
	}

	UpdateCombatState(InDeltaTime);
}

// ══════════════════════════════════════════════════════════════════════
//  ASC 绑定
// ══════════════════════════════════════════════════════════════════════

ARPG_BaseCharacter* URPG_HUDWidget::GetLocalCharacter() const
{
	// GetOwningPlayerPawn：这个 Widget 属于哪个玩家，就返回哪个 Pawn。
	// HUD 是由本地 PlayerController 创建的，所以拿到的就是本地玩家。
	return Cast<ARPG_BaseCharacter>(GetOwningPlayerPawn());
}

URPG_CombatComponent* URPG_HUDWidget::GetLocalCombatComponent() const
{
	const ARPG_BaseCharacter* Character = GetLocalCharacter();
	return Character ? Character->GetCombatComponent() : nullptr;
}

bool URPG_HUDWidget::TryBindToLocalPlayer()
{
	ARPG_BaseCharacter* Character = GetLocalCharacter();
	if (!Character)
	{
		return false;
	}

	// 走角色基类的接口而不是直接找 PlayerState ——
	// "玩家的 ASC 挂在 PlayerState 上"这个差异已经被基类抹平了，
	// HUD 不该知道这件事。
	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	BoundASC = ASC;

	// ── 一次绑 8 个属性，共用一个回调 ──
	// 分派逻辑放在 HandleAttributeChanged 里按 Data.Attribute 走。
	// 写 8 个独立回调函数的话，每个都要重复一遍"读最大值、算比例"，
	// 而且加属性时要记得再加一个函数 —— 忘了就是"某个条永远不动"。
	BoundAttributes.Reset();
	AttributeDelegateHandles.Reset();

	BoundAttributes.Add(URPG_AttributeSet::GetHealthAttribute());
	BoundAttributes.Add(URPG_AttributeSet::GetMaxHealthAttribute());
	BoundAttributes.Add(URPG_AttributeSet::GetManaAttribute());
	BoundAttributes.Add(URPG_AttributeSet::GetMaxManaAttribute());
	BoundAttributes.Add(URPG_AttributeSet::GetStaminaAttribute());
	BoundAttributes.Add(URPG_AttributeSet::GetMaxStaminaAttribute());
	BoundAttributes.Add(URPG_AttributeSet::GetAttackAttribute());
	BoundAttributes.Add(URPG_AttributeSet::GetDefenseAttribute());

	for (const FGameplayAttribute& Attribute : BoundAttributes)
	{
		// ⚠️ 这是引擎的**非动态**多播委托，所以用 AddUObject 而不是 AddDynamic。
		// 它的触发时机有两处，正好覆盖单机和联机：
		//   ① 服务器上属性被 GE 改动的瞬间
		//   ② 客户端上 GAMEPLAYATTRIBUTE_REPNOTIFY 把复制来的值写进属性时
		AttributeDelegateHandles.Add(
			ASC->GetGameplayAttributeValueChangeDelegate(Attribute)
				.AddUObject(this, &URPG_HUDWidget::HandleAttributeChanged));
	}

	// 绑完立刻整表刷一次。
	// 不刷的话要等第一次属性变化 —— 满血的玩家会看到三条空条，
	// 直到他挨第一刀。这是纯 UI bug，但看起来像"属性没初始化"。
	RefreshAllAttributes();

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] HUD 已接上本地玩家的 ASC（%s）"),
		*GetName(), *GetNameSafe(Character));

	return true;
}

void URPG_HUDWidget::UnbindFromASC()
{
	UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!ASC)
	{
		AttributeDelegateHandles.Reset();
		BoundAttributes.Reset();
		return;
	}

	// ★ 用保存下来的 handle 逐个解绑，而不是 RemoveAll。
	// RemoveAll 会把**别人**（比如头顶血条、将来的其他 UI）注册在同一个
	// 属性上的回调一起摘掉 —— 症状是"开过一次菜单之后敌人血条就不动了"，
	// 而且完全查不到原因。
	const int32 Count = FMath::Min(AttributeDelegateHandles.Num(), BoundAttributes.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ASC->GetGameplayAttributeValueChangeDelegate(BoundAttributes[Index])
			.Remove(AttributeDelegateHandles[Index]);
	}

	AttributeDelegateHandles.Reset();
	BoundAttributes.Reset();
	BoundASC.Reset();
}

// ══════════════════════════════════════════════════════════════════════
//  属性刷新
// ══════════════════════════════════════════════════════════════════════

void URPG_HUDWidget::RefreshAllAttributes()
{
	for (const FGameplayAttribute& Attribute : BoundAttributes)
	{
		RefreshAttribute(Attribute);
	}
}

void URPG_HUDWidget::HandleAttributeChanged(const FOnAttributeChangeData& Data)
{
	RefreshAttribute(Data.Attribute);
}

void URPG_HUDWidget::RefreshAttribute(const FGameplayAttribute& Attribute)
{
	UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!ASC)
	{
		return;
	}

	const URPG_AttributeSet* AttributeSet = ASC->GetSet<URPG_AttributeSet>();
	if (!AttributeSet)
	{
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  为什么 MaxHealth 变化时也要刷血条
	// ══════════════════════════════════════════════════════════════════
	// 血条的显示比例是 Current / Max。最大值变了（吃了个加生命的 Buff、
	// 或者 Buff 到期掉回去），**当前值可能一点没变**，
	// 但血条该变长/变短 —— 因为"满血"的定义变了。
	//
	// 只监听 Health 的话会漏掉这一类，表现是
	// "血量上限变了但条没动，要掉一次血才对齐"。
	const float Health = AttributeSet->GetHealth();
	const float MaxHealth = AttributeSet->GetMaxHealth();
	const float Mana = AttributeSet->GetMana();
	const float MaxMana = AttributeSet->GetMaxMana();
	const float Stamina = AttributeSet->GetStamina();
	const float MaxStamina = AttributeSet->GetMaxStamina();

	// 用 if-else 链而不是全部刷一遍：
	// 每次挨打会触发 1~2 个属性回调，全刷 8 个属性 + 8 个控件是纯浪费。
	// 属性条内部的 SetPercent 会触发排版，能省则省。
	if (Attribute == URPG_AttributeSet::GetHealthAttribute()
		|| Attribute == URPG_AttributeSet::GetMaxHealthAttribute())
	{
		if (HealthBar)
		{
			HealthBar->SetAttributeValues(Health, MaxHealth);
		}
	}
	else if (Attribute == URPG_AttributeSet::GetManaAttribute()
		|| Attribute == URPG_AttributeSet::GetMaxManaAttribute())
	{
		if (ManaBar)
		{
			ManaBar->SetAttributeValues(Mana, MaxMana);
		}
	}
	else if (Attribute == URPG_AttributeSet::GetStaminaAttribute()
		|| Attribute == URPG_AttributeSet::GetMaxStaminaAttribute())
	{
		if (StaminaBar)
		{
			StaminaBar->SetAttributeValues(Stamina, MaxStamina);
		}
	}
	else if (Attribute == URPG_AttributeSet::GetAttackAttribute())
	{
		if (AttackText)
		{
			AttackText->SetText(FText::Format(
				AttackValueFormat, FText::AsNumber(FMath::RoundToInt(AttributeSet->GetAttack()))));
		}
	}
	else if (Attribute == URPG_AttributeSet::GetDefenseAttribute())
	{
		if (DefenseText)
		{
			DefenseText->SetText(FText::Format(
				DefenseValueFormat, FText::AsNumber(FMath::RoundToInt(AttributeSet->GetDefense()))));
		}
	}
}

// ══════════════════════════════════════════════════════════════════════
//  战斗状态（每帧读标签）
// ══════════════════════════════════════════════════════════════════════

void URPG_HUDWidget::UpdateCombatState(float InDeltaTime)
{
	UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!ASC)
	{
		return;
	}

	// ── 闪避图标 ──
	// "闪避时亮起，之后熄灭（常态）" —— 直接映射成标签在不在。
	if (DodgeIcon)
	{
		const bool bDodging = ASC->HasMatchingGameplayTag(RPGTags::State_Dodging);
		DodgeIcon->SetVisibility(bDodging
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}

	// ── 蓄力计时 ──
	const bool bCharging = ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Charging);

	if (bCharging)
	{
		// 边沿：刚开始蓄力 → 计时归零重新数
		if (!bWasCharging)
		{
			ChargeDisplayTime = 0.f;
		}
		ChargeDisplayTime += InDeltaTime;
	}
	else
	{
		// 松手 / 被打断 / 耐力耗尽强制释放 —— 三种结束方式都走到这里，
		// 因为它们的共同点是 State.Attack.Charging 被摘掉了。
		//
		// 这就是需求里"右键松开后条归零"的实现：不需要订阅输入事件，
		// 标签消失本身就是最可靠的信号。
		ChargeDisplayTime = 0.f;
	}
	bWasCharging = bCharging;

	UpdateSkillPanel(bCharging);

	// ── 死亡面板 + 重生倒计时 ──
	const bool bDead = ASC->HasMatchingGameplayTag(RPGTags::State_Dead);

	if (bDead)
	{
		// 边沿：刚死的那一帧，用角色上配的重生时长启动倒计时。
		//
		// 倒计时在**客户端自己数**：State.Dead 是复制的、重生时长是配置常量，
		// 两端都能拿到 —— 不需要服务器再复制一个"还剩几秒"。
		if (!bWasDead)
		{
			const ARPG_BaseCharacter* Character = GetLocalCharacter();
			RespawnCountdown = Character ? Character->GetRespawnDelay() : 0.f;
		}
		else
		{
			RespawnCountdown = FMath::Max(0.f, RespawnCountdown - InDeltaTime);
		}
	}
	else
	{
		RespawnCountdown = 0.f;
	}
	bWasDead = bDead;

	if (DeathPanel)
	{
		DeathPanel->SetVisibility(bDead
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}

	// ── 倒计时文本 ──
	//
	// ⚠️ RespawnDelay = 0 表示**不重生**（敌人的默认值），
	// 这时候不能显示"0 秒后重生" —— 那是在骗玩家，他会一直等着复活。
	// 直接隐藏倒计时，只留一句"你死了"。
	//
	// 剩余秒数向上取整：显示 "3 秒后重生" 应该对应剩 2.1 秒，
	// 而不是剩 3.0 秒的那一瞬间 —— 玩家看到 0 的时候正好该复活。
	if (RespawnCountdownText)
	{
		const ARPG_BaseCharacter* Character = GetLocalCharacter();
		const float RespawnDelay = Character ? Character->GetRespawnDelay() : 0.f;
		const bool bWillRespawn = bDead && RespawnDelay > 0.f;

		RespawnCountdownText->SetVisibility(bWillRespawn
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);

		if (bWillRespawn)
		{
			RespawnCountdownText->SetText(FText::Format(
				RespawnCountdownFormat,
				FText::AsNumber(FMath::CeilToInt(RespawnCountdown))));
		}
	}
}

void URPG_HUDWidget::UpdateSkillPanel(bool bCharging)
{
	UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!ASC)
	{
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  招式名的判定顺序：切手 > 蓄力 > 轻击
	// ══════════════════════════════════════════════════════════════════
	// 顺序不能乱。切手技和蓄力共用同一个 GA、同一个输入，
	// 判定依据是"激活瞬间在不在轻击连段里"。
	// 如果先判 State.Attacking（切手技激活时它可能还在），
	// 就会出现"放的是切手技、UI 显示轻击"。
	//
	// State.Attack.Transition 是这一阶段新加的标签 —— 见 RPG_GameplayTags.h
	// 里"为什么要单独一个标签"的说明。
	FText MoveName;
	bool bShowPanel = false;

	if (ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Transition))
	{
		MoveName = TransitionAttackText;
		bShowPanel = true;
	}
	else if (ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Charging))
	{
		MoveName = HeavyAttackText;
		bShowPanel = true;
	}
	else if (ASC->HasMatchingGameplayTag(RPGTags::State_Attacking))
	{
		// 轻击段位从战斗组件读 —— 它才是连段索引的持有者。
		// 不用数标签，因为"第几段"本来就不是标签能表达的（没有上限）。
		const URPG_CombatComponent* Combat = GetLocalCombatComponent();
		const int32 ComboIndex = Combat ? Combat->GetComboIndex() : 0;

		// 连段索引 0 表示"不在连段中"。理论上不会和 State.Attacking 同时出现，
		// 但真出现了也没必要显示"第 0 段"。
		if (ComboIndex > 0)
		{
			MoveName = FText::Format(LightAttackFormat, FText::AsNumber(ComboIndex));
			bShowPanel = true;
		}
	}

	if (SkillPanel)
	{
		SkillPanel->SetVisibility(bShowPanel
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}

	// ── 招式名文本 ──
	// 只在内容真的变了的时候写。SetText 会触发 Slate 的排版与失效重算，
	// 每帧无脑写等于每帧做一次文本布局 —— 而招式名一秒才变几次。
	if (MoveNameText && !MoveName.EqualTo(LastMoveName))
	{
		LastMoveName = MoveName;
		MoveNameText->SetText(MoveName);
	}

	// ── 蓄力条 ──
	if (ChargeBar)
	{
		float ChargePercent = 0.f;

		if (bCharging)
		{
			// 满格的定义：最后一段蓄力的时间门槛。
			// 从数据资产读而不是写死 —— DA 里把第三段改成 2.5 秒，UI 自动跟上。
			//
			// ⚠️ 不用 GA 上的 MaxChargeTime：那是"强制释放"的保护上限，
			// 通常比第三段门槛大。用它当分母的话，蓄满三段时条才走到一半，
			// 玩家会以为还能继续蓄。
			const URPG_CombatComponent* Combat = GetLocalCombatComponent();
			const URPG_AttackModuleData* Module = Combat ? Combat->GetAttackModule() : nullptr;

			float FullChargeTime = 0.f;
			if (Module)
			{
				const int32 LevelCount = Module->GetHeavyLevelCount();
				if (const FRPG_HeavyAttackLevel* LastLevel = Module->GetHeavyLevel(LevelCount))
				{
					FullChargeTime = LastLevel->RequiredChargeTime;
				}
			}

			ChargePercent = FullChargeTime > KINDA_SMALL_NUMBER
				? FMath::Clamp(ChargeDisplayTime / FullChargeTime, 0.f, 1.f)
				: 0.f;
		}

		ChargeBar->SetPercent(ChargePercent);
	}
}
