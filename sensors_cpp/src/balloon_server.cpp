#include "rclcpp/rclcpp.hpp"
#include "blimp_interfaces/srv/detection.hpp"
#include "opencv2/opencv.hpp"
#include <vector>

using std::placeholders::_1;
using std::placeholders::_2;

class BalloonDetectionServerNode : public rclcpp::Node {
    public:
        BalloonDetectionServerNode() : Node("balloon_detection_server") {
            server_ = this->create_service<blimp_interfaces::srv::Detection>(
                "balloon_detection", 
                std::bind(&BalloonDetectionServerNode::callback_balloon_detect, 
                this, _1, _2));

            // setting up variables for balloon detection
            purple_lower_bound = cv::Scalar(120, 40, 30);
            purple_upper_bound = cv::Scalar(150, 255, 255);
            green_lower_bound = cv::Scalar(41, 80, 80);
            green_upper_bound = cv::Scalar(56, 255, 255);

            RCLCPP_INFO(this->get_logger(), "Balloon Detection Server has been started!");
        }

        const int minimum_radius = 15;
        const int maximum_radius = 300;
        cv::Scalar purple_lower_bound;
        cv::Scalar purple_upper_bound;
        cv::Scalar green_lower_bound;
        cv::Scalar green_upper_bound;
        rclcpp::Service<blimp_interfaces::srv::Detection>::SharedPtr server_;
    
    private:
        void callback_balloon_detect(const blimp_interfaces::srv::Detection::Request::SharedPtr request,
            const blimp_interfaces::srv::Detection::Response::SharedPtr response) {
            std::cout << "I have been called!" << std::endl;
            
            // Converting vector back into cv::Mat (98% sure will have to change)
            cv::Mat frame(request->rows, request->cols, CV_8UC1, request->frame.data());

            // Creating HSV matrices to store the color filtering
            cv::Mat hsv_frame;
            cv::cvtColor(frame, hsv_frame, cv::COLOR_BGR2HSV);

            // Color filtering mask matrices, leaves the HSV values NOT THE DETECTED COLOR
            cv::Mat purple_mask, green_mask;
            cv::inRange(hsv_frame, purple_lower_bound, purple_upper_bound, purple_mask);
            cv::inRange(hsv_frame, green_lower_bound, green_upper_bound, green_mask);

            std::vector<std::vector<cv::Point>> green_contours, purple_contours, all_contours;
            cv::findContours(purple_mask, purple_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            cv::findContours(green_mask, green_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            all_contours.insert(all_contours.end(), purple_contours.begin(), purple_contours.end());
            all_contours.insert(all_contours.end(), green_contours.begin(), green_contours.end());

            cv::RotatedRect largest_contour;
            double largest_contour_area = 0;

            // Finding contour with the largest area
            for (const auto &contour : all_contours) {
                double contour_area = cv::contourArea(contour);
                if (contour_area  > largest_contour_area) {
                    largest_contour = cv::minAreaRect(contour);
                    largest_contour_area = contour_area;
                }
            }

            if (largest_contour.size.width != 0 && largest_contour.size.height != 0) {
                cv::Point2f center = largest_contour.center;
                int radius = std::max(largest_contour.size.width, largest_contour.size.height) / 2;
                if ((radius >= minimum_radius && radius <= maximum_radius) && (center.x >= 0 && center.x < frame.cols && center.y >= 0 && center.y < frame.rows)) {
                    response->detection = true;
                    response->x = center.x;
                    response->y = center.y;
                    return;
                }
            }

            response->detection = false;
            response->x = 0;
            response->y = 0;
        }
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<BalloonDetectionServerNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
