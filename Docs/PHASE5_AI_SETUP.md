# 阶段 5 · 敌人 AI 配置指南

> 让敌人从"站着不动的木桩"变成"会巡逻、会追你、会打你"。
> 行为树的逻辑在 C++ 节点里，**编辑器里只做连线和填表**。

---

## 0. C++ 侧已经就绪的东西

| 类 | 编辑器里显示为 | 作用 |
|---|---|---|
| `ARPG_AIController` | — | 感知 + 黑板 + 启动行为树 |
| `URPG_BTService_CombatUpdate` | **`RPG 战斗状态更新`** | 每 0.2 秒维护目标记忆与脱战计时 |
| `URPG_BTTask_Patrol` | **`RPG 巡逻`** | 依次走遍巡逻点 |
| `URPG_BTTask_MoveToTarget` | **`RPG 追击目标`** | 追玩家 / 去最后已知位置搜索 |
| `URPG_BTTask_Attack` | **`RPG 攻击`** | 发动一次攻击并等它播完 |
| `URPG_BTDecorator_CanAttack` | **`RPG 可以攻击`** | 距离 + 存活判定 |

---

## 1. 步骤 1 · 建黑板资产

`Content/_My/AI/` → 右键 → **Artificial Intelligence → Blackboard**，命名 `BB_RPG_Enemy`

打开它，按下面这张表建键。**名字必须逐字一致**（区分大小写）——
`RPG_BlackboardKeys.h` 里的常量就是这些字符串。

| 键名 | 类型 | 谁写 | 作用 |
|---|---|---|---|
| `SelfActor` | Object | 引擎自动 | 自身。**建资产时会自动带上**，不用手加 |
| `TargetActor` | **Object** | AIController 感知回调 | 当前锁定的目标 |
| `LastKnownLocation` | **Vector** | 感知回调 / 战斗状态更新 | 目标最后出现的位置 |
| `bTargetVisible` | **Bool** | 感知回调 | 此刻看不看得见 |
| `HomeLocation` | **Vector** | AIController 出生时 | 出生点 |
| `PatrolLocation` | **Vector** | 巡逻任务 | 当前巡逻点 |
| `PatrolIndex` | **Int** | 巡逻任务 | 巡逻到第几个了 |
| `AttackRange` | **Float** | AIController 出生时 | 攻击距离 |
| `bInCombat` | **Bool** | 感知回调 / 战斗状态更新 | 是否在战斗中 |

> ⚠️ **类型必须对**。键存在但类型不对时，`SetValueAsXxx` 会**静默失败** ——
> 不报错，只是值永远是空的。表现和"键名拼错"一模一样。
>
> 💡 **键名拼错也不会报错**。这是为什么 C++ 侧把键名收在
> `RPG_BlackboardKeys.h` 里 —— 那边拼错编译不过，只有黑板资产这一处
> 需要你人工对齐名字。

---

## 2. 步骤 2 · 建行为树资产

`Content/_My/AI/` → 右键 → **Artificial Intelligence → Behavior Tree**，命名 `BT_RPG_Enemy`

创建时会问你用哪个黑板，选 **`BB_RPG_Enemy`**。

### 2.1 整体结构

```
[Root]
│
└── Selector 「根选择」
    │
    ├── Sequence 「战斗」
    │   │   Decorator: Blackboard → bInCombat  Is Set
    │   │   Service:   RPG 战斗状态更新
    │   │
    │   └── Selector 「战斗动作」
    │       │
    │       ├── Sequence 「攻击」
    │       │   │   Decorator: RPG 可以攻击
    │       │   ├── Task: RPG 攻击
    │       │   └── Task: Wait  (0.6 ~ 1.4 秒)
    │       │
    │       └── Sequence 「追击」
    │           ├── Task: RPG 追击目标
    │           └── Task: Wait  (0.2 秒)
    │
    └── Sequence 「巡逻」
        ├── Task: RPG 巡逻
        └── Task: Wait  (1.0 ~ 3.0 秒)
```

