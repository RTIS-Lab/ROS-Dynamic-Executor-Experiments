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

// bool should_do_task = true;
std::atomic<bool> should_do_task(true);

void run_one_executor(std::function<void(std::atomic<bool> &)> exec_fun, int idx)
{
  (void)idx;
  // cpu_set_t cpuset;
  // CPU_ZERO(&cpuset);
  // CPU_SET(0, &cpuset);
  // CPU_SET(1, &cpuset);
  // CPU_SET(2, &cpuset);
  // CPU_SET(3, &cpuset);

  // int result = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
  // if (result != 0)
  // {
  //   std::cout << "ros_experiment: sched_setaffinity failed: " << result << ": " << strerror(errno) << std::endl;
  // }

  // set this process to second highest priority
  struct sched_param param;
  param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 2;
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
  auto node = rclcpp::Node::make_shared("experiment_parameters");
  node->declare_parameter("executor_type", "edf");

  std::string executor_type;
  node->get_parameter("executor_type", executor_type);
  std::cout << "using executor type: " << executor_type << std::endl;

  // TODO: move experiment configuration to main
  config.executor_type = executor_type;
  config.parallel_mode = true;
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
  std::string outputname = "casestudy_example";
  experiment.writeLogsToFile(file_name, outputname);

  return 0;
}

int main(int argc, char *argv[])
{

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
  config.chain_lengths = {2, 2};
  config.node_ids = {{0, 1}, {2, 3}};
  config.node_priorities = {1, 0, 3, 2};
  config.chain_timer_control = {0, 1};

  config.node_runtimes = {10, 10, 10, 10};
  // node 0 has a period of 80, and is the only timer
  config.chain_periods = {100, 100};
  config.node_executor_assignments = {};
  config.parallel_mode = true;
  config.cores = 2;

  sanity_check_config(config);

  std::thread ros_task(ros_experiment, argc, argv, "cs_example", config);
  std::cout << "tasks started" << std::endl;
  ros_task.join();
  std::cout << "tasks joined" << std::endl;
}