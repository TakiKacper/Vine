#include "vine/vine.hpp"

#include <queue>
#include <vector>
#include <unordered_map>

#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

#include <cstdlib>

/*
    Macros
*/

#define VINE_MAX_THREADS 2

#ifndef VINE_MAX_THREADS
    #define VINE_MAX_THREADS 1e16
#endif

/*
    State
*/

namespace {
    std::mutex           state_mutex;
    const vine::machine* current_machine = nullptr;
    const vine::machine* queued_machine  = nullptr;
    bool                 should_shutdown = false;
}

vine::default_machine_link::default_machine_link(const machine& m) {
    set_machine(m);
}

void vine::set_machine(const machine& m) {
    std::lock_guard<std::mutex> lock{state_mutex};
    queued_machine = &m;
}

void vine::request_shutdown() {
    std::lock_guard<std::mutex> lock{state_mutex};
    should_shutdown = true;
}

static void apply_machine() {
    if (queued_machine != current_machine) {
        std::lock_guard<std::mutex> lock{state_mutex};
        current_machine = queued_machine;
    }
}

/*
    Objects Implementation
*/

namespace {
    template<class node_object>
    struct executable_graph_node {
        node_object         object;
        std::vector<size_t> dependant;
        size_t              depedencies;
    };

    template<class node_object>
    struct executable_graph {
        std::vector<executable_graph_node<node_object>> nodes;
        std::vector<size_t>                             independant; //ids of nodes with depedencies == 0
    };

    std::unordered_map<
        const vine::machine*, 
        executable_graph<const vine::stage*>
    > machines_reg;

    std::unordered_map<
        const vine::stage*,
        executable_graph<vine::func>        
    > stages_reg;

    std::unordered_map<const void*, size_t> link_object_to_graph_id;
}

static executable_graph<const vine::stage*>& get_machine_impl(const vine::machine& m) {
    auto itr = machines_reg.find(&m);
    if (itr == machines_reg.end()) itr = machines_reg.insert({&m, {}}).first;
    return itr->second;
}

static executable_graph<vine::func>& get_stage_impl(const vine::stage& m) {
    auto itr = stages_reg.find(&m);
    if (itr == stages_reg.end()) itr = stages_reg.insert({&m, {}}).first;
    return itr->second;
}

/*
    Graph Operations
*/

template<class node_object, class link_object_class>
size_t get_node_id(executable_graph<node_object>& graph, const link_object_class* link) {
    auto itr = link_object_to_graph_id.find(link);
    if (itr != link_object_to_graph_id.end()) return itr->second;

    graph.nodes.push_back({});
    auto id = graph.nodes.size() - 1;

    link_object_to_graph_id.insert({link, id});
    
    return id;
}

template<class node_object, class link_object>
void link_node(
    const link_object*                               node_link_object_ptr,
    executable_graph<node_object>&                   graph,
    const node_object&                               node_obj,
    const std::initializer_list<const link_object*>& depedencies
) {
    auto  this_node_id = get_node_id(graph, node_link_object_ptr);  
    auto& this_node    = graph.nodes[this_node_id];

    this_node.object            = node_obj;
    this_node.depedencies = depedencies.size();

    for (auto& dep_link : depedencies) {
        auto  dep_id   = get_node_id(graph, dep_link);
        auto& dep_node = graph.nodes[dep_id];

        dep_node.dependant.push_back(this_node_id);
    }
}

/*
    Linking
*/

vine::stage_machine_link::stage_machine_link(
    const stage& stage, const machine& target, const std::initializer_list<const stage_machine_link*>& depedencies
) {
    auto& graph = get_machine_impl(target);

    link_node(
        this, 
        graph,
        &stage,
        depedencies
    );
};

vine::func_stage_link::func_stage_link(
    const func func, const stage& target, const std::initializer_list<const func_stage_link*>& depedencies
) {
    auto& graph = get_stage_impl(target);

    link_node(
        this, 
        graph,
        func,
        depedencies
    );
}

void find_independants() {
    for (auto& pair : machines_reg) {
        auto& graph = pair.second;
        for (size_t i = 0; i < graph.nodes.size(); i++) {
            if (graph.nodes[i].depedencies) continue;
            graph.independant.push_back(i);
        }
    }

    for (auto& pair : stages_reg) {
        auto& graph = pair.second;
        for (size_t i = 0; i < graph.nodes.size(); i++) {
            if (graph.nodes[i].depedencies) continue;
            graph.independant.push_back(i);
        }
    }
}

