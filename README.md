# LTTit

[中文介绍](./docs/中文/README中文.md)

LTTit is a distributed embedded operating system that enables **runtime migration of compiled programs across heterogeneous bare‑metal microcontrollers**. The system unifies multiple MCUs into a single execution environment where computation can move between nodes without restarting.

# WiKi

LTTit's wiki: [Wiki](https://github.com/skaiui2/lttit/wiki)

## Demo Video

Full demonstration:

https://github.com/skaiui2/lttit/raw/main/video/migrate.mp4

## Demonstration Summary

The demo shows the complete workflow of migrating a running program between two MCUs:

- Writing a program in **ccBPF**, a small C‑subset language
- Compiling the program directly on the MCU into bytecode
- Executing the bytecode inside a lightweight virtual machine
- Capturing execution state at runtime
- Transferring the state across heterogeneous nodes
- Resuming execution on the target MCU without restarting the program

## Core Concept

LTTit uses **ccBPF**, a compact language and VM designed for constrained devices. It provides:

- On‑device compilation
- Virtual machine execution
- Instruction‑level state capture
- Built‑in migration primitives

Migration is part of the execution model itself, not an external mechanism.

## System Components

LTTit consists of the following subsystems:

- **ccBPF** — compiler and virtual machine
- **CSC** — distributed communication stack
- **RTOS** — microkernel runtime
- **FS / MG** — filesystem and memory management
- **TcpIp** — lightweight network stack
- **world** — unified node abstraction model

## Execution Model

Programs run as bytecode inside a VM. At any instruction boundary:

- Registers, stack, and program counter can be serialized
- The state is transferred to another node
- Execution resumes at the exact point of suspension

## Hardware Setup

The demo uses:

- Raspberry Pi Pico 2W (source node)
- STM32F103C8T6 with 20 KB RAM (target node)

## Result

During the demonstration:

- The program produces output on the source MCU
- Execution is suspended at runtime
- The VM state is transferred to the target MCU
- Execution continues seamlessly on the target
- Output remains correct and continuous

## Repository Structure

```
ccBPF   - language and virtual machine
CSC     - distributed communication stack
fs      - filesystem
mg      - memory management
RTOS    - microkernel
shell   - interactive shell
TcpIp   - network stack
world   - unified node abstraction
```

## Current Status

LTTit is a research prototype focused on:

- Execution mobility on constrained devices
- Language‑level distributed execution
- Bare‑metal MCU interoperability
