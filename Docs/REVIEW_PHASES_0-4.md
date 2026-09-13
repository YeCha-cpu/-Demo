# 阶段 0~4 复盘：把散落的零件连成一条线

> **这份文档和 `ARCHITECTURE.md` 的分工**
>
> `ARCHITECTURE.md` 回答「**是什么**」——有哪些类、哪些标签、怎么配。
> 这份回答「**为什么是这样**」，以及「这些零件怎么咬合在一起」。
>
> 建议读法：先读第一节把骨架立起来，再读第二节的五条原则，
> 然后**带着骨架**回头看第三节的阶段清单。顺序反了会变成背功能列表。

---

## 一、先把骨架立起来：一次攻击，从按键到掉血

这是整个工程最重要的一张图。**四个阶段做的所有事，都只是往这条链上挂了一段。**

```
①  你按下鼠标左键
         ↓
②  RPG_PlayerController 收到增强输入事件
      ├─ 把 Input.Attack.Light 推进输入缓存（CombatComponent）
      └─ 通知 ASC："试试激活这个标签对应的能力"
         （两件事都要做：推缓存是为了连段衔接，激活是为了打出这一下）
         ↓
③  ASC 查映射表：Input.Attack.Light → GA_LightAttack
      → TryActivateAbilityByInputTag
         ↓
④  GA_LightAttack 激活
      ├─ 查连段索引：这是第几段
      ├─ 扣耐力（应用 GE_StaminaCost）+ 挂"恢复阻断"标签
      └─ 播这一段的蒙太奇
         ↓
⑤  蒙太奇播到「判定窗口」的位置
      → AnimNotifyState_AttackWindow 广播 Event.Combat.AttackWindow.Open
         ↓
⑥  GA 收到事件 → 启动 WeaponTrace 任务
      → 每帧在两帧之间的位置连线上做连续 Sweep（防高速穿透）
         ↓
⑦  扫到敌人 → GA 构造一个伤害 GE 的 Spec
      → SetByCaller 写上"这一段的倍率"
         ↓
⑧  GE 里的 ExecutionCalculation 结算
      基础伤害 = 攻击力 × 倍率
      减伤率   = 防御 / (防御 + 100)
      最终伤害 = 基础伤害 × (1 - 减伤率)，且不低于 1
      → 结果写进目标的元属性 IncomingDamage
         ↓
⑨  目标的 AttributeSet::PostGameplayEffectExecute 接住
      → 检查无敌帧 → 扣 Health → 死亡判定
         ↓
⑩  Health 变化 → 自动复制给所有客户端 → 血条刷新、死亡表现
```

**四个阶段分别在图的哪个位置：**

| 阶段 | 在图里的位置 | 一句话 |
|---|---|---|
| **0 地基** | 让 ①~⑩ 有可能发生 | 目录结构、日志通道、74 个标签、能编译 |
| **1 角色 + GAS 骨架** | ②③⑩ | 按键怎么进来、能力怎么被找到、状态怎么复制 |
| **2 战斗核心** | ④~⑨ | 连段、蒙太奇驱动、碰撞检测、伤害结算 |
| **3 资源系统** | ④ 的消耗侧 + 产出侧 | 耐力怎么扣、怎么恢复、什么时候不许恢复 |
| **4 动画** | ⑤ 的**触发源** | "播到哪一帧"这件事由动画资产说了算 |

> 💡 **第 ⑤ 步是整个工程的分水岭。**
> 在你做动画之前，GA 里的判定窗口是用**定时器**模拟的（"模拟时序"分支）；
> 动画接上之后，变成由蒙太奇上的 Notify **在真实的那一帧**触发。
> 这就是为什么阶段 4 要提前做 —— 没有它，后面所有手感调优都是空谈。

---

## 二、五条"承重"原则

**理解了这五条，工程里 90% 的设计都不用背，是推出来的。**

### 原则一 · 状态用 GameplayTag，不用 bool 成员

**反例**：在 `CombatComponent` 里存一个 `bool bAttacking`。

**为什么会坏**：于是"是否在攻击"有了两份真相。GA 被外部打断时，
GAS 会自动摘掉标签，但那个 bool 不会 —— 角色永久卡在"攻击中"，不能移动。
而且这种 bug **只在特定打断时序下出现**，单机测十次九次正常。

