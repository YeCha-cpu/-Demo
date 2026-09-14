# 阶段 8 · 独立代码审查记录

> 这次审查的触发点：写 [`REVIEW_GUIDE.md`](./REVIEW_GUIDE.md) 时发现，
> 那份文档里写着"独立代码审查从阶段 8 起固定为提交前动作"，
> **但阶段 8 的六个提交一次都没跑过**。文档承诺的工作流得和实际做到的对得上。
>
> 审查方式：三个互不知情的审查者，各自负责一块，独立对着
> `D:/UE5/UE_5.8/Engine/Source` 的引擎源码核对。范围是 `6fef4c6..HEAD`（阶段 8 全部改动）。
>
> **为什么要分开三个人**：互不知情时，同一处被两个人盯上就是强信号；
> 一个人盯全部则容易在自己的判断上打转。

---

## 1. 结论摘要

| | 数量 |
|---|---|
| 确认的问题 | **15** |
| 本轮已修 | **11** |
| 列为待办（需决定 / 需重构） | **4** |
| 注释与引擎事实不符（项目硬规矩禁止） | **5**（全部已修） |

**最重要的一条是好的**：阶段 8 那些修复本身是有效的 —— 特别是 8-④ 那个
"订阅重试是死代码"的修复，三个状态路径 (i)~(iv) 都被独立核实覆盖了。
真正的问题集中在**新加的诊断代码本身有假阳性**和**注释的因果描述写反了**。

---

## 2. 已修（本轮）

### 2.1 敌人每次生成都打一条**误诊** Warning ★

`RPG_OverheadHealthBarWidget.cpp`

上一轮加的"属性集在不在"自检，判据是"绑定成功那一瞬属性集在不在"。
但组件的 `BeginPlay()`（→ `InitWidget()` → 订阅）**早于**角色的 `BeginPlay()`，
而敌人的属性集是在 `ARPG_Enemy::InitializeAbilitySystem()` 里才
`AddSpawnedAttribute` 的 —— **在组件 BeginPlay 之后**。

所以"ASC 在、属性集还没登记"是**每个敌人必经的中间态**，不是故障。
结果是每刷一个敌人报一次"属性集没复制过来"，**内容还是错的**，非常误导。

**修法**：就位判定改成 `!ASC || !ASC->GetSet<URPG_AttributeSet>()`，
两者缺一都继续重试；只有**重试 25 次（约 5 秒）后仍然拿不到属性集**才报一次 Warning。

> 用"重试次数"而不是"是否为空"来判定故障，就是为了把必经的中间态和真故障分开 ——
> 这类"把正常中间态当故障"的错误，比不写诊断更糟，因为它会训练人忽略警告。

### 2.2 蒙太奇诊断的两个假阳性来源

`RPG_AnimInstanceBase.cpp/.h`

上一轮的诊断**声称**消灭了"实播 0.02s / 全长 0.00s"这类垃圾日志，
实际上只是**换了个入口重新出现**：

| 成因 | 说明 |
|---|---|
| **同一资产重播** | 记录以**资产**为键，而受击蒙太奇池小的时候很容易连续两次取到同一个。`Montage_Play` 的顺序是"先停同组旧的 → 建新实例 → 广播 `Started`"，于是旧实例的 `Ended` 会在记录被覆盖**之后**才到，算出"实播 0.02s" |
| **`PlayRate != 1`** | `PlayedFor` 是**墙钟时间**，`Length` 是**蒙太奇秒**，只在 `Rate == 1` 时可以直接比。角色配了 `HitReactPlayRate` / `DeathMontagePlayRate` 时，全长 1.77s 的动画 0.88s 播完是**完全正常**的，却会被报成"被提前结束 50%" |

**修法**：

- 记录值改成 `TArray<FMontagePlayRecord>`，`Ended` 时取**最早一条**（FIFO）——
  旧实例先结束，配对最早的记录
- 记录里加上 `PlayRate`，判据用 `Length / PlayRate` 换算成墙钟秒再比
- 日志里配了速率时把全长和速率一起打出来，否则看到"实播 0.88s / 应播 0.88s"
  会不知道为什么和蒙太奇全长对不上

### 2.3 `AddDynamic` 不查重 + 缺 `NativeUninitializeAnimation`

`RPG_AnimInstanceBase.cpp/.h`

**引擎事实**：`AddDynamic` 展开成 `AddInternal`，实现是**无条件**
`InvocationList.Add(...)`（`ScriptDelegates.h`），只在 `DO_ENSURE` 构建里先 `ensure` 一下。
查重的是另一个宏 `AddUniqueDynamic`。

