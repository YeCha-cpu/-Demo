# 阶段 4 · 动画蓝图搭建指南

> 把已经写好的战斗代码变成"能看得见的战斗"。
> 这一阶段之前，所有攻击都走 GA 里的模拟时序分支（用定时器代替 AnimNotify），
> 数值能验证，但手感无从评估。

---

## 0. C++ 侧已经就绪的东西

`Source/RPG/Animation/RPG_AnimInstanceBase.h/.cpp` 已编译通过。
它每帧从 CharacterMovement 和 GameplayTag 读出下面这些量，**蓝图里直接取用即可**：

| 变量 | 类型 | 含义 |
|---|---|---|
| `Speed` | float | 水平速度（cm/s），已剔除垂直分量 |
| `MaxSpeed` | float | 当前姿态的最大速度（走 300 / 冲刺 850 / 蹲 180） |
| **`SpeedRatio`** | float | `Speed / MaxSpeed`，恒在 0~1 ★ 混合空间横轴用这个 |
| `Direction` | float | 移动方向（度）：0 = 前，+90 = 右，-90 = 左，±180 = 后 |
| `VerticalVelocity` | float | 垂直速度，用于区分起跳 / 滞空 / 下落 |
| `bIsInAir` | bool | 是否腾空 |
| `bIsCrouching` | bool | 是否蹲伏 |
| **`MovementState`** | 枚举 | `Grounded` / `Sprinting` / `InAir` / `Crouching` ★ 状态机切换依据 |
| `bIsAttacking` | bool | 攻击中（轻击连段 + 重击都算） |
| `bIsDodging` | bool | 闪避中 |
| `bIsSprinting` | bool | 冲刺中（挂 `State.Sprinting` 标签，**且真的在移动**）|
| `bIsCharging` | bool | 蓄力中 |
| `ChargeLevel` | int32 | 蓄力段位 0~3 |
| `bIsInvulnerable` | bool | 无敌帧中 |
| `bIsDead` | bool | 已死亡 |
| `AttackModuleType` | 枚举 | 当前攻击模组（徒手 / 近战 / 远程） |
| `ComboIndex` | int32 | 连段索引 0~5，调试显示用 |

> 💡 全部标了 `Transient`（不序列化）和 `BlueprintReadOnly`（蓝图只读）。
> 你在 AnimGraph 里能引用它们，但改不了 —— 改值的地方只有 C++ 一处。

---

## ⚠️ 1. 先纠正一个设计错误：UE 的状态机**不能嵌套**

架构文档 8.1 节原本写的是这样：

```
SM_Main
 ├── State: Grounded  → Sub-Graph / Linked Anim Graph → SM_Locomotion_Walk
 ├── State: Running   → Sub-Graph / Linked Anim Graph → SM_Locomotion_Run
 ...
```

**这个结构在 UE 里做不出来。** 我查了引擎源码确认：

| 查证项 | 结论 | 出处 |
|---|---|---|
| 状态机图的右键菜单 | 只有 `Add State` / `Add State Alias` / `Add Conduit` / `Add Entry Point` / `Add Comment` | `AnimationStateMachineSchema.cpp:377-418` |
| 状态节点的类型枚举 | 只有 `Single animation` 和 `Blend graph` 两个值 | `AnimStateNode.h:16-20` |

也就是说：**状态机图里放不下另一个状态机**。这是引擎的硬限制，不是配置问题。

### 实际可行的方案

UE 里实现"模块化的移动动画"有两条路，我推荐第一条：

#### 方案 A · 一个状态机 + 四个"Blend graph"状态 ★ 推荐

```
AnimGraph
  └── [SM_Main]                         ← 唯一的顶层状态机
        ├── State: Grounded  (Blend graph) → 双击进去是一个完整的动画图
        ├── State: Sprinting (Blend graph) → 独立动画图
        ├── State: InAir     (Blend graph) → 独立动画图
        └── State: Crouching (Blend graph) → 独立动画图
```

**每个状态选 `Blend graph` 类型后，都会拥有一个独立的动画图**（可以放混合空间、
可以放骨骼控制节点）。这样就同时拿到了：

- **模块化**：四段逻辑分别存在四个图里，改跳跃不会碰到走路
- **真正的状态转移**：有转移规则、混合时间、`Always Reset on Entry` 开关
  —— 这些是"四状态机并列"方案给不了的

把状态类型改成 `Blend graph`：选中状态 → Details → `Animation State` → `State Type`。

