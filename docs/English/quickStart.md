# **Hardware Preparation**

To run **LTTit**, you only need three core hardware components:

- **STM32F103C8T6 minimum system board**  
   Acts as the execution node (NodeB)
- **Raspberry Pi Pico 2W**  
   Acts as the Leader node (NodeA)
- **USB‑to‑Serial module (CH340)**  
   Used for PC ↔ Pico shell interaction

# Minimal Connection Topology

```
PC  <--USB-->  CH340  <--UART0-->  Pico 2W  <--UART1-->  STM32F103C8T6
```

- **PC ↔ CH340**: Shell + logging
- **CH340 ↔ Pico UART0**: command input
- **Pico UART1 ↔ STM32 UART1**: node communication link
- All devices **must share a common ground**

# **Pin Connections**

## **Pico 2W ↔ STM32F103C8T6 (UART1)**

| Pico                 | STM32          | Description   |
| -------------------- | -------------- | ------------- |
| **GPIO4 (UART1 TX)** | **PA10 (RX1)** | Pico → STM32  |
| **GPIO5 (UART1 RX)** | **PA9  (TX1)** | STM32 → Pico  |
| **GND**              | **GND**        | Common ground |

## **PC ↔ Pico (via CH340, UART0)**

| CH340   | Pico                 | Description   |
| ------- | -------------------- | ------------- |
| **TX**  | **GPIO1 (UART0 RX)** | PC → Pico     |
| **RX**  | **GPIO0 (UART0 TX)** | Pico → PC     |
| **GND** | **GND**              | Common ground |

# **Build & Flash**

## **Build Pico Leader**

Recommended environment: any Linux distribution (Ubuntu is fine)

```
cd test
mkdir build && cd build
cmake ..
make
```

After building, drag the generated **.uf2** firmware onto the Pico 2W USB drive.

## **Build STM32 NodeB**

Open the **test1** project in CLion and flash it to the STM32.

# **Power‑On & Startup**

1. Connect all hardware
2. Open a serial terminal (115200 baud)
3. Power on the Pico
4. You should see:

```
Pico leader boot ok
```

This indicates the Leader node has started successfully.

1. Power on the STM32

At this point, the entire **lttit** system is running.

# **Run Your First Task**

In the shell, enter:

```
tree
```

You will see a world file tree:

```
└── root
    ├── nodeA
    └── nodeB
        └── dev
            ├── led
            └── led1
```

This means:

- The world has been initialized
- NodeB has registered through the distributed protocol stack
- The RPC channel is established

You can now perform cross‑node RPC.

# **Everything Is a File**

All resources, all devices, all nodes are represented as files.
 Every device is exposed through a file interface.

Node A can access Node B’s resources as if they were local, and vice versa, because they are part of the same system and share the same world file tree.

# **Cross‑Node Calls & File Tree**

In the Leader shell, run:

```
open root/nodeB/dev/led
```

You will observe:

- NodeB’s **PC13 LED turns on (steady)**

Then run:

```
close root/nodeB/dev/led
```

You will observe:

- NodeB’s **PC13 LED turns off**

This is a **stable on/off control**.

## **Device: `led1` (blink once)**

Implementation on NodeB:

In the Leader shell:

```
open root/nodeB/dev/led1
```

You will observe NodeB’s PC13 LED:

1. **Turns on** (100 ms)
2. **Turns off** (100 ms)
3. **Turns on again** (stays on)

This is a **blink action**, used to test cross‑node RPC timing.

Run:

```
close root/nodeB/dev/led1
```

- NodeB’s **PC13 LED turns off**

