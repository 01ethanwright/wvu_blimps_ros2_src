#include "rclcpp/rclcpp.hpp"
#include "blimp_interfaces/msg/camera_coord.hpp"
#include "blimp_interfaces/srv/detection.hpp"
#include "sensor_msgs/msg/joy.hpp"
#include "opencv2/opencv.hpp"
#include <vector>
#include <thread>

class CamNode : public rclcpp::Node {
    public:
        CamNode() : Node("Cam_Node") {
            // creates publisher, publishing on the topic "cam_data"
            cam_data_publisher_ = this->create_publisher<blimp_interfaces::msg::CameraCoord>("cam_data", 3);

            // subscribing to the topic "joy"
            subscriber_ = this->create_subscription<sensor_msgs::msg::Joy>(
                "joy", 10, std::bind(&CamNode::callback_read_joy, this, std::placeholders::_1));
            
            // setting variable cap_ to default constructor VideoCapture, CAP_V4L2 sets the cap to the proper video channel for linux
            cap_ = cv::VideoCapture(0, cv::CAP_V4L2);

            // setting frame width and height of pi camera
            cap_.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
            cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 720);

            control_mode = true; // set for manual more first
            cam_mode = true; // set for balloon detection first
            x_button = 0;

            hasDetection = false;
            x_coord = 0;
            y_coord = 0;

            timer_ = this->create_wall_timer(std::chrono::milliseconds(200), std::bind(&CamNode::callback_read_image, this));
            RCLCPP_INFO(this->get_logger(), "CamNode - Video Detection has started!");
        }

        void callback_read_joy(const sensor_msgs::msg::Joy::SharedPtr button) {
            x_button = button->buttons[3];
        }

        void callback_read_image() {
            // seeing if button to switch cam mode has been pressed
            if (x_button == 1) {
                cam_mode = !cam_mode;
                sleep(1);
                RCLCPP_INFO(this->get_logger(), "CamNode - Cam Mode has been switched!");
            }

            if (!cap_.isOpened()) {
                RCLCPP_ERROR(this->get_logger(), "CamNode - ERROR: Could not open video source!");
                return;
            }

            // checking to see if in manual or autonomous control (true -> manual, false -> autonomous)
            if (!control_mode) {
                // capture frame and setting it to matrix frame
                cv::Mat frame;
                cap_ >> frame;

                // creating int8[] for sending through service
                uint8_t* frameArray = new uint8_t[frame.total() * frame.elemSize()];

                // copying frame data to frameArray
                std::memcpy(frameArray, frame.data, frame.total() * frame.elemSize());

                // checking to see if balloon or goal detection (true -> balloon, false -> goal)
                if (cam_mode) {
                    // balloon detection
                    call_thread = std::thread(std::bind(&CamNode::call_balloon_detection_service, this, frameArray, frame.rows, frame.cols));
                } else {
                    // goal detection
                    call_thread = std::thread(std::bind(&CamNode::call_goal_detection_service, this, frameArray, frame.rows, frame.cols));
                }
            } else {
                // manual control mode
            }

            call_thread.join(); // waits until thread is finished execution (a little backwards to call a thread and then wait till finsihed but idk)

            /*
                TODO:
                  - fix stupid ass lines above
                  - create an the 10 average for detected coords
                  - possibly create something that will clear when recently switched from balloon to goal or vice versa
            */

           RCLCPP_INFO(this->get_logger(), "CamNode - X: %i Y: %i", x_coord, y_coord);

            // checks to see if a detection was made
            if (hasDetection) {
                auto msg = blimp_interfaces::msg::CameraCoord();
                msg.position = {x_coord, y_coord};
                cam_data_publisher_->publish(msg);
            }
        }

        void call_balloon_detection_service(uint8_t[] convertedFrame, int rows, int cols) {
            auto client = this->create_client<blimp_interfaces::srv::Detection>("balloon_detection");
            while (!client->wait_for_service(std::chrono::milliseconds(500))) {
                RCLCPP_WARN(this->get_logger(), "Waiting for balloon detection server to be up...");
            }

            auto request = std::make_shared<blimp_interfaces::srv::Detection::Request>();
            request->frame = convertedFrame;
            request->rows = rows;
            request->cols = cols;

            auto future = client->async_send_request(request);

            try {
                auto response = future.get();
                hasDetection = response->detection;
                x_coord = response->x;
                y_coord = response->y;
            } catch (const std::exception &e) {
                RCLCPP_ERROR(this->get_logger(), "Balloon Detection service call failed!");
            }  
        }

        void call_goal_detection_service(uint8_t[] convertedFrame, int rows, int cols) {
            auto client = this->create_client<blimp_interfaces::srv::Detection>("goal_detection");
            while (!client->wait_for_service(std::chrono::milliseconds(500))) {
                RCLCPP_WARN(this->get_logger(), "Waiting for balloon detection server to be up...");
            }

            auto request = std::make_shared<blimp_interfaces::srv::Detection::Request>();
            request->frame = convertedFrame;
            request->rows = rows;
            request->cols = cols;

            auto future = client->async_send_request(request);

            try {
                auto response = future.get();
                hasDetection = response->detection;
                x_coord = response->x;
                y_coord = response->y;
            } catch (const std::exception &e) {
                RCLCPP_ERROR(this->get_logger(), "Balloon Detection service call failed!");
            } 
        }

        cv::VideoCapture cap_;
        rclcpp::TimerBase::SharedPtr timer_;
        std::thread call_thread;
        rclcpp::Publisher<blimp_interfaces::msg::CameraCoord>::SharedPtr cam_data_publisher_;
        rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr subscriber_;

        bool control_mode;
        bool cam_mode;

        int x_button;

        int hasDetection;
        int x_coord;
        int y_coord;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<CamNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    node->cap_.release();
    //cv::destroyAllWindows(); //needed when showing frame windows
    return 0;
}
