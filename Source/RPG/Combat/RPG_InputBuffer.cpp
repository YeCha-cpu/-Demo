// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RPG_InputBuffer.h"

#include "Core/RPG_LogChannels.h"

void URPG_InputBuffer::Push(const FGameplayTag& InputTag, float LifeTime, float Now)
{
	if (!InputTag.IsValid()) return;

	// ── 容量控制：满了丢最旧的 ──
	// 为什么丢旧而不是拒绝新？因为玩家**最新**按下的才代表当前意图。
	// 比如玩家连按三次轻击，我们要保住最后一次的意图，而不是让容器被
	// 前两次占满后拒绝第三次。
	if (Entries.Num() >= MaxEntries)
	{
		Entries.RemoveAt(0);

		UE_LOG(LogRPG_Combat, VeryVerbose,
			TEXT("输入缓存已满（%d），丢弃最旧条目以腾出空间"), MaxEntries);
	}

	FRPG_BufferedInput NewEntry;
	NewEntry.InputTag = InputTag;
	NewEntry.Timestamp = Now;
	NewEntry.LifeTime = LifeTime;

	Entries.Add(MoveTemp(NewEntry));	// MoveTemp 会将引用转换为右值引用

	UE_LOG(LogRPG_Combat, VeryVerbose, TEXT("缓存输入 %s（当前缓存 %d 条）"), *InputTag.ToString(), Entries.Num());
}

bool URPG_InputBuffer::Consume(FGameplayTag& OutTag, float Now)
{
	// 顺带清理过期条目 —— 惰性清理，不开 Timer
	PruneExpired(Now);

	if (Entries.IsEmpty())
	{
		return false;
	}

	// ── 取出位置由策略决定 ──
	int32 Index;
	if (Mode == ERPG_InputBufferMode::Stack)
	{
		// 栈：取末尾（最后压入的）—— 尊重玩家最新的意图
		Index = Entries.Num() - 1;
	}
	else
	{
		// 队列：取开头（最早压入的）—— 按输入顺序依次执行
		Index = 0;
	}

	OutTag = Entries[Index].InputTag;
	Entries.RemoveAt(Index);

	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("消耗缓存输入 %s（剩余 %d 条）"), *OutTag.ToString(), Entries.Num());

	return true;
}

void URPG_InputBuffer::PruneExpired(float Now)
{
	// 惰性清理：只在 Push / Consume 时调用。
	// 为什么不开 Timer？因为容器只在这两个时刻被访问，
	// 惰性清理的时机恰好就是"需要准确内容"的时刻，而且零额外开销。
	// 用 Timer 反而会引入"清理时机与使用时机错位"的微妙问题。
	const int32 RemovedCount = Entries.RemoveAll([Now](const FRPG_BufferedInput& Entry)
	{
		return Entry.IsExpired(Now);
	});

	if (RemovedCount > 0)
	{
		UE_LOG(LogRPG_Combat, VeryVerbose,
			TEXT("清理了 %d 条过期输入（剩余 %d 条）"), RemovedCount, Entries.Num());
	}
}

FString URPG_InputBuffer::ToDebugString() const
{
	if (Entries.IsEmpty()) return TEXT("(空)");

	TStringBuilder<256> Builder;
	Builder.Appendf(TEXT("[%s] "), Mode == ERPG_InputBufferMode::Stack ? TEXT("栈") : TEXT("队列"));

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		// 标记栈顶/队头，调试时一眼能看出下一个会被消耗的是哪个
		const bool bIsNext = (Mode == ERPG_InputBufferMode::Stack)
			? (i == Entries.Num() - 1)
			: (i == 0);

		Builder.Appendf(TEXT("%s%s%s"),
			bIsNext ? TEXT("→") : TEXT(""),
			*Entries[i].InputTag.ToString(),
			(i < Entries.Num() - 1) ? TEXT(", ") : TEXT(""));
	}

	return FString(Builder.ToString());
}