#### 方案 B · Anim Layer Interface（Lyra 的做法）

定义 `UAnimLayerInterface` 接口资产，把四条移动逻辑做成 Layer 函数，
主 AnimGraph 通过 `Linked Anim Layer` 节点调用。

- 优点：真正的跨蓝图复用（别的角色直接接同一套 Layer），大团队分工友好
- 代价：多一层接口资产 + 接口图 + Layer 函数的编译期约束，
  调一个状态转移要跨三个资产找

**本阶段选 A。** 理由：方案 B 的收益在"多个角色共用一套动画"，
而我们只有玩家和敌人两个角色、两套完全不同的动作集。为 MVP 引入接口层
是典型的过度工程 —— 等真的要加第三个、第四个共用骨架的角色时再上 B 不迟。

> 📌 架构文档 8.1 节已按此更正。

---

## 2. 数据流总览

```
CharacterMovement                     GameplayTag（由 GA / GE 授予）
   Speed / MaxSpeed                      State.Attacking
   Velocity / IsFalling                  State.Dodging / State.Sprinting
   bIsCrouched                           State.Attack.Charging.LvN
        │                                        │
        └──────────────┬─────────────────────────┘
                       ↓
          URPG_AnimInstanceBase（每帧读一次）
                       ↓
        SpeedRatio / Direction / MovementState / bIsAttacking ...
                       ↓
        ABP_RPG_Base 的 AnimGraph
          SM_Main → 四条移动图 → [Slot 'DefaultSlot'] → Output Pose
```

**AnimGraph 里一行计算都不要写** —— 所有判断都在 C++ 里做完，蓝图只连线。

---

## 3. 步骤 1 · 创建动画蓝图

`Content/_My/Animation/` → 右键 → **Animation → Animation Blueprint**

| 字段 | 值 |
|---|---|
| Target Skeleton | `SK_Mannequin`（或你实际用的骨架） |
| Preview Mesh | 默认即可 |
| **Parent Class** | 搜索 **`RPG_AnimInstanceBase`** ★ 一定要改 |

命名 **`ABP_RPG_Base`**。

> ⚠️ Parent Class 默认是 `AnimInstance`。忘了改的话，你在 AnimGraph 里
> 找不到 `SpeedRatio`、`MovementState` 这些变量 —— 会以为是 C++ 没编译。

---

## 4. 步骤 2 · 建四个混合空间

`Content/_My/Animation/BlendSpaces/` → 右键 → **Animation → Blend Space**（或 Blend Space 1D）

| 资产名 | 维度 | 横轴 | 用途 |
|---|---|---|---|
| `BS_RPG_IdleWalk` | 1D | `SpeedRatio` 0~1 | 站定 → 走 |
| `BS_RPG_WalkSprint` | 1D | `SpeedRatio` 0~1 | 走 → 冲刺 |
| `BS_RPG_Crouch` | 1D | `SpeedRatio` 0~1 | 蹲站 → 蹲走 |
| `BS_RPG_Jump` | 1D | `VerticalVelocity` -600~600 | 上升 / 滞空 / 下落（可选） |

**为什么横轴用 `SpeedRatio` 而不是 `Speed`？**

因为 `MaxSpeed` 会变（走 300 / 冲刺 850 / 蹲 180）。用绝对速度做横轴的话，
"走路"只能占满 0~300 这一段，冲刺时速度 850 直接顶到轴外、被钳在最大值；
想正确显示就必须为每种姿态各做一个混合空间，`MaxSpeed` 一改还要重做一遍。

用 0~1 的比率则一个混合空间三种速度通用，调数值也不影响动画资产。

> 💡 采样点上放动画序列：`SpeedRatio = 0` 放站定、`0.5` 放走、`1.0` 放跑。
> 具体放几个采样点取决于你手上有哪些动画。

---

## 5. 步骤 3 · 搭 AnimGraph

打开 `ABP_RPG_Base` → **AnimGraph** 标签页。

### 5.1 建状态机

空白处右键 → **Add New State Machine**，重命名为 **`SM_Main`**，
把它的输出连到 `Output Pose`（中间还要过 Slot，见 5.4）。

### 5.2 建四个状态

双击 `SM_Main` 进入状态机图 → 右键 → **Add State**，建四个：

| 状态名 | State Type | 内容 |
|---|---|---|
| `Grounded` | **Blend graph** | 双击进去 → 放 `BS_RPG_IdleWalk` 混合空间 |
| `Sprinting` | **Blend graph** | → `BS_RPG_WalkSprint` |
| `InAir` | **Blend graph** | → `BS_RPG_Jump` 混合空间（或起跳 / 滞空 / 落地三个序列） |
| `Crouching` | **Blend graph** | → `BS_RPG_Crouch` |

