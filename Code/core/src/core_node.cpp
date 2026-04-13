#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"

using std::placeholders::_1;

class CoreNode : public rclcpp::Node
{
public:
    CoreNode() : Node("core_node")
    {
        // Publisher an Teensy (Cmd-Daten)
        teensy_cmd_pub_ = this->create_publisher<geometry_msgs::msg::Twist>(
            "cmd_pub", 10);

        // Subscriber vom Teensy
        teensy_state_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
            "vehicle_data_raw", 10,
            std::bind(&CoreNode::teensyStateCallback, this, _1));

        // Subscriber vom Controller
        controller_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
            "controller_publisher", 10,
            std::bind(&CoreNode::controllerCallback, this, _1));

        RCLCPP_INFO(this->get_logger(), "CoreNode gestartet");
    }

private:

    // ====== Controller-Daten ======
    void controllerCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
    {
        latest_cmd_.linear.x = msg->linear.x;
        latest_cmd_.angular.z = msg->angular.z;
        

        sendToTeensy();
    }

    // ====== Fahrzeugdaten vom Teensy ======
    void teensyStateCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
    {
        latest_state_.linear.x = msg->linear.x;
        latest_state_.linear.y = msg->linear.y;
        latest_state_.angular.z = msg->angular.z;
    }

    // ====== Zusammenführen & Senden ======
    void sendToTeensy()
    {
        geometry_msgs::msg::Twist out;

        double m_x = 1;
        // y = m*x+t mit t = 0
        out.linear.x = m_x * latest_cmd_.linear.x; 

        double m_phi = 0.5;
        out.angular.z= m_phi * latest_cmd_.linear.x;

        // Lenkung
        double constrain_steering = 0.25;
        out.linear.y= constrain_steering * latest_cmd_.angular.z;

        teensy_cmd_pub_->publish(out);

    }

    // Publisher
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr teensy_cmd_pub_;

    // Subscriber
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr teensy_state_sub_;
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr controller_sub_;

    // Gespeicherte Daten
    geometry_msgs::msg::Twist latest_cmd_;
    geometry_msgs::msg::Twist latest_state_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<CoreNode>());
    rclcpp::shutdown();
    return 0;
}
