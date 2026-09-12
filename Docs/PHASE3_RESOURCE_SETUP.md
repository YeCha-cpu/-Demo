# 阶段 3 · 资源系统配置指南

> 耐力恢复、治疗、增益。阶段 2 做完后耐力**只减不增**，这一阶段补上产出端。

---

## 数据流总览

```
消耗侧（阶段 2 已完成）              产出侧（本阶段）
─────────────────────              ─────────────────────
攻击/闪避/跳跃/奔跑                   GE_StaminaRegen
      ↓                              （Infinite，每 0.25 秒 +3.75）
ConsumeStamina()
      ↓                                    ↑
扣耐力 + 挂 GE_StaminaRegenDelay     被 OngoingTagRequirements 控制
      ↓                                    ↑
State.Stamina.Blocked（3 秒）═══ 抑制 ═══╝
```

**核心机制**：整个"停手 3 秒后缓慢恢复"的规则，**没有一行计时代码**。
时间完全由 GE 的 Duration 表达，开关完全由标签表达。

---

## 步骤 1 · `GE_StaminaRegenDelay`（恢复阻断）★ 关键

`Content/_My/GAS/GE/` → 右键 → Gameplay Effect，命名 `GE_StaminaRegenDelay`

| 字段 | 值 |
|---|---|
| Duration Policy | **Has Duration** |
| Duration Magnitude | **`3.0`**（秒）—— 这就是"停手多久才开始恢复" |

**加组件**：Details → **`Components`** 数组 → `+` → 选 **`Target Tags`**

| 组件内字段 | 值 |
|---|---|
| Add Tags | **`State.Stamina.Blocked`** |

**关键：配置叠加以便刷新持续时间**

| 字段 | 值 |
|---|---|
| Stacking Type | Aggregate by Target |
| Stack Limit Count | `1` |
| **Stack Duration Refresh Policy** | **Refresh on Successful Application** |
| **Stack Period Reset Policy** | **Reset on Successful Application** |

> ⚠️ 那两个 Refresh/Reset 策略必须打开。否则连续消耗耐力时，
> 阻断时间**不会往后推**——玩家在 3 秒内又攻击一次，恢复依然会按时开始，
> 表现为"边打边回耐力"。

---

## 步骤 2 · `GE_StaminaRegen`（周期恢复）★ 关键

命名 `GE_StaminaRegen`

| 字段 | 值 |
|---|---|
| Duration Policy | **Infinite** |
| Period | **`0.25`** |
| bExecutePeriodicEffectOnApplication | ✅ 勾上 |

**Modifier 加一条：**

| 字段 | 值 |
|---|---|
| Attribute | `Stamina` |
| Modifier Op | **Additive** |
| Magnitude | **`3.75`** |

> 3.75 = 15/秒 × 0.25 秒。想改成"每秒恢复 20"，就把这里改成 5.0。

**加组件**：`Components` → `+` → 选 **`Target Tag Requirements`**

| 组件内字段 | 值 |
|---|---|
| **Ongoing Tag Requirements** → Ignore Tags | **`State.Stamina.Blocked`** |

> ⚠️ **注意区分两个组件**：
> - **`Target Tags`** = **授予**标签给目标（步骤 1 用的）
> - **`Target Tag Requirements`** = **检查**目标有没有某些标签（这里用的）
>
> 名字很像，但一个是"给"，一个是"看"。配错了表现为"耐力压根不恢复"。

> ⚠️ 老教程里的 `OngoingTagRequirements` 是 GE 上的**内联字段**，
> UE 5.3 起已废弃（附录 B 有完整清单）。5.8 里必须通过组件配置。

---

## 步骤 3 · `GA_StaminaRegen` 蓝图（被动能力）

`Content/_My/GAS/GA/` → 蓝图，父类选 **`RPG_GA_StaminaRegen`**，命名 `GA_StaminaRegen`