**工程里的体现**：`State.Attacking` / `State.Dodging` / `State.Sprinting` /
`State.Attack.Charging.Lv1~3`。动画蓝图每帧查标签，GA 什么都不用推。

**延伸**：标签是**层级**的。挂 `State.Attack.Charging.Lv2` 时，
查父标签 `State.Attack.Charging` 也是 true —— 所以动画用一条粗查询就够了。

**怎么检验你理解了**：如果我问"角色死亡时正在攻击，那个攻击状态要谁来清"——
答案应该是"没有人需要清，因为挂它的能力被删掉了，标签自动就没了"。

---

### 原则二 · 按键和能力之间隔一层映射

**反例**：按键事件里直接 `Cast` 到 `GA_LightAttack` 然后激活。

**为什么会坏**：改键位要动代码；想让一个技能被两个键触发要写两遍；
做按键重绑定无从下手。

**工程里的体现**：两级。

```
物理按键  ──①──▶  Input.* 标签  ──②──▶  Ability.* 能力
          (IMC 资产)      (ASC 里的映射表)
```

- ① 在 `IMC_RPG_Default` 里配 —— 改键不碰代码
- ② 在角色的 `StartupAbilities` 里配 —— 改技能不碰输入代码

**顺带解决了另一个问题**：能力因此**不知道按键的存在**。
同一个 `GA_LightAttack`，玩家按键能触发，AI 用 `TryActivateAbilityByInputTag`
也能触发 —— 走的完全是同一条路，不会出现"玩家能放、AI 放不出来"。

**怎么检验你理解了**：`Input.*` 和 `Ability.*` 为什么要分成两个域，
而不是共用一个？——因为它们的**生命周期和用途不同**：Input 是"玩家此刻的意图"，
Ability 是"这个能力的身份"（`CancelAbilitiesWithTag` 要匹配它）。

---

### 原则三 · 表现层不做逻辑

**反例**：在 `AnimNotify` 里直接 `SpawnActor` 做碰撞检测、直接改属性。

**为什么会坏**：
- 数值和动画焊死，改平衡要动动画资产
- 没办法在没有动画的情况下验证数值链路（阶段 2 就是靠这点提前跑通的）
- 联机下 Notify 在客户端和服务器上的播放时机有细微差异，
  把权威逻辑放这里会引入极难复现的 bug

**工程里的体现**：四个 Notify **只做一件事** —— 广播一个 GameplayEvent，
然后就结束了。收到之后干什么，是 GA 的事。

```cpp
// Notify 的全部工作：
EventData.EventTag = RPGTags::Event_Combat_AttackWindow_Open;
EventData.OptionalObject2 = Animation;   // 带上来源，便于过滤
SendGameplayEventToActor(Owner, ..., EventData);
```

**同一条原则的另一个体现**：`AnimGraph 里一行计算都不要写`。
所有状态判断都在 `URPG_AnimInstanceBase` 的 C++ 里做完，蓝图只连线。
理由很实际：蓝图里的计算没法 diff、没法 code review。

**怎么检验你理解了**：如果策划说"第 3 段的判定窗口往前挪两帧"，要改什么？
——只动蒙太奇资产，一行代码都不碰。这就是这条原则的价值。

---

### 原则四 · 状态由服务器产生，客户端只消费

**反例**：客户端自己 `GiveAbility`、自己应用初始属性 GE。

**为什么会坏**：`GiveAbility` 产生的 Spec 会自动复制到客户端，
客户端自己也调一次就是**两份** —— 症状是"放一次技能触发两遍效果"，
而且**只在联机时出现**。

**工程里的体现**：`RPG_Player::InitializeAbilitySystem()` 里那个 `HasAuthority()` 分支。

```cpp
ASC->InitAbilityActorInfo(PS, this);   // ← 两边都要做（预测和 Cue 定位需要）
if (!HasAuthority()) { return; }        // ← 以下只在服务器做
//   应用初始属性
//   授予起始能力
```

**关键区别**：`InitAbilityActorInfo` **两边都要**，授予能力和初始属性**只服务器做**。
分不清这一条的人，写出来的东西单机永远正常。

**怎么检验你理解了**：为什么初始属性 GE 客户端不用自己应用一遍？
——因为属性的**修改结果**会通过属性集复制同步过来，客户端不需要自己算。
只有"预测"才需要客户端自己跑，那由 GA 的 `NetExecutionPolicy` 负责。

---

### 原则五 · 能配的不要写死

