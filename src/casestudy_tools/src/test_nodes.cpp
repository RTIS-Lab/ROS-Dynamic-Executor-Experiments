#include "casestudy_tools/test_nodes.hpp"
#include "casestudy_tools/primes_workload.hpp"
#include "rclcpp/rclcpp.hpp"
#include "simple_timer/rt-sched.hpp"
#include <cstddef>
#include <ctime>
#include <rclcpp/logger.hpp>

std::string CaseStudyNode::get_name() { return name; }

#define BUFFER_LENGTH 0

rclcpp::QoS get_qos()
{
  if (BUFFER_LENGTH == 0)
  {
    return rclcpp::QoS(rclcpp::KeepAll());
  }
  else
  {
    return rclcpp::QoS(rclcpp::KeepLast(BUFFER_LENGTH));
  }
}

void timespec_diff(timespec *start, timespec *end, timespec *result)
{
  if ((end->tv_nsec - start->tv_nsec) < 0)
  {
    result->tv_sec = end->tv_sec - start->tv_sec - 1;
    result->tv_nsec = 1000000000 + end->tv_nsec - start->tv_nsec;
  }
  else
  {
    result->tv_sec = end->tv_sec - start->tv_sec;
    result->tv_nsec = end->tv_nsec - start->tv_nsec;
  }
}

PublisherNode::PublisherNode(std::string publish_topic, double runtime,
                             int period, int chain,
                             rclcpp::Node::SharedPtr node,
                             rclcpp::CallbackGroup::SharedPtr callback_group)
    : CaseStudyNode(publish_topic, node), runtime_(runtime), period_(period),
      chain_(chain)
{
  logger = create_logger();
  pub_ = node->create_publisher<std_msgs::msg::Int32>(publish_topic, get_qos());
  auto timer_callback = [this]() -> void
  {
    std::ostringstream ss;
    ss << "{\"operation\": \"start_work\", \"chain\": " << chain_
       << ", \"node\": \"" << get_name() << "\", \"count\": " << count_max_;
    std::chrono::nanoseconds time_until_trigger = timer_->time_until_trigger();
    std::chrono::microseconds time_until_trigger_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            time_until_trigger);
    ss << ", \"next_release_us\": " << time_until_trigger_us.count() << "}";
    log_entry(logger, ss.str());
    // nth_prime_silly(runtime_);
    double used_runtime = runtime_;
    if (use_random_runtime)
    {
      if (rand() % 100 < runtime_over_chance * 100)
      {
        used_runtime *= over_runtime_scale;
      }
      else
      {
        used_runtime *= normal_runtime_scale;
      }
    }
    // RCLCPP_INFO(rclcpp::get_logger(get_name()), "running %s with runtime %f",
    //             get_name().c_str(), used_runtime);
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    dummy_load(used_runtime);
    std_msgs::msg::Int32 msg;
    msg.data = count_max_;
    // RCLCPP_INFO(this->get_logger(), "Publishing: '%s'", msg.data.c_str());
    pub_->publish(msg);
    // TEST: can we take more than one msg from DDS?
    // pub_->publish(msg);
    ss.str("");
    ss << "{\"operation\": \"end_work\", \"chain\": " << chain_
       << ", \"node\": \"" << get_name() << "\", \"count\": " << count_max_ << ", \"target_runtime\": " << runtime_
       << "}";
    log_entry(logger, ss.str());
    count_max_--;
    if (count_max_ == 0)
    {
      rclcpp::shutdown();
    }
/*     struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    struct timespec diff;
    timespec_diff(&start_time, &end_time, &diff);
    size_t diff_ns = diff.tv_sec * 1000000000 + diff.tv_nsec;
    size_t diff_ms = diff_ns / 1000000;
    RCLCPP_INFO(rclcpp::get_logger(get_name()), "done running %s, took %lu ms",
                get_name().c_str(), diff_ms); */
  };
  if (callback_group != nullptr)
  {
    callback_group_ = callback_group;
  }
  else
  {
    callback_group_ =
        node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  }
  timer_ = node->create_wall_timer(std::chrono::milliseconds(period),
                                   timer_callback, callback_group_);
}

WorkerNode::WorkerNode(std::string subscribe_topic, std::string publish_topic,
                       double runtime, int period, int chain,
                       rclcpp::Node::SharedPtr node,
                       rclcpp::CallbackGroup::SharedPtr callback_group)
    : CaseStudyNode(publish_topic, node), runtime_(runtime), period_(period),
      chain_(chain)
{
  logger = create_logger();
  auto callback = [this](const std_msgs::msg::Int32::SharedPtr msg) -> void
  {
    // prevent unused variable warning
    (void)msg;

    std::ostringstream ss;
    ss << "{\"operation\": \"start_work\", \"chain\": " << chain_
       << ", \"node\": \"" << get_name() << "\", \"count\": " << msg->data
       << "}";
    log_entry(logger, ss.str());
    // RCLCPP_INFO(this->get_logger(), "I heard: '%s'", msg->data.c_str());
    // nth_prime_silly(runtime_);
    double used_runtime = runtime_;
    if (use_random_runtime)
    {
      if (rand() % 100 < runtime_over_chance * 100)
      {
        used_runtime *= over_runtime_scale;
      }
      else
      {
        used_runtime *= normal_runtime_scale;
      }
    }
    dummy_load(used_runtime);
    // RCLCPP_INFO(this->get_logger(), "Result: %d", result);
    auto new_msg = std_msgs::msg::Int32();
    new_msg.data = msg->data;
    pub_->publish(new_msg);
    ss.str("");
    ss << "{\"operation\": \"end_work\", \"chain\": " << chain_
       << ", \"node\": \"" << get_name() << "\", \"count\": " << msg->data
       << "}";
    log_entry(logger, ss.str());
  };
  rclcpp::SubscriptionOptions sub_options;
  if (callback_group)
  {
    callback_group_ = callback_group;
  }
  else
  {
    callback_group_ =
        node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  }

  sub_options.callback_group = callback_group_;
  sub_ = node->create_subscription<std_msgs::msg::Int32>(
      subscribe_topic, get_qos(), callback, sub_options);
  pub_ = node->create_publisher<std_msgs::msg::Int32>(publish_topic, get_qos());
}
