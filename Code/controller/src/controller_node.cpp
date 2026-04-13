#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joy.hpp"
#include "geometry_msgs/msg/twist.hpp"

using std::placeholders::_1;

class ControllerNode : public rclcpp::Node
{
public:
    ControllerNode() : Node("controller_node")
    {
        // Publisher für Twist-Nachrichten (Bewegungsbefehle)
        cmd_pub_ = this->create_publisher<geometry_msgs::msg::Twist>("controller_publisher", 10);

        // Subscriber auf den Joy-Topic
        joy_sub_ = this->create_subscription<sensor_msgs::msg::Joy>(
            "joy", 10, std::bind(&ControllerNode::joyCallback, this, _1));
        RCLCPP_INFO(this->get_logger(), "Xbox Controller Node gestartet");
    }

private:
    void joyCallback(const sensor_msgs::msg::Joy::SharedPtr msg)
    {
        geometry_msgs::msg::Twist cmd;

        double left_stick_x = msg->axes[0];
        double lt = msg->axes[2];
        double rt = msg->axes[5];
        bool stop_button = msg->buttons[1];

        // --- Geschwindigkeit berechnen ---
        double lt_val = (1.0 - lt) / 2.0; // Rückwärts
        double rt_val = (1.0 - rt) / 2.0; // Vorwärts
            
        double linear_speed = rt_val - lt_val; 
        double angular_speed = left_stick_x;   

        if (stop_button)
        {
            linear_speed = 0.0;
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,"STOP gedrückt -> Geschwindigkeit 0");
        }

        cmd.linear.x = linear_speed;
        cmd.angular.z = angular_speed;

        cmd_pub_->publish(cmd);
    }

    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
    rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr joy_sub_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ControllerNode>());
    rclcpp::shutdown();
    return 0;
}