**反例**：五段轻击的倍率写成 `switch (Index)`，或者蒙太奇路径硬编码。

**工程里的体现**：`DA_AttackModule_*`（`PrimaryDataAsset`）。

一把武器 = 一个 DA：5 段轻击的蒙太奇 / 倍率 / 耐力消耗、3 段蓄力的门槛与倍率、
切手技配置、检测源类型与 Socket 名。

**加一把太刀** = 新建一个 DA、填表、挂到角色上，**一行 C++ 都不用改**。

**为什么用 DataAsset 而不是 DataTable**：
- 需要嵌套结构（蓄力组里含三个等级），DataTable 表达起来很别扭
- 含大量软引用（蒙太奇），DataAsset 能直接拖拽赋值并做引用校验
- 能在编辑器里做数据校验（`IsDataValid`），配错了当场标黄
- 数量少（三五个），不需要 DataTable 的批量编辑能力

**怎么检验你理解了**：如果美术说"轻击第 4 段换一个动画"，要动什么？
——DA 里换一个字段，重新编译都不用。

---

## 三、逐阶段体检

### 阶段 0 · 地基

| | |
|---|---|
| **解决的问题** | 让后面所有代码有个地方放，且"出错时看得见" |
| **核心产出** | Feature Folder 结构、`LogRPG_*` 五个日志通道、74 个原生标签、`RPG_AbilitySystemLibrary` |
| **面试会问** | "你们的目录结构是怎么组织的？为什么？" |

**能讲出来的点**：

- **Feature Folder**（`.h`/`.cpp` 同目录）而不是 UE 默认的 `Public/Private` 镜像结构。
  后者是为**插件/跨模块依赖**设计的，单一游戏模块用不上它的好处，
  却要承担"改个头文件要在两个目录间来回跳"的成本。
  配合 `PublicIncludePaths.Add(ModuleDirectory)` 让 include 自带层级：
  `#include "Combat/RPG_CombatTypes.h"` 一眼看出属于哪一层。

- **日志分类**（`LogRPG_Ability` / `LogRPG_Combat` / `LogRPG_AI` / `LogRPG_Animation`）
  而不是全用 `LogTemp`。好处是可以单独开 Verbose：
  `Log LogRPG_Combat Verbose` —— 排查战斗时不会被其他系统的日志淹没。

- **标签用 C++ 声明**（`UE_DEFINE_GAMEPLAY_TAG_COMMENT`）而不是纯 ini。
  纯 ini 只能 `RequestGameplayTag(FName("..."))`，**字符串拼错编译期不报错**，
  运行时才炸而且炸在很远的地方。C++ 声明则是真变量：拼错编译不过、能跳转、
  能全局查找、重构安全。

---

### 阶段 1 · 角色 + GAS 骨架

| | |
|---|---|
| **解决的问题** | 输入怎么进来、ASC 挂在哪、状态怎么复制 |
| **核心文件** | `Core/RPG_PlayerController` / `Core/RPG_PlayerState` / `Character/RPG_*` / `Interfaces/` |
| **面试会问** | "ASC 为什么玩家的放 PlayerState、敌人的放自己身上？" |

**能讲出来的点**：

- **ASC 归属的不对称**：玩家 → `PlayerState`，敌人 → 自己。
  为什么？因为 PlayerState **在角色重生时不会被销毁**。
  如果 ASC 挂在 Pawn 上，死亡重生一次所有能力、冷却、Buff 全没了。
  敌人不需要这个，挂在自身更简单。

- **基类实现接口，子类只提供 ASC**：
  `ARPG_BaseCharacter` 实现 `IAbilitySystemInterface`，把"取 ASC"抽成一个纯虚函数
  `GetASCInternal()`。玩家返回 `PlayerState->GetASC()`，敌人返回自己的。
  好处是接口实现只有一份，行为绝对一致。

- **`IRPG_AbilitySystemInterface` 只加 `GetRPGAttributeSet()` 和 `IsAlive()`**，
  **刻意不重复** `GetAbilitySystemComponent()` —— 那个引擎接口已经有了。
  接口设计的一条经验：不要复制已有的东西。

**踩过的坑（面试可以讲成"你怎么定位问题的"）**：

- 我一度给 `ARPG_PlayerController` 设了 `bCanEverTick = false`，
  理由是"增强输入是事件驱动的，不需要每帧"。结果 WASD 全部失灵。
  **增强输入的事件是在 `TickActor → TickPlayerInput` 里产生的**，
  关掉 tick 等于废掉整个输入管线。`Controller.cpp:62` 特意把它设成 true 就是为这个。