### 2.2 为什么这么连

**根选择器只有两个分支，战斗在前。**
Selector 从左往右试，第一个成功的分支胜出。战斗分支不成立（`bInCombat` 为假）时才轮到巡逻。

**★ 战斗分支的装饰器必须把 `Flow Abort Mode` 设成 `Lower Priority`** ——
这是"玩家一进视野就立刻追"的关键，见下面 2.3 的第一条。

**巡逻分支故意不加 Decorator。**
看起来应该加一个"`bInCombat` 为假"的条件，但那样一旦战斗分支**整个失败**
（比如目标死了、最后已知位置又丢了），两个分支都不成立 ——
行为树什么都不做，AI 就**站着发呆**了。

不加条件的巡逻分支是个永远可用的兜底：任何情况下 AI 至少还在动。
"永远有一个能执行的分支"是写行为树的一条实用原则。

**`bInCombat` 用 `Is Set` 而不是 `Is True`。**
Bool 键的 `Is Set` 在"键存在且有值"时成立，配合我们只往里写 true/false，
效果等价于"值为真"。语义上更贴近"这个键有没有被赋值"。

**攻击分支在追击分支前面。**
够得着就打，够不着才追。反过来连的话 AI 会永远在追、永远不打。

**每个动作后面都跟一个 `Wait`。**
- 攻击后的 Wait 是**攻击间隔** —— 没有它 AI 会零延迟连打，变成绞肉机
- 追击后的 Wait 防止每帧重新发起寻路请求（0.2 秒一次足够跟住人）
- 巡逻后的 Wait 是"到了巡逻点站一会儿"，让巡逻有节奏

> 💡 `Wait` 节点在 **Flow Control** 分类下。它的 `Wait Time` 和
> `Random Deviation` 都支持两种模式：填常量，或者从黑板上取一个
> Float 键。这里用常量就行。

### 2.3 各节点的配置

**「战斗」分支的 Blackboard 装饰器 ★ 最容易漏的一条**

挂在 `bInCombat` 上那个。选中它 → Details → **`Flow Control`** 分类：

| 字段 | 值 |
|---|---|
| **`Flow Abort Mode`** | **`Lower Priority`** |
| `Notify Observer` | `On Result Change`（默认，不用动） |

> ⚠️ **`Flow Abort Mode` 默认是 `Nothing`**，而这一条不改的话，
> 敌人**不会**在玩家进入视野时立刻反应 —— 它会先把当前这个巡逻点走完、
> `Wait` 站完，才回头看战斗分支。
>
> **为什么**：Selector 不会主动重新判断。敌人正在执行巡逻分支时，
> Selector 就"卡"在那个分支上，直到它自己结束才回头。
>
> 把 `Flow Abort Mode` 设成 `Lower Priority` 之后，这个装饰器会把自己
> **注册成一个观察者**：条件不成立时它每帧重新检查，一旦变成成立就
> 立刻打断排在它右边的巡逻分支，抢过执行权。延迟在一帧以内。
>
> 引擎源码里的说明（`BTCompositeNode.cpp:287-292`）：
> ```
> // - observers with mode "Lower Priority" will try to reactivate themselves
> ```
>
> **四个选项的区别**：
>
> | 值 | 含义 |
> |---|---|
> | `Nothing` | 只当"进门条件"用 —— **默认值，就是反应迟钝的原因** |
> | `Lower Priority` | 条件成立时打断排在右边的分支 ← **本项目要的** |
> | `Self` | 条件变化时打断自己（用于"进了分支后条件再变就退出"） |
> | `Both` | 两者都要 |

**`RPG 战斗状态更新`（Service）**

放在「战斗」Sequence 上。选中节点后在右侧 Details 里：

| 字段 | 值 |
|---|---|
| `Lose Target After Seconds` | `6.0` |

**`RPG 可以攻击`（Decorator）**

