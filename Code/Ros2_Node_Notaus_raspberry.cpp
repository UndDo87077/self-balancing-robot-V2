#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

class UdpBitReceiver : public rclcpp::Node
{
public:
    UdpBitReceiver() : Node("udp_bit_receiver")
    {
        // ROS Parameter
        port_ = this->declare_parameter<int>("port", 5005);
        topic_ = this->declare_parameter<std::string>("topic", "esp/bit");

        pub_ = this->create_publisher<std_msgs::msg::Bool>(topic_, 10);

        // UDP Socket aufsetzen
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) {
            throw std::runtime_error("socket() failed");
        }

        // Non-blocking
        int flags = fcntl(sock_, F_GETFL, 0);
        fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0

        if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock_);
            throw std::runtime_error("bind() failed (port belegt?)");
        }

        RCLCPP_INFO(get_logger(), "Listening UDP on 0.0.0.0:%d, publishing to '%s'",
                    port_, topic_.c_str());

        // Timer pollt den Socket regelmäßig (passt gut zu ROS2 Executor)
        timer_ = this->create_wall_timer(
                std::chrono::milliseconds(5),
                std::bind(&UdpBitReceiver::poll_udp, this)
        );
    }

    ~UdpBitReceiver() override
    {
        if (sock_ >= 0) close(sock_);
    }

private:
    void poll_udp()
    {
        uint8_t buf[16];
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);

        // ggf. mehrere Pakete pro Tick abarbeiten
        while (true) {
            ssize_t n = recvfrom(sock_, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr*>(&src), &srclen);

            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // nichts mehr da
                }
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                                     "recvfrom() error: %d", errno);
                break;
            }

            if (n >= 1) {
                bool bit = (buf[0] != 0);

                std_msgs::msg::Bool msg;
                msg.data = bit;
                pub_->publish(msg);

                // optional debug (nicht zu spammy)
                // RCLCPP_INFO(get_logger(), "RX bit=%d", bit ? 1 : 0);
            }
        }
    }

    int sock_{-1};
    int port_{5005};
    std::string topic_{"esp/bit"};

    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<UdpBitReceiver>());
    rclcpp::shutdown();
    return 0;
}