#pragma once

#include <initializer_list>
#include <any>

#define DELETE_MOVE_COPY(class_name)                    \
    class_name(const class_name&)            = delete;  \
    class_name& operator=(const class_name&) = delete;  \
                                                        \
    class_name(class_name&& oth)            = delete;   \
    class_name& operator=(class_name&& oth) = delete;

//=================
// Compile Flags

//VINE_MAX_THREADS - max number of thread workers
    
//=================
// Stage

namespace vine {
    using func = void(*)();

    // declare variable of this type in global scope to create a new stage
    struct stage {
        stage(){};
        DELETE_MOVE_COPY(stage)
    };

    // declare variable of this type in global scope to link function to the target stage
    // use other links to specify function depedencies
    struct func_stage_link {
        func_stage_link(
            func func, 
            const stage& target, 
            const std::initializer_list<const func_stage_link*>& depedencies
        );
        DELETE_MOVE_COPY(func_stage_link)
    };
};

//=================
// Machine

namespace vine {
    // declare variable of this type in global scope to create a new machine
    struct machine {
        machine(){};
        DELETE_MOVE_COPY(machine)
    };

    // declare variable of this type in global scope to link stage to the target machine
    // use other links to specify stage depedencies
    struct stage_machine_link {
        stage_machine_link(
            const stage& stage, 
            const machine& target, 
            const std::initializer_list<const stage_machine_link*>& depedencies
        );
        DELETE_MOVE_COPY(stage_machine_link);
    };
}

//=================
// Tasks

namespace vine {
    using task = void(*)(std::any);

    struct task_promise {
        struct implementation;
        implementation* impl;

        task_promise() { impl = nullptr; };
        ~task_promise();

        task_promise(const task_promise&);
        task_promise& operator=(const task_promise& other);
    
        bool completed(); //whether task completed execution
        void join();      //wait task completion                  todo forbid joins on other tasks
    };

    // use to push the task onto the execution queue
    task_promise issue_task(task task, std::any arg);
};

//=================
// State

namespace vine {
    // declare variable of this type in global scope to specify program deafult machine
    // should be used once per program
    struct default_machine_link {
        default_machine_link(
            const machine& machine
        );
    };

    // sets machine to be executed after the current finishes
    void set_machine(const machine&);

    // request program shutdown
    void request_shutdown();
}

#undef DELETE_MOVE_COPY
