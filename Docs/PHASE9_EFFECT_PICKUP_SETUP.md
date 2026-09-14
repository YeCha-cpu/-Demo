# 阶段 9 · 效果触发器（治疗 / 增益 / 减益）配置指南

> 场景里可碰撞的道具与区域：**治疗药水、加攻卷轴、减防毒瓶、治疗泉、毒池、陷阱**。
>
> 代码已经写完并编译通过。这份文档讲**蓝图侧怎么配** ——
> 蒙太奇、GE、Static Mesh 这些是资产，必须手工创建。

---

## 0. 先搞清楚它是什么

新增了一个 C++ 类：**`ARPG_EffectVolume`**（`Source/RPG/World/`）。

它做且只做一件事：

```
玩家碰到它  ──▶  服务器上往这个玩家发一个 GameplayEvent  ──▶  GA 施加 GE
```

### ★ 为什么不直接施加 GE

因为它**必须**走这条链路（这是项目定的规矩）：

```
ASC 初始化 → 触发 GA → GE 上 Buff/标签 → GC 播放特效音效
```

如果触发器图省事直接 `ApplyGameplayEffectToTarget`，短期能跑，但会一次性丢掉：

| 丢掉的 | 具体表现 |
|---|---|
| **消耗** | 捡药水不花耐力、没冷却 —— 因为钱不在能力层收 |
| **动画** | 喝药没有一个抬手的动作（那属于 GA） |
| **打断与阻挡** | 死了、眩晕中照样能捡 —— `ActivationBlockedTags` 管不到 |
| **能力层标签** | `State.UsingItem` 之类挂不上，动画和 AI 查不到 |

**而且：新增一种效果不需要改 C++。** 做一个新 GE，在拾取物上指过去就行。

### 一个类能当什么用

| 玩法 | TriggerMode | TargetMode | TriggerEvent | 用完消失 |
|---|---|---|---|---|
| 治疗药水 | `OnEnter` | `TriggeringActor` | `Event.Item.Heal` | ✅ |
| 加攻卷轴 | `OnEnter` | `TriggeringActor` | `Event.Item.Buff` | ✅ |
| 减防毒瓶 | `OnEnter` | `TriggeringActor` | `Event.Item.Debuff` | ✅ |
| 治疗泉 | `WhileInside` | `AllOverlapping` | `Event.Item.Heal` | ❌ |
| 毒池 / 熔岩 | `WhileInside` | `AllOverlapping` | `Event.Item.Debuff` | ❌ |
| 一次性陷阱 | `OnEnter` | `AllOverlapping` | `Event.Item.Debuff` | ✅ |

---

## 1. 前置条件（**先做这一步，否则什么都捡不起来**）

触发器发出去的事件**必须有人接**。没人接的话 GAS **完全沉默** ——
不报错、不警告、日志一片空白，表现就是"走过去，东西还在，什么都没发生"。

所以角色身上必须先**授予**对应的能力：

| 能力 | 响应的事件 | 放在哪 |
|---|---|---|
| `GA_Heal` | `Event.Item.Heal` | 角色的 `Startup Abilities` |
| `GA_ApplyBuff` | `Event.Item.Buff` + `Event.Item.Debuff` | 角色的 `Startup Abilities` |

> ✅ 触发标签已经写在 C++ 构造函数里了，**不需要在蓝图里再配 Triggers**。

⚠️ **`GA_ApplyBuff` 只能授予一次。**
它同时响应增益和减益事件 —— 授予两份的话，一次事件会把两份都激活，**效果施加两次**。

要多个不同效果，靠"**不同 GE + 不同拾取物**"，不是"不同 GA 蓝图"。

> 这个类已经写好了"没人接就报警"的诊断：发送前会遍历目标的能力查一遍，
> 没有响应者就打一条 Warning 告诉你差了哪两件事。**看到那条 Warning 就照着做。**

---

## 2. 建一个效果触发器

1. Content Browser → `Content/_My/` 下右键
2. `Blueprint Class` → 展开 `All Classes` → 搜 **`EffectVolume`**
3. 选 **`ARPG_EffectVolume`** → 命名（例如 `BP_Pickup_HealPotion`）

### 组件结构（已自动建好）

```
ARPG_EffectVolume
├── TriggerSphere      ← 根组件，碰撞球。**在这里调半径**
└── Mesh               ← 外观。换个 Static Mesh / 材质就是一个新道具
```

> ⚠️ **根组件是碰撞球，不是模型。**
> 反过来（模型当根）的话，在编辑器里缩放模型会**同时缩放碰撞范围** ——
> 调外观顺手改了玩法数值，而且没有任何提示。
> 现在是"调碰撞球大小"和"调模型大小"两件独立的事。

---

## 3. 属性配置表

选中 BP → `Details` 面板 → `RPG|Effect` 分类。

