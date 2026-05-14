# 引言

目前的LTTit的传输协议使用的是SCP协议，这是一个类TCP协议。

本篇我们讨论的是经典的TCP AIMD算法，我们需要使用随机过程的知识对reno这些算法的吞吐进行建模。

人的记忆和对全局的把控是有极限的，代码是累赘而不是资产，项目的熵增也是不可避免的。当项目复杂到一定程度时，就会成为一个复杂问题，复杂问题的特征就是难以被数学建模，更不可能被分析，无法被观察，无法用语言言说，这就是一个克苏鲁。

在项目膨胀为克苏鲁怪物之前，拆分问题并进行建模，可以更好压缩复杂度并且用最小的代价实现需要的功能。

在我设计的协议SCP中，使用的就是reno算法，但是，简单的调参或者工程堆砌已经越来越不够看了，如何让我设计的可靠协议适应随机的、不稳定的分布式环境，这是我最近在思考的问题。

本篇内容主要参考该论文：

Modeling TCP Reno Performance: A Simple Model and Its Empirical Validation Jitendra Padhye, Victor Firoiu, Donald F. Towsley, Fellow, IEEE, and James F. Kurose, Fellow, IEEE

参考书籍：Ross的《随机过程》

# 类TCP协议模型

SCP是一个类TCP协议，它是如何运行的呢？

本质上是ack驱动：

1. SCP会发一堆数据包，只要窗口有剩
2. 收到一个ack窗口增长一点点，我们就可以多发一个数据包
3. 窗口下降，等ack

上面是理想情况下的SCP协议的稳态，此时系统会形成一个不动点，所有流平分带宽。

但是，丢包是不可避免的。

丢包的信号：

1. 超时惩罚，这个数据包很久很久都没被ack
2. 收到三次重复ack，说明这地方形成了一个空洞

对于这两种情况，超时的时候，整个系统会受到最严厉的惩罚，从头再来。

而收到重复ack三次，scp会让窗口减半，并且触发快速重传。

超时是最迫不得已的手段，一般来说，理想的协议应当尽量避免超时的发生。

# 吞吐量建模

对于reno模型，我们要在一个窗口内发w个包：

```
Round k:
    1, 2, 3, ..., a-1, [a lost], a+1, a+2, ..., W
```

TCP是ack驱动的，每一个rtt发w个包：

1.第一次发w个，a丢了，或者a后面的也丢了

2.收到ack，我们知道前面有a-1个已经被收到了，那么每收到一个ack，就继续发一个包，根据reno

结果到了a这里，我们收到了3个重复ack。

那么，这个过程，我们一共发了w + a -1 

# 一些随机过程的基础知识

## 无记忆性的定义（离散）
$$
P(X > s+t \mid X > t) = P(X > s),
\qquad s,t \in \mathbb{Z}_{\ge 0}.
$$

## 几何分布是唯一的离散无记忆分布

设尾概率：
$$
q_k = P(X > k).
$$

无记忆性要求：
$$
q_{s+t} = q_s q_t.
$$

取 \(t=1\) 得递推：
$$
q_{s+1} = q_s q_1.
$$

解得：
$$
q_s = q_1^s.
$$

设 \(q_1 = 1-p\)，则：
$$
P(X > s) = (1-p)^s.
$$

点概率为：
$$
\begin{aligned}
P(X = k)
&= P(X > k-1) - P(X > k) \\
&= (1-p)^{k-1} - (1-p)^k \\
&= (1-p)^{k-1}p.
\end{aligned}
$$

因此：
$$
\boxed{
X \sim \mathrm{Geom}(p)
\text{ 是唯一满足无记忆性的离散分布}
}
$$

## 指数分布是唯一的连续无记忆分布

设尾概率：
$$
Q(x) = P(T > x),\quad x\ge 0.
$$

无记忆性：
$$
Q(s+t) = Q(s)Q(t),\qquad s,t\ge 0.
$$

在连续条件下唯一解为：
$$
Q(x) = e^{-\lambda x}.
$$

于是密度：
$$
f_T(x) = \lambda e^{-\lambda x},\quad x\ge 0.
$$

因此：
$$
\boxed{
T \sim \mathrm{Exp}(\lambda)
\text{ 是唯一满足无记忆性的连续分布}
}
$$

## 几何分布与指数分布的极限关系

几何分布尾概率：
$$
P(X > k) = (1-p)^k.
$$

指数分布尾概率：
$$
P(T > t) = e^{-\lambda t}.
$$

当 \(p-> 0\) 时：
$$
(1-p)^k \approx e^{-pk}.
$$

因此若随机变量 \(X\) 服从参数为 \(p\) 的几何分布，则：
$$
pX \xrightarrow{d} \mathrm{Exp}(1).
$$

