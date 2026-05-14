# 引言

动态分布式操作系统LTTit是我个人业余时间开发的一个嵌入式操作系统，目标是把一百块单片机当成一块来用。

为了达到这个目标，我不仅要设计软件，0.3版本后还要用FPGA设计硬件，深入物理世界，同时设计数学语义，对各个模块进行形式化建模与分析。

但是，有朋友问我：大家已经看你说这些很多次了，能不能告诉我，现在的LTTit的0.3版本，有没有什么好玩的东西？各个组件完成度如何？性能怎么样？到底能做什么？能不能具体一点？

好吧，其实我感觉软件模块基本快完事了，当然，我不是说成熟度，我是说功能从0到1的跨越，至于从1到100，还要时间的考验。

实在是抱歉，我压根没什么时间写文档，可能很多读者打开项目连怎么运行都搞不清楚。

那么我就简单介绍一下目前的LTTit0.3版本到底有哪些功能，能做什么。

## 一句话概括

LTTit 0.3 是一个让多块 MCU 像一台计算机一样协作的动态分布式操作系统。 你可以在 A 板上 open 一个文件，让 B 板的 LED 亮起； 你还可以写脚本编程语言，并对任意节点注入 BPF 程序，动态改变整个操作系统的行为。

# 组件与功能

它有以下组件：

```
├─ccBPF  
│  ├─compiler   C-subset compiler
│  └─vm/bpf     BPF virtual machine
├─CSC           Distributed communication stack
│  ├─ccnet      Routing protocol
│  ├─ccrpc      Remote procedure call
│  └─scp        Reliable transport protocol
├─fs            File system
├─lib           Data structures and math utilities
├─mg            Memory management
├─RTOS          Real-time microkernel
├─shell         Interactive shell
├─TcpIp         TCP/IP protocol stack
├─world         namespace
```

其中的软件模块有：

一个一切皆文件的全局命名空间

一个实时操作系统内核

一个TCP/IP协议栈

一个c子集编译器

一个虚拟机，配合上面的编译器实现了类似Linux的eBPF的功能

一个网络路由协议

一个类TCP可靠协议

一个远程调用协议RPC，而且是定死了文件接口

一个文件系统

一个数据结构库，包含链表、栈、队列、红黑树、radix树等

一个内存管理库，包含位图内存管理、内存池、区域内存管理等

一个命令行

一个文本编辑器，类vim极简版本

## 目前可以做到的使用

按这个目录来看，这已经非常豪华了，比很多微型操作系统多太多了，

你可以用RTOS调度任务：

```c
uint32_t task_create(TaskFunction_t task_code,
                     uint16_t stack_depth,
                     void *parameters,
                     uint16_t period,
                     uint16_t deadline,
                     TaskHandle_t *self)
```

像在Linux中写ebpf那样利用ccbpf写map，注册本地函数当脚本编程语言，这就是一个map:

```c
uint32_t native_map_lookup(struct ccbpf_program *p,
                           uint32_t a0,
                           uint32_t a1,
                           uint32_t a2,
                           uint32_t a3)
{
    uint32_t map_id = a0;
    uint32_t key    = a1;

    if (map_id >= p->map_count)
        return 0;

    void *val_ptr = hashmap_get(&p->maps[map_id],
                                (void *)(uintptr_t)key);
    return val_ptr ? (uint32_t)(uintptr_t)val_ptr : 0;
}
```

使用文件系统存文件，在shell里面查看目录，用类似vim的文本编辑器编辑文件内容:

```c
> ls
. .. hello.c bpf
> vim hello.c 
```

使用命令查看全局命名空间，看看有哪些资源，不同的节点的资源都被统一挂载到了全局命名空间中，你会发现这就是一颗树。

```c
> tree
 └── root
    ├── nodeA
    └── nodeB
        └── dev
            ├── led
            └── led1
```

### 不同节点共享组件

A节点可以使用B节点的led灯，只需要一个open命令，B节点的led灯就会亮，close nodeB/dev/led，nodeB的led灯就会灭。

B节点也可以使用A节点的设备，比如说：

B节点想通过shell发消息给PC，但是只有A节点的uart1是直连PC的，那么，A节点可以注册自己的uart1到文件树上：

世界树会变成这样：

```
> tree
 └── root
    ├── nodeA
    |        └── dev
    |             └── uart1
    └── nodeB
        └── dev
            ├── led
            └── led1
```

那么，如何使用呢？

B节点只需要把printf(xxx)，改成：