| 字段 | 值 |
|---|---|
| `Range Tolerance` | `1.15` |

> 略大于 1 是故意的：攻击有前摇，等"确实进范围"再出手，
> 玩家往往已经走出去了，会出现"AI 一直追但永远打不到"。

> 💡 这个装饰器的 `Flow Abort Mode` **已经在 C++ 里默认设成 `Lower Priority`** 了
> （构造函数里设的，你不用管）。理由和上面那条一样：不设的话，
> 敌人追到你面前了也不会停下来挥拳，会一路贴着你走 ——
> 因为 Selector 卡在"追击"分支上，不会回头重新检查攻击条件。
>
> 这也是"自己的装饰器"和"引擎装饰器"的区别：前者能在代码里定默认值，
> 后者只能在 Details 里手动设。

**`RPG 攻击`（Task）**

| 字段 | 值 |
|---|---|
| `Input Tag` | **`Input.Attack.Light`** |
| `Attack Timeout Seconds` | `3.0` |

> ⚠️ `Input Tag` 用的是 **Input 域**的标签，不是 `Ability.Attack.Light`。
> 因为它模拟的是"一次按键" —— 和玩家走完全相同的入口。
> 选错了的话能力不会激活（ASC 的映射表里只有 Input 域的键）。

**`RPG 巡逻`（Task）**

| 字段 | 值 |
|---|---|
| `Acceptance Radius` | `60` |
| `Move Timeout Seconds` | `10` |
| `Fallback To Home Location` | ✅ 勾上 |

**`RPG 追击目标`（Task）**

| 字段 | 值 |
|---|---|
| `Acceptance Radius` | `60` |
| `Move Timeout Seconds` | `10` |
| `Chase Visible Target Only` | ⬜ **不勾** |

> `Chase Visible Target Only` 不勾 = 看不见目标时去"最后已知位置"搜索。
> **这是"有脑子"和"很傻"的分界线**：勾上的话玩家绕柱子跑一圈敌人就放弃了。

---

## 3. 步骤 3 · AIController 蓝图

`Content/_My/AI/` → 右键 → Blueprint Class → 展开 All Classes → 搜 **`RPG_AIController`**
命名 `BP_RPG_AIController`

| 分类 | 字段 | 值 |
|---|---|---|
| `RPG\|AI` | **Behavior Tree Asset** | **`BT_RPG_Enemy`** |
| `RPG\|AI\|Perception` | Sight Radius | `1500` |
| `RPG\|AI\|Perception` | Lose Sight Radius | `2000` |
| `RPG\|AI\|Perception` | Peripheral Vision Angle Degrees | `45` |
| `RPG\|AI\|Perception` | Hearing Range | `1200` |

> 💡 `Sight Radius` 是**厘米**。1500 = 15 米。

---

## 4. 步骤 4 · 敌人蓝图

打开 `BP_RPG_Enemy`：

### 4.1 用哪个大脑

| 分类 | 字段 | 值 |
|---|---|---|
| `Pawn` | **AI Controller Class** | **`BP_RPG_AIController`** |
| `Pawn` | Auto Possess AI | `Placed in World or Spawned`（C++ 里已默认设好） |

### 4.1b 移动速度（发现玩家就跑起来）

| 分类 | 字段 | 值 |
|---|---|---|
| `RPG\|Movement` | **Combat Move Speed** | **`600`**（默认值） |

**它的行为**：

| 状态 | MaxWalkSpeed |
|---|---|
| 巡逻 / 没发现目标 | `Walk Speed`（300） |
| **发现目标后** | **`Combat Move Speed`（600）** |
| 脱战后 | 自动降回 300 |

**切点**：感知回调里一发现目标就立刻切（不是等行为树轮询），
脱战时由 `RPG 战斗状态更新` 服务切回来。

> 💡 **为什么是 600 而不是和玩家冲刺一样快（850）**：
> 敌人的定位是"跟得上你，但你还能甩掉它"。
> 跑得和玩家一样快的话，追逐就失去张力了 —— 玩家永远拉不开距离。