---

### 阶段 2 · 战斗核心

| | |
|---|---|
| **解决的问题** | 连段、蒙太奇驱动、碰撞检测、伤害结算 |
| **核心文件** | `Combat/` 全部 + `AbilitySystem/Abilities/RPG_GA_LightAttack` `RPG_GA_HeavyAttack` `RPG_GA_Dodge` + `Effects/RPG_DamageExecution` + `Tasks/RPG_AbilityTask_WeaponTrace` |
| **面试会问** | "你的连段状态机怎么设计的？""伤害怎么算的？" |

**能讲出来的点**：

- **输入缓存 vs 衔接窗口 —— 两个不同的问题**：
  - **输入缓存**解决"玩家按早了"：按键记下来，等窗口开
  - **衔接窗口**解决"什么时候能接"：定义规则

  缺一不可。只有窗口没有缓存 → 按早了白按，不跟手；
  只有缓存没有窗口 → 可以无限快速连打，失去节奏。
  缓存条目带生命周期（0.5 秒）—— 太短会丢输入，太长会让角色"自己动"。

- **5 段连段用一个 GA，不是 5 个**：每段一个 GA 的话，连段状态无处安放、
  衔接逻辑要写 5 遍、还要处理"旧 GA 结束 + 新 GA 开始"的时序竞争。
  一个 GA 内部循环播 5 段蒙太奇则很自然 —— 连段索引就是它的一个成员变量。

- **切手技为什么合并进 `GA_HeavyAttack`**：两者共用同一个输入（右键）、
  同一套伤害窗口机制、同一个攻击模组配置。合并之后，
  分支判断就是 `ActivateAbility` 开头的一行标签查询。

- **连续 Sweep 防隧穿**：高速挥砍时，如果只在每帧"当前位置"做一次检测，
  两帧之间会**穿过**目标（一帧移动 60cm，目标只有 40cm 厚）。
  正确做法是在"上一帧的位置"和"这一帧的位置"之间连成线段做 Sweep，
  线段扫过的所有东西都能检测到。

- **伤害用 ExecutionCalculation 而不是普通 Modifier**：
  最终伤害依赖**攻守双方的属性**（攻击力 × 倍率 × 防御减伤），
  而普通 GE Modifier 只能引用**一方的**属性。
  Execution 能同时 Capture 两边的值，并且把结果写进元属性
  `IncomingDamage` 中转，让所有后处理（无敌、格挡、飘字、吸血）集中在一处。

**伤害公式**：

```
基础伤害 = 攻击力 × 段位倍率        （默认攻击力 10，5 段倍率 1.0/1.15/1.4/1.6/2.0）
减伤率   = 防御 / (防御 + 100)      （默认防御 10 → 减伤 9%；防御 100 → 减伤 50%）
最终伤害 = 基础伤害 × (1 - 减伤率)  （不低于 1）
```

> 减伤用 `防御/(防御+K)` 而不是线性扣减，是为了**永不出现"完全免疫"**，
> 且收益递减 —— 防御从 0 堆到 100 收益巨大，从 100 堆到 200 收益就小多了。
> 这是 RPG 数值设计里最常用的减伤曲线。

---

### 阶段 3 · 资源系统

| | |
|---|---|
| **解决的问题** | 耐力的产出端（阶段 2 只做了消耗端） |
| **核心文件** | `RPG_GA_StaminaRegen` / `RPG_GA_Sprint` / `RPG_GA_Jump` / `RPG_GA_Heal` / `RPG_GA_ApplyBuff` |
| **面试会问** | "耐力恢复的计时是怎么做的？" |

**能讲出来的点 —— 这是整个工程最"GAS 原生"的一块**：

**"停手 3 秒后开始缓慢恢复"这条规则，没有一行计时代码。**

```
消耗侧                          产出侧
攻击/闪避/跳跃/奔跑                GE_StaminaRegen
      ↓                          （Infinite，每 0.25 秒 +3.75）
ConsumeStamina()
      ↓                                ↑
扣耐力 + 挂 GE_StaminaRegenDelay   被 OngoingTagRequirements 控制
      ↓                                ↑
State.Stamina.Blocked（3 秒）═══ 抑制 ═══╝
```