> ⚠️ **`State Type` 一定要改成 `Blend graph`**。默认的 `Single animation`
> 只能挂一个动画序列，放不了混合空间。
> 改法：选中状态 → Details 面板 → `Animation State` 分组 → `State Type`。

每个状态图里：放一个 **Blend Space Player** 节点 → 在 Details 里选对应的
混合空间资产 → 把 `SpeedRatio`（或 `VerticalVelocity`）拖进去连到 `X` 引脚
→ 输出连到 `Output Animation Pose` 节点。

### 5.3 设置转移

在状态机图里，从一个状态**拖到**另一个状态即生成转移线。双击转移线配置：

**Grounded ↔ Sprinting**

| 方向 | Transition Rule | 混合时间 |
|---|---|---|
| Grounded → Sprinting | `MovementState == Sprinting` | 0.2 |
| Sprinting → Grounded | `MovementState == Grounded` | 0.2 |

**任意状态 → InAir**（从每个状态各拉一条）

| Transition Rule | 混合时间 |
|---|---|
| `MovementState == InAir` | 0.1 |

**InAir → Grounded / Crouching**

| 方向 | Transition Rule | 混合时间 |
|---|---|---|
| InAir → Crouching | `MovementState == Crouching` | 0.15 |
| InAir → Grounded | `MovementState == Grounded` | 0.15 |
| InAir → Sprinting | `MovementState == Sprinting` | 0.15 |

**Grounded ↔ Crouching**

| 方向 | Transition Rule |
|---|---|
| Grounded → Crouching | `MovementState == Crouching` |
| Crouching → Grounded | `MovementState == Grounded` |

**状态机 Entry Point**：右键 → `Add Entry Point` → 连到 `Grounded`。
（`Grounded` 也就是默认状态。）

> 💡 **`InAir` 状态要勾 `Always Reset on Entry`**（选中状态 → Details →
> `Always Reset on Entry` ✅）。这样每次起跳都会从第一帧开始播，
> 而不是接着上次落地时的姿势继续 —— 后者表现为"第二次跳起来动作是断的"。

> 💡 `MovementState` 是枚举，转移规则里用 `==` 比较即可，
> 不需要拆成 `bIsInAir && !bIsCrouching && ...` 那种容易写漏的布尔组合。

### 5.4 接蒙太奇插槽

回到 **AnimGraph**（不是状态机图），在 `SM_Main` 的输出后面串一个
**Slot 'DefaultSlot'** 节点，再连到 `Output Pose`：

```
[SM_Main] ──→ [Slot 'DefaultSlot'] ──→ [Output Pose]
```

Slot 节点的 Details → `Slot Name` 填 **`DefaultSlot`**。

> ⚠️ 这个名字**必须和蒙太奇里建的 Slot 名完全一致**（大小写敏感）。
> 不一致的表现是"攻击蒙太奇完全没反应"，而且不报任何错 ——
> 这是新手最常踩的坑，见 [`PHASE4_MONTAGE_SETUP.md`](./PHASE4_MONTAGE_SETUP.md)。

**Slot 之后还可以加**（可选，后续做）：
- `Layered Blend per Bone`：上半身覆盖，用于施法 / 喝药这类不打断腿的动作

---

## 6. 步骤 4 · 建子类动画蓝图

`ABP_RPG_Base` 是共享骨架，玩家和敌人各建一个子类（将来要放各自特有的动作）：

`Content/_My/Animation/` → 右键 → Animation Blueprint → Parent Class 选
**`ABP_RPG_Base`**（不是 C++ 类）

| 资产名 | 用途 |
|---|---|
| `ABP_RPG_Player` | 玩家 |
| `ABP_RPG_Enemy` | 敌人 |

现在可以先什么都不改 —— 建它们是为了以后加"玩家专属受击"之类不用动基类。

> 💡 如果暂时不想建子类，直接把 `ABP_RPG_Base` 挂到两个角色上也完全能用。

---

## 7. 步骤 5 · 挂到角色

### 7.1 `BP_RPG_Player`

选中 `Mesh` 组件 → `Animation` 分组：

| 字段 | 值 |
|---|---|
| **Anim Class** | `ABP_RPG_Player`（或 `ABP_RPG_Base`） |