即：
$$
\boxed{
\text{几何分布在小 }p\text{ 极限下收敛到指数分布}
}
$$

# 期望线性性

离散情形：

$$
E[aX + bY]
= \sum_{x,y} (a x + b y) P(X=x,Y=y)
= a \sum_x x P(X=x) + b \sum_y y P(Y=y)
= aE[X] + bE[Y].
$$

连续情形：

$$
E[aX+bY]
= \iint (a x + b y) f(x,y)\,dx\,dy
= a \int x f_X(x)\,dx + b \int y f_Y(y)\,dy
= aE[X] + bE[Y].
$$

# 无记忆性与几何/指数分布

几何分布尾概率：

$$
P(X>k) = (1-p)^k.
$$

无记忆性：

$$
P(X>s+t \mid X>t)
= \frac{(1-p)^{s+t}}{(1-p)^t}
= (1-p)^s
= P(X>s).
$$

指数分布尾概率：

$$
P(X>x) = e^{-\lambda x}.
$$

无记忆性：

$$
P(X>s+t \mid X>t)
= \frac{e^{-\lambda(s+t)}}{e^{-\lambda t}}
= e^{-\lambda s}
= P(X>s).
$$

几何与指数的对应（小 \(p\)）：

$$
(1-p)^k \approx e^{-pk}.
$$



# 更新奖励定理

若周期长度 \(X_n\)、奖励 \(R_n\) 独立同分布，且E[X]<无限，则：
$$
\lim_{t\to\infty} \frac{R(t)}{t}
= \frac{E[R]}{E[X]},
$$

其中：

$$
R(t) = R_1 + R_2 + \cdots + R_{N(t)}.
$$

让我们用大数定律推导结构：

$$
\frac{R(t)}{t}
= \frac{R_1+\cdots+R_{N(t)}}{N(t)} \cdot \frac{N(t)}{t}
\to E[R] \cdot \frac{1}{E[X]}.
$$

# 丢包与几何分布

对于大部分情况来说，我们可以认为上一次丢包与下一次丢包是无关的，当环境拥塞时，我们假定我们的算法都不是缺德份子，不会尝试破坏环境，因此丢包的概率对于每一次传输都是一样的。

那么，这就是一个几何分布了，我们需要求一下期望这些。

### 几何分布设定

设随机变量 \(X\) 服从几何分布：

$$
P(X = k) = (1-p)^{k-1} p,\quad k = 1,2,3,\dots,\ 0<p\le 1
$$

我们要算：

$$
E[X] = \sum_{k=1}^{\infty} k (1-p)^{k-1} p
$$

### 推导方法一：用等比级数求导

先记：

$$
q = 1 - p
$$

所以：

$$
E[X] = p \sum_{k=1}^{\infty} k q^{k-1}
$$

关键是算：

$$
S = \sum_{k=1}^{\infty} k q^{k-1}
$$

先从基本等比级数开始：

$$
\sum_{k=0}^{\infty} q^k = \frac{1}{1-q},\quad |q|<1
$$

两边对 \(q\) 求导：

$$
\sum_{k=1}^{\infty} k q^{k-1} = \frac{1}{(1-q)^2}
$$

所以：

$$
S = \frac{1}{(1-q)^2}
$$

代回去：

$$
E[X] = p \cdot \frac{1}{(1-q)^2}
$$

又因为 \(q = 1-p\)，所以：

$$
1 - q = p
$$

于是：

$$
E[X] = p \cdot \frac{1}{p^2} = \frac{1}{p}
$$

最终

$$
\boxed{E[X] = \frac{1}{p}}
$$

### 推导方法二：用自洽方程

设 E[X] = u。

假设一共X次，那么让X = 1 + X*.这样我们可以方便考虑第一次：

第一次试验有两种情况：

- 以概率 \(p\) 成功：\(X = 1\)
- 以概率 \(1-p\) 失败：此时已经用掉 1 次试验，剩下的期望仍是 u，所以总共 \(1 + X'\)，且 \(X'\) 与 \(X\) 同分布

写成期望方程：

这里之所以可以写成1+u，就是因为E(X) = E(X*) = u,那么写成第一次成功的期望 + 后面的次数成功的期望:
$$
\mu = p \cdot 1 + (1-p)(1+\mu)
$$

展开：

$$
\mu = p + (1-p) + (1-p)\mu
$$

移项：

$$
p\mu = 1
$$

得到：

$$
\boxed{\mu = \frac{1}{p}}
$$
如何理解第二种呢？为什么我们只用考虑第一次呢？因为几何分布是无关的，所以第一次实验的期望与后面都是相同的。

自洽其实就是从定义出发，自己指向自己。

# TCP Reno 吞吐量推导（TD、无超时、无窗口上限）

