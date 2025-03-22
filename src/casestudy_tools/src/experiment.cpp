#include "casestudy_tools/experiment.hpp"
#include "casestudy_tools/primes_workload.hpp"
#include "casestudy_tools/test_nodes.hpp"
#include "priority_executor/multithread_priority_executor.hpp"
#include "priority_executor/priority_executor.hpp"
#include "priority_executor/priority_memory_strategy.hpp"

#include <cstddef>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <sys/prctl.h>
#include <vector>

Experiment::Experiment(ExperimentConfig config) : config(config) {
  _logger = create_logger();
}

void sanity_check_config(ExperimentConfig config) {
  // make sure chain_lengths and node_ids are the same size
  if (config.chain_lengths.size() != config.node_ids.size()) {
    std::cout << "ros_experiment: chain_lengths.size()= "
              << config.chain_lengths.size()
              << " != node_ids.size()= " << config.node_ids.size() << std::endl;
    exit(1);
  }
  // make sure each chain_lengths is the same size as the corresponding node_ids
  for (uint32_t i = 0; i < config.chain_lengths.size(); i++) {
    if (config.chain_lengths[i] != config.node_ids[i].size()) {
      std::cout << "ros_experiment: chain_lengths[" << i
                << "]= " << config.chain_lengths[i] << " != node_ids[" << i
                << "].size()= " << config.node_ids[i].size() << std::endl;
      exit(1);
    }
  }
  // make sure chain_timer_control is the same size as chain_lengths
  if (config.chain_timer_control.size() != config.chain_lengths.size()) {
    std::cout << "ros_experiment: chain_timer_control.size()= "
              << config.chain_timer_control.size()
              << " != chain_lengths.size()= " << config.chain_lengths.size()
              << std::endl;
    exit(1);
  }
  std::set<int> all_node_ids = std::set<int>();
  for (uint32_t i = 0; i < config.node_ids.size(); i++) {
    for (uint32_t j = 0; j < config.node_ids[i].size(); j++) {
      all_node_ids.insert(config.node_ids[i][j]);
    }
  }
  // make sure we have the right number of node_priorities and node_runtimes
  if (all_node_ids.size() != config.node_priorities.size()) {
    std::cout << "ros_experiment: all_node_ids.size()= " << all_node_ids.size()
              << " != node_priorities.size()= " << config.node_priorities.size()
              << std::endl;
    exit(1);
  }
  if (all_node_ids.size() != config.node_runtimes.size()) {
    std::cout << "ros_experiment: all_node_ids.size()= " << all_node_ids.size()
              << " != node_runtimes.size()= " << config.node_runtimes.size()
              << std::endl;
    exit(1);
  }
}

std::string Experiment::run(std::atomic<bool> &should_do_task) {
  // TODO: split into setup and run, so that run can be re-used in
  // Experiment::getRunFunctions
  if (config.node_executor_assignments.size() != 0 ||
      numExecutorsRequired(config) != 1) {
    std::cerr
        << "called Experiment::run with node_executor_assignments.size() != 0"
        << std::endl;
    return "";
  }

  createExecutors();

  createNodesAndAssignProperties();

  setInitialDeadlines();
  // reset all timers
  resetTimers();
  // in this function (run), there is only one executor, so retrieve it now
  experiment_executor executor = getExecutorForNode();
  if (config.executor_type == "edf" || config.executor_type == "picas") {
    executor.executor->spin();
  } else if (config.executor_type == "default") {
    executor.default_executor->spin();
  }
  std::cout << "spin done" << std::endl;
  should_do_task.store(false);

  return buildLogs();
}

std::vector<std::function<void(std::atomic<bool> &)>>
Experiment::getRunFunctions() {
  // do pre-run setup
  createExecutors();
  createNodesAndAssignProperties();

  // create a function for each executor
  std::vector<std::function<void(std::atomic<bool> &)>> run_functions;
  for (size_t i = 0; i < numExecutorsRequired(config); i++) {
    // get the executor for this function
    experiment_executor executor = executors[i];
    run_functions.push_back([this,
                             executor](std::atomic<bool> &should_do_task) {
      if (config.executor_type == "edf" || config.executor_type == "picas") {
        executor.executor->spin();
      } else if (config.executor_type == "default") {
        executor.default_executor->spin();
      }
      std::cout << "spin done" << std::endl;
      should_do_task.store(false);
    });
  }

  // do these last, since they're time-sensitive
  setInitialDeadlines();
  // resetTimers();

  return run_functions;
}