| 属性 | 类型 | 说明 |
|---|---|---|
| `TriggerEvent` | GameplayTag | ★ **发哪个事件**。决定由哪个 GA 响应 |
| `EffectClass` | `UGameplayEffect` 子类 | ★ **要施加的 GE**。留空 = 用 GA 自己配的默认 GE |
| `Magnitude` | float | 数值（治疗量 / 强度）。**0 = 用 GA 配的默认值** |
| `TriggerMode` | 枚举 | `OnEnter`（拾取物）/ `WhileInside`（治疗泉、毒池） |
| `TargetMode` | 枚举 | `TriggeringActor`（谁捡谁受益）/ `AllOverlapping`（范围内所有人） |
| `RepeatInterval` | float | 仅 `WhileInside`：多久触发一次（默认 1 秒） |
| `bConsumeOnTrigger` | bool | 仅 `OnEnter`：用了之后是否消失 |
| `RespawnDelay` | float | 用掉之后多久重生。**<= 0 = 不重生** |
| `bAffectDead` | bool | 死了的角色还吃不吃。毒池一般勾上，药水一般不勾 |

### 三个可选的事件标签

| 标签 | 用在哪 |
|---|---|
| `Event.Item.Heal` | 治疗 |
| `Event.Item.Buff` | 增益 |
| `Event.Item.Debuff` | 减益 |

---

## 4. 三个完整配方

### 配方 A：治疗药水（一次性）

**GE 侧**：用现成的 `GE_Heal`（Instant + `SetByCaller(Data.Heal.Amount)`）。

**BP 侧**：

| 属性 | 值 |
|---|---|
| `TriggerEvent` | `Event.Item.Heal` |
| `EffectClass` | `GE_Heal`（留空也行，会用 `GA_Heal` 的默认值） |
| `Magnitude` | `50` |
| `TriggerMode` | `OnEnter` |
| `TargetMode` | `TriggeringActor` |
| `bConsumeOnTrigger` | ✅ |
| `RespawnDelay` | `0`（不重生）|
| `bAffectDead` | ❌ |

`TriggerSphere` 的 `Sphere Radius` 调到 `120` 左右。

### 配方 B：加攻卷轴（一次性增益）

**GE 侧**：新建 `GE_Buff_AttackUp`

| 设置 | 值 |
|---|---|
| Duration Policy | `Has Duration` |
| Duration Magnitude | `15.0` |
| Modifiers[0] | `Attack` → `Add`（或 `Multiply`）→ 幅度 `20` |
| Granted Tags | `State.Buff.AttackUp` |

> ★ **`Granted Tags` 是给动画 / UI / 驱散逻辑查的**，别省。
> "他是不是被加攻了"这个问题靠标签回答，不靠遍历 GE。
>
> 顺带一提：`Ability.Buff.AttackUp`（能力标签）和 `State.Buff.AttackUp`
> （状态标签）是**两个东西** —— 前者是能力的身份，后者是角色的状态。
> 项目里这两个命名空间是刻意分开的，详见 `ARCHITECTURE.md` §6。

**BP 侧**：

| 属性 | 值 |
|---|---|
| `TriggerEvent` | `Event.Item.Buff` |
| `EffectClass` | `GE_Buff_AttackUp` |
| `Magnitude` | `0`（这个 GE 不需要 SetByCaller）|
| `TriggerMode` / `TargetMode` | `OnEnter` / `TriggeringActor` |
| `bConsumeOnTrigger` | ✅ |

### 配方 C：减防毒池（持续区域，**减益**）

**GE 侧**：新建 `GE_Debuff_DefenseDown`

| 设置 | 值 |
|---|---|
| Duration Policy | `Has Duration` |
| Duration Magnitude | `5.0` |
| Modifiers[0] | `Defense` → `Multiply` → 幅度 `0.7` |
| Granted Tags | `State.Debuff.DefenseDown` |

**BP 侧**：

| 属性 | 值 |
|---|---|
| `TriggerEvent` | `Event.Item.Debuff` |
| `EffectClass` | `GE_Debuff_DefenseDown` |
| `Magnitude` | `0` |
| `TriggerMode` | `WhileInside` |
| `TargetMode` | `AllOverlapping` |
| `RepeatInterval` | `1.0` |
| `bAffectDead` | ✅（尸体也泡在里面）|

`TriggerSphere` 的 `Sphere Radius` 调到 `400`。

> ⚠️ **持续区域的 GE 必须是 `Has Duration` 或 `Infinite`。**
> 用 `Instant` 的话，每秒触发一次就是每秒扣一次 —— 对减益来说不是你要的。
> 而且要注意 GE 的叠加策略：`Duration` 的 GE 每秒重挂一次会不断刷新持续时间，
> 这是**通常想要的行为**（离开毒池 5 秒后才失效）。

---

## 5. 验收清单

> 每一项都要在 **2 个 PIE 窗口**里各做一次（联机部分）。
> 单机测试用 `Play Standalone` 即可。

### 5.1 基础（单机）

| # | 操作 | 预期 |
|---|---|---|
| 1 | 走进治疗药水 | 血量立刻上升，药水**消失** |
| 2 | 再走回原处 | 什么都没有（`RespawnDelay = 0`） |
| 3 | 走进加攻卷轴 | `Attack` 上升，15 秒后回落 |
| 4 | 走进减防毒池 | `Defense` 下降，每秒刷新一次持续时间 |
| 5 | 走出毒池 5 秒后 | `Defense` 恢复 |
| 6 | 满血时捡治疗药水 | 血量**不变**（不会超血 —— 属性集钳制住了），药水照样消失 |

