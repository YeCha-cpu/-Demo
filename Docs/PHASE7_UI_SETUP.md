# 阶段 7 · 战斗 HUD 视觉规范与配置指南

> 左下的属性区、右下的招式区、头顶血条、伤害飘字、死亡面板。
> **C++ 已经把数据和控制逻辑写好了**，编辑器里要做的是搭 WBP、按命名表填控件、连三个配置。

---

## 0. C++ 侧已经就绪的东西

| 类 | 编辑器里显示为 | 作用 |
|---|---|---|
| `ARPG_HUD` | — | 本地玩家的 HUD 宿主，创建主 HUD + 生成飘字 |
| `URPG_HUDWidget` | — | 主 HUD：属性区 / 招式区 / 死亡面板 |
| `URPG_AttributeBarWidget` | — | 一条属性条（三条共用同一个基类） |
| `URPG_OverheadHealthBarWidget` | — | 头顶血条 |
| `URPG_OverheadHealthBarComponent` | — | 头顶血条的组件（**必须用这个，不要换成普通 WidgetComponent**） |
| `URPG_DamageNumberWidget` | — | 伤害飘字 |

编辑器里要做的：**3 个 WBP + 1 个 BP_HUD + 3 处配置**。

### 0.1 这一阶段同时改了 6 个不属于 `UI/` 的文件

HUD 本身只读不写玩法状态，但**要读的那些状态当时并不存在**，
所以先在别的层把数据通道补上了。全部就下面这些，没有更多：

| 文件 | 改了什么 | 为什么非改不可 |
|---|---|---|
| `Core/RPG_GameplayTags` | 新增 `State.Attack.Transition` | HUD 要显示"切手技"，但它当时只是 `GA_HeavyAttack` 的私有成员 `bTransitionBranch`，UI 读不到；GA 实例还会随能力结束被回收 |
| `AbilitySystem/Abilities/RPG_GA_HeavyAttack` | 切手技分支挂/摘该标签 | 标签的授予方 |
| `AbilitySystem/RPG_AttributeSet` | 伤害落地后调 `Multicast_ShowDamageNumber` | 飘字的数据源（伤害值 + 命中点） |
| `Character/RPG_BaseCharacter` | `Multicast_ShowDamageNumber()` | 飘字要**每端各画一份**，而现成的 `Event.Combat.Hit` 只在服务器广播 |
| `Character/RPG_BaseCharacter` | `OverheadHealthBar` 组件 + `IsLocallyControlledPlayer()` + `GetRespawnDelay()` | 头顶血条的载体、显示判据、重生倒计时数据源 |
| `Core/RPG_GameModeBase` | `HUDClass` | HUD 的挂载点 |

> 📌 这么列出来是为了让"这一阶段到底动了什么"是可审计的 ——
> **改到别的层不是问题，说不清改了哪些才是问题**。

---

## 1. ★ 视觉规范

> 这是我给的一份**可直接落地的起点**，不是唯一答案 —— 美术风格是你的创作部分，
> 想改配色/字号/布局随时改，C++ 那边一行都不用动。
> 规范里的所有尺寸都按 **1920×1080** 基准，锚点用**比例**而不是像素，
> 这样 16:10 / 21:9 下布局不会跑偏。

### 1.1 布局总览

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                                                                              │
│                                                                              │
│                                                                              │
│                                                                              │
│                                                                              │
│                                                                              │
│                                                                              │
│   ◉  ╔══════════════════════════╗                    ┌──────────────────┐    │
│   ▨  ║████████████████████ 72/100║   ← 血              │      重击 · 蓄力中 │    │
│   ◉  ╚══════════════════════════╝                    │ ══════════════▌  │    │
│      ╔══════════════════════════╗                    └──────────────────┘    │
│   ◉  ║███████████▁▁▁▁▁▁▁▁ 40/100║   ← 蓝                                  │
│      ╚══════════════════════════╝                                            │
│      ╔══════════════════════════╗                                            │
│   ◉  ║███████████████████▁▁ 88/100║  ← 耐力                                │
│      ╚══════════════════════════╝                                            │
│                                                                              │
│      攻 25        防 18                                                      │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
   ↑ 距左 48px                                            距右 48px ↑
   ↑ 距底 40px                                            距底 40px ↑
```

### 1.2 左下 · 属性区

**锚点**：`(0, 1)` 左下角。距左 `48px`，距底 `40px`。

| 元素 | 尺寸 | 间距 | 说明 |
|---|---|---|---|
| 图标列 | `24 × 24` | 距条 `8px` | 血/蓝/耐力各一个 |
| 属性条 | `320 × 18` | 条间 `6px` | 圆角 `3px` |
| 条内数值 | 右对齐，内边距 `8px` | — | 字号 **12** |
| 攻防数值行 | 自动 | 距最后一条 `10px` | 字号 **14**，两项间距 `24px` |

**配色**

| 元素 | 色值 | 备注 |
|---|---|---|
| 血条填充 | `#C0392B` | 低于 30% 时切 `#E74C3C` 并做 0.6 秒周期的透明度脉动 |
| 蓝条填充 | `#2E86DE` | |
| 耐力条填充 | `#27AE60` | |
| 条背景 | `#1A1A1A` @ `0.75` | |
| 条描边 | `#000000` @ `0.6`，`1px` | 没有描边的话在亮背景上会糊 |
| 数值文本 | `#FFFFFF` @ `0.9` | 带 1px 黑色描边（`#000000` @ `0.8`），保证任何背景下可读 |
| 攻防文本 | `#D5D8DC` | |
| **伤害残影条** | `#FFFFFF` @ `0.45` | 见下方动效说明 |

**★ 掉血残影（这一条最影响"手感"）**

血条不要瞬间跳到新值，用两层：

```
        当前值          残影值
掉血瞬间  │←──── 立即 ────→│        ← 白条停在旧值
0.15 秒后 │←─ 缓动跟上 ─→│          ← 红条滑到新值，白条不动
0.35 秒后 │←──── 缓慢 ────→│        ← 白条也滑到新值
```

实现方式：`URPG_AttributeBarWidget` 提供了一个蓝图事件
**`On Attribute Values Changed(Current, Max, bIncreased)`**，在 WBP 里：

1. 放第二个 `UProgressBar` 命名 `LagBar`，叠在 `Bar` 下面（ZOrder 更低）
2. 在 `On Attribute Values Changed` 里：
   - 如果 `bIncreased == false`（掉血）：把 `LagBar` 的百分比**先设成掉血前的值**
     （用 `GetPercent` 拿不到旧值，所以在蓝图里自己存一个变量 `PreviousPercent`），
     然后 Play 一个 `LagCatchUp` 动画，0.35 秒里把 `LagBar` 补到 `Bar` 的当前值
   - 如果 `bIncreased == true`：`LagBar` 直接跟上（回血不需要残影）