> ⚠️ **这个改动只影响服务器**。`MaxWalkSpeed` 在引擎里**不是复制属性**
> （`CharacterMovementComponent.h:274` 的 `UPROPERTY` 上没有 `replicated`）。
> 客户端的 `MaxWalkSpeed` 会保持原值 —— 但这不影响正确性：
> 敌人的**位置移动本身是复制的**，客户端看到的就是对的速度；
> 而客户端的 `MaxWalkSpeed` 只被动画拿来算 `SpeedRatio`，
> 算出来会被 Clamp 到 0~1，方向上依然正确。
>
> 如果将来要做"敌人被减速 50%"这类**会让速度变慢**的效果，
> 那个 Clamp 就不够用了（分母偏大 → 比值偏小，动画会变慢），
> 届时需要把速度做成 `ReplicatedUsing` 属性并在 `OnRep` 里重算。

### 4.1c 追击为什么是"跟随 Actor"而不是"走到某个坐标"

这是**追击手感**的分水岭，而且两种写法看起来都很合理。

```cpp
// ❌ 走到目标"现在"的位置 —— 一个坐标快照
MoveRequest.SetGoalLocation(Target->GetActorLocation());

// ✅ 跟随目标 Actor —— 引擎持续观察它的位置
FAIMoveRequest MoveRequest(Target);
```

**用坐标快照会发生什么**：

```
拍快照(你在 A 点) → 敌人走向 A 点 → 走到时你已经到 B 点了
    → 停一下 → 再拍快照 → 再走
    表现：一顿一顿地追，而且永远在追你几秒前站的地方
```

**用 Actor 会发生什么**：引擎内部会调

```cpp
PathResult.Path->SetGoalActorObservation(*MoveRequest.GetGoalActor(), 100.0f);
// 见 AIController.cpp:913
```

接口注释写得很清楚（`NavigationData.h:362`）：

> "enables path observing specified AActor's location and
>  **update itself if actor changes location**"

也就是**目标移动超过 100cm 就自动重算路径** —— 这才是"时刻确认玩家位置"。

**顺带一个好处**：跟随期间移动任务**不会结束**，所以追击分支后面那个
`Wait 0.2` 根本轮不到执行（它只在真正追到人时才生效）。
"追一下就停一下"里的"停"自然就没了。

**还有一个必须处理的细节**：追着追着玩家躲进墙后了怎么办？

如果继续跟着 Actor 跑，那等于**透视** —— 玩家躲哪都甩不掉。
所以代码里在 `TickTask` 里检测"可见性是否翻转"：

| 变化 | 处理 |
|---|---|
| 看得见 → 看不见 | 立刻改成去 `LastKnownLocation`（最后看到你的地方） |
| 看不见 → 看得见 | 重新锁定，跟着真人跑 |

而且这个切换是**就地换移动目标**，不是结束任务让行为树绕一圈 ——
绕一圈意味着原地站 `Wait` 那么久，又是一次"一顿"。

---

### 4.1d 为什么改了速度还要管动画

**只改 `MaxWalkSpeed` 是不够的** —— 人会变快，但看着还在"走"。

原因是动画蓝图用**两条独立的判据**：

| 判据 | 来源 | 决定什么 |
|---|---|---|
| `SpeedRatio` | `Speed / MaxWalkSpeed` | BlendSpace **内部**播到哪一帧 |
| `MovementState` 枚举 | **`State.Sprinting` 标签** | 用哪条**状态机** |

而 `State.Sprinting` 原本只有 `GA_Sprint` 会挂 —— 敌人没有这个能力，
所以它的 `MovementState` 一直停在 `Grounded`，走的是「走路」那条状态机。

C++ 里已经在 `SetCombatMovement()` 里补上了：进入战斗移动时挂标签、
退出时摘掉，并且用 `CountToOwner` 让标签**复制到所有客户端**
（否则会出现"服务器上在跑、客户端上在走"）。