size_t Experiment::numExecutorsRequired(const ExperimentConfig &config) {
  // get the number of unique values in the node_executor_assignments
  if (config.node_executor_assignments.size() == 0) {
    return 1;
  }
  std::set<int> unique_executors;
  for (auto &assignment : config.node_executor_assignments) {
    unique_executors.insert(assignment);
  }
  return unique_executors.size();
}

experiment_executor Experiment::getExecutor(int executor_idx) {
  return executors[executor_idx];
}

void Experiment::createExecutors() {
  if (config.node_executor_assignments.size() == 0) {
    // create a single executor
    executors.push_back(createSingleExecutor(0));
    return;
  }

  // depending on config.executor_to_cpu_assignments and config.parallel_mode,
  // we may need to create the host nodes
  if (config.executor_to_cpu_assignments.size() != 0 && config.parallel_mode) {
    std::cerr << "executor_to_cpu_assignments not supported for parallel mode"
              << std::endl;
  }

  if (config.executor_to_cpu_assignments.size() != 0 &&
      config.executor_to_cpu_assignments.size() !=
          numExecutorsRequired(config)) {
    std::cerr
        << "executor_to_cpu_assignments.size() != numExecutorsRequired(config)"
        << std::endl;
    exit(1);
  }

  // create the required host nodes - re-use any existing nodes
  for (uint i = config.nodes.size();
       i < config.executor_to_cpu_assignments.size(); i++) {
    // create a node for each executor
    rclcpp::Node::SharedPtr node =
        std::make_shared<rclcpp::Node>("node_" + std::to_string(i));
    config.nodes.push_back(node);
  }
  std::cout << "created " << config.nodes.size() << " nodes" << std::endl;

  int num_executors = numExecutorsRequired(config);
  std::cout << "creating " << num_executors << " executors" << std::endl;
  for (int i = 0; i < num_executors; i++) {
    executors.push_back(createSingleExecutor(i));
  }
}

experiment_executor Experiment::createSingleExecutor(uint executor_num) {
  experiment_executor executor = {nullptr, nullptr, nullptr};

  if (config.executor_type == "edf" || config.executor_type == "picas") {
    executor.strat = std::make_shared<PriorityMemoryStrategy<>>();
    rclcpp::ExecutorOptions options;
    options.memory_strategy = executor.strat;
    if (config.parallel_mode) {
      executor.executor =
          std::make_shared<timed_executor::MultithreadTimedExecutor>(
              options, "priority_executor", config.cores);
    } else {
      executor.executor =
          std::make_shared<timed_executor::TimedExecutor>(options);
    }
    executor.executor->prio_memory_strategy_ = executor.strat;
    if (config.node_executor_assignments.size() == 0) {
      // add all nodes to the executor
      for (auto &node : config.nodes) {
        executor.executor->add_node(node);
      }
    } else {
      // add only this executor's nodes
      executor.executor->add_node(config.nodes[executor_num]);
    }
  } else if (config.executor_type == "default") {
    if (config.parallel_mode) {
      // executor.default_executor =
      // std::make_shared<ROSDefaultMultithreadedExecutor>(rclcpp::ExecutorOptions(),
      // config.cores);
      executor.default_executor =
          std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
              rclcpp::ExecutorOptions(), config.cores);
    } else {
      executor.default_executor = std::make_shared<ROSDefaultExecutor>();
    }
    if (config.node_executor_assignments.size() == 0) {
      // add all nodes to the executor
      for (auto &node : config.nodes) {
        executor.default_executor->add_node(node);
      }
    } else {
      // add only this executor's nodes
      executor.default_executor->add_node(config.nodes[executor_num]);
    }
  } else {
    // RCLCPP_ERROR(node->get_logger(), "Unknown executor type: %s",
    // executor_type.c_str());
    return executor;
  }
  return executor;
}

experiment_executor Experiment::getExecutorForNode(int node_id) {
  if (config.node_executor_assignments.size() == 0) {
    return executors[0];
  } else {
    int executor_idx = config.node_executor_assignments[node_id];
    std::cout << "node " << node_id << " assigned to executor " << executor_idx
              << std::endl;
    return executors[executor_idx];
  }
}