> 💡 累计伤害也能这么做：连挨三刀时，只让 `LagBar` 在**最后一次**伤害后 0.35 秒才追上，
> 玩家就能一眼看出"这一套连招一共削了多少血"。这是动作游戏里很值钱的反馈。

### 1.3 左下 · 闪避图标

放在属性区**右侧**，距属性条 `24px`，尺寸 `48 × 48`。

| 状态 | 表现 |
|---|---|
| 常态 | **隐藏**（`Hidden`，不占布局 —— C++ 用的是 `ESlateVisibility::Hidden`） |
| `State.Dodging` 存在 | 显示，进入时播一个 0.12 秒的缩放回弹（0.6 → 1.15 → 1.0） |

> 这里刻意做成"只有闪避时出现"，而不是"常态显示一个暗的图标"。
> 理由是它本质是**状态提示**不是**技能冷却**。将来要做冷却显示，
> 建议在常态也显示图标但压暗到 `#FFFFFF` @ `0.25`，并在上面叠一个扇形遮罩 ——
> 那时候改 C++ 里的 `UpdateCombatState()` 一行即可（把 `Hidden` 改成 `HitTestInvisible`）。

### 1.4 右下 · 招式区

**锚点**：`(1, 1)` 右下角。距右 `48px`，距底 `40px`。整块宽 `220px`，右对齐。

| 元素 | 尺寸 | 间距 | 说明 |
|---|---|---|---|
| 招式名 | 自动 | 距条 `6px` | 字号 **15**，右对齐 |
| 蓄力条 | `220 × 10` | — | 圆角 `5px`（做成胶囊形） |

**招式名文案**（在 WBP 的 Class Defaults 里可改，C++ 已给出默认值）：

| 状态 | 文案 | 配色 |
|---|---|---|
| 轻击 | `轻击 · 第 N 段` | `#ECF0F1` |
| 重击蓄力 | `重击 · 蓄力中` | `#F39C12` |
| 切手技 | `切手技` | `#9B59B6` |

**蓄力条配色**

| 区段 | 色值 |
|---|---|
| 填充 | `#F39C12` |
| 背景 | `#1A1A1A` @ `0.75` |
| 满格瞬间 | 填充色切 `#E74C3C`，条整体做一次 0.25 秒的放大脉动 |

> 💡 **三段刻度的做法**：C++ 只给一个 0~1 的百分比，不分段。
> 想做三段刻度，在 WBP 里用一张 `220×10` 的贴图当填充笔刷，
> 上面画两条竖线 —— 分母是"第三段的门槛时间"（C++ 从 `DA_AttackModule` 读的），
> 所以三段在条上的位置天然均匀。
>
> ⚠️ 不要用 `MaxChargeTime`（GA 上那个"强制释放"上限）来算刻度：
> 它比第三段门槛大，用它会变成"蓄满三段时条才走到一半"，玩家会以为还能继续蓄。

### 1.5 头顶血条

| 项 | 值 |
|---|---|
| 屏幕尺寸 | `140 × 20`（**恒定**，不随距离变 —— 用的是 Screen 空间的 WidgetComponent） |
| 相对位置 | 胶囊体中心上方 `110px` |
| 名字 | 字号 **11**，居中，`#FFFFFF` @ `0.85` |
| 血条 | 高 `6px`，宽 `80px`，居中；填充 `#C0392B`，背景 `#1A1A1A` @ `0.7` |
| 残血 | 低于 30% 时填充切 `#E74C3C` 并脉动（C++ 的 `On Low Health Changed` 事件驱动） |

**有没有血条（C++ 已实现，配置对了就自动生效）**

> ⚠️ 这张表说的是"**配不配**有血条"，不是"此刻亮不亮"。
> 常态下一律是收起的 —— 挨打才亮，见下面的 §1.5.1。

| 视角 | 自己的角色 | 其他玩家 | 敌人 |
|---|---|---|---|
| 本地玩家 | ❌ **没有** | ✅ 有 | ✅ 有 |
| 服务器（主机） | ❌ 没有（自己那份） | ✅ 有 | ✅ 有 |

> ⚠️ 判据是 `IsLocallyControlledPlayer()`（**不是** `IsLocallyControlled()`）
> 而不是"是玩家还是敌人"—— 因为玩家的血条在**别人的屏幕上**是要显示的。
> 这也是为什么这个组件放在 `ARPG_BaseCharacter`（敌我共用）而不是只放敌人身上。
>
> 🕳️ **这两个函数的区别踩过一次坑**：`APawn::IsLocallyControlled()` 内部就是
> `Controller->IsLocalController()`，而 `AController::IsLocalController()`
> 对**服务器上的 AIController 也返回 true**（`Controller.cpp:94-110`：
> Standalone 无脑 true；权威且非 AutonomousProxy 也 true）。
> 所以拿它当判据，**单机下所有敌人都被当成"本地玩家"，血条全被藏起来**。
> 而客户端上复制来的 AIController 不是权威、判定反而是对的 ——
> 这个 bug 只在单机/主机上出现。

#### 1.5.1 挨打才亮（常规隐藏）★

**头顶血条平时是收起来的，只有掉血那一刻亮出来，5 秒后自己收回去。**

> ★ **"挨打才亮"对 AI 和"其他玩家"一视同仁** —— 需求里"除本地玩家外的其他玩家、AI
> 要有头顶血条"这条规则，对两层都成立：
>   · **有没有**血条 → §1.5 的表（本地玩家没有，其他玩家和 AI 都有）
>   · **此刻亮不亮** → 本节（谁挨打谁亮，不分敌我）
>
> 换句话说：**你在别人的屏幕上，挨打时头顶也会亮血条**，和你看到敌人挨打亮血条是同一套逻辑。

| 项 | 值 |
|---|---|
| 常驻状态 | `Collapsed`（完全收起，不参与布局） |
| 亮起条件 | **血量下降** |
| 亮起时长 | `5.0` 秒（WBP Class Defaults 里的 `Reveal Duration` 可改） |
| 连续挨打 | **刷新计时**，不是叠加（所以不会因为连击亮 20 秒） |
| 亮起时的可见性 | `Hit Test Invisible` —— 纯展示，不吃鼠标事件 |

> ⚠️ 这跟"本地玩家不显示"是**两层不同的东西**，别混为一谈：
>
> | | 判据 | 谁在管 | 管什么 |
> |---|---|---|---|
> | 角色层 | `IsLocallyControlledPlayer()` | `ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()` | 这个角色**配不配**有头顶血条（本地玩家永远没有） |
> | 控件层 | 血量有没有掉 | `URPG_OverheadHealthBarWidget` | 配了血条的角色，**此刻亮不亮** |
>
> 本地玩家的血条组件整个是关的，所以第二层在他身上根本不会跑到。