- 时间由 GE 的 **Duration** 表达（3 秒）
- 开关由 **标签** 表达（`State.Stamina.Blocked`）
- 恢复速率由 GE 的 **Period + Magnitude** 表达

**对比传统写法**：存一个 `LastConsumeTime`，每帧 Tick 判断
`Now - LastConsumeTime > 3.f && !bIsDoingSomething`。后者的每个条件
都要手动维护，漏一个就是"耐力永远不恢复"或"边打边回"。

而标签方案的额外好处：**联机下天然正确**（标签会复制），
**调试图里一眼能看出为什么没恢复**（GE 显示为 Inhibited，抑制它的是哪个标签）。

> 📌 另外那块 **`Target Tags` vs `Target Tag Requirements`** 的区别
> 也值得记住：一个是"给目标贴标签"，一个是"检查目标有没有标签"。
> 名字像，作用相反。

---

### 阶段 4 · 动画

| | |
|---|---|
| **解决的问题** | 把"播到哪一帧"从定时器换成真实动画 |
| **核心文件** | `Animation/RPG_AnimInstanceBase` / `Animation/Notifies/` 四个 Notify（**无 .cpp 逻辑**） |
| **面试会问** | "动画和逻辑怎么解耦的？""动画状态怎么驱动？" |

**能讲出来的点**：

- **`URPG_AnimInstanceBase` 是"翻译层"**：把 CharacterMovement 的速度、
  GameplayTag 的战斗状态，翻译成动画能懂的 17 个量。
  AnimGraph 只连线、不计算。

- **混合空间横轴用 `SpeedRatio`（= Speed / MaxSpeed）而不是 `Speed`**：
  因为 `MaxSpeed` 会变（走 300 / 冲刺 850 / 蹲 180）。
  用绝对速度做轴，就得为每种姿态各做一个混合空间，改数值还要重做动画。

- **动画蓝图结构**：`SM_Main` 一个状态机 + 四个 `Blend graph` 类型的状态。
  > UE 的**状态机不能嵌套**（状态机图的右键菜单里没有"子状态机"这个选项）。
  > `Blend graph` 类型的状态拥有自己独立的动画图 —— 这是 UE 里实现
  > "移动动画模块化"的正确姿势。

- **一个反直觉的坑**：新建蒙太奇的 `Blend In` / `Blend Out` 默认各 **0.25 秒**。
  对一段 0.37 秒的攻击动画来说，淡出在 `0.37 - 0.25 = 0.12 秒` 就开始了，
  **而淡入要到 0.25 秒才走完** —— 两者重叠，混合权重永远到不了 1，
  角色只能做出"半吊子模糊版"的动作。
  攻击蒙太奇必须手工把混合时间调到 0.05~0.1 秒。

---

## 四、现在这个工程能做什么

诚实清单（**能**的）：

- 玩家：走 / 跑 / 冲刺 / 跳 / 蹲，五段轻击连段、三段蓄力重击、切手技、翻滚无敌帧
- 耐力一进一出：消耗 + 停手 3 秒后恢复，全部由标签和 GE 驱动
- 治疗、加攻 Buff、减防 Debuff
- 敌人：有角色和 ASC，**但不会动**（AI 是阶段 5）
- 联机：L1 属性复制 + L2 权威与预测（Listen Server，PIE 双客户端可测）
- 有完整的编辑器配置文档和资产体检

**不能**的：
- 敌人不会动、不会攻击
- 没有死亡表现（血扣到 0 只停住，没有动画）
- 没有 UI（血条耐力条得靠 `showdebug abilitysystem` 看）
- 远程模组的发射物没做（阶段 9）

---

## 五、已知缺口汇总

| 缺口 | 影响 | 在哪记录 |
|---|---|---|
| 切手技分支在客户端/服务器各自判断 | 联机下两端可能选到不同分支 | `ARCHITECTURE.md` 14.3 |
| `ChargeMoveSpeedScale` 没接入 | 蓄力时移动不会变慢 | `PHASE4_MONTAGE_SETUP.md` §11 |
| `AttackTag` 字段只用于日志 | 想做"按段位触发不同特效"时要理清 | 同上 |
| 闪避的无敌帧事件也没按来源过滤 | 理论上"第一次闪避的迟到 End 事件清掉第二次刚上的无敌"可能发生。目前靠"闪避进行中无法再次激活"挡住了大部分情况 —— 但那是运气，不是设计 | `RPG_GA_Dodge.cpp:171-191` |
| ~~`State.Hit` / `Ability.Death` 等标签已定义未使用~~ | ~~受击表现、死亡表现还没做~~ | ✅ 阶段 6 已补上 |

