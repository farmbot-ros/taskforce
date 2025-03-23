#include "farmbot_interfaces/msg/agent.hpp"
#include "farmbot_interfaces/msg/agents.hpp"
#include "farmbot_interfaces/msg/auction.hpp"
#include "farmbot_interfaces/msg/bid.hpp"
#include "farmbot_interfaces/msg/job.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>

class Bidder {
  private:
    rclcpp::Node::SharedPtr node;

    rclcpp::Publisher<farmbot_interfaces::msg::Auction>::SharedPtr auction_publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Bid>::SharedPtr bid_subscriber_;
    rclcpp::Publisher<farmbot_interfaces::msg::Job>::SharedPtr job_publisher_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr auction_end_publisher_;
    farmbot_interfaces::msg::KeyValue kv;

  public:
    ~Bidder() {}
    Bidder(rclcpp::Node::SharedPtr node) : node(node) {
        auction_publisher_ = node->create_publisher<farmbot_interfaces::msg::Auction>("/job/auction", 10);
        bid_subscriber_ = node->create_subscription<farmbot_interfaces::msg::Bid>(
            "/job/bid", 10, std::bind(&Bidder::bid_callback, this, std::placeholders::_1));
        job_publisher_ = node->create_publisher<farmbot_interfaces::msg::Job>("/job/job", 10);
        auction_end_publisher_ = node->create_publisher<std_msgs::msg::Bool>("/job/auction_end", 10);
    }

    void bid_callback(const farmbot_interfaces::msg::Bid::SharedPtr msg) {
        RCLCPP_INFO(node->get_logger(), "Got bid of %ld from %s", msg->bid, msg->agent.name.c_str());
        return;
    }

    void test_auction() {
        RCLCPP_INFO(node->get_logger(), "Sending test auction");

        farmbot_interfaces::msg::Auction auction;
        auction.auction_id = "test";
        auction.job_type = "harvest";

        kv.key = "testq";
        kv.value = "test1";
        auction.parameters.push_back(kv);

        kv.key = "testq2";
        kv.value = "test2";
        auction.parameters.push_back(kv);

        auction.signature = "test";
        auction.timestamp = rclcpp::Time(0);

        auction_publisher_->publish(auction);
    }

  private:
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
    rclcpp::NodeOptions options;
    options.allow_undeclared_parameters(true);
    options.automatically_declare_parameters_from_overrides(true);

    rclcpp::Node::SharedPtr node1 = rclcpp::Node::make_shared("tasker", options);
    std::shared_ptr<Bidder> taskerrr = std::make_shared<Bidder>(node1);
    taskerrr->test_auction();

    try {
        executor.add_node(node1);
        executor.spin();
    } catch (const std::exception &e) {
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