**触发源为什么是"血量下降"而不是 `Event.Combat.Hit`**

`Event.Combat.Hit` 是 `RPG_AttributeSet::PostGameplayEffectExecute` 里广播的，
而那里有一道**权威守卫** —— 事件只在服务器发出。客户端上的血条根本收不到，
表现会变成"主机看得见、客户端看不见"。

"血量下降"在两端都会发生：

| 端 | 路径 |
|---|---|
| 服务器 | GA 扣血 → `SetHealth` → 属性变化委托 |
| 客户端 | 服务器把属性复制过来 → `OnRep_Health` → **同一个**属性变化委托 |

白捡的好处：持续伤害（中毒、燃烧）没有单次命中事件，走血量也一样能覆盖。

**WBP 里要做的**（可选）

`On Reveal Changed(bool bRevealed)` 这个事件在亮起/收回时各调一次，
接一个 `Fade In` / `Fade Out` 动画就能做成淡入淡出，不接就是硬切。

> 🕳️ **别用 `Set Visibility` 去覆盖 C++ 设的 Collapsed**：
> WBP 的动画里如果有 `Set Visibility → Visible`，动画播完之后控件会**一直停在那里**，
> C++ 的 `HideAfterDamage()` 只是再设一次 Collapsed，而动画已经播完了不会自动收回。
> 要用可见性做动画的话，用 `Opacity` 而不是 `Visibility`。

**死亡时不亮**

`CanBeRevealed()` 在血量 ≤ 0 时返回 false。少了这一条，
尸体会在血量归零那一下亮出一根空血条、再挂满 5 秒。

**为什么订阅走定时器重试而不是每帧 tick**

组件 `BeginPlay()` 里就 `InitWidget()` 了，而组件 BeginPlay 由 `AActor::BeginPlay`
**先于**角色自己的 `ReceiveBeginPlay` 派发 —— Widget 被造出来时角色的 GAS 还没初始化，
ASC 是空的。所以必须要有一条"ASC 没就位就重试"的路径。

这条路径**没有**用 `NativeTick`，而是 `TimerManager` 每 0.2 秒重试一次、订阅成功即停。
原因：血条常规状态下是 `Collapsed` 的，而"隐藏的 Widget 还 tick 不 tick"属于
Slate 的实现细节（当前行为是会 tick，但没有任何东西保证它）。把订阅押在那上面，
一旦不成立就是"血条永远不亮、且不报任何错"—— 和之前 `GA_HitReact` 漏配
`AbilityTriggers` 是同一类故障，静默且难查。定时器与控件可见性完全无关。

### 1.6 伤害飘字

| 项 | 值 |
|---|---|
| 字号 | **22** |
| 配色 | `#FFFFFF`，暴击（后续）可切 `#F1C40F` |
| 描边 | `2px`，`#000000` @ `0.85` —— **必须要有**，否则打在亮背景上完全看不清 |
| 存活时长 | `0.9` 秒 |
| 上飘距离 | `70px`，先快后慢（缓动指数 2） |
| 淡出 | 后 55% 时间内从 1 渐隐到 0 |
| 轴心 | 居中（`0.5, 0.5`），以命中点为中心冒出 |

**同屏上限**：`32` 个，超出时最旧的先消失（防止群怪混战糊成一片）。

**随机散开**：同一个点附近的飘字会随机偏移最多 `22px`，避免多段伤害的数字完全重叠。

### 1.7 死亡面板

| 元素 | 尺寸 | 说明 |
|---|---|---|
| 全屏遮罩 | 铺满 | `#000000` @ `0.6`，0.4 秒淡入 |
| 标题 | 字号 **48**，居中 | `你死了`，`#E74C3C` |
| 倒计时 | 字号 **20**，标题下方 `16px` | `{N} 秒后重生`，`#BDC3C7` |

> 倒计时是**客户端自己数**的：`State.Dead` 标签是复制的，`RespawnDelay`
> 是配置常量，两端都有 —— 不需要服务器再复制一个"还剩几秒"。
> 敌人 `RespawnDelay = 0`（不重生）时倒计时会显示 `0 秒后重生`，
> 这时候把倒计时控件在 WBP 里按 `RespawnDelay <= 0` 隐藏掉即可。

---

## 2. 控件命名表 —— **这是契约，名字错了编译不过**

> 📌 **每条要建哪个 WBP、选什么父类，见 §3.0 的速查表。**
> 这一节只讲"每个 WBP 里要放哪些控件"。

C++ 用 `BindWidget` 按**变量名**去 WBP 里找同名控件。
标了 `BindWidgetOptional` 的可以不做（指针为 `nullptr`，功能降级）；
标了 `BindWidget` 的**必须有**，否则 WBP 编译失败。

表中"类型"那一列写的是 **Palette 面板里的控件名**（拖进来说的那个），
不是 C++ 类型 —— 比如 `Progress Bar` 拖出来是 `UProgressBar`。

> ⚠️ 有一类更隐蔽的情况：**控件建了但类型不对**（比如把 `Progress Bar` 建成了 `Image`）。
> 这个也会编译报错，所以还好。真正会静默失败的是"忘了给控件标 `BindWidget`"——
> 但那是我的事，不是你的 —— 表里所有控件的绑定标记都已经写好了。

### `WBP_AttributeBar`（父类 `RPG_AttributeBarWidget`）

| 控件名 | 类型 | 必填 | 用途 |
|---|---|---|---|
| `Bar` | Progress Bar | ✅ | 主填充条 |
| `ValueText` | Text | ⬜ | `72 / 100` |
| `Icon` | Image | ⬜ | 左侧图标 |

> 三条属性条都用**同一个 WBP**，只是图标和填充色不同。
> 做法：建一个 `WBP_AttributeBar`，再做三个子类蓝图 `WBP_HealthBar` /
> `WBP_ManaBar` / `WBP_StaminaBar`，各自在 Class Defaults 的 `Icon` 里换图、在
> `Bar` 的 `Fill Color and Opacity` 里换色。
>
> 💡 更省事的做法：只做一个 `WBP_AttributeBar`，把颜色/图标做成 `EditInstanceOnly`
> 的变量，然后在 `WBP_RPG_HUD` 里放三个实例分别设值。两种都行。

### `WBP_OverheadHealthBar`（父类 `RPG_OverheadHealthBarWidget`）

| 控件名 | 类型 | 必填 | 用途 |
|---|---|---|---|
| `HealthBar` | Progress Bar | ✅ | 血条 |
| `NameText` | Text | ⬜ | 角色名（敌人可以不显示） |

### `WBP_DamageNumber`（父类 `RPG_DamageNumberWidget`）

| 控件名 | 类型 | 必填 | 用途 |
|---|---|---|---|
| `AmountText` | Text | ✅ | 数字 |

