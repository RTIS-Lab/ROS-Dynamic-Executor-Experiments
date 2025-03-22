#ifndef RTIS_TEST_NODES
#define RTIS_TEST_NODES
#include <string>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/int32.hpp"
#include "simple_timer/rt-sched.hpp"

class CaseStudyNode
{
 public: node_time_logger logger;
     // inherit constructor
     CaseStudyNode(std::string publish_topic, rclcpp::Node::SharedPtr node)
     {
         this->name = publish_topic;
         this->node = node;
         // seed random number generator
            srand(time(NULL));
     }

     bool use_random_runtime = false;
     double normal_runtime_scale = 0.8;
     double over_runtime_scale = 1.2;
     double runtime_over_chance = 0.1;
     std::string get_name();
     rclcpp::CallbackGroup::SharedPtr callback_group_;

 private:
     rclcpp::Node::SharedPtr node;
     std::string name;
};

#define COUNT_MAX 500

class PublisherNode : public CaseStudyNode{
    public:
        PublisherNode(std::string publish_topic, double runtime, int period, int chain, rclcpp::Node::SharedPtr node, rclcpp::CallbackGroup::SharedPtr callback_group = nullptr);
        rclcpp::TimerBase::SharedPtr timer_;
        int count_max_ = COUNT_MAX;

    private:
        rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr pub_;
        double runtime_;
        int period_;
        int chain_;
};

class WorkerNode : public CaseStudyNode{
    public:
        WorkerNode(std::string subscribe_topic, std::string publish_topic, double runtime, int period, int chain, rclcpp::Node::SharedPtr node, rclcpp::CallbackGroup::SharedPtr callback_group = nullptr);
        rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr sub_;
        int count_max_ = COUNT_MAX;

    private:
        double runtime_;
        int period_;
        int chain_;
        rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr pub_;
};

#endif // RTIS_TEST_NODES