而 `UAnimInstance::InitializeAnimation()` **第一句就是 `UninitializeAnimation()`**，
且同一个实例可以被重复初始化（`USkeletalMeshComponent::InitializeAnimScriptInstance()`
在"实例已存在 + `bForceReinit`"时直接对现有实例再调一次，`bForceReinit` 来自重新注册路径）。

一旦重入：两个回调各绑两份 → 每段蒙太奇打两条日志、`ensure` 报红，
而日志里的"实播"数字会被第二条 `-1.00s` 污染 —— **正好把诊断本身弄坏**。

**修法**：换 `AddUniqueDynamic`；override `NativeUninitializeAnimation()`
成对解绑并清空记录。

### 2.4 头顶血条的三处小缺陷

| 问题 | 后果 | 修法 |
|---|---|---|
| `NativeConstruct` 没重置 `LastSeenHealth` | `NativeConstruct` **会跑第二遍**（组件不可见时 Slate 对象析构 → `NativeDestruct`；恢复可见时重建 → 再跑一次）。隐藏期间血量掉了，重建时会被判成"掉血了"，血条**凭空亮一下** | 加 `LastSeenHealth = -1.f`（与 `SetOwningActor` 里的处理对齐） |
| `RevealForDamage` 没有边沿检测 | 5 秒窗口内每挨一下都重发 `BP_OnRevealChanged(true)`，WBP 的淡入动画被反复重启 → 观感是"闪" | 判 `GetVisibility()`，已亮着就只重置计时 |
| `RevealDuration = 0` 时永不收回 | `ClampMin` 只约束编辑器输入框。运行时设成 0 的话，`InternalSetTimer` 在 `InRate > 0` 为假时**只清旧定时器、不建新的**，`HideAfterDamage` 永不触发 | 提前返回 |

> 第二条尤其值得记：本文件末尾对 `BP_OnLowHealth` 是**特意做了边沿检测**的
> （注释原话"每帧无脑调的话，WBP 里的闪烁动画会被反复重启"），
> `BP_OnRevealChanged` 却漏了同一条纪律 —— **同一个文件里两处同类问题，只修了一处。**

### 2.5 `Server_PushInputTag` 的服务器侧两道门

`RPG_PlayerController.cpp`

RPC 的参数来自客户端，服务器不能无条件相信。

- **门 ①**：标签白名单（`HasAbilityForInputTag` 查映射表）。
  不查的话任意 `GameplayTag` 都能进缓存，白占槽位（容量 4，满了挤掉最旧的），
  把**合法**的连段输入挤出去
- **门 ②**：存活判断。客户端在 `OnAbilityInputPressed` 里判过，但那是在客户端 ——
  服务器不能依赖客户端的判断

> **注释里写清它不是什么**：这道门防的是"改过的客户端"，不是"别的玩家"
> （`ShouldCallRemoteFunction` 已保证只有 Owner 能对自己 Actor 发 Server RPC）。
> 也别把它说成安全防线 —— 真正的安全来自"服务器有权重算一切"。

### 2.6 五处"注释撒谎"（项目硬规矩禁止）

| 位置 | 错误 | 实际情况 |
|---|---|---|
| `RPG_AttributeSet.h` ★ | "GE 的 Modifier 走 `PreAttributeBaseChange`，不走 `PreAttributeChange`" | **GE 的 Modifier 会经过 `PreAttributeChange`** —— 两处调用点在 `SetNumericValueChecked`（通用 setter）里，GE 路径最终也会走到它。真正的区别是**时序**：轮到 `PreAttributeChange` 时 BaseValue 已经落盘了，它只能纠正 CurrentValue。结论（两个都要写）不变，理由要换 |
| `RPG_AttributeSet.h` | "两处调用点都在属性拷贝函数里" | 在 `FGameplayAttribute::SetNumericValueChecked`（`AttributeSet.cpp:72-106`），是写属性值的通用 setter |
| `RPG_AttributeSet.cpp` | 尸体挨打会广播 `Event.Combat.Hit` → `GA_HitReact` | 走的是**死亡**分支（`NewHealth <= 0` 对尸体成立）→ `Event.Combat.Death` → `GA_Death`。"从活到死只触发一次"是**由那道早退创造出来的**，不是本来就成立 |
| `RPG_BaseCharacter.cpp` | `IsLocalController()` 的行为归因于 `AController` 基类实现 | 那只适用于 **AI**（`AAIController` 没重写）。**玩家**走的是 `APlayerController` 的重写（`PlayerController.cpp:304-352`），规则完全不同。对玩家的结论之所以成立靠的是别的前提（`bIsLocalPlayerController` 只在 SimulatedProxy 时置位 + `bOnlyRelevantToOwner`）。**这个前提一旦被打破（旁观者流程、L3），会静默吞掉别人的表现** |
| `RPG_AbilitySystemComponent.h` | `FGameplayTag InputTag` "早期版本有，后来移除" | 引擎**从来没提供过** InputTag 版本。网上那种写法来自 Lyra 的 `FLyraAbilitySet_GameplayAbility`，是示例项目扩展 |
| `RPG_AttributeSet.cpp` | `GameplayEffectExtension.h:18-28` | 实际是 `:17-30`，`Target` 在**第 29 行** —— 引用区间刚好把要说明的成员漏在外面（已补） |
| `RPG_AttributeSet.cpp` | `GetAvatarActor()` 兜底的场景 | 它内部第一句是 `check(AbilityActorInfo.IsValid())`（`AbilitySystemComponent.cpp:2184-2188`）—— 无效时**直接断言/崩**，不是返回 null。兜底覆盖的是"Avatar 弱指针为空"，不是"ActorInfo 无效"（已补） |