### `WBP_RPG_HUD`（父类 `RPG_HUDWidget`）

| 控件名 | 类型 | 必填 | 用途 |
|---|---|---|---|
| `HealthBar` | `WBP_AttributeBar` | ⬜ | 血条 |
| `ManaBar` | `WBP_AttributeBar` | ⬜ | 蓝条 |
| `StaminaBar` | `WBP_AttributeBar` | ⬜ | 耐力条 |
| `AttackText` | Text | ⬜ | `攻 25` |
| `DefenseText` | Text | ⬜ | `防 18` |
| `DodgeIcon` | Image | ⬜ | 闪避图标 |
| `SkillPanel` | 任意容器（推荐 `Canvas Panel` / `Size Box`） | ⬜ | 右下角整块，不在出招时隐藏 |
| `ChargeBar` | Progress Bar | ⬜ | 蓄力条 |
| `MoveNameText` | Text | ⬜ | 招式名 |
| `DeathPanel` | 任意容器 | ⬜ | 死亡面板整块 |
| `RespawnCountdownText` | Text | ⬜ | 重生倒计时 |

> **HUD 里的控件全是可选的**（`BindWidgetOptional`）——
> 你可以先只做血条跑通，再逐步补。少哪个就少哪个功能，不会编译失败。

---

## 3. 步骤 1 · 建四个 WBP

> 📌 **先看这张表**，它决定了"用哪个菜单、选哪个父类"。
> 父类名就是你在 Class Picker 里**逐字输入搜索框**要打的字符串。

### 3.0 父类速查表

| 要建的资产 | 创建菜单 | 父类（C++ 类 → 选它） | 放在哪 |
|---|---|---|---|
| `WBP_AttributeBar` | **User Interface → Widget → Widget Blueprint** | `URPG_AttributeBarWidget` → 搜 `AttributeBar` | `Content/_My/UI/` |
| `WBP_OverheadHealthBar` | **User Interface → Widget → Widget Blueprint** | `URPG_OverheadHealthBarWidget` → 搜 `OverheadHealthBar` | `Content/_My/UI/` |
| `WBP_DamageNumber` | **User Interface → Widget → Widget Blueprint** | `URPG_DamageNumberWidget` → 搜 `DamageNumber` | `Content/_My/UI/` |
| `WBP_RPG_HUD` | **User Interface → Widget → Widget Blueprint** | `URPG_HUDWidget` → 搜 `HUDWidget` | `Content/_My/UI/` |
| `BP_RPG_HUD` | **Blueprint Class**（和建 `BP_RPG_Player` 同一个菜单） | `ARPG_HUD` → 搜 `RPG_HUD` | `Content/_My/Core/` |

**两个必须搞清楚的区分：**

- **前四个是 Widget Blueprint**，走 `User Interface` 分类。
- **`BP_RPG_HUD` 不是控件，是一个 Actor**（`ARPG_HUD` 继承自 `AHUD`）。
  它**不会**出现在 `User Interface` 分类里 —— 要用建 `BP_RPG_Player` 时用的那个
  **Blueprint Class** 菜单。这是最容易找错的一个。

### 3.0.1 怎么选父类（每个 WBP 都一样的操作）

1. 在 `Content/_My/UI/`（没有就新建）里**右键**
2. 展开 **User Interface** → 展开 **Widget** → 点 **Widget Blueprint**
3. 弹出的窗口标题是 **"Pick Parent Class for New Widget Blueprint"**
4. 窗口里先是几个常用父类。**点左下角的 `All Classes`** 展开完整列表，
   上面的**搜索框**里输入上表里那一列字符串
5. 选中它 → 点 **Select** → 命名 → 回车

**关于搜索不到：**

| 现象 | 原因 / 怎么办 |
|---|---|
| 搜了没结果 | 只输入**有辨识度的那一段**再试，比如 `AttributeBar` / `HUDWidget` / `DamageNumber`。列表里可能显示成带空格的 "RPG Attribute Bar Widget"，也可能显示成 `RPG_AttributeBarWidget`，取决于编辑器的显示设置 —— 搜中间那段两种都能匹配到 |
| 展开了 `All Classes` 也没有 | C++ 还没编译成功。**先跑一次编译再开编辑器**（命令见下面），编译没过的类不会出现在列表里 |
| 能看到但选不了 | 说明它继承链上有 `NotBlueprintable`。本项目的四个控件基类都可以被继承 |

> 💡 **这些类都标了 `UCLASS(Abstract)`，但它们照样能当父类。**
> `Abstract` 只禁止"直接实例化"，不禁止"被继承" ——
> 引擎选父类时的判定函数 `CanCreateBlueprintOfClass()` 只排除
> `Deprecated` / `NewerVersionExists` / 已经是蓝图生成的类，**不看 `Abstract`**。
> （`Kismet2.cpp:1065-1089`）所以列表里能看到它们，是你想要的行为。

> ⚠️ **必须先把 C++ 编过一遍再开编辑器。** 上面这五个类都是 C++ 类，
> 只存在于源码里 —— 编译没成功的话，Class Picker 里压根不会有它们：
> ```
> "D:/UE5/UE_5.8/Engine/Build/BatchFiles/Build.bat" RPGEditor Win64 Development -Project="E:/UE5/RPG/RPG.uproject" -WaitMutex
> ```

### 3.1 `WBP_AttributeBar`（父类 `RPG_AttributeBarWidget`）

按 3.0.1 建好之后：

1. 打开它 → 左侧 **Palette** 面板拖入三个控件：
   - 一个 **Progress Bar**，改名 `Bar`
   - 一个 **Text**，改名 `ValueText`
   - 一个 **Image**，改名 `Icon`
2. **改名方法**：选中控件 → 右侧 Details 面板最上面的 **Name** 字段

> ⚠️ 改的是 Details 里的 **Name**，不是控件的**文本内容**。
> 这两个容易混：文本内容改了没用，C++ 找的是 Name。
> 更麻烦的是**名字对不上会直接编译失败**（因为 `Bar` 我标的是
> `BindWidget` 而不是 `BindWidgetOptional`），所以改错了会当场报错 ——
> 这反而是好事。

3. 排布成一行：`Icon`（24×24）→ 间距 → `Bar`（选中后按 `Fill` 铺满剩余宽度）
   → `ValueText` 拖到 `Bar` 内部右下角（叠在条上）
4. `Bar` 的 Details 里设 `Fill Color and Opacity` = `#C0392B`
5. **编译并保存**

> 💡 三条属性条（血/蓝/耐力）可以只做**这一个** WBP，然后在 `WBP_RPG_HUD` 里
> 放三个实例，各自设不同的图标和填充色；也可以再做三个子类 WBP 各改各的。
> 两种都行，取决于你想不想在 HUD 里统一改。

