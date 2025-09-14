# Vine 🌱  
*A minimalistic C++17+ graph-based framework for creating applications*  

Vine lets you design your program as a **graph of functionalities** with explicit dependencies.  
Instead of juggling threads, locks, and synchronization headaches, you focus only on **what needs to happen** and **what depends on what**.  
Vine takes care of the rest — running everything as early and as parallel as possible.  

---

## Why use Vine? 🚀

Multithreading is hard. Get it wrong, and your threads just sit around twiddling their thumbs, waiting for each other.  
Get it *really* wrong, and you’re stuck in deadlocks and spaghetti sync code.  

Vine fixes this by:  
- Treating your program like a **dependency graph** (stages and machines).  
- Running everything in **parallel** when possible.  
- Giving you a **task system** for async one-offs (asset loading, I/O, background jobs) without blocking your main flow.  

In short: you describe *what depends on what*, and Vine figures out *when* and *where* things run.  

**What you get:**  
* ⚡ **Less boilerplate** – declare dependencies once, Vine handles scheduling  
* 🏎 **Better performance** – no wasted CPU cycles, no idle waiting  
* 🛡 **Safer design** – less chance of deadlocks or sync chaos  
* 🌱 **Scalable architecture** – stages & machines make extending your program painless  
* 🤹 **Parallel execution** – Vine squeezes out every drop of concurrency it can  
* 📦 **Task system** – simple async execution for longer or rare jobs  

---

## Table of Contents
- [Why use Vine](#why-use-vine-🚀)
- [Concepts](#concepts)
  - [Stage](#stage-🎭)
  - [Machine](#machine-🏭)
  - [Executing Machines](#executing-machines-▶️)
  - [Program Shutdown](#program-shutdown-🛑)
  - [Tasks](#tasks-🧵)
- [Building](#building-🛠)
- [Hello World Example](#hello-world-example-🌍)

---

## Concepts

### Stage 🎭
A **stage** is a graph of functions. Each function may depend on other functions.  
When a stage runs, Vine walks the graph and executes everything in the right order, parallelizing when possible.  

```cpp
vine::stage rendering;

void upload() { /* ... */ }
void setup()  { /* ... */ }
void render() { /* ... */ }

vine::func_stage_link upload_link(
    upload, 
    rendering, 
    {} //no dependencies
);

vine::func_stage_link setup_link(
    setup, 
    rendering, 
    {}
);

vine::func_stage_link render_link(
    render, 
    rendering, 
    { &upload_link, &setup_link } // depends on upload and setup
);
```

Execution order:  

```
 upload - \
           \
           render
           /
 setup ---/
```

---

### Machine 🏭
A **machine** is one level up — instead of functions, it’s a graph of stages.  
This is the top abstraction in Vine.  

```cpp
extern vine::stage 
    game_logic, 
    physic, 
    networking;

extern vine::stage 
    game_logic_networking_sync, 
    game_logic_physics_sync;

vine::machine update;

vine::stage_machine_link game_logic_link(
    game_logic, 
    update, 
    {} //no dependencies
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
    { &game_logic_link, &networking_link } //depends on game logic and networking
);

vine::stage_machine_link game_logic_physics_sync_link(
    game_logic_physics_sync, 
    update, 
    { &game_logic_link, &physic_link } //depends on game logic and physic
);
```

Flow looks like:  

```
 physic -----------> game_logic_networking_sync
                    /
                   /
 game_logic -------
                   \
                    \
 networking -------> game_logic_physics_sync
```

---

### Executing Machines ▶️

Pick an initial machine:  

```cpp
extern vine::machine initial_machine;
vine::default_machine_link default_machine_link(initial_machine);
```

Swap to another machine mid-execution:  

```cpp
vine::set_machine(new_machine);
```

Using above you can build state machines on top of vine's machines:

```
init_machine
    |
    \/
game_loop_machine --> loading_screen_machine
    |             <--
    \/
termination_machine
```

---

### Program Shutdown 🛑

End a Vine program cleanly:

```cpp
vine::request_shutdown();
```
This function waits current machine execution finish, and then kill the program.

---

### Tasks 🧵

Tasks let you run **background jobs** without blocking your main machine.  

```cpp
void my_task(std::any arg) {
    std::cout << std::any_cast<int>(arg);
}

void some_sync_code() {
    vine::task_promise promise = vine::issue_task(
        my_task, 
        {128}
    );

    std::cout << promise.completed() << '\n';
    promise.join();
    std::cout << promise.completed() << '\n'; // true
}
```

Tasks use idling worker threads and never block your machine execution.  
`vine::task_promise` lets you check or wait for completion.  

---

## Building 🛠

1. Include Vine:  

```cpp
#include "vine/vine.hpp"
```

2. Compile all files in `source/`  
3. Requires **C++17** or newer  

Optional compile flag:  
* `VINE_MAX_THREADS=[number]` – max number of worker threads  

---

## Hello World Example 🌍

```cpp
#include "vine/vine.hpp"
#include <iostream>

vine::stage stage1;

void hello() { std::cout << "Hello"; }
void world() { std::cout << " World!"; vine::request_shutdown(); }

vine::func_stage_link hello_link(hello, stage1, {});
vine::func_stage_link world_link(world, stage1, { &hello_link });

vine::machine machine1;
vine::stage_machine_link example_stage_link(stage1, machine1, {});
vine::default_machine_link default_link(machine1);

// Output: "Hello World!"
```
