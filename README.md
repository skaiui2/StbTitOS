## LTTit

[中文介绍](./docs/中文/README中文.md)
LTTit is a distributed runtime execution framework that allows many independent nodes to operate as a single machine.

It provides a unified execution abstraction for multi‑node systems, enabling the entire cluster to behave as a single‑system image.

LTTit is not an RTOS — it is a distributed runtime system.
RTOSes solve single‑node problems; LTTit solves system‑wide problems.

LTTit is a higher‑level distributed execution environment that provides:

Unified namespace: all node resources exist within the same global tree

Unified access semantics: accessing remote resources is identical to accessing local ones

Unified execution model: programs can be loaded, executed, migrated, and resumed on any node

Unified communication framework: nodes cooperate through routing, RPC, and reliable transport

The goal of LTTit is not to manage hardware, but to unify the execution logic of the entire system.

# Everything Is a Node
All resources, all devices, and all execution units are represented as nodes.

Every component in the system is modeled as a node and can be described and accessed through a unified node interface.

Node A can seamlessly access the resources of Node B as if they were its own, and Node B can do the same.

This is because they are fundamentally part of the same system, sharing the same global node tree.

# Namespace for the Unified View

In LTTit, the `tree` command prints the nodes of a prefix tree; this tree is the namespace for the unified view.

```
> tree
└── root
    ├── nodeA
    │   └── mem
    │       └── region1
    └── nodeB
        └── dev
            ├── led
            └── led1
```

# Quick Start
LTTit has no platform requirements.
It can run on microcontrollers or in Linux userspace, but MCU clusters are an excellent experimental environment.

I develop LTTit on multiple MCUs, but you only need two boards to run the LTTit demo.

[Quick Start](./docs/English/quickStart.md): how to run LTTit quickly.

# Demo Video

The full system demonstration is available here:

[Demo Video](./video/migrate.mp4)

The video shows:
- Writing ccBPF program
- On-device compilation
- Execution on Pico 2W
- Runtime migration to STM32F103
- Continued execution after migration

This demo is fully reproducible on two MCU boards without any host OS dependency.