### 3.2 `WBP_OverheadHealthBar`（父类 `RPG_OverheadHealthBarWidget`）

1. 按 3.0.1 建好，父类选 `RPG_OverheadHealthBarWidget`
2. 拖入两个控件：
   - **Progress Bar** → 改名 `HealthBar`（**必填**）
   - **Text** → 改名 `NameText`（可选，不建也行）
3. 尺寸按 §1.5：整块 `140×20`，血条 `80×6` 居中，名字在血条上方

### 3.3 `WBP_DamageNumber`（父类 `RPG_DamageNumberWidget`）

1. 按 3.0.1 建好，父类选 `RPG_DamageNumberWidget`
2. 拖入一个 **Text** → 改名 `AmountText`（**必填**）
3. Details 里：
   - `Font Size` = `22`
   - 勾上 **Outline**，`Outline Size` = `2`，颜色黑

> 根节点用默认的 `Canvas Panel` 就行，不容易出错。
> 飘字的**位置由 C++ 控制**（`SetPositionInViewport`），
> 你在 WBP 里只需要决定"这个数字长什么样"。

### 3.4 `WBP_RPG_HUD`（父类 `RPG_HUDWidget`）

按 3.0.1 建好，父类选 `RPG_HUDWidget`。**根节点保持 `Canvas Panel`**，
然后按 §1.1 的布局往里放下面这些控件（名字必须逐字一致）：

1. **左下属性区** —— 拖一个 `Vertical Box` 进去，选中它，在 Details 里设：
   - `Anchors` 选**左下角**那一格（`(0, 1)`）
   - `Position X` = `48`，`Position Y` = `-40`（Y 相对锚点是负的，往上走）
   - 里面依次放：三个 `WBP_AttributeBar` 实例（分别改名
     `HealthBar` / `ManaBar` / `StaminaBar`），
     再放一个 `Horizontal Box` 装 `AttackText` 和 `DefenseText` 两个 Text

2. **`DodgeIcon`** —— 一个 `Image`，挂在属性区右侧

3. **右下招式区** —— 拖一个 `Size Box`（宽设 `220`），改名 `SkillPanel`：
   - `Anchors` 选**右下角**那一格（`(1, 1)`）
   - `Position X` = `-48`，`Position Y` = `-40`
   - 里面放 `MoveNameText`（Text，上）和 `ChargeBar`（Progress Bar，下）

4. **死亡面板** —— 拖一个 `Canvas Panel`，改名 `DeathPanel`，铺满全屏：
   - 里面：一个半透明黑色 `Image` 当遮罩（`Color` 设 `#000000`，`A` = `0.6`）
     + 居中的标题 `Text` + `RespawnCountdownText`

5. **Class Defaults** 里可以改文案格式（`Light Attack Format` / `Heavy Attack Text` 等）

> ⚠️ **HUD 里这 11 个控件的名字就是契约。** 全表在 §2。
> 好消息是我把它们都标成了 `BindWidgetOptional` ——
> **少做哪个就少哪个功能，不会编译失败**。所以你可以先只做血条跑通，
> 再逐个补。这比"一次全做完再发现某个名字错了"要好受得多。

> ⚠️ **不要把 `Tick Frequency` 改成 `Never`。**
> HUD 的右侧招式区、闪避图标、死亡面板全靠 `NativeTick` 更新
> （规范 §6.2 讲了为什么这几项用轮询而不是委托）。
> 关掉 tick 之后这些**全都不再更新**，而且一条日志都不打 ——
> 表现是"进游戏会显示一次，之后永远不动"。

---

## 4. 步骤 2 · 建 `BP_RPG_HUD` 并挂到 GameMode

> ⚠️ **`BP_RPG_HUD` 不是控件，别走 `User Interface` 那一栏。**
> `ARPG_HUD` 继承的是 `AHUD`（一个 Actor），所以要用**建 `BP_RPG_Player` 时
> 用的那个 `Blueprint Class` 菜单**。在 `User Interface → Widget` 分类里
> 翻破天也找不到它。

1. `Content/_My/Core/` 右键 → **Blueprint Class** → 点左下 **All Classes** →
   搜索框输入 `RPG_HUD` → 选中列表里的 `RPG_HUD`（父类是 `HUD` 的那个）
   → 命名 `BP_RPG_HUD`
2. 打开 `BP_RPG_HUD` → **Class Defaults**，配置：

| 字段 | 值 |
|---|---|
| `HUD Widget Class` | `WBP_RPG_HUD` |
| `Damage Number Widget Class` | `WBP_DamageNumber` |
| `b Damage Numbers Track World` | ⬜ 不勾（默认，省性能） |
| `Max Damage Numbers` | `32` |
| `Damage Number Scatter Radius` | `22` |

3. 打开 `BP_RPG_GameModeBase` → **Class Defaults** → `HUD Class` = `BP_RPG_HUD`

> ⚠️ **这一步最容易漏。** 不设的话进游戏什么都看不到，日志里只有一条 Warning：
> `没有配置 HUDWidgetClass —— 界面上不会显示任何东西`。
> 游戏不会有任何异常，只是"UI 没出现"，很容易往 WBP 那边查。

---

## 5. 步骤 3 · 给角色挂头顶血条

打开 `BP_RPG_Player` **和** `BP_RPG_Enemy` → 在 Components 面板选中
**`OverheadHealthBar`** 组件 → Details：

| 字段 | 值 |
|---|---|
| `Widget Class` | `WBP_OverheadHealthBar` |
| `Space` | `Screen`（C++ 已设，确认一下） |
| `Draw Size` | `140 × 20`（C++ 已设） |
| `Relative Location` | `(0, 0, 110)`（C++ 已设） |

> ⚠️ **`Widget Class` 不设 = 头顶血条什么都不显示，且不报错。**
> C++ 里加了一道检查：`URPG_OverheadHealthBarComponent::InitWidget()` 里
> 如果发现拿不到 `URPG_OverheadHealthBarWidget`，会打一条 Warning。
> 看到这条就知道问题在哪。

> ⚠️ **不要把这个组件换成引擎自带的 `Widget Component`。**
> 项目自己的 `URPG_OverheadHealthBarComponent` 多做一件关键的事：
> 在 Widget 被造出来的那一刻，把"我是谁的血条"显式传进去。
>
> 少了这一步，血条会去问 `GetOwningPlayerPawn()` —— 而那个函数返回的是
> **本地玩家**，于是每条敌人的血条都显示玩家自己的血量和名字，
> 而且完全不报错。原因是 `UWidgetComponent` 用
> `CreateWidget(World, Class)` 造 Widget，那个重载把 PlayerContext 设成了
> `GameInstance.GetFirstGamePlayer()`，Widget 本身还被 outer 到 GameInstance 上 ——
> 想从 Widget 内部顺着 outer 链找回来也是死路。
>
> 组件在敌人身上已经有实例了（`ARPG_BaseCharacter` 的构造函数里建的），
> 你只需要在蓝图里选它、设 Widget Class。

