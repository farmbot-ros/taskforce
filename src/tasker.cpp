#include <rclcpp/rclcpp.hpp>

class JobTasker {
  private:
    rclcpp::Node::SharedPtr node;
    bool swarm;

  public:
    ~JobTasker() {}
    JobTasker(rclcpp::Node::SharedPtr node) : node(node) { swarm = node->get_parameter_or<bool>("swarm", false); }

  private:
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
    rclcpp::NodeOptions options;
    options.allow_undeclared_parameters(true);
    options.automatically_declare_parameters_from_overrides(true);

    rclcpp::Node::SharedPtr divide_node = rclcpp::Node::make_shared("tasker", options);
    std::shared_ptr<JobTasker> divide = std::make_shared<JobTasker>(divide_node);

    try {
        executor.add_node(divide_node);
        executor.spin();
    } catch (const std::exception &e) {
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
