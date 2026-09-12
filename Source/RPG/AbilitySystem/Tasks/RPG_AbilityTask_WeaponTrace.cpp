// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Tasks/RPG_AbilityTask_WeaponTrace.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

#include "Core/RPG_LogChannels.h"

URPG_AbilityTask_WeaponTrace::URPG_AbilityTask_WeaponTrace()
{
	// 开启逐帧 Tick —— 轨迹检测的本质就是"每帧采样连成线"，
	// 没有 Tick 就没法形成连续轨迹。
	// （bTickingTask 定义在 UGameplayTask 基类，不是 UAbilityTask）
	bTickingTask = true;
}

URPG_AbilityTask_WeaponTrace* URPG_AbilityTask_WeaponTrace::CreateWeaponTraceTask(
	UGameplayAbility* OwningAbility,
	ERPG_TraceSource TraceSource,
	float TraceRadius,
	FName SocketStart,
	FName SocketEnd)
{
	URPG_AbilityTask_WeaponTrace* Task = NewAbilityTask<URPG_AbilityTask_WeaponTrace>(OwningAbility);

	Task->TraceSource = TraceSource;
	Task->TraceRadius = FMath::Max(TraceRadius, 1.f);
	Task->SocketStart = SocketStart;
	Task->SocketEnd = SocketEnd;

	return Task;
}

void URPG_AbilityTask_WeaponTrace::Activate()
{
	Super::Activate();

	// 首帧只记录基准位置，不做检测 ——
	// 因为"上一帧在哪"还不存在，没有可扫掠的区间。
	bHasPreviousSample = false;
	HitActorsThisSwing.Reset();
}

void URPG_AbilityTask_WeaponTrace::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	SampleAndSweep();
}

void URPG_AbilityTask_WeaponTrace::OnDestroy(bool bInOwnerFinished)
{
	// 清空去重表。虽然 Task 每次都是新实例，但显式清理能避免
	// 万一被复用时的隐蔽 bug，也让意图更清楚。
	HitActorsThisSwing.Reset();

	Super::OnDestroy(bInOwnerFinished);
}

bool URPG_AbilityTask_WeaponTrace::GetCurrentSample(FVector& OutStart, FVector& OutEnd) const
{
	const AActor* Avatar = GetAvatarActor();
	if (!Avatar)
	{
		return false;
	}

	// 目前两种检测源都从角色的骨骼网格体取 Socket：
	//   · 徒手   → hand_l / hand_r
	//   · 近战武器 → 武器 Mesh 上的 Socket（武器挂在角色骨骼上时，
	//                这些 Socket 名在角色网格体上同样能取到）
	//
	// 将来如果武器是独立的 Actor，这里改成先找武器组件再取其 Socket。
	const USkeletalMeshComponent* MeshComp = Avatar->FindComponentByClass<USkeletalMeshComponent>();
	if (!MeshComp)
	{
		return false;
	}

	if (SocketStart.IsNone() || SocketEnd.IsNone())
	{
		return false;
	}

	// 检查 Socket 是否存在。缺失时静默返回 false 而不是每帧报错刷屏，
	// 但第一次出现时会在 SampleAndSweep 里打一条 Warning（见下）。
	if (!MeshComp->DoesSocketExist(SocketStart) || !MeshComp->DoesSocketExist(SocketEnd))
	{
		return false;
	}

	OutStart = MeshComp->GetSocketLocation(SocketStart);
	OutEnd = MeshComp->GetSocketLocation(SocketEnd);

	return true;
}

void URPG_AbilityTask_WeaponTrace::SampleAndSweep()
{
	FVector CurrentStart, CurrentEnd;
	if (!GetCurrentSample(CurrentStart, CurrentEnd))
	{
		// 拿不到采样点通常是 Socket 名字配错了。只在第一次提示，避免每帧刷屏。
		static bool bWarnedOnce = false;
		if (!bWarnedOnce)
		{
			bWarnedOnce = true;
			UE_LOG(LogRPG_Combat, Warning,
				TEXT("轨迹检测拿不到采样点：请检查 Socket 名「%s」/「%s」是否存在于角色的骨骼网格体上"),
				*SocketStart.ToString(), *SocketEnd.ToString());
		}
		return;
	}

	// 首帧只建立基准
	if (!bHasPreviousSample)
	{
		PreviousStart = CurrentStart;
		PreviousEnd = CurrentEnd;
		bHasPreviousSample = true;
		return;
	}

	const AActor* Avatar = GetAvatarActor();
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  把检测线段包成一个胶囊
	// ══════════════════════════════════════════════════════════════════
	// 胶囊的"高度方向"对齐线段方向，这样武器横着挥时检测体也横着，
	// 而不是永远竖着 —— 后者会导致横扫时判定严重失真。
	const FVector CurrentCenter = (CurrentStart + CurrentEnd) * 0.5f;
	const FVector PreviousCenter = (PreviousStart + PreviousEnd) * 0.5f;

	const FVector Segment = CurrentEnd - CurrentStart;
	const float HalfHeight = FMath::Max(Segment.Size() * 0.5f, 1.f);

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(TraceRadius, HalfHeight);

	const FQuat Orientation = Segment.IsNearlyZero()
		? FQuat::Identity
		: FRotationMatrix::MakeFromZ(Segment).ToQuat();

	// 排除自己 —— 否则刀会砍到自己身上
	FCollisionQueryParams Params(TEXT("RPGWeaponTrace"), /*bTraceComplex*/ false, Avatar);

	TArray<FHitResult> Hits;

	// 统一走 Sweep 路径。
	// 这一帧如果手没动（比如前摇的停顿），给一个极小的位移让 Sweep
	// 退化成重叠检测 —— 这样代码只有一条路径，不必维护两套逻辑，
	// 也避免了 Overlap 和 Sweep 返回类型不同带来的麻烦。
	const FVector MoveDelta = CurrentCenter - PreviousCenter;
	const FVector SafeDelta = MoveDelta.IsNearlyZero()
		? FVector(0.f, 0.f, 0.01f)
		: MoveDelta;

	// ★ 关键：从上一帧位置**扫掠**到当前帧位置。
	// 这一步就是防隧穿的全部意义所在。
	World->SweepMultiByChannel(
		Hits, PreviousCenter, PreviousCenter + SafeDelta, Orientation, ECC_Pawn, Shape, Params);

	// 更新基准位置
	PreviousStart = CurrentStart;
	PreviousEnd = CurrentEnd;

	if (Hits.IsEmpty())
	{
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  去重：同一次挥砍只命中同一目标一次
	// ══════════════════════════════════════════════════════════════════
	// 检测窗口通常持续好几帧，不去重的话同一刀会打出多倍伤害。
	// （ThisSwing 在 Task 创建时清空，所以下一刀能重新命中同一个目标。）
	TArray<FHitResult> NewHits;
	NewHits.Reserve(Hits.Num());

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor)
		{
			continue;
		}

		if (HitActorsThisSwing.Contains(HitActor))
		{
			continue;
		}

		HitActorsThisSwing.Add(HitActor);
		NewHits.Add(Hit);
	}

	if (NewHits.Num() > 0)
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("轨迹检测命中 %d 个新目标"), NewHits.Num());

		OnHit.Broadcast(NewHits);
	}
}