---

## 6. 数据是怎么流进 UI 的（面试考点）

这一节的取舍值得在面试里讲 —— 因为它体现的是"按问题性质选工具"，而不是"背 API"。

### 6.1 属性走委托

```
服务器：GE 改属性
  → PostGameplayEffectExecute
    → SetHealth() 触发 OnGameplayAttributeValueChange
      → HUD 的 HandleAttributeChanged

客户端：属性值复制到达
  → OnRep_Health → GAMEPLAYATTRIBUTE_REPNOTIFY
    → SetBaseAttributeValueFromReplication 内部广播同一个委托
      → HUD 的 HandleAttributeChanged     ← 同一份代码，两端都能跑
```

**为什么用委托而不是每帧读属性**：属性有"值"，变化是离散事件。
委托在变化的那一刻给出新旧值，客户端上还能蹭引擎自己的复制回调 ——
每帧读 8 个 float 再比对是纯浪费。

**一个必须处理的细节**：`MaxHealth` 变化时血条也要重画。
吃了个加生命上限的 Buff，当前血量一点没变，但"满血"的定义变了 ——
只监听 `Health` 的话，表现是"上限涨了但条没动，要掉一次血才对得齐"。

### 6.2 布尔状态走每帧读标签

```
URPG_HUDWidget::UpdateCombatState(DeltaTime)
  ├─ State.Dodging            → 闪避图标显隐
  ├─ State.Attack.Charging    → 蓄力计时 + 蓄力条 + 招式名
  ├─ State.Attack.Transition  → 招式名（切手技）
  ├─ State.Attacking          → 招式名（轻击 · 第 N 段）
  └─ State.Dead               → 死亡面板 + 重生倒计时
```

**为什么这里不用 `RegisterGameplayTagEvent`**：这些是布尔状态，不是连续值。
用事件监听要注册 6~8 个标签、各自维护解绑句柄，代码量翻倍；
而 `HasMatchingGameplayTag` 本身只是一次哈希查找，每帧做几次的开销可以忽略。

**和 `URPG_AnimInstanceBase::UpdateCombatState()` 是同一套写法** ——
项目里"C++ 读 GAS 状态、暴露给表现层"已经有惯例了，这里保持一致。

### 6.3 ★ 蓄力条为什么不让 GA 直接喂数据

`URPG_GA_HeavyAttack` 里确实有 `ChargeElapsed`，但 HUD 拿它有三个问题：

1. 它是 **private 成员**，要读必须先 Cast 到具体 GA 类 —— 而 UI 不该知道是哪个 GA
2. GA 实例会**随能力结束被回收**，UI 存它的指针就是悬垂指针
3. 它每 `0.1` 秒才更新一次（`ChargeTickInterval`），直接画进度条能看出台阶

所以改成：**标签当节拍器，UI 自己计时**

| 事件 | UI 的动作 |
|---|---|
| `State.Attack.Charging` **出现** | `ChargeDisplayTime = 0` 然后开始累加 |
| 蓄力中每帧 | `ChargeDisplayTime += DeltaTime`，填充 = `ChargeDisplayTime / 第三段门槛` |
| 标签**消失** | `ChargeDisplayTime = 0` → **条归零** |

**"右键松开后条归零"就是这么实现的** —— 不需要订阅输入事件。
松手、被打断、耐力耗尽强制释放，三种结束方式的共同点都是
`State.Attack.Charging` 被摘掉，所以一个信号覆盖全部路径。

**段位不靠 UI 猜**：第几段仍然读权威的 `State.Attack.Charging.LvN` 标签。
UI 自己数的只是"比例"，所以即使两个时钟差了一两帧，也只会影响
"条过没过刻度线"，**不会出现"UI 说二段、实际打了三段"**。

### 6.4 伤害飘字为什么要走 NetMulticast

```
服务器：PostGameplayEffectExecute
  → Character->Multicast_ShowDamageNumber(伤害, 命中点)     ← NetMulticast, Unreliable
      ├─ 服务器本机：找到本地 PlayerController → ARPG_HUD::ShowDamageNumber
      ├─ 客户端 A： 同上（各自找各自本地的那个）
      └─ 客户端 B： 同上
```

**不能用现成的 `Event.Combat.Hit`**：那个 GameplayEvent 只在服务器广播
（见 `RPG_AttributeSet` 里的权威判断），而飘字是每个客户端各自要画的东西 ——
服务器上画了没人看得见。

**为什么用 `Unreliable`**：飘字是纯表现，丢一个数字不影响任何逻辑，
而 Reliable RPC 的确认与重发机制在挨打密集时会白白吃带宽。
这是"表现类 RPC 一律 Unreliable"这条通用规则的实例。

**为什么由 HUD 而不是角色来画**：世界坐标 → 屏幕坐标要相机信息，
而相机是每个客户端各自的。角色不该知道相机的存在。

---

## 7. 测试清单

### 7.1 属性区

| 操作 | 预期 |
|---|---|
| 进游戏 | 三条属性条显示正确数值，攻防数值正确（**不是三条空条**） |
| 挨一刀 | 血条下降，数值同步 |
| 按 Shift 跑 | 耐力条下降 |
| 松开 Shift 停 3 秒 | 耐力条恢复 |
| 蹲下再挨刀 | 数值照常变 |
| 血量降到 30% 以下 | 血条变色 / 脉动（如果在 WBP 里做了） |

### 7.2 招式区

| 操作 | 预期 |
|---|---|
| 按左键 | 招式名显示 `轻击 · 第 1 段`，条隐藏 |
| 连按左键 | 段号递增到 5 |
| **按住右键** | 招式名切 `重击 · 蓄力中`，**蓄力条从 0 开始涨** |
| **松开右键** | **蓄力条立刻归零**，招式名在攻击结束后消失 |
| 蓄力超过第三段门槛 | 条走满（而不是只到一半） |
| 轻击连段中按右键 | 招式名显示 `切手技` |
| 什么都不按 | 整块 `SkillPanel` 隐藏 |
| 蓄力中被打断 | 条归零、面板消失 |

### 7.3 闪避图标

| 操作 | 预期 |
|---|---|
| 什么都不做 | **图标隐藏** |
| 按闪避 | 图标出现 |
| 闪避动作结束 | 图标消失 |

### 7.4 头顶血条

**第 1 组：谁有血条（常规状态 —— 全是收起来的）**

| 视角 | 预期 |
|---|---|
| 单机，看自己 | **自己头上没有血条**（组件整个是关的） |
| 单机，看敌人 | 敌人头上**什么也看不见**（血条常规隐藏，这是对的） |
| PIE 双客户端，客户端 1 看客户端 2 | 客户端 2 头上**什么也看不见** |
| PIE 双客户端，客户端 1 看自己 | 自己头上没有血条 |