> 第 6 项验证的是属性集的钳制：`Health` 增加时会被 `PreAttributeChange` 夹到
> `MaxHealth` 以内，不会出现"治疗溢出成超血"。

### 5.2 联机（Listen Server + 2 玩家）

| # | 操作 | 预期 |
|---|---|---|
| 7 | 窗口 2（客户端）走进药水 | **两个窗口里**药水都消失 |
| 8 | 窗口 2 捡完之后，窗口 1 走过去 | 什么都没有（已经被用掉了） |
| 9 | 两个人同时在毒池里 | **两个人都掉防御**（`AllOverlapping`） |
| 10 | 窗口 2 走进药水 | 只有**窗口 2 的**血量上升，窗口 1 不变 |
| 11 | 客户端捡药水，同时看窗口 1 | 窗口 1 屏幕上药水**同步消失**（`bConsumed` 复制） |

### 5.3 诊断（故意配错，确认能报出来）

| # | 操作 | 预期 |
|---|---|---|
| 12 | 把拾取物的 `TriggerEvent` 清空 | 生成时打 Warning：`效果触发器没有配置 TriggerEvent` |
| 13 | 把 `GA_Heal` 从 `Startup Abilities` 里移掉 | 走过去时打 Warning：`没有任何能力会响应它` |
| 14 | 把一张蒙太奇配进 `EffectClass` | Warning：`不是 UGameplayEffect 的子类` |

> 第 12-14 项是**故意破坏性测试**。这个项目里最难查的 bug 一直是
> "不报错但什么都不发生"，所以这三条诊断值得亲手验一遍 ——
> 确认它们真的会响，而不是你以为它会响。

---

## 6. 排查表

| 症状 | 原因 | 修法 |
|---|---|---|
| **走过去什么都没发生，日志空白** | 事件发出去没人接 | 看有没有 `没有任何能力会响应它` 的 Warning。有的话查两件事：① 角色的 `Startup Abilities` 里有没有对应的 GA；② 那个 GA 的触发标签对不对（本项目的标签写在 C++ 构造函数里，正常不该错） |
| **生成时就报 `没有配置 TriggerEvent`** | BP 里没选标签 | Class Defaults → `RPG|Effect` → `TriggerEvent` 选一个 `Event.Item.*` |
| **单据掉一次血/回一次血，但东西没消失** | `bConsumeOnTrigger` 没勾 | 勾上。或者你用的是 `WhileInside` 模式 —— 那个模式本来就不消耗 |
| **站进毒池只触发一次** | `TriggerMode` 是 `OnEnter` | 改成 `WhileInside` 并设 `RepeatInterval` |
| **毒池里只有一个人掉血** | `TargetMode` 是 `TriggeringActor` | 改成 `AllOverlapping` |
| **联机时治疗量翻倍 / Buff 叠两层** | 检查两处：① 触发器是不是在客户端也施加了（C++ 有 `HasAuthority()` 守卫，正常不该）；② `GA_ApplyBuff` 是不是被授予了两次 | ② 更常见 —— 它同时响应增益和减益事件，**只能授予一次** |
| **客户端捡了东西，但别人屏幕上它还在** | `bConsumed` 没复制 | 确认 BP 的 `Replicates` 是勾上的（C++ 构造函数里设了 `bReplicates = true`，别在 BP 里关掉）|
| **捡了之后碰撞还在，人走不过去** | 碰撞球在 `bConsumed` 时应该切成 `NoCollision` | C++ 的 `RefreshVisualState()` 统一管这件事。如果还发生，检查是不是在 BP 里重写了碰撞设置 |
| **满血捡药水不消失** | 理论上应该消失 | `CanAffect()` 只看"活着"和"没用过"，不看血量。如果真不消失，看 `bAffectDead` 是不是被误改了 |

---

## 7. 这一版没做的

| 项 | 说明 |
|---|---|
| **拾取动画 / 音效** | 现在触发器只发事件。要做"喝药抬手"就在 `GA_Heal` 里加蒙太奇 —— 那正是走 GA 而不是直接施加 GE 的意义 |
| **拾取提示 UI**（"按 F 拾取"） | 现在走过去就自动吃。要做交互式拾取，需要 `TargetMode` 之外再加一个"交互键"分支，并接 UI |
| **物品栏 / 携带** | 这是"捡起来存着以后用"，和本节的"碰到就生效"是两套东西。要做的话物品栏本身是另一个系统 |
| **效果图标** | 增益 / 减益的 HUD 图标需要读 GE 的 `GrantedTags`（`State.Buff.*` / `State.Debuff.*`）—— 数据已经在了，UI 侧没做 |
| **敌对判定** | 现在**谁碰谁吃**，包括敌人。要做"只对玩家生效"或"只对敌人生效"的角色过滤，加一个阵营判断即可 |

---

*对应提交：阶段 9-① · 代码已编译通过（`Result: Succeeded`），蓝图资产待配置*