> ⚠️ **但"挂了标签"不等于"要进跑步状态机"。**
>
> `MovementState` 变成 `Sprinting` 的条件是 **`State.Sprinting` 标签 且
> 角色真的在移动**（`Speed > IdleSpeedThreshold`）。
>
> 只判标签的话，敌人**攻击后站在 Wait 里也会进跑步状态机** ——
> 那条状态机的混合空间拿 `SpeedRatio` 当横轴，站着时它是 0，
> 于是播混合空间最慢的采样点（走路）→ **原地踏步**。
> 玩家站着按 Shift 出现"行走"也是同一个原因。
>
> 判定逻辑在 `URPG_AnimInstanceBase::UpdateLocomotion`。

**你还需要确认动画蓝图这边**：

| 检查项 | 在哪 |
|---|---|
| `SM_Main` 里有 `Sprinting` 状态 | `ABP_RPG_Base` 的 AnimGraph |
| 它的转移规则是 `MovementState == Sprinting` | 转移线 |
| 那个状态的动画图里放的是**跑的**混合空间 | `BS_RPG_WalkSprint` 或类似 |
| 混合空间的 1.0 采样点是**跑步**动画而不是走路 | 打开混合空间看采样点 |

> ⚠️ 最后一条最容易漏：如果你的走路混合空间的 1.0 采样点放的是走路动画，
> 那么 `SpeedRatio` 顶到 1.0 时播的仍然是走路 —— 这时问题不在状态机，
> 在混合空间的采样点配置。

### 4.2 能力（**关键 —— 不配 AI 就挥空拳**）

`RPG|Abilities` 分类：

| 字段 | 值 |
|---|---|
| **Startup Abilities** | `Input.Attack.Light` → **`GA_LightAttack`** |
| Startup Passive Abilities | `GA_StaminaRegen` |
| Init Attributes Effect | `GE_InitAttributes` |

> ⚠️ 敌人**不需要**配 `GA_Dodge` / `GA_Sprint` / `GA_Jump` —— AI 用不上。
> 但 `GA_StaminaRegen` 要配，否则打两下就没耐力了（而且不会恢复）。

### 4.3 战斗组件

选中 `CombatComponent`：

| 字段 | 值 |
|---|---|
| `RPG\|Combat` → Default Module | **`DA_AttackModule_Unarmed`**（和玩家共用同一套徒手招式） |

### 4.4 动画

| 组件 | 字段 | 值 |
|---|---|---|
| `Mesh` | Anim Class | `ABP_RPG_Enemy` |

---

## 5. 步骤 5 · 关卡准备

### 5.1 NavMeshBoundsVolume

拖一个 **`NavMeshBoundsVolume`**（Place Actors → 搜 `NavMesh`）到关卡里，
用缩放工具把它拉大，**覆盖敌人活动的整片区域**。

> ⚠️ **没有它敌人一步都走不了** —— 而且不报错，只是站着不动。
>
> 摆完按 **`P`** 键，能看到**绿色网格**才算导航生成成功。
> 看不到绿色的排查顺序：
> ① Volume 是不是太小/没把地面包住 ② `Brush Settings` 里 `Z` 方向的厚度
> 够不够（太薄的话地面不算在导航里）③ 地面是不是 `Static` 移动性

### 5.2 巡逻点（TargetPoint）

> 💡 **`ATargetPoint` 是引擎自带的 C++ 类，不是内容资产** ——
> 不需要"新建资产"，直接往关卡里摆。

**怎么摆**：

1. 打开 **Window → Place Actors**（面板没显示的话）
2. 面板顶部的**搜索框**里输入 `Target`
3. 出现 **Target Point** → **拖进视口**
4. 搜不到就切到 **All Classes** 分类再搜

或者用 World Outliner 左上角的 **`+ Add`** 按钮，同样搜 `Target`。