> 这些**不是忘了**，是有意识地记下来。工程里最危险的不是"还没做"，
> 是"不知道还没做"。

---

## 六、复习路线：按这个顺序读代码

如果你想真正吃透，按这条路线读，**每读一个文件问自己"它为什么在这里"**：

| 顺序 | 文件 | 读的时候问自己 |
|---|---|---|
| 1 | `Core/RPG_GameplayTags.h` | 为什么标签分这么多域？`Input` 和 `Ability` 为什么要分开？ |
| 2 | `Interfaces/RPG_AbilitySystemInterface.h` | 为什么这个接口这么小？ |
| 3 | `Character/RPG_BaseCharacter.h` | 为什么接口在基类实现、ASC 却由子类提供？ |
| 4 | `Character/RPG_Player.cpp` | `InitAbilityActorInfo` 为什么两边都要调，而授予能力只服务器调？ |
| 5 | `Core/RPG_PlayerController.cpp` | 为什么绑定能力输入时要 `PushInputTag` 再 `TryActivate`？ |
| 6 | `AbilitySystem/RPG_AbilitySystemComponent.h` | 为什么要用 Handle 而不是 Class 来激活？ |
| 7 | `Combat/RPG_InputBuffer.h` + `RPG_CombatComponent.h` | 为什么"是否攻击中"不放在这个组件里？ |
| 8 | `AbilitySystem/Abilities/RPG_GameplayAbilityBase.cpp` | `CanActivateAbility` 为什么不能用 `this` 的成员？ |
| 9 | `AbilitySystem/Abilities/RPG_GA_LightAttack.cpp` | 切段时为什么要拆三样东西（回调/蒙太奇/任务）？ |
| 10 | `AbilitySystem/Effects/RPG_DamageExecution.cpp` | 为什么要用 Execution 而不是普通 Modifier？ |
| 11 | `AbilitySystem/RPG_AttributeSet.cpp` | 为什么 clamp 要写两个回调？ |
| 12 | `Animation/RPG_AnimInstanceBase.cpp` | 为什么每帧重取 ASC 而不缓存？ |

---

## 七、自测：你应该能回答的问题

不看文档，试着回答下面这些。**答不上来的就是还没消化的地方**，
可以回头翻对应章节，或者直接问我。

1. ASC 为什么玩家的挂在 PlayerState 上？换成 Pawn 会出什么问题？
2. `Input.Attack.Light` 和 `Ability.Attack.Light` 分别在什么时候被读？
   为什么要分成两个？
3. 攻击时角色被打断，`State.Attacking` 标签是谁清掉的？
4. 玩家按了攻击键，但当前在硬直里。这一下会怎样？（提示：三个阶段）
5. 五段轻击的倍率配在哪个资产里？策划想改成 1.0/1.2/1.5/1.8/2.5 要动什么？
6. 伤害公式里为什么用 `防御/(防御+100)` 而不是直接减？
7. "停手 3 秒恢复耐力"—— 如果策划改成 5 秒，要改什么？（不该改代码）
8. 动画蓝图怎么知道"角色现在在攻击"？为什么不是 GA 推一个 bool 过去？
9. 蒙太奇的 `Blend In` 默认值是多少？为什么它对短动画是灾难？
10. 联机下，客户端的 `RPG_Player::InitializeAbilitySystem` 会做哪几件事、
    不会做哪几件事？
11. 为什么蒙太奇上的 Notify 只广播事件，而不直接做碰撞检测？
12. 连续 Sweep 相比逐帧单点检测，解决了什么问题？

---

## 八、接下来

阶段 5（敌人 AI）、阶段 6（受击/死亡/重生）、阶段 7（战斗 HUD）、
阶段 8（联机补完）、阶段 9（打磨与扩展）都在 [`ARCHITECTURE.md`](./ARCHITECTURE.md) 里。
等你消化完再动 —— 这不是拖延，**后面几层要用的标签、事件、能力接口
现在都已经就位了，看懂它们再往下写会快很多**。

有想深挖的点随时说，我可以：
- 挑一条链路带你逐行走一遍代码
- 拿上面的自测题考你，答错的地方展开讲
- 就某一个设计给你几个替代方案，讲清各自的取舍