### 7.2 `BP_RPG_Enemy`

同样配 `ABP_RPG_Enemy`。

> ⚠️ 阶段 1 的编辑器清单里写了"`Anim Class` 先留空"—— 现在填上。

---

## 8. 验收

PIE，逐项确认：

| 检查项 | 期望 |
|---|---|
| 站立不动 | 播站定动画（不是 T-Pose） |
| 按 W 移动 | 平滑过渡到走 / 跑，**脚步不打滑** |
| 按住 Shift 移动 | 切到冲刺动画 |
| 空格跳跃 | 起跳动画完整播放，落地有落地动作 |
| 按 C 蹲下 | 切到蹲伏动画 |
| 左键攻击 | **蒙太奇覆盖全身**，播完自动回到移动动画 |

### 用日志确认状态

C++ 侧提供了 `GetAnimationDebugString()`。在 AnimBP 的
**AnimGraph → 右键 → Add New Function 或直接建一个 Event Blueprint Update Animation**，
接一个 `Print String`，输入连 `GetAnimationDebugString`，就能看到实时状态。

典型输出：

```
移动[Sprinting] 速度 850/850 (1.00) 方向 0° 垂直 0 | 攻击 0 闪避 0 冲刺 1 蓄力 0(段 0) 无敌 0 死亡 0 | 模组 1 连段 0
移动[Grounded] 速度 0/300 (0.00) 方向 -90° 垂直 0 | 攻击 1 闪避 0 冲刺 0 蓄力 0(段 0) 无敌 0 死亡 0 | 模组 1 连段 2
```

> "方向 -90°"表示角色朝前但往左侧移动 —— 这正是方向混合空间要用的信息。

### 验证"标签驱动"确实生效

控制台输入 `showdebug abilitysystem`，切到 **Tags** 页：

- 按左键 → 出现 `State.Attacking`
- 按 Shift 移动 → 出现 `State.Sprinting`
- 按住右键 → 出现 `State.Attack.Charging` + `State.Attack.Charging.Lv1`（升段后变 `Lv2`）

**动画读的就是这些标签。** 看到标签但动画没变，说明是 AnimGraph 连线的问题，
不是 GAS 的问题 —— 这条排查路径能省很多时间。

---

## 9. 常见问题排查

| 症状 | 最可能的原因 |
|---|---|
| AnimGraph 里找不到 `SpeedRatio` / `MovementState` | AnimBP 的 Parent Class 没改成 `RPG_AnimInstanceBase` |
| 角色是 T-Pose，完全不动 | `Mesh` 组件没配 Skeletal Mesh，或 `Anim Class` 为空 |
| 走动时脚步明显打滑 | 混合空间的动画播放速率没和速度对齐：BlendSpacePlayer 要勾 `Scale by Speed`，或手动用 `SpeedRatio` 驱动播放速率 |
| 站定时腿还在动 | `IdleSpeedThreshold` 太小，角色减速到不了那个值 —— 调大到 15~25 |
| 方向混合空间左右反了 | 混合空间的角度轴配成了"逆时针为正"，本工程约定 **+90 = 右侧** |
| 跳跃动画每次都接上次的姿势 | `InAir` 状态没勾 `Always Reset on Entry` |
| 攻击蒙太奇没反应 | 蒙太奇的 Slot 名和 AnimGraph 里 Slot 节点的名字不一致 |
| 蹲下时动画是"蹲着慢跑" | 混合空间横轴用了 `Speed` 而不是 `SpeedRatio` |
| 冲刺起步瞬间播的是跑步动画 | 正常现象 —— 冲刺状态靠标签判定，标签在 GA 激活那一帧才挂上。嫌突兀就把 Sprinting 状态的混合时间调大 |
| **站着不动却原地踏步** | `MovementState` 判成了 `Sprinting`。它要求**标签和你真的在动两个条件**（`bIsSprinting && Speed > IdleSpeedThreshold`）—— 只挂标签不判速度的话，站着也会进跑步状态机，而那个混合空间在 `SpeedRatio = 0` 处播的是最慢的采样点（走路）|

---

## 10. 下一步

动画能跑起来之后，回到 [`ARCHITECTURE.md`](./ARCHITECTURE.md) 的阶段 5（敌人 AI），
或者先补做 [`PHASE4_MONTAGE_SETUP.md`](./PHASE4_MONTAGE_SETUP.md) 里的攻击蒙太奇
（**没有蒙太奇的话攻击动画永远播不出来**，Slot 是空的）。