```c
 mes = "hello";
 open("nodeA/dev/uart1", mes);
```

这段消息就会由节点A的uart1输出，然后打印到PC的终端上。

这就是一切皆文件的设计哲学。



# 组件设计与使用

## 实时操作系统

我们可以使用RTOS调度任务，其中RTOS使用EDF算法，为数不多基于时间的调度算法，与常见的RTOS都不一样。

因为我们是一个分布式操作系统，固定优先级在分布式系统中不成立了，它们只是单机RTOS的一个符号，鬼知道被转发过来的这个1234是什么意思，这些符号对完成任务根本毫无意义。

但时间的语义对于万事万物都是成立的。

一个消息，它被发送到另一台计算机，比如一小段分布式计算的程序，它需要目标计算机创建一个线程完成这个计算任务并且返回结果。

这个消息一定要携带处理的紧急程度，那么，如何表示呢？

时间是衡量万事万物的基础，那么，只有以时间为语义的调度算法才能承担分布式操作系统的核心。

所以，你可以做到：

1.使用我们的脚本编程语言ccbpf写一段计算程序，要求多久时间完成，然后通过分布式协议栈发给其中一个节点

2.节点收到消息，创建或者唤醒一个线程，根据时间计算动态优先级

3.算完后，通过分布式协议栈返回结果

但是，真正有新意的地方在哪里？

其实在于两个地方：

1.动态，具体的模块是ccBPF这个hook编程语言

2.分布式语义，具体的模块是world和CSC分布式通信协议栈



# 动态

我们的动态来自于一个神奇的脚本编程语言组件：ccbpf。

它的灵感来自Linux内核的eBPF，

它的职责是调试器\脚本编程语言\性能分析工具。

## 如何使用ccBPF

### 1.GCC 的 `-finstrument-functions`

这是 GCC 官方提供的 **全函数自动插桩机制**。

开启后，GCC 会在 **每一个函数的开头和结尾** 自动插入：

```
void __cyg_profile_func_enter(void *this_fn, void *call_site);
void __cyg_profile_func_exit(void *this_fn, void *call_site);
```

我们可以在这两个函数里面插上hook点，我们只需要在main函数里面注册hook就行了：

```
__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
}
```

然后编译：

```
gcc -finstrument-functions ...
```

这样就能对 **所有函数** 自动插桩，加上我们的ccbpf需要的hook点，然后，当系统运行后，就可以在任何函数开头和结尾加上一小段bpf程序了。

### 2.ccbpf的cdb组件

cdb组件是我写的组件，实现效果也是一样的，在所有函数开头和结尾自动插上hook信息，不过与第一种不同的是，cdb会自动收集所有hook点，我之所以写cdb，是因为我想实现line级别的hook插入，这样用户可以拿ccbpf当一个调试器用。

目前只实现函数级别的hook，line级别的hook很麻烦，相当于写小半个c语言编译器了。



# 对比

一个是当成嵌入式RTOS来看，另一个是当成plan9这一类unix操作系统来看。

如果你把LTTit当成嵌入式RTOS看，你就会发现，它是一个微内核、语义逐渐开始闭环的完整的操作系统，而不是一堆没有中心的模块：

### 对比嵌入式RTOS

如果拿LTTit当成RTOS看，它是一个功能丰富的OS，不仅有文件系统、shell、RTOS这些，甚至连脚本编程语言和分布式网络协议栈都有，文本编辑器也有。

但是，很显然，它功能不成熟，属于实验性质，商用是不现实的，自然无法与FreeRTOS这些比较。

### 对比类unix

如果拿LTTit当类unix看，你会发现它就是plan9部分思想的mcu版本，甚至可以说同出一源，文件系统也是参考自经典的unix fs。

但很显然，它的缺点在于：语义没有统一，抽象机制不够完善，分布式容错、一致性压根没有实现，设计哲学只能说在逐渐收敛，但目前还比较发散。

# 快速运行LTTit 0.3

# 硬件准备

要运行 lttit 0.3，你只需要三块核心硬件：

- **STM32F103C8T6 最小系统板** 
  作为执行节点（NodeB）
- **Raspberry Pi Pico 2W** 
  作为 Leader 节点（NodeA）
- **USB‑转‑串口模块 CH340** 
  用于 PC ↔ Pico Shell 交互

# 最小连接拓扑

```
PC  <--USB-->  CH340  <--UART0-->  Pico 2W  <--UART1-->  STM32F103C8T6
```

