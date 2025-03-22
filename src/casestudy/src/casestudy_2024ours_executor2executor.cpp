#include "casestudy_tools/experiment.hpp"
#include "casestudy_tools/primes_workload.hpp"
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

// bool should_do_task = true;
std::atomic<bool> should_do_task(true);

void run_one_executor(std::function<void(std::atomic<bool> &)> exec_fun,
                      int idx) {
  (void)idx;
  // set this process to highest priority
  // struct sched_param param;
  // param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
  // int result = sched_setscheduler(0, SCHED_FIFO, &param);
  // if (result != 0)
  // {
  //   std::cout << "ros_experiment: sched_setscheduler failed: " << result <<
  //   ": " << strerror(errno) << std::endl;
  // }
  sched_attr rt_attr;
  rt_attr.size = sizeof(rt_attr);
  rt_attr.sched_flags = 0;
  rt_attr.sched_nice = 0;
  rt_attr.sched_priority = 99;
  rt_attr.sched_policy = SCHED_FIFO;
  rt_attr.sched_runtime = 0;
  rt_attr.sched_period = 0;
  int result = sched_setattr(0, &rt_attr, 0);
  if (result != 0) {
    std::cout << "executor task: could not set scheduler: " << result << ": "
              << strerror(errno) << std::endl;
  }

  // set process name
  prctl(PR_SET_NAME, "ros_experiment", 0, 0, 0);

  exec_fun(should_do_task);
}

int ros_experiment(rclcpp::Node::SharedPtr node, std::string file_name,
                   ExperimentConfig config) {

  std::string executor_type;
  node->get_parameter("executor_type", executor_type);
  int num_subscribers;
  node->get_parameter("num_subscribers", num_subscribers);
  int num_chains;
  node->get_parameter("num_chains", num_chains);
  std::cout << "using executor type: " << executor_type << std::endl;

  // TODO: move experiment configuration to main
  config.executor_type = executor_type;
  config.nodes.push_back(node);

  Experiment experiment(config);
  // std::string result_log = experiment.run(should_do_task);
  std::vector<std::function<void(std::atomic<bool> &)>> exec_funs =
      experiment.getRunFunctions();
  std::cout << "got " << exec_funs.size() << " executors" << std::endl;
  std::vector<std::thread> exec_threads;
  int i = 0;
  experiment.resetTimers();
  for (auto exec_fun : exec_funs) {
    exec_threads.push_back(std::thread(run_one_executor, exec_fun, i));
    i++;
  }

  for (auto &t : exec_threads) {
    t.join();
  }

  std::string outputname = "2024ours_executor2executor";
  outputname +=
      "_" + std::to_string(num_subscribers) + "_" + std::to_string(num_chains);
  experiment.writeLogsToFile(file_name, outputname);

  return 0;
}

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  // we have to create a node to process any passed parameters
  auto node = rclcpp::Node::make_shared("experiment_node");
  // auto node = rclcpp::Node::make_shared("experiment_node",
  // rclcpp::NodeOptions().use_intra_process_comms(true));
  node->declare_parameter("executor_type", "edf");
  node->declare_parameter("num_subscribers", 3);
  node->declare_parameter("num_chains", 1);
  node->declare_parameter("runtime", 0);

  int num_s;
  node->get_parameter("num_subscribers", num_s);
  int runtime;
  node->get_parameter("runtime", runtime);
  int num_chains;
  node->get_parameter("num_chains", num_chains);

  // set ourselves to the second highest priority
  // struct sched_param param;
  // param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 2;
  // int result = sched_setscheduler(0, SCHED_FIFO, &param);
  sched_attr rt_attr;
  rt_attr.size = sizeof(rt_attr);
  rt_attr.sched_policy = SCHED_FIFO;
  // rt_attr.sched_priority = sched_get_priority_max(SCHED_FIFO) - 2;
  rt_attr.sched_priority = 99;
  int result = sched_setattr(0, &rt_attr, 0);

  if (result != 0) {
    std::cout << "ros_experiment: sched_setscheduler failed: " << result << ": "
              << strerror(errno) << std::endl;
  }
  // calibrate the dummy load
  // Naive way to calibrate dummy workload for current system
  int dummy_load_calib = 1;
  while (1) {
    timeval ctime, ftime;
    int duration_us;
    gettimeofday(&ctime, NULL);
    dummy_load(100); // 100ms
    gettimeofday(&ftime, NULL);
    duration_us = (ftime.tv_sec - ctime.tv_sec) * 1000000 +
                  (ftime.tv_usec - ctime.tv_usec);
    RCLCPP_INFO(rclcpp::get_logger("rclcpp"),
                "dummy_load_calib: %d (duration_us: %d ns)", dummy_load_calib,
                duration_us);
    if (abs(duration_us - 100 * 1000) < 500) { // error margin: 500us
      break;
    }
    dummy_load_calib = 100 * 1000 * dummy_load_calib / duration_us;
    if (dummy_load_calib <= 0)
      dummy_load_calib = 1;
    DummyLoadCalibration::setCalibration(dummy_load_calib);
  }
  // DummyLoadCalibration::setCalibration(2900);
  ExperimentConfig config;

  // runtimes and timers subject to change
  config.chain_lengths = {};
  config.node_ids = {};
  config.chain_timer_control = {};
  config.chain_periods = {};
  config.node_runtimes = {};
  config.node_executor_assignments = {};
  int node_id = 0;
  for (int c = 0; c < num_chains; c++) {
    config.node_ids.push_back({});
    for (int i = 0; i < num_s + 1; i++) {
      config.node_ids[c].push_back(node_id);
      if (i == 0) {
        // first node is the publisher, and goes in the first executor
        config.node_executor_assignments.push_back(0);
      } else {
        // all other nodes go in the second executor
        config.node_executor_assignments.push_back(1);
      }
      node_id++;
    }
    for (int i = 0; i < num_s + 1; i++) {
      config.node_priorities.push_back(i);
    }
    config.chain_lengths.push_back(num_s+1);
    config.chain_periods.push_back(10);
    for (int i = 0; i < num_s + 1; i++) {
      config.node_runtimes.push_back(runtime);
    }
    config.chain_timer_control.push_back(c);
  }

  config.executor_to_cpu_assignments = {0, 1};

  std::cout << "node ids: " << std::endl;
  for (size_t i = 0; i < config.node_ids.size(); i++) {
    for (size_t j = 0; j < config.node_ids[i].size(); j++) {
      std::cout << config.node_ids[i][j] << " ";
    }
    std::cout << std::endl;
  }


  config.num_groups = 0;
  config.group_memberships = {}; // no groups

  config.parallel_mode = false;
  config.cores = 1;
  config.special_cases.push_back("2024ours_latency");

  // sanity_check_config(config);

  // from 35/65 to 100/0 in increments of 5
  // for (int i = 0; i <= (100 - 35) / 5; i++)
  // {
  // for now, run with 100% ros
  int i = (100 - 35) / 5;
  int ros_budget = 35 + i * 5;
  int other_task_budget = 65 - i * 5;
  std::cout << "ROS budget: " << ros_budget
            << " Other task budget: " << other_task_budget << std::endl;
  // do other task in a separate thread
  should_do_task.store(true);

  // do ros task
  std::string file_name = "ros_" + std::to_string(ros_budget);
  // ros_experiment(argc, argv, file_name);
  std::thread ros_task(ros_experiment, node, file_name, config);
  std::cout << "tasks started" << std::endl;
  ros_task.join();
  std::cout << "tasks joined" << std::endl;

  // // run once
  // break;
  // }
}