**第 2 组：挨打才亮（§1.5.1）—— AI**

| 操作 | 预期 |
|---|---|
| 打敌人一刀 | 敌人头顶血条**亮出来**，血量立刻下降 |
| 亮起后再等 5 秒（不碰它） | 血条**自己收回去**，头上重新变干净 |
| 亮起后 2 秒再打一刀 | 血条**还在**（不是重新淡入），并**重新开始** 5 秒计时 |
| 连打 10 刀（每刀间隔 1 秒） | 血条一直亮着直到最后一次挨打后 5 秒，**不会**累积成 50 秒 |
| 打到敌人残血（<30%） | 血条填充切深红并脉动（`On Low Health Changed` 驱动） |
| **把敌人一刀打死** | 血条**不收起来也不重亮** —— 那一刀亮出来后 5 秒收掉，之后不再出现 |
| 敌人血量归零后 | 尸体头上**不再冒血条**（`CanBeRevealed()` 挡掉了） |

**第 3 组：挨打才亮 —— 其他玩家 ★**

需求："**除本地玩家外的其他玩家也要在受击时显示血条**"。
这一组和第 2 组走的是**同一套代码**（判据、计时、收回全一样），
只是 ASC 在 PlayerState 上而不是角色身上。分开列是为了让"验证过"这件事有据可查。

| 操作 | 预期 |
|---|---|
| 客户端 1 打客户端 2 一刀 | **客户端 1 屏幕上**：客户端 2 头顶血条亮 |
| 同上 | **客户端 2 自己的屏幕上**：**没有**血条 —— 那是他本人，头顶永远不显示（他看左下的 HUD） |
| 客户端 2 被打后 | **客户端 2 屏幕上**：客户端 1 头顶**没有**血条（客户端 1 没挨打） |
| 宿主看客户端 2 挨打 | **宿主屏幕上**客户端 2 的血条**也亮**（掉血在两端都触发） |
| 客户端 2 挨打后等 5 秒 | 三台机器上**同时**收回（各自本地计时，时长一致） |
| 客户端 2 打宿主（主机玩家） | **两台客户端屏幕上**都看不到宿主的血条（宿主是那些机器眼里的"其他玩家"→ 应该看得到！见下行说明） |

> ⚠️ 最后一行有个**容易自我怀疑**的点，单独说清楚：
> 在**客户端 1 和客户端 2 的屏幕上**，主机玩家是"其他玩家"，
> 所以**他挨打时他的血条应该亮**。只有在他**自己的**屏幕上才不显示。
> 验证时别只看自己那一端就下结论。

> 🕳️ **如果第 3 组不亮而第 2 组正常**，先查**蓝图配置**而不是代码：
> `OverheadHealthBar` 组件是从 `ARPG_BaseCharacter` 继承来的，
> 而 **`Widget Class` 是每个角色蓝图各自设的** ——
> 只在 `BP_RPG_Enemy` 上配了、没在 `BP_RPG_Player` 上配，就正好是"敌人亮、玩家不亮"。
> 看日志确认：`头顶血条没有设置 Widget Class`（每个玩家角色生成时报一次）。

### 7.5 飘字

| 操作 | 预期 |
|---|---|
| 打敌人一刀 | 命中点冒出一个数字，往上飘并淡出 |
| 数字消失后 | 屏幕上不留残影（说明自毁正常） |
| AOE / 多段命中 | 数字散开不重叠 |
| 连续打 50 刀 | 同屏数字不超过 32 个，帧率不掉 |

### 7.6 死亡面板

| 操作 | 预期 |
|---|---|
| 把自己打死 | 出现"你死了" + 倒计时 |
| 倒计时结束 | 面板消失，角色复活 |
| 复活后 | 属性条回满、招式区正常、闪避图标隐藏 |

### 7.7 联机

| 操作 | 预期 |
|---|---|
| PIE 双客户端 | 两边都能看到对方头顶血条 |
| 客户端 2 打敌人 | 客户端 1 屏幕上也能看到飘字 |
| 客户端 1 死亡 | 只有客户端 1 看到死亡面板 |

---

## 8. 排查表

