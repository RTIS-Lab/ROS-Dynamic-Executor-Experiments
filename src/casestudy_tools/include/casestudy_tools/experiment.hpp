#ifndef RTIS_EXPERIMENT_HOST
#define RTIS_EXPERIMENT_HOST

#include <vector>
#include <cstdint>
#include <string>
#include "priority_executor/priority_executor.hpp"
#include "casestudy_tools/test_nodes.hpp"
#include "simple_timer/rt-sched.hpp"


struct ExperimentConfig
{
    std::vector<uint32_t> chain_lengths;
    // TODO: rename to callback_ids
    std::vector<std::vector<uint32_t>> node_ids;
    // TODO: rename to callback_runtimes
    std::vector<double> node_runtimes;
    // TODO: rename to callback_priorities
    std::vector<uint32_t> node_priorities;
    std::vector<int> chain_periods;
    // for each chain, the index of the timer node
    std::vector<int> chain_timer_control;

    uint num_groups = 0;
    std::vector<int> group_memberships;

    // TODO: change to enum
    // can be "default", "picas", "edf"
    std::string executor_type = "default";

    // if this is empty, then the nodes will be assigned to a single executor
    // TODO: rename this to callback_executor_assignments
    std::vector<uint32_t> node_executor_assignments;
    // used in single-threaded mode to assign executors to cores
    std::vector<uint32_t> executor_to_cpu_assignments;

    bool parallel_mode = false;
    size_t cores = 2;

    // rclcpp::Node::SharedPtr node;
    // nodes to host the callbacks. In parallel (one executor using multiple threads) mode, there is one node and one executor. The node is provided by the experiment provider
    // in singlethread (each executor uses one thread), there is one node per executor. Additional nodes may be created by the experiment host
    std::vector<rclcpp::Node::SharedPtr> nodes;
    std::vector<std::string> special_cases = {};
};

struct experiment_executor
{
  std::shared_ptr<timed_executor::TimedExecutor> executor;
  std::shared_ptr<PriorityMemoryStrategy<>> strat;

  std::shared_ptr<rclcpp::Executor> default_executor;
};

class Experiment
{
public:
    Experiment(ExperimentConfig config);

    /**
     * @brief Run the experiment
     * runs in the current thread
     * @param should_do_task a flag that can be set to false to stop the experiment. The flag will be set to false when the experiment is complete.
    */
    std::string run(std::atomic<bool> &should_do_task);


    /**
     * @brief Get the functions required to run the experiment
     * returns a vector of functions. Each function will run a different executor. It's up to the caller to run these functions in different threads.
     * The caller may set the thread properties (e.g. priority) before running the function.
     * Each function accepts a flag that can be set to false to stop the experiment. The flag will be set to false when the experiment is complete.
    */
    std::vector<std::function<void(std::atomic<bool> &)>> getRunFunctions();

    static size_t numExecutorsRequired(const ExperimentConfig &config);

    /**
     * get the executor (and strategy) for a given executor index (0 by default)
    */
    experiment_executor getExecutor(int executor_idx = 0);

    node_time_logger getInternalLogger();

    /**
     * collect logs from the executors. Also add any logs from passed in loggers
     * @param optional array of loggers to add to the logs
    */
    std::string buildLogs(std::vector<node_time_logger> loggers = {});

    void resetTimers();

    void writeLogsToFile(std::string filename, std::string experiment_name);

private:
    ExperimentConfig config;
    std::vector<experiment_executor> executors;

    // values to keep track of runtime values

    // chain-indexed vector of deques of deadlines
    std::vector<std::deque<uint64_t> *> chain_deadlines_deque;
    // maps node_id to node
    std::map<uint32_t, std::shared_ptr<CaseStudyNode>> nodes;
    // maps chain_id to timer
    std::map<uint32_t, std::shared_ptr<rclcpp::TimerBase>> chain_timer_handles;

    /**
     * @brief Create the executors using the type and amount set in the config
     *
     */
    void createExecutors();

    /**
     * @brief Create a single executor using the type set in the config
     *
     * @return experiment_executor
     */
    experiment_executor createSingleExecutor(uint executor_num = 0);

    /**
     * @brief Get the executor for a given node id
     * @param node_id if the node_executor_assignments is empty, then this parameter is ignored, and the first executor is returned
    */
    experiment_executor getExecutorForNode(int node_id=0);

    void createNodesAndAssignProperties();


    void setInitialDeadlines();


    node_time_logger _logger;
};

int do_other_task(double work_time, uint period, std::atomic<bool> &should_do_task);
void sanity_check_config(ExperimentConfig config);

#endif // RTIS_EXPERIMENT_HOST