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

using namespace std::chrono_literals;

template <typename T> T deserialize(const std::vector<uint8_t> &blob) {
    rmw_serialized_message_t cmsg = rmw_get_zero_initialized_serialized_message();
    rcutils_allocator_t alloc = rcutils_get_default_allocator();
    if (rmw_serialized_message_init(&cmsg, blob.size(), &alloc) != RMW_RET_OK) {
        std::cerr << "Failed to initialize serialized message" << std::endl;
    }
    memcpy(cmsg.buffer, blob.data(), blob.size());
    cmsg.buffer_length = blob.size();
    rclcpp::SerializedMessage serialized(cmsg);
    rclcpp::Serialization<T> serializer;
    T msg;
    serializer.deserialize_message(&serialized, &msg);
    return msg;
}

class FieldOp {
  private:
    rclcpp::Node::SharedPtr node_;
    std::string geojson_file_;
    std::vector<std::pair<farmbot_interfaces::msg::Agent, int>> agents;
    int32_t bid_count = 20;
    std::string auction_id = "1111111111";
    farmbot_interfaces::msg::KeyValue kv;

    rclcpp::CallbackGroup::SharedPtr callback_group;
    rclcpp::Publisher<farmbot_interfaces::msg::Auction>::SharedPtr auction_publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Bid>::SharedPtr bid_subscriber_;
    rclcpp::Client<farmbot_interfaces::srv::Job>::SharedPtr job_assigner_;
    rclcpp::TimerBase::SharedPtr auction_timer_, job_timer_, close_timer_;

  public:
    ~FieldOp() {}
    FieldOp(rclcpp::Node::SharedPtr node) : node_(node) {

        geojson_file_ = node_->get_parameter_or<std::string>("geojson_file", "field.geojson");

        callback_group = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

        // setup auction
        auction_setup();
    }

    void auction_setup() {
        auction_timer_ = node_->create_wall_timer(1s, std::bind(&FieldOp::open_auction, this));
        auction_publisher_ = node_->create_publisher<farmbot_interfaces::msg::Auction>("/job/auction", 10);
        bid_subscriber_ = node_->create_subscription<farmbot_interfaces::msg::Bid>(
            "/job/bid", 10, std::bind(&FieldOp::recieve_bids, this, std::placeholders::_1));
        job_timer_ = node_->create_wall_timer(1s, std::bind(&FieldOp::assign_job, this), callback_group);
        close_timer_ = node_->create_wall_timer(1s, std::bind(&FieldOp::close_auction, this));
    }

  private:
    void open_auction() {
        RCLCPP_INFO_ONCE(node_->get_logger(), "~<>~---->> Opening  auction <<----~<>~");
        RCLCPP_INFO_ONCE(node_->get_logger(), "Calling for auction and waiting for %ds for bidders", bid_count);
        farmbot_interfaces::msg::Auction auction;
        auction.timestamp = rclcpp::Time(0);
        auction.signature = "test"; // TODO: generate signature
        auction.auction_id = auction_id;
        auction.job_type = "field_op";
        kv.key = "geojson_file";
        kv.value = geojson_file_;
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
            RCLCPP_INFO(node_->get_logger(), "Agent [%s] bid with [%ld] points", msg->agent.name.c_str(), msg->bid);
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

        job_assigner_ = node_->create_client<farmbot_interfaces::srv::Job>(highest_bidder_agent.name + "/job/field_op");

        RCLCPP_INFO_ONCE(node_->get_logger(), "Job assigned to [%s]", highest_bidder_agent.name.c_str());
        farmbot_interfaces::msg::Job job;
        job.timestamp = rclcpp::Time(0);
        job.signature = "test"; // TODO: generate signature
        job.job_id = "1111111111";
        job.agent = highest_bidder_agent;
        job.auction_id = auction_id;
        job.agents = partakers;

        kv.key = "geojson_file";
        kv.value = geojson_file_;
        job.parameters.push_back(kv);

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
        RCLCPP_INFO_ONCE(node_->get_logger(), "~<>~-------->> Job sent <<--------~<>~");

        nlohmann::json gsn = nlohmann::json::from_cbor(job_result->data);
        std::ofstream dfile("/tmp/field.geojson");
        dfile.write(gsn.dump(4).c_str(), gsn.dump(4).size());
        dfile.close();

        job_timer_->cancel();
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
