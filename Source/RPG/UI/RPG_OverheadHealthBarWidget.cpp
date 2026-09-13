// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RPG_OverheadHealthBarWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"

void URPG_OverheadHealthBarWidget::SetOwningActor(AActor* InOwner)
{
	OwningActor = InOwner;

	// 先按新主人刷一次，让血条在**出生那一帧**就是对的。
	//
	// 订阅本身交给 NativeTick 里那段"ASC 变了就重绑"的逻辑（下一帧会接上）——
	// 这里不重复一遍绑定代码，否则两处各绑一次就是双份回调。
	// 代价只是"出生后到下一帧之间属性变化不会刷新"，那一帧里玩家看不出来。
	RefreshFromOwner();
}

UAbilitySystemComponent* URPG_OverheadHealthBarWidget::ResolveOwnerASC() const
{
	// ⚠️ **不要**用 GetOwningPlayerPawn()。
	//
	// UWidgetComponent 造 Widget 走的是 `CreateWidget(World, WidgetClass)`，
	// 那个重载把 Widget 的 PlayerContext 设成了**第一个本地玩家**
	// （UserWidget.cpp 里的 CreateWidgetInstance(UGameInstance&) → GetFirstGamePlayer），
	// Widget 本身还被 outer 到 GameInstance 上。
	//
	// 后果是每条敌人的血条都以为自己是玩家的血条 —— 显示玩家的血量、玩家的名字，
	// 而且完全不报错。所以主人只能由 URPG_OverheadHealthBarComponent 显式传进来。
	const AActor* Owner = OwningActor.Get();
	if (!Owner)
	{
		return nullptr;
	}

	// 角色基类实现了 IAbilitySystemInterface，
	// 它会把"玩家去 PlayerState 找、敌人从自己身上拿"这个差异抹平。
	if (const ARPG_BaseCharacter* Character = Cast<ARPG_BaseCharacter>(Owner))
	{
		return Character->GetAbilitySystemComponent();
	}

	return nullptr;
}

void URPG_OverheadHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 先刷一次。属性委托只在**变化时**触发，
	// 而血条出现的那一刻（比如敌人刚进入视野）往往没有变化发生 ——
	// 不主动读一次的话，满血的敌人头上会是空条，
	// 要等它挨第一刀才显示正确血量。
	RefreshFromOwner();
}

void URPG_OverheadHealthBarWidget::NativeDestruct()
{
	// ★ 解绑必须成对。
	// 不解的话，角色销毁后 ASC 上的委托还指着这个 Widget ——
	// UObject 委托对已 GC 的对象会安全跳过，但 handle 会一直堆积；
	// 更要紧的是"Widget 被复用（List View 之类）后重复 AddUObject"，
	// 那会让同一个回调被调两次。
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		if (HealthChangedHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute())
				.Remove(HealthChangedHandle);
		}
		if (MaxHealthChangedHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetMaxHealthAttribute())
				.Remove(MaxHealthChangedHandle);
		}
	}
	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	BoundASC.Reset();

	Super::NativeDestruct();
}