- **PC ↔ CH340**：Shell + 日志
- **CH340 ↔ Pico UART0**：命令输入
- **Pico UART1 ↔ STM32 UART1**：节点通信链路
- 所有设备必须 **共地**

# 引脚连接

## Pico 2W ↔ STM32F103C8T6（UART1）

| Pico                 | STM32          | 说明         |
| -------------------- | -------------- | ------------ |
| **GPIO4 (UART1 TX)** | **PA10 (RX1)** | Pico → STM32 |
| **GPIO5 (UART1 RX)** | **PA9  (TX1)** | STM32 → Pico |
| **GND**              | **GND**        | 共地         |

## PC ↔ Pico（通过 CH340，UART0）

| CH340   | Pico                 | 说明      |
| ------- | -------------------- | --------- |
| **TX**  | **GPIO1 (UART0 RX)** | PC → Pico |
| **RX**  | **GPIO0 (UART0 TX)** | Pico → PC |
| **GND** | **GND**              | 共地      |

# 编译与烧录

## 编译 Pico Leader

推荐环境：Linux发行版，ubuntu即可

```
cd test
mkdir build && cd build
cmake ..
make
```

编译后，把uf2固件拖入到pico 2w的u盘即可。

## 编译 STM32 NodeB

使用clion打开test1工程，烧录进stm32即可。

# 上电与启动

1. 连接所有硬件
2. 打开串口终端（115200）
3. 上电 Pico
4. 看到输出：

```
Pico leader boot ok
```

表示 Leader 节点启动成功。

1. 上电 STM32

此时整个 lttit 系统已经运行。

# 运行你的第一个任务

在 Shell 中输入：

```
tree
```

你将看到一颗世界文件树：

```
└── root
    ├── nodeA
    └── nodeB
        └── dev
            ├── led
            └── led1
```

这意味着：

- world 已初始化
- NodeB 已通过分布式协议栈注册
- RPC 通道已建立

你现在可以执行跨节点 RPC。

## 一切皆文件

所有的资源，所有的设备，所有的节点，都是文件上的一个设备，都可以通过文件接口描述。

A节点可以轻松调用B节点的资源，就像使用自己的一样，B节点也可以这样做，因为它们本来就是同一个系统，共享世界文件树上的资源。

# 跨节点调用与文件树

在 Leader Shell 执行：

```
open root/nodeB/dev/led
```

你将看到的现象：

- NodeB 的 **PC13 LED 立即点亮（常亮）**

然后执行：

```
close root/nodeB/dev/led
```

你将看到的现象：

- NodeB 的 **PC13 LED 熄灭**

这是一个 **稳定的开/关控制**。

文件：`led1`（闪烁一次）

NodeB 中 `led1` 的实现：

在 Leader Shell 执行：

```
open root/nodeB/dev/led1
```

你将看到的现象：

NodeB 的 PC13 LED 会：

1. **亮**（100ms）
2. **灭**（100ms）
3. **再亮一次**（保持亮）

这是一个 **闪烁动作**，用于测试跨节点 RPC 的实时性。

执行：

```
close root/nodeB/dev/led1
```

- NodeB 的 **PC13 LED 熄灭**

# 结语

OK，就这样，愉快玩耍吧！

# 附录

## 资源占用

在pico 2w硬件上，使用python脚本按代码模块统计map文件中的ROM和RAM的占用。

输出：

我对我的代码还是很有自信的，虽然成熟度不够高，但在简洁和语义化这方面，我认为我做得相当不错了。

```
=== LTTIT ROM Usage (from .o) ===
port                388 bytes   接口代码
shell_port           96 bytes
fs_port             476 bytes
mg                 2336 bytes   内存管理
vim                2164 bytes   文本编辑器
shell              4564 bytes
world              1384 bytes
lib                8756 bytes
fs                 6624 bytes   文件系统
scp                6856 bytes   这个类TCP协议也只用了6KB的ROM
ccrpc              3012 bytes   远程调用
ccnet              1612 bytes
ccBPF             22716 bytes   没想到吧，脚本编程语言只用了20KB的ROM
rtos               5948 bytes   实时操作系统内核

=== LTTIT RAM Usage (from .o) ===
port                  0 bytes
shell_port           24 bytes
fs_port            4132 bytes
mg                61484 bytes   这里通用分配器配置了50KB的静态数组，还有10KB是另一个内存分配器配置的静态数组
vim                  24 bytes
shell               584 bytes
world                24 bytes
lib                   0 bytes
fs                 2224 bytes
scp                  36 bytes
ccrpc               293 bytes
ccnet               136 bytes
ccBPF             11100 bytes   这里配置了10KB的虚拟机栈
rtos                118 bytes
```