**摆几个**：2~4 个，围成一条路线。摆完在大纲里改名叫
`PatrolPoint_01` / `_02` / `_03`，后面拖数组时好认。

**它是什么**：一个 `BillboardComponent`（编辑器图标）+ 一个 `ArrowComponent`（箭头），
**没有碰撞、没有运行时逻辑**，纯粹是"位置书签"。

> ⚠️ **箭头方向不参与逻辑**。巡逻代码只取 `GetActorLocation()`，不读旋转 ——
> 想让敌人到点后朝某个方向看，当前版本做不到。

**两个注意点**：

- 巡逻点要在导航网格覆盖范围内，**或者正上方** ——
  代码里 `MoveTo` 开了 `SetProjectGoalLocation(true)`，点会被**投影到导航网格上**，
  所以稍微悬空没关系。但那一带压根没有导航网格的话，移动请求直接失败。
- **别摆在墙里/石头里** —— 投影后落点在几何体内部的话，寻路找不到路径。

### 5.3 拖进敌人的巡逻点数组

`RPG_Enemy::PatrolPoints` 标的是 `EditInstanceOnly` —— **每个敌人实例各配各的**：

1. **在大纲里选中关卡里的那只 `BP_RPG_Enemy` 实例**（不是双击打开蓝图）
2. Details 面板 → `RPG|AI` → **Patrol Points**
3. 点数组的 `+` 加项，然后从大纲里把 TargetPoint **拖**进去

```
World Outliner                Details 面板（选中 BP_RPG_Enemy）
  ├─ PatrolPoint_01  ──拖──▶  Patrol Points [0]  ●
  ├─ PatrolPoint_02  ──拖──▶  Patrol Points [1]  ●
  ├─ PatrolPoint_03  ──拖──▶  Patrol Points [2]  ●
  └─ BP_RPG_Enemy
```

> 💡 用 `EditInstanceOnly` 是刻意的：摆十个敌人不用做十个蓝图子类，
> 每个实例拖一次就够。

---

## 6. 验收

PIE，逐项确认：

| 检查项 | 期望 |
|---|---|
| 敌人出生后 | 开始沿巡逻点走，每到一个点停 1~3 秒 |
| 你走向它 | 进入视觉范围后**转向你** |
| 你继续靠近 | 停止巡逻，朝你走过来 |
| 走到攻击距离 | **挥拳**，你能看到攻击动画和伤害 |
| 你绕到柱子后面 | 它走到柱子后面（最后已知位置）找你 |
| 你躲够 6 秒 | 它放弃，走回巡逻路线 |
| 你被打到 0 血 | 它**立刻**停止攻击（不会对尸体挥拳） |

### 用日志确认

控制台输入 `Log LogRPG_AI Log`，正常应该看到：

```
[BP_RPG_AIController_C_0] 感知已配置：视觉 1500/丢失 2000 半径、45.0° 半角；听觉 1200
[BP_RPG_AIController_C_0] 行为树已启动：BT_RPG_Enemy
[BP_RPG_AIController_C_0] 目标已 6.2 秒不可见，脱战返回巡逻
```

想看逐条感知事件（每次发现/丢失都打）：
```
Log LogRPG_AI Verbose
```

### 直接看 AI 在想什么

**这是行为树最好用的调试手段**——PIE 运行时按 **`'`（单引号）** 键，
调出 **Behavior Tree Debugger**：

- 当前正在执行哪个分支（高亮显示）
- 黑板上的每个值实时是多少
- 每个节点执行过几次

"AI 为什么不动"这类问题的答案，九成在这个窗口里一眼就能看出来 ——
比读日志快得多。

---

## 7. 常见问题排查