> 第 1 条最要命：它是**教学论据**，被反复引用。理由是错的、结论是对的 ——
> 这种最难被发现，因为它"看起来能用"。

---

## 3. 列为待办（未修）

### 3.1 【高】`Server_PushInputTag` 与 `ServerTryActivateAbility` 走**不同 ActorChannel**

**这是本轮最重要的一条未修问题。**

`NetDriver.cpp` 的 `InternalProcessRemoteFunctionPrivate` 里通道是按 **owner Actor** 取的。
玩家的 ASC 挂在 `ARPG_PlayerState` 上，所以：

- `ServerTryActivateAbility` 走 **PlayerState 的通道**
- `Server_PushInputTag` 声明在 `ARPG_PlayerController` 上，走 **PlayerController 自己的通道**

UE 只保证**同一通道内**有序。真实网络下（丢包重传、上行拥塞）两条通道的相对顺序会被打乱。

**失败场景**：客户端按轻击 → 本地推缓存 + 发 RPC + 激活。若 RPC **晚于**
`ServerTryActivateAbility` 到达，服务器的连段 GA 先跑（缓存是空的，第 1 段正常开始），
随后 RPC 才把条目塞进去 —— 而这条输入**没有任何对应的激活**。
只要 1 秒内开出衔接窗口，服务器就会消费它，**单方面开始第 2 段**，
打出所属客户端从未预测过的一刀。

**PIE 双客户端抓不到**：loopback 无损、同帧发送，调用顺序恰好就是正确顺序。

**修法**：把 RPC 挪到 `URPG_AbilitySystemComponent` 上，与 `ServerTryActivateAbility`
共用 PlayerState 通道。客户端本来就通过 ASC 发 Server RPC（`ServerTryActivateAbility`
就是这么工作的），所以这条路是通的。

> 或者改用引擎自带的 `AbilityLocalInputPressed(int32 InputID)` +
> `ServerSetInputPressed(Handle)` —— 它本来就和激活同通道、同预测键。
> 那正是本项目**明确放弃**的那条路（为了标签的可读性），
> 放弃的代价就是这个顺序保证，值得在注释里写明。

### 3.2 【中低】"主人变成 null"路径会保留旧订阅

`TryBindToOwnerASC()` 的 (i)~(iv) 四条状态路径都已核实覆盖，唯一残留：

`OwningActor` 失效/换成一个还没有 ASC 的新主人时，`if (!ASC)` 提前返回，
**`BoundASC` 与两条委托原样保留**。而 `ResolveOwnerAttributeSet()` 优先认 `BoundASC` ——
于是新主人头顶会挂着**旧主人的血条**，旧主人挨打时这根血条还会亮。

**当前无调用点可达**（全库只有一处 `SetOwningActor`，每个 Widget 只调一次），
属于潜伏缺口。修法：`if (!ASC)` 里先判断主人身份是否变了，变了就先解绑。

### 3.3 【低】`HideAfterDamage` 立刻 `Collapsed`，"淡出"永远看不到

头文件说 `BP_OnRevealChanged` 是"给 WBP 做**淡入淡出**用"，但收回时先
`SetVisibility(Collapsed)` 再广播 —— 控件当场没有几何体，淡出动画一帧都看不见。
要么改成 `Hidden` + 延时 `Collapsed`，要么把注释改成"只做淡入"。

### 3.4 【待决定】尸体挨打不冒飘字