# TCP Reno 中的更新奖励定理应用

周期（TDP）：从一次 TD 丢包到下一次 TD 丢包。

- 周期奖励：发送包数 \(Y\)
- 周期长度：时间 \(A\)

吞吐量：

$$
B(p) = \frac{E[Y]}{E[A]}.
$$

这里为什么用packet呢？TCP是字节流，但论文这里的packet按照我的理解其实是单位。

# 一个周期发送的包数 \(Y\)

真实 TCP 行为：

$$
Y = W + a - 1.
$$

期望：

$$
E[Y] = E[W] + E[a] - 1
= E[W] + \frac{1}{p} - 1.
$$



# TCP Reno 数学模型

我们假设这个窗口值持续 b **个 RTT**，每个 RTT 发窗口大小个包。

## 窗口演化公式

$$
W_i = \frac{W_{i-1}}{2} + \frac{X_i}{b}
$$

- TD 丢包 → 窗口减半 → \(W_{i-1}/2\)
- 每 RTT 窗口线性增长 \(1/b\)
- 共 \(X_i\) 个 RTT → 增加 \(X_i/b\)个窗口大小

## 一个 TDP 内发送包数 \(Y_i\)

由于最后一段不完整，我们可以在求和时写成beta:

持续b个RTT，那么就是：
$$
Y_i
=
b\sum_{k=0}^{X_i/b - 1}
\left(
\frac{W_{i-1}}{2} + k
\right)
+
\beta_i
$$

等差数列求和：

$$
b\sum_{k=0}^{X_i/b - 1}
\left(
\frac{W_{i-1}}{2} + k
\right)
=
X_i\cdot
\frac{
\frac{W_{i-1}}{2} + (W_i - 1)
}{2}
$$

因此：

$$
Y_i
=
X_i\cdot
\frac{
\frac{W_{i-1}}{2} + (W_i - 1)
}{2}
+
\beta_i
$$

## 关键关系式 (11)

窗口增长量：

$$
W_i - \frac{W_{i-1}}{2} = \frac{X_i}{b}
$$

取期望：

$$
E[W] - \frac{E[W]}{2} = \frac{E[X]}{b}
$$

得到：

$$
E[W] = \frac{2}{b}E[X]
$$

## 关系式 (12)

$$
E[X]
=
\frac{1-p}{p}
+
E[W]
-
1
+
E[\beta]
$$

这里我们假设最终beta = w/2，那么有：

$$
E[\beta] = \frac{E[W]}{2}
$$

## 联立 (11)(12) 得到 (13)

$$
E[W]
=
\frac{2+b}{3b}
+
\sqrt{
\frac{8(1-p)}{3bp}
+
\frac{(2+b)^2}{3b}
}
$$

## 小 \(p\) 极限 (14)

根号主导项：

$$
\frac{8}{3bp}
$$

泰勒展开步骤：

设

$$
\sqrt{\frac{A}{p} + C'}
=
\sqrt{\frac{A}{p}}
\sqrt{1 + \frac{C'p}{A}}
$$

令

$$
\varepsilon = \frac{C'p}{A}
$$

当 \(p-> 0\) 时：

$$
\sqrt{1+\varepsilon}
= 1 + \frac{\varepsilon}{2} + O(\varepsilon^2)
$$

因此：

$$
\sqrt{\frac{A}{p} + C'}
=
\sqrt{\frac{A}{p}}
\left(1 + O(p)\right)
=
\sqrt{\frac{A}{p}} + O(\sqrt{p})
$$

于是得到：

$$
E[W]
=
\sqrt{\frac{8}{3bp}}
+
o\!\left(\frac{1}{\sqrt{p}}\right)
$$

## 求 \(E[X]\)

$$
E[X]
=
\frac{b}{2}E[W]
\sim
\frac{b}{2}
\sqrt{\frac{8}{3bp}}
=
\sqrt{\frac{2b}{3p}}
$$

## 一个周期发送包数 \(E[Y]\)

真实 TCP 行为：\(Y = W + a - 1\)

几何分布：\(E[a] = 1/p\)

$$
E[Y]
=
E[W] + \frac{1}{p} - 1
$$

主导项：

$$
E[Y] \sim \frac{1}{p}
$$

## 周期时长

$$
E[A] = \text{RTT}\cdot E[X]
$$

## 最终吞吐量公式

$$
B(p)
=
\frac{E[Y]}{E[A]}
\sim
\frac{1/p}{\text{RTT}\cdot \sqrt{2b/(3p)}}
=
\frac{1}{\text{RTT}}
\sqrt{\frac{3}{2bp}}
$$

最终：

$$
\boxed{
B(p)
\sim
\frac{1}{\text{RTT}}
\sqrt{\frac{3}{2bp}}
}
$$

