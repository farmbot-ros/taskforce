#include "farmbot_interfaces/msg/agent.hpp"
#include "farmbot_interfaces/msg/agents.hpp"
#include "farmbot_interfaces/msg/auction.hpp"
#include "farmbot_interfaces/msg/bid.hpp"
#include "farmbot_interfaces/msg/job.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>

using namespace std::chrono_literals;

class Bidder {
  private:
    rclcpp::Node::SharedPtr node;
    farmbot_interfaces::msg::Agents participants;
    int32_t bid_count = 30;
    std::string auction_id = "1234567890";

    rclcpp::Publisher<farmbot_interfaces::msg::Auction>::SharedPtr auction_publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Bid>::SharedPtr bid_subscriber_;
    rclcpp::Publisher<farmbot_interfaces::msg::Job>::SharedPtr job_publisher_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr auction_end_publisher_;
    rclcpp::TimerBase::SharedPtr timer_publisher_;

  public:
    ~Bidder() {}
    Bidder(rclcpp::Node::SharedPtr node) : node(node) {
        auction_publisher_ = node->create_publisher<farmbot_interfaces::msg::Auction>("/job/auction", 10);
        bid_subscriber_ = node->create_subscription<farmbot_interfaces::msg::Bid>(
            "/job/bid", 10, std::bind(&Bidder::bid_callback, this, std::placeholders::_1));
        job_publisher_ = node->create_publisher<farmbot_interfaces::msg::Job>("/job/job", 10);
        auction_end_publisher_ = node->create_publisher<std_msgs::msg::Bool>("/job/auction_end", 10);
        timer_publisher_ = node->create_wall_timer(1s, std::bind(&Bidder::test_auction, this));
    }

    void test_auction() {
        RCLCPP_INFO(node->get_logger(), "Sending test auction");
        farmbot_interfaces::msg::Auction auction;
        auction.timestamp = rclcpp::Time(0);
        auction.signature = "test";
        auction.auction_id = auction_id;
        auction.job_type = "harvest";
        farmbot_interfaces::msg::KeyValue kv;
        kv.key = "geojson_file";
        kv.value = "/doc/code/farmbot/src/trailblazer/config/field4.geojson";
        auction.parameters.push_back(kv);
        auction_publisher_->publish(auction);
        bid_count--;
        if (bid_count <= 0) {
            RCLCPP_INFO(node->get_logger(), "-------- Bidding finished ----------");
            timer_publisher_->cancel();
        }
    }

    void bid_callback(const farmbot_interfaces::msg::Bid::SharedPtr msg) {
        if (msg->auction_id != auction_id) {
            return;
        }
        for (const auto &agent : participants.beacons) {
            if (msg->agent.name == agent.name) {
                return;
            }
            RCLCPP_INFO(node->get_logger(), "%s bids %ld", msg->agent.name.c_str(), msg->bid);
            participants.beacons.push_back(agent);
        }
        return;
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

    try {
        executor.add_node(node1);
        executor.spin();
    } catch (const std::exception &e) {
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
