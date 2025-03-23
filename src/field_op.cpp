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
    rclcpp::Node::SharedPtr node_;
    std::string geojson_file_;
    double vehicle_coverage_, path_angle_;
    std::vector<std::pair<farmbot_interfaces::msg::Agent, int>> agents;
    int32_t bid_count = 20;
    std::string auction_id = "1234567890";
    farmbot_interfaces::msg::KeyValue kv;

    rclcpp::Publisher<farmbot_interfaces::msg::Auction>::SharedPtr auction_publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Bid>::SharedPtr bid_subscriber_;
    rclcpp::Publisher<farmbot_interfaces::msg::Job>::SharedPtr job_publisher_;
    rclcpp::TimerBase::SharedPtr auction_timer_, job_timer_, close_timer_;

  public:
    ~Bidder() {}
    Bidder(rclcpp::Node::SharedPtr node) : node_(node) {

        vehicle_coverage_ = node_->get_parameter_or<double>("vehicle_coverage", 3.0);
        path_angle_ = node_->get_parameter_or<double>("path_angle", 90);
        geojson_file_ = node_->get_parameter_or<std::string>("geojson_file", "field.geojson");

        auction_publisher_ = node->create_publisher<farmbot_interfaces::msg::Auction>("/job/auction", 10);
        bid_subscriber_ = node->create_subscription<farmbot_interfaces::msg::Bid>(
            "/job/bid", 10, std::bind(&Bidder::recieve_bids, this, std::placeholders::_1));
        job_publisher_ = node->create_publisher<farmbot_interfaces::msg::Job>("/job/job", 10);
        auction_timer_ = node->create_wall_timer(1s, std::bind(&Bidder::open_auction, this));
        job_timer_ = node->create_wall_timer(1s, std::bind(&Bidder::assign_job, this));
        close_timer_ = node->create_wall_timer(1s, std::bind(&Bidder::close_auction, this));
    }

  private:
    void open_auction() {
        RCLCPP_INFO_ONCE(node_->get_logger(), "~<>~---->> Opening  auction <<----~<>~");
        RCLCPP_INFO_ONCE(node_->get_logger(), "Calling for auction and waiting for %ds for bidders", bid_count);
        farmbot_interfaces::msg::Auction auction;
        auction.timestamp = rclcpp::Time(0);
        auction.signature = "test"; // TODO: generate signature
        auction.auction_id = auction_id;
        auction.job_type = "abliner";
        kv.key = "geojson_file";
        kv.value = geojson_file_;
        auction.parameters.push_back(kv);
        kv.key = "vehicle_coverage";
        kv.value = std::to_string(vehicle_coverage_);
        auction.parameters.push_back(kv);
        kv.key = "path_angle";
        kv.value = std::to_string(path_angle_);
        auction.parameters.push_back(kv);
        auction_publisher_->publish(auction);
        bid_count--;
        if (bid_count <= 0) {
            RCLCPP_INFO_ONCE(node_->get_logger(), "~<>~---->> Bidding finished <<----~<>~");
            auction_timer_->cancel();
        }
    }

    void recieve_bids(const farmbot_interfaces::msg::Bid::SharedPtr msg) {
        if (msg->auction_id != auction_id) {
            return;
        }

        bool found = false;
        for (const auto &ag : agents) {
            if (ag.first.name == msg->agent.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            RCLCPP_INFO(node_->get_logger(), "Agent [%s] bid", msg->agent.name.c_str());
            agents.push_back({msg->agent, msg->bid});
        }
    }

    void assign_job() {
        bid_count--;
        if (agents.empty() || bid_count >= 0) {
            return;
        }
        std::vector<farmbot_interfaces::msg::Agent> partakers;
        // auto agent = agents[randomAgentIndex];
        int highest_bidder = 0;
        farmbot_interfaces::msg::Agent highest_bidder_agent;

        for (uint i = 0; i < agents.size(); i++) {
            partakers.push_back(agents[i].first);
            if (agents[i].second > highest_bidder) {
                highest_bidder_agent = agents[i].first;
            }
        }

        RCLCPP_INFO_ONCE(node_->get_logger(), "Job assigned to [%s]", highest_bidder_agent.name.c_str());
        farmbot_interfaces::msg::Job job;
        job.timestamp = rclcpp::Time(0);
        job.signature = "test"; // TODO: generate signature
        job.job_id = "1234567890";
        job.agent = highest_bidder_agent;
        job.auction_id = auction_id;
        job.agents = partakers;

        kv.key = "geojson_file";
        kv.value = geojson_file_;
        job.parameters.push_back(kv);
        kv.key = "vehicle_coverage";
        kv.value = std::to_string(vehicle_coverage_);
        job.parameters.push_back(kv);
        kv.key = "path_angle";
        kv.value = std::to_string(path_angle_);
        job.parameters.push_back(kv);

        job_publisher_->publish(job);
        if (bid_count <= -10) {
            RCLCPP_INFO_ONCE(node_->get_logger(), "~<>~-------->> Job sent <<--------~<>~");
            bid_count = 0;
            job_timer_->cancel();
        }
    }
    void close_auction() {
        bid_count--;
        if (bid_count <= -10) {
            // close node
            RCLCPP_INFO_ONCE(node_->get_logger(), "~<>~---->> Closing  auction <<----~<>~");
            rclcpp::shutdown();
        }
    }
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
    // rclcpp::shutdown();
    return 0;
}