| 分类 | 字段 | 值 |
|---|---|---|
| `RPG\|Regen` | Regen Effect Class | **`GE_StaminaRegen`** |

> 不需要配 `Stamina Cost Effect Class` 之类 —— 它不是通过输入触发的。

---

## 步骤 4 · `GA_Sprint` / `GA_Jump` 蓝图

各建一个蓝图，父类分别选 `RPG_GA_Sprint` 和 `RPG_GA_Jump`。

**两个都要配：**

| 分类 | 字段 | 值 |
|---|---|---|
| `RPG\|Effects` | Stamina Cost Effect Class | `GE_StaminaCost` |

`GA_Sprint` 额外：

| 字段 | 值 |
|---|---|
| `RPG\|Sprint` | Stamina Drain Per Second | `5` |
| `RPG\|Sprint` | Drain Tick Interval | `0.1` |

`GA_Jump` 额外：

| 字段 | 值 |
|---|---|
| `RPG\|Jump` | Jump Stamina Cost | `10` |
| `RPG\|Jump` | Max Jump Hold Time | `1.5` |

> 💡 `Max Jump Hold Time` 是可变高度跳跃的兜底：松手越早跳得越低，
> 但最多按住 1.5 秒后强制结束能力（防止玩家一直不松手导致能力永久激活）。

---

## 步骤 5 · 各能力的 `Stamina Regen Delay Effect Class` ★ 别忘了

**所有会消耗耐力的 GA 蓝图**都要配这一项：

| 蓝图 | 字段 | 值 |
|---|---|---|
| `GA_LightAttack` | `RPG\|Effects` → Stamina Regen Delay Effect Class | **`GE_StaminaRegenDelay`** |
| `GA_HeavyAttack` | 同上 | **`GE_StaminaRegenDelay`** |
| `GA_Dodge` | 同上 | **`GE_StaminaRegenDelay`** |
| `GA_Sprint` | 同上 | **`GE_StaminaRegenDelay`** |
| `GA_Jump` | 同上 | **`GE_StaminaRegenDelay`** |

> ⚠️ 漏配的后果：那个能力的消耗**不会触发恢复延迟**。
> 如果你只给轻击配了，那么闪避之后耐力会立刻开始恢复 —— 很违和，但很难查。

---

## 步骤 6 · 治疗与增益

### 6.1 `GE_Heal`

| 字段 | 值 |
|---|---|
| Duration Policy | **Instant** |
| Modifiers | `Health` / **Additive** / **SetByCaller** → `Data.Heal.Amount` |

然后建 `GA_Heal` 蓝图（父类 `RPG_GA_Heal`），配 `Heal Effect Class = GE_Heal`。

### 6.2 `GE_Buff_AttackUp`

| 字段 | 值 |
|---|---|
| Duration Policy | **Has Duration** |
| Duration | `15` |
| Modifiers | `Attack` / **Multiplicative** / `0.2` |

> `Multiplicative` + `0.2` = 攻击力 × 1.2（即 +20%）。
> 用乘法而不是加法，是为了让 Buff 的收益随装备成长而缩放，
> 而不是后期变得无关痛痒。

### 6.3 `GE_Buff_DefenseUp`

同上，`Defense` / Multiplicative / `0.3`，Duration 15 秒。

### 6.4 `GE_Debuff_DefenseDown`

`Defense` / Multiplicative / **`-0.3`**（减防 30%），Duration 10 秒。

然后建 `GA_ApplyBuff` 蓝图，各自配 `Buff Effect Class`。
一个 GA 蓝图对应一种 Buff —— 想加新 Buff 就新建一个蓝图，C++ 不用动。

---

## 步骤 7 · 挂到角色

### 7.1 `BP_RPG_Player` → `Startup Abilities`（输入触发的）

现在是 **5 条**：

| Key | Value |
|---|---|
| `Input.Attack.Light` | `GA_LightAttack` |
| `Input.Attack.Heavy` | `GA_HeavyAttack` |
| `Input.Dodge` | `GA_Dodge` |
| `Input.Jump` | `GA_Jump` ← **新增** |
| `Input.Sprint` | `GA_Sprint` ← **新增** |