/*
    Thread Pool
*/

static void thread_worker_loop();

namespace {
    bool                     threads_should_terminate = false;
    std::vector<std::thread> thread_pool;
}

static size_t get_thread_pool_size() {
    size_t hc = std::thread::hardware_concurrency();
    if (hc > VINE_MAX_THREADS) hc = VINE_MAX_THREADS;
    return hc;
}

static void alloc_thread_pool(size_t size) {
    threads_should_terminate = false;
    for (size_t i = 0; i < size; i++) 
        thread_pool.push_back(std::thread{thread_worker_loop});
}

namespace {
    extern std::condition_variable queues_update_cv;
}

static void free_thread_pool() {
    threads_should_terminate = true;
    queues_update_cv.notify_all();
    for (auto& t : thread_pool) t.join();
    thread_pool.clear();
}

/*
    Execution Queues
*/

namespace {
    struct func_node_locant {
        size_t stage_node_id;
        size_t func_node_id;
    };

    struct task_enqueued {
        vine::task_promise promise;
        vine::task         task_func;
        std::any            arg;
    };

    std::mutex                       queues_mutex;
    std::condition_variable          queues_update_cv;
    std::condition_variable          machine_completed_cv;

    //all of those are sync under queues_mutex

    std::queue<func_node_locant>     funcs_queue;
    std::queue<task_enqueued>        tasks_queue;

    std::vector<size_t>              stages_depedencies_conters;
    std::vector<size_t>              funcs_procesed_counters;
    std::vector<std::vector<size_t>> funcs_depedencies_conters;

    std::atomic<size_t>              threads_working_on_machine = 0;
}

/*
    Tasks
*/

struct vine::task_promise::implementation {
    std::atomic<size_t>     promises;
    std::atomic<bool>       completed;
    std::condition_variable condition;
    std::mutex              mutex;
};

vine::task_promise vine::issue_task(task task, std::any arg) {
    vine::task_promise tp;
    tp.impl = new vine::task_promise::implementation;

    tp.impl->promises  = 1;
    tp.impl->completed = false;

    task_enqueued te;
    te.promise   = tp;
    te.task_func = task;
    te.arg       = std::move(arg);

    std::lock_guard<std::mutex> lock{queues_mutex};
    tasks_queue.push(std::move(te));
    queues_update_cv.notify_one();

    return tp;
}

vine::task_promise::~task_promise() {
    if (impl && impl->promises.fetch_sub(1, std::memory_order_acq_rel) == 1) delete impl;
    impl = nullptr;
}

vine::task_promise::task_promise(const task_promise& other) {
    impl = other.impl;
    if (impl) impl->promises.fetch_add(1, std::memory_order_acq_rel);
}

vine::task_promise& vine::task_promise::operator=(const task_promise& other) {
    if (impl != other.impl) {
        if (impl && impl->promises.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete impl;
        }
        impl = other.impl;
        if (impl) impl->promises.fetch_add(1, std::memory_order_acq_rel);
    }
    return *this; 
}

bool vine::task_promise::completed() {
    if (!impl) return true;
    return impl->completed;
}

void vine::task_promise::join() {
    if (impl == nullptr || impl->completed) return;
    std::unique_lock<std::mutex> lock{impl->mutex};
    impl->condition.wait(lock, [&]{return impl->completed.load();});
}

/*
    Execution
*/

