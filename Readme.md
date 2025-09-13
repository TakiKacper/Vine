# Vine 🌱
*A minimalistic C++ framework for creating applications, by linking functions into an execution graph*

Vine treats program as graph of functionalisites - with order depedencies between them.

## Features
* **Parrel Execution** - if something can be done in parrel, vine will do it in parrel to improve performance
* **Easily Scalable** - implement your funtionality in function, specify depedenices and you're done!
* **Task System** - Easy async execution for longer jobs

## Concepts
### Stage
A stage is a graph of functions. Each function in the stage may depend on other functions.
When a stage is executed, the program walks on it's graph and execute all of it's functions with respect to their depedencies.

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

First we create ``stage`` *rendering* - it will holds all of the logic related to rendering  

Then we link out render logic to the stage using ``func_stage_link``s

Note *upload*'s and *setup*'s links have no dependencies, therefore they are being executed right-away

The *render_link* of *render* function though have depedencies - both *upload* and *setup*. Therefore *render* will be executed after *upload* and *setup*.

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

In example above we create ``machine`` *update*. It consists of three independant nodes - game_logic, physic and networking ``stages``. 

Those three works in parrel, and their results are then merged with two, relativly independant ``stage``s:   *game_logic_networking_sync* and *game_logic_physics_sync*.

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

If this function is called during machine execution, instead of looping to the begining, the program will begin to execute the newly set machine on loop.

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

## Building

To use vine, include it's header: ``vine/vine.hpp`` from ``include`` folder.  

You must also compile all files in ``source`` folder

Compile flags:

* **VINE_MAX_THREADS [number]** - max number of thread workers