void URPG_OverheadHealthBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// ══════════════════════════════════════════════════════════════════
	//  惰性订阅：ASC 没就位就一直重试
	// ══════════════════════════════════════════════════════════════════
	// 为什么不在 NativeConstruct 里一次绑完就完事：
	// WidgetComponent 可能在角色 BeginPlay（也就是 GAS 初始化）之前
	// 就把 Widget 造出来了。那时 ASC 是空的 —— 绑不上，而且**不会报错**，
	// 表现就是"敌人血条永远是空的"，非常难查。
	//
	// 每帧一次的代价是"一个空指针判断"，可以忽略；
	// 换来的是"无论初始化顺序如何都能接上"。
	UAbilitySystemComponent* ASC = ResolveOwnerASC();

	if (ASC != BoundASC.Get())
	{
		// 主人换了（或第一次拿到）—— 先摘掉旧的再绑新的
		if (UAbilitySystemComponent* OldASC = BoundASC.Get())
		{
			if (HealthChangedHandle.IsValid())
			{
				OldASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute())
					.Remove(HealthChangedHandle);
			}
			if (MaxHealthChangedHandle.IsValid())
			{
				OldASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetMaxHealthAttribute())
					.Remove(MaxHealthChangedHandle);
			}
			HealthChangedHandle.Reset();
			MaxHealthChangedHandle.Reset();
		}

		BoundASC = ASC;

		if (ASC)
		{
			// 引擎自带的属性变化委托，**非动态多播**，所以用 AddUObject 而不是 AddDynamic。
			// 它在两个时机被触发：
			//   ① 服务器上属性被 GE 修改时
			//   ② 客户端上 GAMEPLAYATTRIBUTE_REPNOTIFY 把复制来的值写进属性时
			// 后者正是客户端血条能跟着动的原因。
			HealthChangedHandle = ASC
				->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute())
				.AddUObject(this, &URPG_OverheadHealthBarWidget::HandleHealthChanged);

			// ⚠️ 上限也要监听。
			// 吃了个加生命上限的 Buff，**当前血量一点没变**，
			// 但血条该变短 —— 因为"满血"的定义变了。
			// 只监听 Health 的话，表现是"上限涨了但条没动，要挨一刀才对得齐"。
			// （URPG_HUDWidget 里对玩家那三条血条做了同样的事。）
			MaxHealthChangedHandle = ASC
				->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetMaxHealthAttribute())
				.AddUObject(this, &URPG_OverheadHealthBarWidget::HandleHealthChanged);

			// 刚接上，立刻读一次当前值
			RefreshFromOwner();
		}
	}
}

void URPG_OverheadHealthBarWidget::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	// 委托本身只带了"哪一个属性、新旧值是多少"，够不够用取决于业务。
	// 血条的比例是 Health / MaxHealth 两个属性一起决定的，
	// 而委托一次只送一个 —— 所以统一去属性集读当前值，不看 Data。
	// （也因此 Health 和 MaxHealth 可以共用这一个回调。）
	RefreshBarValues();
}

void URPG_OverheadHealthBarWidget::RefreshBarValues()
{
	// ⚠️ 这里**不写 BoundASC**。
	// 订阅状态只有 NativeTick 一处负责 —— 如果这里顺手把 BoundASC 也设了，
	// NativeTick 的 `ASC != BoundASC.Get()` 判定就会认为"已经接上了"，
	// 于是**永远不绑委托**：血条初始值是对的，但之后再也不更新。
	// 那种 bug 看起来像"血条卡住不动"，很难往订阅逻辑上想。
	UAbilitySystemComponent* ASC = BoundASC.IsValid() ? BoundASC.Get() : ResolveOwnerASC();
	const URPG_AttributeSet* AttributeSet = ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;

	if (!AttributeSet)
	{
		return;
	}

	const float Health = AttributeSet->GetHealth();
	const float MaxHealth = AttributeSet->GetMaxHealth();

	// MaxHealth 为 0 的窗口期（属性集还没被 InitAttributesEffect 初始化）
	// 必须挡掉 —— 除零得到的 NaN 塞进 ProgressBar 会让血条整个不显示，
	// 看起来像"UI 没接上"，很容易往错的方向查。
	const float Percent = MaxHealth > KINDA_SMALL_NUMBER
		? FMath::Clamp(Health / MaxHealth, 0.f, 1.f)
		: 0.f;

	if (HealthBar)
	{
		HealthBar->SetPercent(Percent);
	}

	// 边沿检测：只在"跨过阈值"的那一次通知蓝图。
	// 每帧无脑调的话，WBP 里的闪烁动画会被反复重启 —— 看起来像在抽搐。
	const bool bIsLow = Percent > 0.f && Percent <= LowHealthThreshold;
	if (bIsLow != bWasLowHealth)
	{
		bWasLowHealth = bIsLow;
		BP_OnLowHealth(bIsLow);
	}
}

void URPG_OverheadHealthBarWidget::RefreshFromOwner()
{
	RefreshBarValues();

	// 名字文本：只有配了才刷。
	//
	// ⚠️ 用 OwningActor 而不是 GetOwningPlayerPawn() ——
	// 后者返回的是**本地玩家**，会让每个敌人的血条都顶着玩家的名字。
	// 原因见 RPG_OverheadHealthBarComponent.h。
	if (NameText)
	{
		if (const AActor* Owner = OwningActor.Get())
		{
			NameText->SetText(FText::FromString(Owner->GetName()));
		}
	}
}
