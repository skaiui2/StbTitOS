## **lttit**

[中文介绍](./docs/中文/README中文.md)

**I want to use one hundred microcontrollers as if they were one.**   This is the purpose of lttit.

lttit is a dynamically programmable distributed operating system designed to make multiple MCU nodes behave like a single machine. It is built on the idea that *local* and *remote* are only differences in space and time—not in nature.

*lttit* is an experimental framework designed to become a **“Tit Cluster Operating System”**—a distributed operating system inspired by the coordinated behavior of bird flocks. It is built around two core capabilities:

1. **Multiple nodes collaborate as a unified system**
2. **Nodes can dynamically modify their logic at runtime**

## **System Components**

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
├─world         world
```

lttit consists of several independently portable modules. The foundational modules include:

- **RTOS Kernel** 
   Dual schedulers (RT), synchronization primitives, timers
- **File System (FS)** 
   Lightweight Unix‑like filesystem
- **Memory Management (mg)** 
   Multiple allocators optimized for MCUs
- **Data Structure Library (lib)** 
   Red‑black trees, radix trees, hash tables, linked lists, etc.
- **Network Protocol Stack (TCP/IP)** 
   Simplified TCP/IP implementation
- **Shell & Text Editor** 
   Interactive command‑line shell and lightweight editor

### **CSC: Distributed Protocol Stack**

CSC enables multiple MCU nodes to behave like **one machine**, providing:

- **Routing (CCNET)**
- **Reliable Transport (SCP)**
- **Remote Procedure Calls (CCRPC)**

### **ccBPF: Dynamic Programming Engine**

ccBPF is a C‑subset compiler paired with a BPF virtual machine, like eBPF for linux.
 It enables nodes to **modify their behavior at runtime**, supporting dynamic updates to:

- Scheduling policies
- Firewall rules
- Flow‑control algorithms
- Any other node‑level logic

In short:

- **CSC** makes nodes form a unified whole
- **ccBPF** lets that whole **change itself dynamically**

## **Design Overview**

**Everything is a file.** 
 This is the core abstraction of lttit.

All resources—local or remote, CPU or device, memory or sensor—are exposed as files in a global namespace.
 Once everything becomes a file, multiple microcontrollers can be organized using the same mechanism:

- Each node registers its devices as files
- The leader maintains a global world tree
- All nodes access all resources through the same file interface
- Remote operations become indistinguishable from local operations

With this model:

- Node A can read `/nodeB/net0` as if it were local
- Node B can write `/nodeA/fs/log` as if it were local
- Any node can call any service simply by opening and writing a file
- The entire cluster forms a single system image

**One abstraction → one namespace → one machine.**

This is how lttit organizes many MCUs into a unified distributed operating system.

## **Maintenance Notice**

lttit is still in an early stage, and I want to clarify the maintenance scope.

I currently only plan to actively maintain the **core of the operating system**, which includes:

- **RTOS** (kernel, EDF scheduling, IPC, timers)
- **ccBPF** (compiler + VM)
- **cluster** (global node tree)
- **vfs** (virtual file system)
- **CSC** (ccnet / scp / ccrpc)
- **lib** (data structures, algorithms)
- **mg** (memory management)

## **lttit Documentation Guide**

### **1. User Guides**

- **Quick Start** — How to run lttit quickly
- **System Overview** — High‑level architecture of the system

### **2. Porting & Usage Manuals**

For engineers integrating or porting individual components.

### **3. Design Documents**

For developers who want to understand the internal architecture and design principles of each subsystem.
