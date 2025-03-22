#include "rclcpp/rclcpp.hpp"
#include "priority_executor/priority_executor.hpp"
#include "priority_executor/priority_memory_strategy.hpp"
#include "casestudy_tools/test_nodes.hpp"
#include <string>
#include <fstream>
#include <unistd.h>
#include "std_msgs/msg/string.hpp"
#include "casestudy_tools/primes_workload.hpp"
#include <sys/prctl.h>
#include "casestudy_tools/primes_workload.hpp"
#include "casestudy_tools/experiment.hpp"
#include "jsoncpp/json/reader.h"

// bool should_do_task = true;
std::atomic<bool> should_do_task(true);

void run_one_executor(std::function<void(std::atomic<bool> &)> exec_fun, int idx)
{
  (void)idx;
  // set this process to highest priority
  struct sched_param param;
  param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
  int result = sched_setscheduler(0, SCHED_FIFO, &param);
  if (result != 0)
  {
    std::cout << "ros_experiment: sched_setscheduler failed: " << result << ": " << strerror(errno) << std::endl;
  }

  // set process name
  prctl(PR_SET_NAME, "ros_experiment", 0, 0, 0);

  exec_fun(should_do_task);
}

int ros_experiment(int argc, char **arg, std::string file_name, ExperimentConfig config)
{

  rclcpp::init(argc, arg);
  // we have to create a node to process any passed parameters
  auto node = rclcpp::Node::make_shared("experiment_node");
  node->declare_parameter("executor_type", "edf");

  std::string executor_type;
  node->get_parameter("executor_type", executor_type);
  std::cout << "using executor type: " << executor_type << std::endl;

  // TODO: move experiment configuration to main
  config.executor_type = executor_type;
  config.nodes.push_back(node);

  Experiment experiment(config);
  // std::string result_log = experiment.run(should_do_task);
  std::vector<std::function<void(std::atomic<bool> &)>> exec_funs = experiment.getRunFunctions();
  std::cout << "got " << exec_funs.size() << " executors" << std::endl;
  std::vector<std::thread> exec_threads;
  int i = 0;
  experiment.resetTimers();
  for (auto exec_fun : exec_funs)
  {
    exec_threads.push_back(std::thread(run_one_executor, exec_fun, i));
    i++;
  }

  for (auto &t : exec_threads)
  {
    t.join();
  }

  experiment.writeLogsToFile(file_name, "2023customfile_singlethread");

  return 0;
}