# 数学与形式化

如果大家比较关注我的专栏的话，就会发现，我最近在读一些论文和经典的数学书籍，比如拥塞控制的非线性动力学分析和随机过程建模。

说实话，这是因为系统已经复杂到一定程度了，我已经有点承受不住了，完全不知道改代码后整个系统会发生什么。

我需要分解整个操作系统的模块，并思考如何让整个系统可分析。

所以，数学到底有什么用呢？

我用我项目中的模块SCP这个类TCP协议来说明吧，我最近打算把它重写为一个马尔科夫链驱动的协议栈。

用一个矩阵来描述所有状态，

## SCP 的离散状态

我把 SCP 的运行模式拆成 6 个状态：

- **S₀：NORMAL** 
  正常发送，按 Reno 规则涨 cwnd。

- **S₁：FAST\_RETRANS** 
  dupACK ≥ 3，进入快速重传 + Fast Recovery。

- **S₂：RTO\_RECOVERY** 
  超时触发，cwnd 被砍到 MSS，RTO 指数退避。

- **S₃：ZERO\_WINDOW** 
  对端窗口为 0，进入 persist 模式。

- **S₄：IDLE** 
  长时间无交互，接近 keepalive / 关闭边缘。

- **S₅：CLOSED** 
  连接关闭（吸收态）。

用一个状态变量表示：

$$
X_t \in \{S_0, S_1, S_2, S_3, S_4, S_5\}
$$

## SCP 的状态转移矩阵

SCP 的行为可以用一个 6×6 的转移矩阵来描述：

$$
P =
\begin{bmatrix}
p_{00} & p_{01} & p_{02} & p_{03} & p_{04} & p_{05} \\
p_{10} & p_{11} & p_{12} & p_{13} & p_{14} & p_{15} \\
p_{20} & p_{21} & p_{22} & p_{23} & p_{24} & p_{25} \\
p_{30} & p_{31} & p_{32} & p_{33} & p_{34} & p_{35} \\
p_{40} & p_{41} & p_{42} & p_{43} & p_{44} & p_{45} \\
0      & 0      & 0      & 0      & 0      & 1
\end{bmatrix}
$$

解释一下：

- 第 \(i\) 行表示当前处于状态 \(S_i\)  
- 第 \(j\) 列表示下一步进入状态 \(S_j\) 的概率  
- 最后一行是吸收态（CLOSED），永远停在那  

比如从 NORMAL（S₀）出发：

- 正常发送继续正常：\(p_{00}\)  
- dupACK ≥ 3 → FAST\_RETRANS：\(p_{01}\)  
- 超时 → RTO\_RECOVERY：\(p_{02}\)  
- 对端窗口变 0 → ZERO\_WINDOW：\(p_{03}\)  
- 长时间无交互 → IDLE：\(p_{04}\)  
- 异常关闭 → CLOSED：\(p_{05}\)

每一行都满足：

$$
\sum_{j=0}^{5} p_{ij} = 1
$$



## 这些概率怎么来的？

不是写死的，而是运行时统计出来的：

- 丢包率估计 \(p\)  
- RTT 抖动  
- 当前 cwnd / flight size  
- 对端窗口变化  
- dupACK 发生频率  
- 超时发生频率  

比如：

- 丢包率高 → \(p_{01}, p_{02}\) 增大  
- 对端窗口经常为 0 → \(p_{03}\) 增大  
- 长时间无交互 → \(p_{04}\) 增大  

最终能得到一个随时间变化的矩阵：

$$
P(t)
$$

这就是 SCP 的行为模型。

换句话说，其中有点机器学习的味道了，哪种情况最可能发生，就切换到这种情况对应的策略。

## 这样做的意义

写成矩阵的话，SCP 就不再是写死逻辑的类 TCP 协议，而是一个可分析、可推导、可优化的马尔科夫链协议栈。

这样就可以：

- 求稳态分布  
  $$
  \pi = \pi P
  $$
- 求期望吞吐量  
- 求超时概率  
- 判断协议是否会振荡  
- 自动调参（根据矩阵优化 cwnd、ssthresh 等）  

最重要的一点是，我可以把各个模块分而治之，每次只关注其中的一小块状态。

而且，配合我们的ccbpf，还可以对这些概率动态调整。

说到底，这是个实验性的项目，我个人做着玩的，自己找点乐子，不过我确实看TCP那一套已经不爽很久了。
