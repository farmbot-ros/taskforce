// auction_node_interface.hpp
#pragma once

#include <farmbot_interfaces/msg/auction.hpp>
#include <farmbot_interfaces/msg/bid.hpp>
#include <farmbot_interfaces/msg/job.hpp>
#include <farmbot_interfaces/srv/job.hpp>
#include <iostream>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace farmbot {
    class AuctionNodeBase {
      protected:
        rclcpp::Node::SharedPtr node_;
        rclcpp::CallbackGroup::SharedPtr callback_group_0, callback_group_1;
        rclcpp::SubscriptionOptions sub_options_;
        rclcpp::Publisher<farmbot_interfaces::msg::Auction>::SharedPtr auction_publisher_;
        rclcpp::Subscription<farmbot_interfaces::msg::Bid>::SharedPtr bid_subscriber_;
        rclcpp::Client<farmbot_interfaces::srv::Job>::SharedPtr job_assigner_;
        rclcpp::TimerBase::SharedPtr auction_timer_, job_timer_, close_timer_;

        std::vector<std::pair<farmbot_interfaces::msg::Agent, int>> agents_;
        int32_t bid_count_;
        std::string auction_id_;
        rclcpp::Time start_time_;
        farmbot_interfaces::msg::KeyValue keyval;

      public:
        AuctionNodeBase(rclcpp::Node::SharedPtr node, int32_t bid_count, const std::string &auction_id)
            : node_(node), bid_count_(bid_count), auction_id_(auction_id) {
            callback_group_0 = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
            callback_group_1 = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
            sub_options_.callback_group = callback_group_1;

            auction_timer_ = node_->create_wall_timer(1s, std::bind(&AuctionNodeBase::open_auction, this));
            auction_publisher_ = node_->create_publisher<farmbot_interfaces::msg::Auction>("/job/auction", 10);
            bid_subscriber_ = node_->create_subscription<farmbot_interfaces::msg::Bid>(
                "/job/bid", 10, std::bind(&AuctionNodeBase::recieve_bids, this, std::placeholders::_1), sub_options_);
            job_timer_ = node_->create_wall_timer(1s, std::bind(&AuctionNodeBase::assign_job, this), callback_group_0);
            close_timer_ = node_->create_wall_timer(1s, std::bind(&AuctionNodeBase::close_auction, this));
        }

        virtual ~AuctionNodeBase() = default;
        // virtual void auction_setup() = 0;

      protected:
        virtual void open_auction() = 0;
        virtual void recieve_bids(const farmbot_interfaces::msg::Bid::SharedPtr msg) = 0;
        virtual void assign_job() = 0;
        virtual void close_auction() = 0;

        static const char *decorate(const std::string &message) noexcept {
            constexpr size_t LINE_WIDTH = 60;
            static thread_local std::string result;
            const std::string decoStart = "~<>~ ";
            const std::string decoEnd = " ~<>~";
            const std::string arrowL = ">> ";
            const std::string arrowR = " <<";

            size_t msgLen = message.size();
            size_t fixedLen = decoStart.size() + decoEnd.size() + arrowL.size() + arrowR.size();

            if (LINE_WIDTH <= msgLen + fixedLen) {
                // too long → no padding
                result = decoStart + arrowL + message + arrowR + decoEnd;
            } else {
                // compute centered padding
                size_t padding = LINE_WIDTH - msgLen - fixedLen;
                size_t dashLeft = padding / 2;
                size_t dashRight = padding - dashLeft;

                result = decoStart + std::string(dashLeft, '-') + arrowL + message + arrowR +
                         std::string(dashRight, '-') + decoEnd;
            }

            return result.c_str();
        }
    };

} // namespace farmbot