int main(int argc, char *argv[])
{
  std::ifstream input_file("taskset.json"); // Replace with actual filename

  if (!input_file.is_open())
  {
    std::cerr << "Error opening file!" << std::endl;
    return 1;
  }

  Json::Value root;
  Json::Reader reader;
  bool parsingSuccessful = reader.parse(input_file, root);

  if (!parsingSuccessful)
  {
    std::cerr << "Error parsing file!" << std::endl;
    return 1;
  }

  // set ourselves to the second highest priority
  struct sched_param param;
  param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 2;
  int result = sched_setscheduler(0, SCHED_FIFO, &param);
  if (result != 0)
  {
    std::cout << "ros_experiment: sched_setscheduler failed: " << result << ": " << strerror(errno) << std::endl;
  }
  // calibrate the dummy load
  // Naive way to calibrate dummy workload for current system
  int dummy_load_calib = 1;
  while (1)
  {
    timeval ctime, ftime;
    int duration_us;
    gettimeofday(&ctime, NULL);
    dummy_load(100); // 100ms
    gettimeofday(&ftime, NULL);
    duration_us = (ftime.tv_sec - ctime.tv_sec) * 1000000 + (ftime.tv_usec - ctime.tv_usec);
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "dummy_load_calib: %d (duration_us: %d ns)", dummy_load_calib, duration_us);
    if (abs(duration_us - 100 * 1000) < 500)
    { // error margin: 500us
      break;
    }
    dummy_load_calib = 100 * 1000 * dummy_load_calib / duration_us;
    if (dummy_load_calib <= 0)
      dummy_load_calib = 1;
    DummyLoadCalibration::setCalibration(dummy_load_calib);
  }
  // DummyLoadCalibration::setCalibration(2900);
  ExperimentConfig config;

  // config.chain_lengths = {4, 3, 4};
  for (uint i = 0; i < root["chain_lengths"].size(); i++)
  {
    config.chain_lengths.push_back(root["chain_lengths"][i].asUInt());
  }

  // config.node_ids = {{0, 1, 2, 3}, {4, 5, 6}, {7, 8, 9, 10}};
  for (uint i = 0; i < root["node_ids"].size(); i++)
  {
    std::vector<uint32_t> node_ids_row;
    for (uint j = 0; j < root["node_ids"][i].size(); j++)
    {
      node_ids_row.push_back(root["node_ids"][i][j].asUInt());
    }
    config.node_ids.push_back(node_ids_row);
  }
  // config.node_priorities = {3, 2, 1, 0, 6, 5, 4, 10, 9, 8, 7}; // PICAS-style prioritiesA
  for (uint i = 0; i < root["node_priorities"].size(); i++)
  {
    config.node_priorities.push_back(root["node_priorities"][i].asUInt());
  }

  // disabled for custom
  config.num_groups = 0;
  // config.group_memberships = {0, 0, 1, 2, 0, 0, 1, 0, 0, 1, 2};

  // config.chain_timer_control = {0, 1, 2};
  for (uint i = 0; i < root["chain_timer_control"].size(); i++)
  {
    config.chain_timer_control.push_back(root["chain_timer_control"][i].asInt());
  }

  // config.node_runtimes = {5, 5, 5, 5,      // chain 1
  //                         10, 20, 5,       // chain 2
  //                         10, 10, 15, 20}; // chain 3
  for (uint i = 0; i < root["node_runtimes"].size(); i++)
  {
    config.node_runtimes.push_back(root["node_runtimes"][i].asDouble());
  }
  // config.chain_periods = {50, 100, 110};
  for (uint i = 0; i < root["chain_periods"].size(); i++)
  {
    config.chain_periods.push_back(root["chain_periods"][i].asInt());
  }
  // config.node_executor_assignments = {}; // empty - multicore mode
  for (uint i = 0; i < root["node_executor_assignments"].size(); i++)
  {
    config.node_executor_assignments.push_back(root["node_executor_assignments"][i].asUInt() - 1);
  }
  // single threaded mode - assign executors to cpu cores
  for (uint i = 0; i < root["executor_to_cpu_core"].size(); i++)
  {
    config.executor_to_cpu_assignments.push_back(root["executor_to_cpu_core"][i].asUInt() - 1);
  }

  config.parallel_mode = false;
  config.cores = 4;

  // sanity_check_config(config);

  // from 35/65 to 100/0 in increments of 5
  // for (int i = 0; i <= (100 - 35) / 5; i++)
  // {
  // for now, run with 100% ros
  int i = (100 - 35) / 5;
  int ros_budget = 35 + i * 5;
  int other_task_budget = 65 - i * 5;
  std::cout << "ROS budget: " << ros_budget << " Other task budget: " << other_task_budget << std::endl;
  // do other task in a separate thread
  should_do_task.store(true);
  double other_task_time = 80 * other_task_budget / 100.0;
  std::thread other_task(do_other_task, other_task_time, 80, std::ref(should_do_task));

  // do ros task

  std::string file_name = "ros_" + std::to_string(ros_budget);
  // ros_experiment(argc, argv, file_name);
  std::thread ros_task(ros_experiment, argc, argv, file_name, config);
  std::cout << "tasks started" << std::endl;
  ros_task.join();
  other_task.join();
  std::cout << "tasks joined" << std::endl;

  // // run once
  // break;
  // }
}