那道 `if (OldHealth <= 0.f) return;` 的注释只声明"不再广播任何事件"，
但它把后面的 `Multicast_ShowDamageNumber` 也一起吞了。
连段最后一刀落在刚死的目标上、或 AOE 扫过尸体时，**屏幕上没有任何伤害反馈**。

如果这是刻意的（尸体不给打击反馈）应该写进注释；如果不是，把飘字块挪到早退之前。

---

## 4. 已核实无误（别再怀疑）

审查者逐一核对过、结果是**对的**的地方。列出来是为了省掉下一个 reviewer 的往返。

### 4.1 引擎行号引用（这些**没有**撒谎）

`PlayerState.cpp:28`、`Controller.cpp:90-113`（对 AI）、`Controller.cpp:67`、
`WidgetComponent.cpp:1761`、`AbilitySystemComponent.cpp:1940-1946`、
`GameplayEffectExtension.h`、`GameplayEffect.cpp:3069`、
`SpawnedAttributes` 的 `COND_None`、`REPNOTIFY_Always` 的必要性、
`AbilitySystemComponent.h:1529` 的 `GetAvatarActor`、
`BaseEngine.ini:1867` 的 `NetServerMaxTickRate=30` —— **全部逐行核对无误**。

### 4.2 代码逻辑

| 处 | 结论 |
|---|---|
| `FTimerManager::SetTimer` 同一 handle 重复调用是**替换**不是叠加 | ✅ `TimerManager.cpp` 先 `InternalClearTimer` |
| `SetTimer` 传 `this`，对象已销毁不会崩 | ✅ UObject 委托持 `TWeakObjectPtr`，`Execute()` 前先判 `IsBound()` |
| 两个定时器都在 `NativeDestruct` 清了，循环定时器不会无界增长 | ✅ |
| `AddUObject` / `Remove(Handle)` 配对正确 | ✅ 没有 `RemoveUObject` 这个 API，头文件说法正确 |
| `Collapsed` 下 tick 的行为 | ✅ `UpdateCanTick` **完全不看可见性** —— 即"当前会 tick"成立，"没有保证"也成立，所以绕开 tick 的决定是对的 |
| 组件与控件两层可见性正交 | ✅ `UWidgetComponent` 不动 `Widget->SetVisibility` |
| `OnRep_ActivateAbilities` 的重试走虚派发 | ✅ 基类成员函数指针 + virtual → 回调仍打回子类覆盖 |
| `Data.Target` 是**受击者**的 ASC（不是攻击者） | ✅ `InternalExecuteMod` 里传的是 `*Owner` |
| `SetIncomingDamage(0)` 先于 `SetHealth` 无隐患、不会递归 | ✅ `PostGameplayEffectExecute` 只由 `InternalExecuteMod` 调用 |
| `RebuildInputTagHandleMap` 不加 `FScopedAbilityListLock` 的取舍 | ✅ 引擎那个 API 本身就不持锁 |
| `IsLocallyControlledPlayer()` 对 AI / 主机上的远端玩家 / 主机自己 | ✅ 三者的返回值都符合注释 |
| Server RPC 不需要额外的"权威判断" | ✅ `ShouldCallRemoteFunction` + `DataReplication.cpp` 的 `FUNC_NetServer` 接收侧校验 |

### 4.3 一条死代码

`HasAbilityForInputTag()` 此前**全库没有调用方**（只有声明和定义）。
本轮 §2.5 的白名单让它有了实际用途 —— 顺带说明：**审查前它是死代码，
不能当成"已验证的修复"来引用。**

---

## 5. 这次审查暴露的方法论问题

| 问题 | 表现 |
|---|---|
| **诊断代码本身没有"假阳性"验收** | 两次都栽在同一件事上：为消灭假警报写的代码，自己引入了新的假警报（§2.1 的误诊 Warning、§2.2 的两个假阳性）。**加诊断时必须问：什么正常情况会走到这条分支？** |
| **同一文件里的同类问题只修一处** | `BP_OnLowHealth` 做了边沿检测，`BP_OnRevealChanged` 没做（§2.4）。修一处时应该顺手扫一遍同文件的同类模式 |
| **注释里的"因果关系"比数字更容易写错** | 行号错了好核对，因果错了很难发现（§2.6 的第 1、3 条）。**写"因为 A 所以 B"之前，先确认 A 是不是真的** |
| **"理论正确"和"实际可达"是两回事** | §3.2 的路径确实有缺陷，但当前无调用点可达。审查报告把它标成"潜伏"而非"bug"是对的 —— **报告严重度时要区分这两者** |

---

*审查时间：阶段 8-⑥（`ce080a5`）之后 · 已修部分提交于 `阶段8-⑦`*
