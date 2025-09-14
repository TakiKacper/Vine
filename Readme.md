# Vine 🌱
*A minimalistic C++ (17 >=) graph based framework for creating applications*

Vine treats program as a graph of functionalities - with order dependencies between them.

# Why use Vine?

When writing a multithreaded program, often the programmer has to synchronise threads, by making one wait for another.
If designed poorly, threads may be regularly spending time idle waiting for each other.

Vine solves this design problem by implementing your program as a graph of functionalities, with dependencies between them.
Instead of you manually orchestrating when functions or threads are run, Vine analyzes the dependency graph and ensures everything runs as soon as it can — and in parallel when possible.

Vine also provides a task system, which you can use to offload occasional or long-running jobs (such as asset loading, file I/O, or background computations) without blocking the main execution flow.

This approach gives you:
* **Less boilerplate** – you only declare dependencies between tasks, Vine handles the scheduling
* **Better performance** – no wasted CPU cycles on idle threads
* **Safer design** – you avoid deadlocks and tangled synchronization logic
* **Scalable architecture** – easily extend your program with new stages or machines without rewriting the execution flow
* **Parallel Execution** - if something can be done in parallel, vine will do it in parallel to improve performance
* **Task System** - Easy async execution for longer jobs

## Table of Contents

- [Introduction](#Vine)
- [Why use Vine](#why-use-vine)
- [Concepts](#concepts)
  - [Stage](#stage)
  - [Machine](#machine)
  - [Executing Machines](#executing-machines)
  - [Program Shutdown](#program-shutdown)
  - [Tasks](#tasks)
- [Building](#building)
- [Hello World Example](#hello-world-example)

## Concepts
### Stage
A stage is a graph of functions. Each function in the stage may depend on other functions.
When a stage is executed, the program walks on it's graph and execute all of it's functions with respect to their dependencies.

```cpp
vine::stage rendering;

void upload() { /* ... */ }
void setup()  { /* ... */ }
void render() { /* ... */ }

vine::func_stage_link upload_link(
    upload, 
    rendering, 
    {}
);

vine::func_stage_link setup_link(
    setup, 
    rendering, 
    {}
);

vine::func_stage_link render_link(
    render, 
    rendering, 
    { &upload_link, &setup_link }
);
```

Example above shows simple implementation of apps' rendering

First we create ``stage`` *rendering* - it will hold all the logic related to rendering  

Then we link our render logic to the stage using ``func_stage_link``s

Note *upload*'s and *setup*'s links have no dependencies, therefore they are being executed right-away

The *render_link* of *render* function though have dependencies - both *upload* and *setup*. Therefore *render* will be executed after *upload* and *setup*.

``stage`` *rendering*:
```
 upload ---\
           \/
           render
           /\
 setup ----/
```

### Machine
``Machine`` is a level up from ``stage`` - instead of being a graph of functions it is a graph of ``stage``s. Machine is the top abstraction over the execution in vine.

```cpp
extern vine::stage game_logic;
extern vine::stage physic;
extern vine::stage networking;

extern vine::stage game_logic_networking_sync;
extern vine::stage game_logic_physics_sync;

vine::machine update;

vine::stage_machine_link game_logic_link(
    game_logic,
    update,
    {}
);

vine::stage_machine_link physic_link(
    physic,
    update,
    {}
);

vine::stage_machine_link networking_link(
    networking,
    update,
    {}
);

vine::stage_machine_link game_logic_networking_sync_link(
    game_logic_networking_sync,
    update,
    {}
);

vine::stage_machine_link game_logic_physics_sync_link(
    game_logic_physics_sync,
    update,
    {}
);
```

In example above we create ``machine`` *update*. It consists of three independent nodes - game_logic, physic and networking ``stages``. 

Those three works in parallel, and their results are then merged with two, relatively independent ``stage``s:   *game_logic_networking_sync* and *game_logic_physics_sync*.

``machine`` *update*:
```
 physic -----------> game_logic_networking_sync
                   /\
                   /
 game_logic -------
                   \
                   \/
 networking -------> game_logic_physics_sync
```

### Executing Machines

You need to specify the initial machine using ``default_machine_link``.

```cpp
extern vine::machine initial_machine;

vine::default_machine_link default_machine_link(
    initial_machine
);
```

Given machine will be executed on loop, unless it is changed with

```cpp
void vine::set_machine(const machine&);
```

If this function is called during machine execution, instead of looping to the beginning, the program will begin to execute the newly set machine on loop.

Given you can create flow machine on between machines:

```
init_machine
    |
    \/
game_loop_machine --> loading_screen_machine
    |             <--
    \/
termination_machine
```

Via such design you can decouple a lot of state from the stage's functions.

### Program Shutdown

To end a vine program call

```cpp
void vine::request_shutdown();
```

### Tasks

Tasks can be used to handle rare jobs like assets loading etc.

```cpp
void my_task(std::any arg) {
    std::cout << std::any_cast<int>(arg);
}

//somewhere in code
vine::task_promise promise = vine::issue_task(my_task, {128});

std::cout << promise.completed() << '\n';

promise.join();

std::cout << promise.completed() << '\n';
```

Tasks are executed whenever there is a free thread, not executing machine's functions.
Therefore tasks are secondary to the normal execution flow.

``vine::task_promise`` can be used to synchronise with task state - check if it's completed, or wait is's completion.

## Building

To use vine, include it's header: ``vine/vine.hpp`` from ``include`` folder.  

You must also compile all files in ``source`` folder
Vine requires c++ 17.

Compile flags:

* **VINE_MAX_THREADS [number]** - max number of thread workers

## Hello World Example

```cpp
#include "vine/vine.hpp"
#include <iostream>

// Define a stage
vine::stage stage1;

// Functions
void hello() {
    std::cout << "Hello";
}

void world() {
    std::cout << " World!";
    vine::request_shutdown();
}

// Link functions to the stage
vine::func_stage_link hello_link(
    hello,
    stage1,
    {} // no dependencies
);

vine::func_stage_link world_link(
    world,
    stage1,
    { &hello_link } // depends on hello
);

// Define a machine
vine::machine machine1;

// Link stage to machine
vine::stage_machine_link example_stage_link(
    stage1,
    machine1,
    {}
);

// Set default machine
vine::default_machine_link default_link(machine1);

//output: "Hello World!"
```