void Experiment::createNodesAndAssignProperties() {

  // BEGIN SPECIAL CASES
  std::vector<std::vector<std::shared_ptr<CaseStudyNode>>> all_nodes;
  rclcpp::CallbackGroup::SharedPtr cb_group0;
  // LEGACY GROUP BEHAVIOR
  bool jiang2022_cb = false;
  bool jiang2022_cs2 = false;
  // one-to-many test. TODO: add generic api for multiple one->many
  bool ours2024_latency = false;
  for (auto &special_case : config.special_cases) {
    if (special_case == "jiang2022_cb") {
      jiang2022_cb = true;
    } else if (special_case == "2022jiang2") {
      jiang2022_cs2 = true;
    } else if (special_case == "2024ours_latency") {
      std::cout << "using 2024ours_latency special case" << std::endl;
      ours2024_latency = true;
    }
  }
  if (jiang2022_cb || jiang2022_cs2) {
    // cb_group0 =
    // config.node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  }
  // END SPECIAL CASES

  std::vector<rclcpp::CallbackGroup::SharedPtr> cb_groups;
  if (config.num_groups != 0 &&
      config.executor_to_cpu_assignments.size() != 0) {
    std::cout << "WARNING: num_groups and executor_to_cpu_assignments are both "
                 "set. This is not supported. Ignoring num_groups."
              << std::endl;
  }
  for (uint32_t i = 0; i < config.num_groups; i++) {
    cb_groups.push_back(config.nodes[0]->create_callback_group(
        rclcpp::CallbackGroupType::MutuallyExclusive));
  }

  // for each chain
  for (uint32_t chain_id = 0; chain_id < config.node_ids.size(); chain_id++) {
    all_nodes.push_back(std::vector<std::shared_ptr<CaseStudyNode>>());

    // for each node in the chain
    bool first_node = true;
    for (uint32_t node_chain_idx = 0;
         node_chain_idx < config.node_ids[chain_id].size(); node_chain_idx++) {
      uint32_t node_id = config.node_ids[chain_id][node_chain_idx];
      // has this node been created yet?
      if (nodes.find(node_id) != nodes.end()) {
        // if it has, then this node exists in another chain
        // if it's a timer node, we should add it to the timer handle map
        continue;
      }
      experiment_executor executor = getExecutorForNode(node_id);
      std::shared_ptr<const void> handle;
      ExecutableType type;
      // is this the first node in the chain?
      if (node_chain_idx == 0) {
        // create a timer node
        rclcpp::CallbackGroup::SharedPtr cb_group = nullptr;
        if (jiang2022_cs2) {
          std::cout << "jiang2022_cs2: placing all nodes in cb_group0"
                    << std::endl;
          cb_group = cb_group0;
        }
        if (config.num_groups != 0 && config.group_memberships[node_id] != 0) {
          cb_group = cb_groups[config.group_memberships[node_id] - 1];
        }
        std::shared_ptr<PublisherNode> node;
        if (config.node_executor_assignments.size() == 0) {
          node = std::make_shared<PublisherNode>(
              "node_" + std::to_string(node_id), config.node_runtimes[node_id],
              config.chain_periods[chain_id], chain_id, config.nodes[0],
              cb_group);
        } else {
          node = std::make_shared<PublisherNode>(
              "node_" + std::to_string(node_id), config.node_runtimes[node_id],
              config.chain_periods[chain_id], chain_id,
              config.nodes[config.node_executor_assignments[node_id]],
              cb_group);
        }
        handle = node->timer_->get_timer_handle();
        nodes[node_id] = node;
        type = ExecutableType::TIMER;
        // this_chain_timer_handle = node->timer_;
        std::cout << "chain timer handle set: " << chain_id << std::endl;
        chain_timer_handles[chain_id] = node->timer_;

        all_nodes[chain_id].push_back(node);
      } else {
        // create a subscriber node
        int subscription_id = config.node_ids[chain_id][node_chain_idx - 1];
        rclcpp::CallbackGroup::SharedPtr cb_group;
        if (jiang2022_cb && ((chain_id == 1 && node_chain_idx == 1) ||
                             (chain_id == 2 && node_chain_idx == 1))) {
          cb_group = cb_group0;
          std::cout << "adding c: " << chain_id << " n: " << node_id
                    << " to cb group 0" << std::endl;
        } else {
          cb_group = nullptr;
        }

        if (config.num_groups != 0 && config.group_memberships[node_id] != 0) {
          cb_group = cb_groups[config.group_memberships[node_id] - 1];
        }

        std::shared_ptr<WorkerNode> node;
        if (ours2024_latency) {
          uint32_t first_node_id = config.node_ids[chain_id][0];
          node = std::make_shared<WorkerNode>(
              "node_" + std::to_string(first_node_id),
              "node_" + std::to_string(node_id), config.node_runtimes[node_id],
              config.chain_periods[chain_id], chain_id, config.nodes[0],
              cb_group);
        } else if (config.node_executor_assignments.size() == 0) {
          node = std::make_shared<WorkerNode>(
              "node_" + std::to_string(subscription_id),
              "node_" + std::to_string(node_id), config.node_runtimes[node_id],
              config.chain_periods[chain_id], chain_id, config.nodes[0],
              cb_group);
        } else {
          node = std::make_shared<WorkerNode>(
              "node_" + std::to_string(subscription_id),
              "node_" + std::to_string(node_id), config.node_runtimes[node_id],
              config.chain_periods[chain_id], chain_id,
              config.nodes[config.node_executor_assignments[node_id]],
              cb_group);
        }
        handle = node->sub_->get_subscription_handle();
        nodes[node_id] = node;
        type = ExecutableType::SUBSCRIPTION;

        all_nodes[chain_id].push_back(node);
      }
      if (config.executor_type == "edf") {
        std::string cb_name = "node_" + std::to_string(node_id) + "_cb";
        executor.strat->set_executable_deadline(
            handle, config.chain_periods[chain_id], type, chain_id, cb_name);
        // executor.executor->add_node(nodes[node_id]);
        // make sure timer handle exists. if not, we did something wrong
        if (chain_timer_handles.find(config.chain_timer_control[chain_id]) ==
            chain_timer_handles.end()) {
          std::cerr << "chain timer handle not found for chain " << chain_id
                    << ": " << config.chain_timer_control[chain_id]
                    << std::endl;
        }
        executor.strat->get_priority_settings(handle)->timer_handle =
            chain_timer_handles[config.chain_timer_control[chain_id]];
        executor.strat->get_priority_settings(handle)->priority =
            config.node_priorities[node_id]; // use priority as tiebreaker
        if (first_node) {
          executor.strat->set_first_in_chain(handle);
          first_node = false;
        }
        // is this the last node in the chain?
        if (node_chain_idx == config.node_ids[chain_id].size() - 1) {
          executor.strat->set_last_in_chain(handle);
        }
      } else if (config.executor_type == "picas") {
        // executor.executor->add_node(nodes[node_id]);
        executor.strat->set_executable_priority(
            handle, config.node_priorities[node_id], type,
            ExecutableScheduleType::CHAIN_AWARE_PRIORITY, chain_id);
        std::cout << "node " << node_id << " has priority "
                  << config.node_priorities[node_id] << std::endl;
      } else if (config.executor_type == "default") {
        // executor.default_executor->add_node(nodes[node_id]);
      }
      // TODO: other types
    }
  }
}

std::string Experiment::buildLogs(std::vector<node_time_logger> node_logs) {
  std::stringstream output_file;

  output_file << "[" << std::endl;
  // for each node
  for (auto node : nodes) {
    // print the node's logs
    node_time_logger logger = node.second->logger;
    for (auto &log : *(logger.recorded_times)) {
      output_file << "{\"entry\": " << log.first
                  << ", \"time\":   " << log.second << "}," << std::endl;
    }
  }

  if (config.executor_type == "edf" || config.executor_type == "picas") {
    for (auto &executor : executors) {
      node_time_logger strat_log = executor.strat->logger_;
      for (auto &log : *(strat_log.recorded_times)) {
        // std::cout << log.first << " " << log.second << std::endl;
        output_file << "{\"entry\": " << log.first
                    << ", \"time\": " << log.second << "}," << std::endl;
      }

      // executor may be a subclass of RTISTimed, so we can get its logger
      if (dynamic_cast<RTISTimed *>(executor.executor.get())) {
        node_time_logger executor_log =
            dynamic_cast<RTISTimed *>(executor.executor.get())->logger_;
        for (auto &log : *(executor_log.recorded_times)) {
          output_file << "{\"entry\": " << log.first
                      << ", \"time\": " << log.second << "}," << std::endl;
        }
      } else {
        std::cout << "executor is not RTISTimed" << std::endl;
      }
    }
  } else {
    // a default executor
    for (auto &executor : executors) {
      // experimental: executor.default_executor's type is rclcpp::Executor, but
      // here it's guaranteed to inherit from RTISTimed we can get a logger from
      // it. for safety, we check the type first
      if (dynamic_cast<RTISTimed *>(executor.default_executor.get())) {
        node_time_logger default_log =
            dynamic_cast<RTISTimed *>(executor.default_executor.get())->logger_;
        for (auto &log : *(default_log.recorded_times)) {
          output_file << "{\"entry\": " << log.first
                      << ", \"time\": " << log.second << "}," << std::endl;
        }
        std::cout << "recovered logs from default executor" << std::endl;
      } else {
        std::cout << "default executor is not RTISTimed" << std::endl;
      }
    }
  }

  // add internal logs
  for (auto &log_entry : *(_logger.recorded_times)) {
    output_file << "{\"entry\": " << log_entry.first
                << ", \"time\": " << log_entry.second << "}," << std::endl;
  }

  // add logs from argument
  for (auto &log : node_logs) {
    for (auto &log_entry : *(log.recorded_times)) {
      output_file << "{\"entry\": " << log_entry.first
                  << ", \"time\": " << log_entry.second << "}," << std::endl;
    }
  }
  // remove the last comma
  output_file.seekp(-2, std::ios_base::end);
  output_file << "]" << std::endl;

  return output_file.str();
}

void Experiment::setInitialDeadlines() {
  timespec current_time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
  uint64_t millis =
      (current_time.tv_sec * 1000UL) + (current_time.tv_nsec / 1000000);
  // we need a logger to record the first deadline, which is generated here
  // TODO: generate the first deadline in the executor (or node) itself
  if (config.executor_type == "edf") {
    for (uint32_t chain_index = 0; chain_index < config.chain_lengths.size();
         chain_index++) {
      size_t executor_idx;
      if (config.node_executor_assignments.size() == 0) {
        executor_idx = 0;
      } else {
        executor_idx = config.node_executor_assignments[chain_index];
      }
      std::shared_ptr<std::deque<uint64_t>> deadlines =
          executors[executor_idx].strat->get_chain_deadlines(chain_index);
      deadlines->push_back(millis + config.chain_periods[chain_index] * 2);
      std::stringstream oss;
      oss << "{\"operation\": \"next_deadline\", \"chain_id\": " << chain_index
          << ", \"deadline\": "
          << millis + config.chain_periods[chain_index] * 2 << "}";
      // std::cout<<"initial deadline: "<<millis +
      // config.chain_periods[chain_index] * 2<<std::endl; std::cout<<"current
      // time: "<<millis<<std::endl; log_entry(executor.strat->logger_,
      // oss.str());
      log_entry(_logger, oss.str());
    }
  }
}

void Experiment::resetTimers() {
  for (auto timer : chain_timer_handles) {
    std::cout << "resetting timer " << timer.first << std::endl;
    if (timer.second == nullptr) {
      std::cout << "timer is null" << std::endl;
    } else {
      timer.second->reset();
      /*       int64_t old_period;
            // set the period to 0 for force immediate execution
            rcl_timer_exchange_period(timer.second->get_timer_handle().get(), 0,
         &old_period);
            // timer.second->get_timer_handle() */
    }
  }
  // sleep 1 seconds to let every timer reset
  // std::this_thread::sleep_for(std::chrono::seconds(1));
}

void Experiment::writeLogsToFile(std::string file_name,
                                 std::string experiment_name) {
  std::ofstream output_file;
  std::string name = experiment_name;
  // make sure results directory exists. we don't have access to std::filesystem
  // here :(
  std::string command = "mkdir -p results/" + name;
  int result = system(command.c_str());
  if (result != 0) {
    std::cout << "could not create results directory: " << result << ": "
              << strerror(errno) << std::endl;
  }
  output_file.open("results/" + name + "/" + file_name + "_" +
                   this->config.executor_type + ".json");
  output_file << buildLogs();
  output_file.close();
  std::cout << "results written to results/" + name + "/" + file_name + "_" +
                   this->config.executor_type + ".json"
            << std::endl;
}

int do_other_task(double work_time, uint period,
                  std::atomic<bool> &should_do_task) {
  if (work_time == 0) {
    std::cout << "other task: work time is 0, exiting" << std::endl;
    return 0;
  }
  // pin to CPU 0 with highest priority
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset);
  int result = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
  if (result != 0) {
    std::cout << "other task: could not set affinity: " << result << ": "
              << strerror(errno) << std::endl;
  }

  struct sched_param param;
  param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
  sched_setscheduler(0, SCHED_FIFO, &param);
  if (result != 0) {
    std::cout << "other task: could not set scheduler: " << result << ": "
              << strerror(errno) << std::endl;
  }

  prctl(PR_SET_NAME, "other_task", 0, 0, 0);

  // do every period milliseconds
  do {
    timespec current_time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
    uint64_t millis_start = (current_time.tv_sec * (uint64_t)1000) +
                            (current_time.tv_nsec / 1000000);

    // do work
    // nth_prime_silly(work_time);
    dummy_load(work_time);

    // sleep until next period
    clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
    uint64_t millis_end = (current_time.tv_sec * (uint64_t)1000) +
                          (current_time.tv_nsec / 1000000);
    uint64_t sleep_time = period - (millis_end - millis_start);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
  } while (should_do_task);
  std::cout << "other task done" << std::endl;
  return 0;
}