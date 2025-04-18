#include "farmbot_interfaces/msg/agent.hpp"
#include "farmbot_interfaces/msg/agents.hpp"
#include "farmbot_interfaces/msg/auction.hpp"
#include "farmbot_interfaces/msg/bid.hpp"
#include "farmbot_interfaces/msg/field.hpp"
#include "farmbot_interfaces/msg/job.hpp"
#include "farmbot_interfaces/srv/job.hpp"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rcutils/allocator.h>
#include <rmw/serialized_message.h>
#include <std_msgs/msg/bool.hpp>

#include <fstream>
#include <nlohmann/json.hpp>

#include "auction_node_interface.hpp"

using namespace std::chrono_literals;

class FieldOp : public farmbot::AuctionNodeBase {
  private:
    std::string geojson_file_;

  public:
    ~FieldOp() {}
    FieldOp(rclcpp::Node::SharedPtr node) : AuctionNodeBase(node, 10, "1111111111") {
        geojson_file_ = node_->get_parameter_or<std::string>("geojson_file", "field.geojson");
    }

  private:
    void open_auction() override {
        RCLCPP_INFO_ONCE(node_->get_logger(), "%s", decorate("Opening  auction"));
        RCLCPP_INFO_ONCE(node_->get_logger(), "Calling for auction and waiting for %ds for bidders", bid_count_);
        farmbot_interfaces::msg::Auction auction;
        auction.timestamp = rclcpp::Time(0);
        auction.signature = "test"; // TODO: generate signature
        auction.auction_id = auction_id_;
        auction.job_type = "field_op";
        keyval.key = "geojson_file";
        keyval.value = geojson_file_;
        auction.parameters.push_back(keyval);
        auction_publisher_->publish(auction);
        if (bid_count_ <= 0) {
            RCLCPP_INFO_ONCE(node_->get_logger(), "%s", decorate("Bidding finished"));
            auction_timer_->cancel();
        }
    }

    void recieve_bids(const farmbot_interfaces::msg::Bid::SharedPtr msg) override {
        if (msg->auction_id != auction_id_) {
            return;
        }
        bool found = false;
        for (const auto &ag : agents_) {
            if (ag.first.name == msg->agent.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            RCLCPP_INFO(node_->get_logger(), "Agent [%s] bid with [%ld] points", msg->agent.name.c_str(), msg->bid);
            agents_.push_back({msg->agent, msg->bid});
        }
    }

    void assign_job() override {
        if (agents_.empty() || bid_count_ >= 0) {
            return;
        }
        std::vector<farmbot_interfaces::msg::Agent> partakers;
        // auto agent = agents[randomAgentIndex];
        int highest_bidder = 0;
        farmbot_interfaces::msg::Agent highest_bidder_agent;

        for (uint i = 0; i < agents_.size(); i++) {
            partakers.push_back(agents_[i].first);
            if (agents_[i].second > highest_bidder) {
                highest_bidder_agent = agents_[i].first;
            }
        }

        job_assigner_ = node_->create_client<farmbot_interfaces::srv::Job>(highest_bidder_agent.name + "/job/field_op");

        RCLCPP_INFO_ONCE(node_->get_logger(), "Job assigned to [%s]", highest_bidder_agent.name.c_str());
        farmbot_interfaces::msg::Job job;
        job.timestamp = rclcpp::Time(0);
        job.signature = "test"; // TODO: generate signature
        job.job_id = "1111111111";
        job.agent = highest_bidder_agent;
        job.auction_id = auction_id_;
        job.agents = partakers;

        keyval.key = "geojson_file";
        keyval.value = geojson_file_;
        job.parameters.push_back(keyval);

        auto job_send_request = std::make_shared<farmbot_interfaces::srv::Job::Request>();
        job_send_request->the_job = job;
        while (!job_assigner_->wait_for_service(1s)) {
            if (!rclcpp::ok()) {
                RCLCPP_ERROR(node_->get_logger(), "Interrupted while waiting for the service. Exiting.");
                return;
            }
            RCLCPP_INFO(node_->get_logger(), "Service not available, waiting again...");
        }
        auto job_future = job_assigner_->async_send_request(job_send_request);
        while (rclcpp::ok() && job_future.wait_for(1s) == std::future_status::timeout) {
            RCLCPP_INFO(node_->get_logger(), "Waiting for response from Job service...");
        }
        auto job_result = job_future.get();

        if (job_result->type != "json/FieldOp") {
            RCLCPP_ERROR(node_->get_logger(), "Job service did not match Field type");
            return;
        }
        RCLCPP_INFO_ONCE(node_->get_logger(), "%s", decorate("Job sent"));

        job_timer_->cancel();
    }

    void close_auction() override {
        bid_count_--;
        if (bid_count_ <= -5) {
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

    rclcpp::Node::SharedPtr node1 = rclcpp::Node::make_shared("field_op", options);
    std::shared_ptr<FieldOp> taskerrr = std::make_shared<FieldOp>(node1);

    try {
        executor.add_node(node1);
        executor.spin();
    } catch (const std::exception &e) {
        return 1;
    }
    // rclcpp::shutdown();
    return 0;
}