| 症状 | 最可能的原因 |
|---|---|
| **敌人站着完全不动** | ①`Behavior Tree Asset` 没配（看日志有没有 Error）②`BB` 没设成 `BB_RPG_Enemy` ③关卡里没有 `NavMeshBoundsVolume` |
| **玩家进视野了，敌人却要把巡逻走完才反应** | 「战斗」分支装饰器的 `Flow Abort Mode` 还是默认的 `Nothing` —— 改成 **`Lower Priority`** |
| 敌人巡逻正常，但**看不见你** | 感知的阵营检测没开 —— C++ 里已经默认全开了，但如果你在蓝图上覆盖过感知配置，检查三个 `Detect *` 是不是都勾上 |
| 敌人看见你但不追 | 黑板里 `bInCombat` 没变成 true；或者 `TargetActor` 是空的（键名/类型对不上） |
| 追过来了但**不打** | ①`AttackRange` 太小 ②敌人的 `Startup Abilities` 里没配 `Input.Attack.Light → GA_LightAttack` ③耐力耗光了 |
| 挥拳但**没有伤害** | 敌人的 `CombatComponent` 没配 `Default Module`；或者那个 DA 里没填蒙太奇 |
| 打一下之后**再也不动了** | 攻击超时保护触发了（日志有 Warning）—— 检查 `State.Attacking` 有没有被正常清理 |
| 敌人不巡逻、只在原地站着 | ①`Patrol Points` 数组是空的 —— 注意要在**关卡实例**上配，不是打开蓝图配 ②TargetPoint 不在导航网格覆盖范围内（悬空没关系，代码会投影到网格上，但那一带得**有**网格） |
| **敌人追人一顿一顿的** | C++ 已修（见 §4.1d）。如果还有，检查追击分支的 `Wait` 是不是太长 —— 追击时移动任务不会结束，这个 Wait 只在追到人时才生效，0.2 秒足够 |
| 敌人追到面前却不打，一路贴着走 | `RPG 可以攻击` 的 `Flow Abort Mode` —— 已在 C++ 里默认设好，如果你改过它记得改回 `Lower Priority` |
| 敌人一直追到天边 | `Lose Target After Seconds` 太大；或者追逐的 `Move Timeout` 太长 |
| **两只敌人时行为错乱** | 行为树节点被当成共享实例了 —— C++ 里已经都设了 `bCreateNodeInstance = true`，如果自己加了新节点记得也设上 |
| 敌人对着尸体挥拳 | 战斗状态更新服务没跑起来（它是挂在「战斗」Sequence 上的，检查挂对没有） |

---

## 8. 面试考点速查

这一阶段涉及的几个高频问题：

**Q：行为树怎么等一个技能播完？**
A：用潜在任务（Latent Task）模式。节点返回 `InProgress`，
行为树会停在它上面；技能结束时调 `FinishLatentTask` 才继续。
直接返回 `Succeeded` 的话，AI 会在攻击动画播放期间继续移动。

**Q：AI 和玩家的技能怎么保证行为一致？**
A：让 AI 走和玩家**完全相同**的入口 —— 都往输入缓存推一个 `Input.*` 标签，
再走 ASC 的标签映射表激活能力。不另写一套"AI 专用"路径，
就不可能不一致。

**Q：怎么判断技能结束了？事件还是状态？**
A：本工程用**状态标签**（`State.Attacking`）而不是结束事件。
因为事件只覆盖"正常播完"这一条路径，而被取消、被打断、
没配蒙太奇等情况都不会发事件，AI 会一直等下去。
标签由 GAS 托管生命周期，**无论怎么结束都会被摘掉**。

**Q：追击时怎么避免"玩家绕个柱子就甩掉 AI"？**
A：把"目标"和"最后已知位置"分成两个数据。
丢视野时清前者、留后者，AI 会去最后看到你的地方找一圈。
再配一个脱战计时，找不到才回去巡逻。

**Q：行为树节点有哪些容易踩的坑？**
A：①忘了用潜在任务（技能没播完就动）②`MoveTo` 是异步的，
发完请求要等回调 ③节点默认是**共享实例**，存逐实例状态要么用
`NodeMemory`、要么开 `bCreateNodeInstance` ④移动没有超时兜底的话，
寻路失败会让 AI 永久卡死