### 7.2 `BP_RPG_Player` → `Startup Passive Abilities`（被动）★ 新增字段

数组加一条：**`GA_StaminaRegen`**

> ⚠️ 被动能力**不要**配到 `Startup Abilities` 里 —— 那张表是"输入标签 → 能力"的映射，
> 而被动能力没有任何输入可以映射。它有专门的 `Startup Passive Abilities`。

### 7.3 `BP_RPG_Enemy` 同理

敌人也需要耐力恢复，所以 `Startup Passive Abilities` 同样加上 `GA_StaminaRegen`。

### 7.4 `DA_RPG_InputConfig` 确认 Jump / Sprint 的映射

阶段 3 起 Jump 和 Sprint 改走 GAS 了（不再是过渡期的原生绑定），
所以它们必须出现在 `Ability Input Mappings` 里：

| Input Action | Input Tag |
|---|---|
| `IA_RPG_Jump` | `Input.Jump` |
| `IA_RPG_Sprint` | `Input.Sprint` |

配好后启动日志里的 `输入绑定完成：N 个能力输入` 应该是 **5**。

---

## 步骤 8 · 测试

### 耐力恢复（核心）

1. PIE，按攻击键打两下 → 观察耐力下降
2. **停手等待** → 3 秒内耐力不动，3 秒后开始缓慢回升

日志验证（`Log LogRPG_Combat Verbose`）：

```
消耗耐力 8.0
消耗耐力 8.0
（3 秒静默）
消耗耐力 0.375      ← 恢复开始（每 0.25 秒 3.75）
消耗耐力 0.375
```

### 用 GameplayDebugger 看恢复为什么没生效

控制台执行 `showdebug abilitysystem`，切到 **Effects** 页，能看到
`GE_StaminaRegen` 处于 **Inhibited**（被抑制）状态，抑制它的标签是
`State.Stamina.Blocked`。

这就是标签方案的好处 —— **一眼能看到"为什么现在不恢复"**。

### 奔跑与跳跃

```
开始奔跑（每秒耗耐力 5.0）
跳跃（耐力 10.0，最长按住 1.5 秒）
耐力耗尽，停止奔跑      ← 跑久了会看到这条
```

### 治疗与 Buff

`GA_Heal` / `GA_ApplyBuff` 没有输入绑定，测试方式：

用控制台命令或临时加个按键触发它们；或者直接在 GameplayDebugger 里
观察属性变化（加攻 Buff 生效时 Attack 会从 10 变成 12）。

---

## 常见问题排查

| 症状 | 最可能的原因 |
|---|---|
| 耐力完全不恢复 | `GE_StaminaRegen` 的组件配成了 `Target Tags` 而不是 `Target Tag Requirements`（一个"给"一个"看"） |
| 耐力一直不恢复 | `GA_StaminaRegen` 没加到 `Startup Passive Abilities`，或没配 `Regen Effect Class` |
| 边打边回耐力 | `GE_StaminaRegenDelay` 的 Stacking 那两个 Refresh/Reset 策略没开 |
| 恢复立刻开始（无延迟） | 消耗耐力的 GA 没配 `Stamina Regen Delay Effect Class` |
| 恢复速度不对 | `GE_StaminaRegen` 的 Magnitude 是"每 Period 恢复多少"，不是"每秒" |
| 跳跃/奔跑没反应 | `DA_RPG_InputConfig` 里没配 `Input.Jump` / `Input.Sprint` 的映射 |
| 奔跑后角色一直保持高速 | 检查 `GA_Sprint` 是否被异常打断 —— `EndAbility` 里有兜底恢复速度 |
| 启动日志显示"该能力标记为授予即激活"但没别的输出 | 被动能力激活了但没配 `Regen Effect Class`，会有一条 Warning |