static void thread_worker_handle_node(func_node_locant& fnl) {
    auto& machine_graph = get_machine_impl(*current_machine);
    auto& stage_node    = machine_graph.nodes[fnl.stage_node_id];
    auto& stage_graph   = get_stage_impl(*stage_node.object);
    auto& func_node     = stage_graph.nodes[fnl.func_node_id];

    //execute func
    auto func = func_node.object;
    func();

    funcs_procesed_counters[fnl.stage_node_id]--;

    //invoke next stage's functions
    if (func_node.dependant.size() != 0) {
        auto& dep_count_vec = funcs_depedencies_conters[fnl.stage_node_id];
        std::lock_guard<std::mutex> lock(queues_mutex);

        for (auto& dep_id : func_node.dependant) {
            auto& count = dep_count_vec.at(dep_id);
            count--;

            if (count != 0) continue;

            funcs_queue.push({fnl.stage_node_id, dep_id});
            funcs_procesed_counters[fnl.stage_node_id]++;
            queues_update_cv.notify_one();
        }
    }
    //check if can invoke next stages since node has no dependants
    else if (funcs_procesed_counters[fnl.stage_node_id] == 0) {
        for (auto& dep_stage_node_id : stage_node.dependant) {
            auto& count = stages_depedencies_conters[dep_stage_node_id];
            count--;

            if (count != 0) continue;

            auto& dep_stage_node  = *machine_graph.nodes[dep_stage_node_id].object;
            auto& dep_stage_graph = get_stage_impl(dep_stage_node);

            for (auto& indpendant_func_node_id : dep_stage_graph.independant) {
                funcs_queue.push({dep_stage_node_id, indpendant_func_node_id});
                queues_update_cv.notify_one();
            }
        }
    }
}

static void thread_worker_handle_task(task_enqueued& e) {
    e.task_func(std::move(e.arg));
        
    std::lock_guard lock(e.promise.impl->mutex);
    e.promise.impl->completed.store(true);
    e.promise.impl->condition.notify_all();
}

static void thread_worker_loop() {
    //Todo: Exceptions

    while (!threads_should_terminate) {
        std::unique_lock lock(queues_mutex);

        bool should_work = threads_should_terminate ||
                        !funcs_queue.empty()        ||
                        !tasks_queue.empty();

        if (!should_work) {
            if (funcs_queue.empty() && !threads_working_on_machine) 
                machine_completed_cv.notify_all();

            queues_update_cv.wait(lock);
            continue;
        }

        if (threads_should_terminate) break;

        if (!funcs_queue.empty()) {
            threads_working_on_machine++;

            auto fnl = std::move(funcs_queue.front());
            funcs_queue.pop();

            lock.~unique_lock();
            thread_worker_handle_node(fnl);

            threads_working_on_machine--;
        }
        else if (!tasks_queue.empty()) {
            auto te = std::move(tasks_queue.front());
            tasks_queue.pop();
            
            lock.~unique_lock();
            thread_worker_handle_task(te);
        }
        else continue;
    }
}

static void execute_current_machine() {
    auto& machine_graph = get_machine_impl(*current_machine);
    auto  stages_amount = machine_graph.nodes.size();

    // Alloc Counters Space (no need for mutex lock, since no machine is processed by workers)

    stages_depedencies_conters.clear();

    for (auto& x : funcs_depedencies_conters) 
        x.clear();

    if (funcs_depedencies_conters.size() < stages_amount)
        funcs_depedencies_conters.resize(stages_amount);

    if (funcs_procesed_counters.size() < stages_amount)
        funcs_procesed_counters.resize(stages_amount);

    //Push First Nodes

    std::unique_lock lock(queues_mutex);

    for (size_t stage_node_id = 0; stage_node_id < stages_amount; stage_node_id++) {
        auto& stage_node = machine_graph.nodes[stage_node_id];

        stages_depedencies_conters.push_back(stage_node.depedencies);

        auto& stage_graph = get_stage_impl(*stage_node.object);
        auto& target_vec = funcs_depedencies_conters[stage_node_id];

        for (size_t func_node_id = 0; func_node_id < stage_graph.nodes.size(); func_node_id++) {
            auto& func_node = stage_graph.nodes[func_node_id];
            target_vec.push_back(func_node.depedencies);

            if (func_node.depedencies != 0 || stage_node.depedencies != 0) continue;
                
            //node has no depedencies; push it
            funcs_queue.push({stage_node_id, func_node_id});
            funcs_procesed_counters[stage_node_id]++;
        }
    }

    queues_update_cv.notify_all();
    machine_completed_cv.wait(lock);
}

int main() {
    link_object_to_graph_id.clear();

    apply_machine();
    if (!current_machine) abort();  //no default machine provided

    find_independants();

    auto threads = get_thread_pool_size();
    alloc_thread_pool(threads);

    while (!should_shutdown) {
        execute_current_machine();
        apply_machine();
    }

    free_thread_pool();
}