| 症状 | 原因 | 修法 |
|---|---|---|
| **进游戏完全没有 UI** | GameMode 的 `HUD Class` 没设 | 见 §4，看日志有没有 `没有配置 HUDWidgetClass` |
| | 或 `BP_RPG_HUD` 的 `HUD Widget Class` 没设 | 同上 |
| **血条一直是空的** | ASC 还没接上 | 看日志有没有 `HUD 已接上本地玩家的 ASC`；没有就说明 HUD 创建早于 PlayerState 复制 —— 这是正常的，`NativeTick` 会重试 |
| 属性条**永远不动** | WBP 的 `Tick Frequency` 被设成了 `Never` | 改回 `Auto`。**注意**：属性条本身不靠 tick，但招式区/死亡面板靠 |
| 某项数值永远不变 | 控件名拼错了 | 对照 §2 的命名表逐字检查（**大小写敏感**） |
| **血条上限变了但条不动** | 理论上不该发生（C++ 同时监听了 `MaxHealth`） | 如果真出现，检查是不是在 WBP 里覆写了 `SetAttributeValues` 只刷了一半 |
| **蓄力条走满只有一半** | 用了 `MaxChargeTime` 当分母 | C++ 用的是 `DA_AttackModule` 第三段的 `RequiredChargeTime`，检查 DA 里三个等级的门槛时间是不是递增的 |
| **松开右键条不归零** | `State.Attack.Charging` 没被摘掉 | 看 `LogRPG_Combat Verbose`，检查 `UpdateChargeTags` / `ClearChargeTags` 的日志 |
| 招式名**一直显示切手技** | 切手技标签没摘 | 已修（`ClearChargeTags` 里先摘切换标签再判空）—— 如果还出现，检查是不是有别的路径结束了能力但没走 `ClearChargeTags` |
| **敌人头顶没血条**（单机/主机） | 曾是 C++ 的判据写错（用了 `IsLocallyControlled()`，它在敌人身上恒为 true） | **已修**。现在用 `IsLocallyControlledPlayer()`。看日志 `[敌人名] 头顶血条：显示（控制器 RPG_AIController_C_0）` 确认 |
| 敌人头顶血条**平时看不见** | **正常** —— §1.5.1 的"挨打才亮" | 打它一刀就会亮。想改回常驻，把 `RevealDuration` 设成一个很大的值 |
| 挨打了血条**还是不亮** | 要么属性委托没订阅上，要么 WBP 里用自己的可见性设置盖掉了 | 按顺序查：<br>① 先在控制台 `AbilitySystem.DebugAttribute Health` 确认血量真的掉了<br>② 若血量掉了但不亮 → 订阅没成功。`TimerManager` 每 0.2 秒重试一次，正常一两秒内必接上；一直不亮说明 `ResolveOwnerASC()` 返回空 —— 检查 `ARPG_BaseCharacter` 的 `GetAbilitySystemComponent()`<br>③ 订阅正常但仍看不见 → WBP 里给根节点设了 `Collapsed`，或某个 UMG 动画用 `Set Visibility` 盖住了 C++ 的值 |
| 血条**亮了就不收回** | 计时器没起来 | `RevealDuration` 是不是被设成 0 或负数（C++ 的 `ClampMin = 0.1` 只约束编辑器输入框，改资产里的默认值要重新编译）。另外检查是不是每帧都在掉血（持续伤害会不断刷新计时，这属于预期） |
| 血条**淡出之后再也不亮** | WBP 动画用 `Set Visibility → Visible` 把它锁住了 | §1.5.1 的说明：用 `Opacity` 做动画，别用 `Visibility` |
| **尸体头上还亮着血条** | `CanBeRevealed()` 应该挡住 | 检查是不是在 WBP 里直接调了 `SetVisibility(Visible)`。C++ 的判据是"血量 ≤ 0 就不再亮" |
| **客户端看玩家不亮、看 AI 亮** | 曾是 C++ 的订阅重试是死代码 | **已修**。`TryBindToOwnerASC()` 里 `ASC == BoundASC.Get()` 排在 `if (!ASC)` 前面，而初始状态下两者**都是 null**，判断成立 → 第一次调用就"确认已接上"返回，重试定时器从没启动过。于是能否订上完全取决于 Widget 构造时 ASC 在不在：AI 在自己身上（有）→ 订上；玩家在 PlayerState 上（要等复制）→ 订不上。详见 `PHASE8_NETWORKING.md` §4.5 |
| **其他玩家**挨打不亮血条（敌人正常） | 先分清楚是"配置"还是"代码" | 两者的现象一模一样。**先看日志**：<br>· 有 `头顶血条没有设置 Widget Class` → **配置问题**，`BP_RPG_Player` 的 `OverheadHealthBar` 组件上没配（`Widget Class` 是每个蓝图各自设的，不会从 `ARPG_BaseCharacter` 继承到具体值）<br>· 没有那条警告，但也不亮 → 才往代码查：`ResolveOwnerASC()` 走的是 `ARPG_Player::GetAbilitySystemComponent()` → PlayerState，客户端上 PlayerState 可能比 Pawn 晚到（重试定时器会兜住，等一两秒再看） |
| **主机看不到其他玩家的血条** | 曾怀疑过 `IsLocallyControlledPlayer()` | **已核实无误**：`IsLocalController()`（`Controller.cpp:106-110`）的第三条要求 `GetRemoteRole() != ROLE_AutonomousProxy`，而服务器上托管的远端玩家 PC 的 RemoteRole 正是 `AutonomousProxy`，所以判定为 false → 血条正常显示。这一条特意写下来，免得下次又怀疑到它头上 |
| **自己头上也有血条** | 可见性没刷新 | C++ 在 `BeginPlay` / `PossessedBy` / `OnRep_Controller` 三处都会刷；如果还出现，说明 WBP 里手动改了 `OverheadHealthBar` 的可见性 |
| 头顶血条**什么都不显示** | `Widget Class` 没设 | 见 §5。日志里两条对照着看：<br>· `头顶血条没有设置 Widget Class` → 组件上没配<br>· `头顶血条：显示` 有了但还是看不见 → WBP 自身的问题（尺寸/颜色） |
| **所有敌人血条都显示玩家自己的血量** | 组件被换成了引擎自带的 `Widget Component` | 换回 `URPG_OverheadHealthBarComponent` —— 见 §5 的说明 |
| 头顶血条**在敌人身上位置不对** | 网格体的实际高度和胶囊体不一致 | 调 `OverheadHealthBar` 的 `Relative Location` Z 值 |
| 血条**上限变了但条没动** | 理论上不该发生（Health 和 MaxHealth 都监听了） | 检查是不是把 `MaxHealthChangedHandle` 的解绑漏了导致重绑失败 |
| **死亡面板一直显示"0 秒后重生"** | 角色 `Respawn Delay = 0`（不重生） | C++ 已经处理：`RespawnDelay <= 0` 时隐藏倒计时文本、只留"你死了" |
| **飘字全堆在左上角** | 位置没设上 | 已在 C++ 里保证 `AddToViewport` → `SetPositionInViewport` 的顺序，若出现说明有人在 WBP 里提前调了 `InitializeDamageNumber` |
| **飘字越打越卡** | 已修的 bug | 旧版用 `IsValid()` 判飘字死活，而 `RemoveFromParent()` 不会让对象失效、UPROPERTY 数组又是强引用 —— 数组里的死控件永远清不掉。现在改用 `IsInViewport()` |
| 飘字**从屏幕另一侧冒出来** | `ProjectWorldLocationToScreen` 的返回值没判 | 已在 C++ 里判了（相机背后的点不显示） |
| **飘字不跟着敌人走** | 正常行为 | 想要跟随就勾上 `b Damage Numbers Track World`，代价见 §6.4 |
| **PIE 双客户端时飘字出现两次** | 用了 `GetController()` 而不是本地 PlayerController | 已在 C++ 里用 `GetFirstPlayerController()` 保证每端只画一次 |
| 死亡面板**倒计时不动** | WBP 的 tick 被关了 | 同"属性条永远不动"那一条 |

---

## 9. 本期没做的

| 项 | 说明 |
|---|---|
| **敌人血条只在头顶，没有锁定/ Boss 条** | 需求是"其他玩家和 AI 头顶有血条"，已经满足。屏幕顶部的大血条需要先有锁定系统 |
| **伤害飘字的暴击/属性区分** | 现在所有伤害一个样式。`Payload.EventMagnitude` 已经带着伤害值，做"大数字用更大的字号"只要在 WBP 的 `On Damage Number Initialized` 里按 `Amount` 判断 |
| **技能冷却图标** | 闪避图标目前只表示"正在闪避"。要做冷却显示需要在 HUD 里读 `GetCooldownRemainingForTag`（项目已有 `Cooldown.Dodge` 标签） |
| **血条残影** | 规范里给了做法（§1.2），但需要你在 WBP 里连一个 UMG 动画 —— C++ 已经把 `On Attribute Values Changed` 事件准备好了 |
| **UI 音效** | 没有按钮，都是纯显示 |
| **手柄导航 / UI 输入模式** | 纯展示型 HUD，不需要焦点。加背包/菜单时要回头处理（`SetInputMode`、`SetShowMouseCursor`） |
| **本地化的正式文本** | 文案用的是 `NSLOCTEXT`，结构对，但只有中文一份 |
