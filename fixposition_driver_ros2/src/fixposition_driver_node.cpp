/**
 *  @file
 *  @brief Main function for the fixposition driver ros node
 *
 * \verbatim
 *  ___    ___
 *  \  \  /  /
 *   \  \/  /   Fixposition AG
 *   /  /\  \   All right reserved.
 *  /__/  \__\
 * \endverbatim
 *
 */

/* PACKAGE */
#include <fixposition_driver_ros2/fixposition_driver_node.hpp>

namespace fixposition {

// auto qos_sensor = rclcpp::QoS(
//     rclcpp::QoSInitialization(
//         rmw_qos_profile_sensor_data.history,
//         rmw_qos_profile_sensor_data.depth,
//     ),
//     rmw_qos_profile_sensor_data
// );

FixpositionDriverNode::FixpositionDriverNode(std::shared_ptr<rclcpp::Node> node, const FixpositionDriverParams& params)
    : FixpositionDriver(params), node_(node),
    // FP_A messages
    fpa_gnssant_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GNSSANT>("/fixposition/fpa/gnssant", 10)),
    fpa_gnsscorr_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GNSSCORR>("/fixposition/fpa/gnsscorr", 10)),
    fpa_llh_pub_(node_->create_publisher<fixposition_driver_ros2::msg::LLH>("/fixposition/fpa/llh", 10)),
    fpa_odomenu_pub_(node_->create_publisher<fixposition_driver_ros2::msg::ODOMENU>("/fixposition/fpa/odomenu", 10)),
    fpa_odometry_pub_(node_->create_publisher<fixposition_driver_ros2::msg::ODOMETRY>("/fixposition/fpa/odometry", 10)),
    fpa_odomsh_pub_(node_->create_publisher<fixposition_driver_ros2::msg::ODOMSH>("/fixposition/fpa/odomsh", 10)),
    fpa_text_pub_(node_->create_publisher<fixposition_driver_ros2::msg::TEXT>("/fixposition/fpa/text", 10)),

    // NMEA messages
    nmea_gpgga_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GPGGA>("/fixposition/nmea/gpgga", 10)),
    nmea_gpgll_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GPGLL>("/fixposition/nmea/gpgll", 10)),
    nmea_gngsa_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GNGSA>("/fixposition/nmea/gngsa", 10)),
    nmea_gpgst_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GPGST>("/fixposition/nmea/gpgst", 10)),
    nmea_gxgsv_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GXGSV>("/fixposition/nmea/gxgsv", 100)),
    nmea_gphdt_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GPHDT>("/fixposition/nmea/gphdt", 10)),
    nmea_gprmc_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GPRMC>("/fixposition/nmea/gprmc", 10)),
    nmea_gpvtg_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GPVTG>("/fixposition/nmea/gpvtg", 10)),
    nmea_gpzda_pub_(node_->create_publisher<fixposition_driver_ros2::msg::GPZDA>("/fixposition/nmea/gpzda", 10)),

    // ODOMETRY
    odometry_ecef_pub_(node_->create_publisher<nav_msgs::msg::Odometry>("/fixposition/odometry_ecef", 10)),
    odometry_llh_pub_(node_->create_publisher<sensor_msgs::msg::NavSatFix>("/fixposition/odometry_llh", 10)),
    odometry_enu_pub_(node_->create_publisher<nav_msgs::msg::Odometry>("/fixposition/odometry_enu", 10)),
    odometry_smooth_pub_(node_->create_publisher<nav_msgs::msg::Odometry>("/fixposition/odometry_smooth", 10)),

    // Orientation
    eul_pub_(node_->create_publisher<geometry_msgs::msg::Vector3Stamped>("/fixposition/ypr", 10)),
    eul_imu_pub_(node_->create_publisher<geometry_msgs::msg::Vector3Stamped>("/fixposition/imu_ypr", 10)),

    // IMU
    rawimu_pub_(node_->create_publisher<sensor_msgs::msg::Imu>("/fixposition/rawimu", 100)),
    corrimu_pub_(node_->create_publisher<sensor_msgs::msg::Imu>("/fixposition/corrimu", 100)),
    poiimu_pub_(node_->create_publisher<sensor_msgs::msg::Imu>("/fixposition/poiimu", 10)),

    // GNSS
    nmea_pub_(node_->create_publisher<fixposition_driver_ros2::msg::NMEA>("/fixposition/nmea", 10)),
    navsatfix_gnss1_pub_(node_->create_publisher<sensor_msgs::msg::NavSatFix>("/fixposition/gnss1", 10)),
    navsatfix_gnss2_pub_(node_->create_publisher<sensor_msgs::msg::NavSatFix>("/fixposition/gnss2", 10)),
      
    // TF
    br_(std::make_shared<tf2_ros::TransformBroadcaster>(node_)),
    static_br_(std::make_shared<tf2_ros::StaticTransformBroadcaster>(node_)) 

{
    ws_sub_ = node_->create_subscription<fixposition_driver_ros2::msg::Speed>(
                     params_.customer_input.speed_topic, 100,
                     std::bind(&FixpositionDriverNode::WsCallback, this, std::placeholders::_1));
    rtcm_sub_ = node_->create_subscription<std_msgs::msg::UInt8MultiArray>(
                       params_.customer_input.rtcm_topic, 10,
                       std::bind(&FixpositionDriverNode::RtcmCallback, this, std::placeholders::_1));
    RegisterObservers();
}

void FixpositionDriverNode::Run() {
    rclcpp::Rate rate(params_.fp_output.rate);
    const auto reconnect_delay =
        std::chrono::nanoseconds((uint64_t)params_.fp_output.reconnect_delay * 1000 * 1000 * 1000);

    while (rclcpp::ok()) {
        // Read data and publish to ros
        const bool connection_ok = RunOnce();
        // process Incoming ROS msgs
        rclcpp::spin_some(node_);
        // Handle connection loss
        if (!connection_ok) {
            printf("Reconnecting in %.1f seconds ...\n", params_.fp_output.reconnect_delay);
            rclcpp::sleep_for(reconnect_delay);
            Connect();
        } else {
            rate.sleep();  // ensure the loop runs at the desired rate
        }
    }
}

void FixpositionDriverNode::RegisterObservers() {
    // NOV_B
    bestgnsspos_obs_.push_back(std::bind(&FixpositionDriverNode::BestGnssPosToPublishNavSatFix, this,
                                         std::placeholders::_1, std::placeholders::_2));
    // FP_A
    for (const auto& format : params_.fp_output.formats) {
        if (format == "ODOMETRY") {
            dynamic_cast<NmeaConverter<FP_ODOMETRY>*>(a_converters_["ODOMETRY"].get())
                ->AddObserver([this](const FP_ODOMETRY& data) {
                    FpToRosMsg(data, fpa_odometry_pub_);
                    FpToRosMsg(data.odom, odometry_ecef_pub_);
                    OdomToImuMsg(data, poiimu_pub_);
                    OdomToNavSatFix(data, odometry_llh_pub_);
                    OdometryDataToTf(data, br_);
                });
        } else if (format == "ODOMENU") {
            dynamic_cast<NmeaConverter<FP_ODOMENU>*>(a_converters_["ODOMENU"].get())
                ->AddObserver([this](const FP_ODOMENU& data) {
                    FpToRosMsg(data, fpa_odomenu_pub_);
                    FpToRosMsg(data.odom, odometry_enu_pub_);
                    OdomToYprMsg(data.odom, eul_pub_);
                });
        } else if (format == "ODOMSH") {
            dynamic_cast<NmeaConverter<FP_ODOMSH>*>(a_converters_["ODOMSH"].get())
                ->AddObserver([this](const FP_ODOMSH& data) {
                    FpToRosMsg(data, fpa_odomsh_pub_);
                    FpToRosMsg(data.odom, odometry_smooth_pub_);
                });
        } else if (format == "LLH") {
            dynamic_cast<NmeaConverter<FP_LLH>*>(a_converters_["LLH"].get())
                ->AddObserver([this](const FP_LLH& data) { FpToRosMsg(data, fpa_llh_pub_); });
        } else if (format == "GNSSANT") {
            dynamic_cast<NmeaConverter<FP_GNSSANT>*>(a_converters_["GNSSANT"].get())
                ->AddObserver([this](const FP_GNSSANT& data) { FpToRosMsg(data, fpa_gnssant_pub_); });
        } else if (format == "GNSSCORR") {
            dynamic_cast<NmeaConverter<FP_GNSSCORR>*>(a_converters_["GNSSCORR"].get())
                ->AddObserver([this](const FP_GNSSCORR& data) { FpToRosMsg(data, fpa_gnsscorr_pub_); });
        } else if (format == "TEXT") {
            dynamic_cast<NmeaConverter<FP_TEXT>*>(a_converters_["TEXT"].get())
                ->AddObserver([this](const FP_TEXT& data) { FpToRosMsg(data, fpa_text_pub_); });
        } else if (format == "RAWIMU") {
            dynamic_cast<NmeaConverter<FP_RAWIMU>*>(a_converters_["RAWIMU"].get())
                ->AddObserver([this](const FP_RAWIMU& data) { FpToRosMsg(data.imu, rawimu_pub_); });
        } else if (format == "CORRIMU") {
            dynamic_cast<NmeaConverter<FP_CORRIMU>*>(a_converters_["CORRIMU"].get())
                ->AddObserver([this](const FP_CORRIMU& data) { FpToRosMsg(data.imu, corrimu_pub_); });
        } else if (format == "TF") {
            dynamic_cast<NmeaConverter<FP_TF>*>(a_converters_["TF"].get())->AddObserver([this](const FP_TF& data) {
                // TF Observer Lambda
                geometry_msgs::msg::TransformStamped tf;
                TfDataToMsg(data.tf, tf);
                if (tf.child_frame_id == "FP_IMUH" && tf.header.frame_id == "FP_POI") {
                    br_->sendTransform(tf);

                    // Publish Pitch Roll based on IMU only
                    Eigen::Vector3d imu_ypr_eigen = gnss_tf::QuatToEul(data.tf.rotation);
                    imu_ypr_eigen.x() = 0.0;  // the yaw value is not observable using IMU alone
                    geometry_msgs::msg::Vector3Stamped imu_ypr;
                    imu_ypr.header.stamp = tf.header.stamp;
                    imu_ypr.header.frame_id = "FP_POI";
                    imu_ypr.vector.set__x(imu_ypr_eigen.x());
                    imu_ypr.vector.set__y(imu_ypr_eigen.y());
                    imu_ypr.vector.set__z(imu_ypr_eigen.z());
                    eul_imu_pub_->publish(imu_ypr);

                } else if (tf.child_frame_id == "FP_POISH" && tf.header.frame_id == "FP_POI") {
                    br_->sendTransform(tf);
                } else {
                    static_br_->sendTransform(tf);
                }
            });
        } else if (format == "GPGGA") {
            dynamic_cast<NmeaConverter<GP_GGA>*>(a_converters_["GPGGA"].get())->AddObserver([this](const GP_GGA& data) {
                FpToRosMsg(data, nmea_gpgga_pub_);
                if (nmea_pub_->get_subscription_count() > 0) { 
                    nmea_message_.AddNmeaEpoch(data);
                    PublishNmea(nmea_message_); // GPGGA controls the NMEA output
                }
            });
        } else if (format == "GPGLL") {
            dynamic_cast<NmeaConverter<GP_GLL>*>(a_converters_["GPGLL"].get())
                ->AddObserver([this](const GP_GLL& data) { FpToRosMsg(data, nmea_gpgll_pub_); });
        } else if (format == "GNGSA") {
            dynamic_cast<NmeaConverter<GN_GSA>*>(a_converters_["GNGSA"].get())
                ->AddObserver([this](const GN_GSA& data) { FpToRosMsg(data, nmea_gngsa_pub_); });
        } else if (format == "GPGST") {
            dynamic_cast<NmeaConverter<GP_GST>*>(a_converters_["GPGST"].get())
                ->AddObserver([this](const GP_GST& data) { FpToRosMsg(data, nmea_gpgst_pub_); });
        } else if (format == "GPGSV" || format == "GAGSV" || format == "GBGSV" || format == "GLGSV") {
            dynamic_cast<NmeaConverter<GX_GSV>*>(a_converters_["GXGSV"].get())
                ->AddObserver([this](const GX_GSV& data) { FpToRosMsg(data, nmea_gxgsv_pub_); });
        } else if (format == "GPHDT") {
            dynamic_cast<NmeaConverter<GP_HDT>*>(a_converters_["GPHDT"].get())
                ->AddObserver([this](const GP_HDT& data) { FpToRosMsg(data, nmea_gphdt_pub_); });
        } else if (format == "GPRMC") {
            dynamic_cast<NmeaConverter<GP_RMC>*>(a_converters_["GPRMC"].get())->AddObserver([this](const GP_RMC& data) {
                FpToRosMsg(data, nmea_gprmc_pub_);
                if (nmea_pub_->get_subscription_count() > 0) { nmea_message_.AddNmeaEpoch(data); }
            });
        } else if (format == "GPVTG") {
            dynamic_cast<NmeaConverter<GP_VTG>*>(a_converters_["GPVTG"].get())
                ->AddObserver([this](const GP_VTG& data) { FpToRosMsg(data, nmea_gpvtg_pub_); });
        } else if (format == "GPZDA") {
            dynamic_cast<NmeaConverter<GP_ZDA>*>(a_converters_["GPZDA"].get())->AddObserver([this](const GP_ZDA& data) {
                FpToRosMsg(data, nmea_gpzda_pub_);
                if (nmea_pub_->get_subscription_count() > 0) { nmea_message_.AddNmeaEpoch(data); }
            });
        }
    }
}

void FixpositionDriverNode::PublishNmea(NmeaMessage data) {
    // If epoch message is complete, generate NMEA output
    if (data.checkEpoch()) {
        // Generate new message
        fixposition_driver_ros2::msg::NMEA msg;

        // ROS Header
        if (data.stamp.tow == 0.0 && data.stamp.wno == 0) {
            msg.header.stamp = rclcpp::Clock().now();
        } else {
            msg.header.stamp = GpsTimeToMsgTime(data.stamp);
        }
        msg.header.frame_id = "FP_POI";

        // Time and date fields
        msg.time = data.time_str;
        msg.date = data.date_str;
        
        // Latitude [degrees]. Positive is north of equator; negative is south
        msg.latitude = data.llh(0);

        // Longitude [degrees]. Positive is east of prime meridian; negative is west
        msg.longitude = data.llh(1);

        // Altitude [m]. Positive is above the WGS-84 ellipsoid
        msg.altitude = data.llh(2);

        // Quality indicator
        msg.quality = data.quality;

        // Number of satellites
        msg.num_sv = data.num_sv;

        // ID numbers of satellites used in solution
        for (unsigned int i = 0; i < data.ids.size(); i++) {
           msg.ids.push_back(data.ids.at(i));
        }

        // Dilution of precision
        msg.hdop_rec = data.hdop_receiver;
        msg.pdop = data.pdop;
        msg.hdop = data.hdop;
        msg.vdop = data.vdop;

        // Populate GNSS pseudorange error statistics
        msg.rms_range = data.rms_range;
        msg.std_major = data.std_major;
        msg.std_minor = data.std_minor;
        msg.angle_major = data.angle_major;
        msg.std_lat = data.std_lat;
        msg.std_lon = data.std_lon;
        msg.std_alt = data.std_alt;

        // Position covariance [m^2]
        Eigen::Map<Eigen::Matrix<double, 3, 3>> cov_map =
            Eigen::Map<Eigen::Matrix<double, 3, 3>>(msg.covariance.data());
        cov_map = data.cov;

        // Method employed to estimate covariance
        msg.cov_type = data.cov_type;

        // Populate GNSS satellites in view
        msg.num_sats = data.num_sats;
        for (unsigned int i = 0; i < data.sat_id.size(); i++) {
            msg.sat_id.push_back(data.sat_id.at(i));
            msg.elev.push_back(data.elev.at(i));
            msg.azim.push_back(data.azim.at(i));
            msg.cno.push_back(data.cno.at(i));
        }

        // True heading
        msg.heading = data.heading;

        // Speed over ground [m/s]
        msg.speed = data.speed;

        // Course over ground [deg]
        msg.course = data.course;

        // Populate differential data information
        msg.diff_age = data.diff_age;
        msg.diff_sta = data.diff_sta;

        // Publish message
        nmea_pub_->publish(msg);
    }
}

void FixpositionDriverNode::WsCallback(const fixposition_driver_ros2::msg::Speed::ConstSharedPtr msg) {
    std::unordered_map<std::string, std::vector<std::pair<bool, int>>> measurements;
    for (const auto &sensor : msg->sensors) {
        measurements[sensor.location].push_back({sensor.vx_valid, sensor.vx});
        measurements[sensor.location].push_back({sensor.vy_valid, sensor.vy});
        measurements[sensor.location].push_back({sensor.vz_valid, sensor.vz});
    }
    FixpositionDriver::WsCallback(measurements);
}

void FixpositionDriverNode::RtcmCallback(const std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) {
    const void *rtcm_msg = &(msg->data[0]);
    size_t msg_size = msg->layout.dim[0].size;
    FixpositionDriver::RtcmCallback(rtcm_msg, msg_size);
}

void FixpositionDriverNode::BestGnssPosToPublishNavSatFix(const Oem7MessageHeaderMem* header,
                                                          const BESTGNSSPOSMem* payload) {
    // Buffer to data struct
    NavSatFixData nav_sat_fix;
    NovToData(header, payload, nav_sat_fix);

    // Publish
    if (nav_sat_fix.frame_id == "GNSS1" || nav_sat_fix.frame_id == "GNSS") {
        if (navsatfix_gnss1_pub_->get_subscription_count() > 0) {
            sensor_msgs::msg::NavSatFix msg;
            NavSatFixDataToMsg(nav_sat_fix, msg);
            navsatfix_gnss1_pub_->publish(msg);
        }
    } else if (nav_sat_fix.frame_id == "GNSS2") {
        if (navsatfix_gnss2_pub_->get_subscription_count() > 0) {
            sensor_msgs::msg::NavSatFix msg;
            NavSatFixDataToMsg(nav_sat_fix, msg);
            navsatfix_gnss2_pub_->publish(msg);
        }
    }
}

}  // namespace fixposition

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    std::shared_ptr<rclcpp::Node> node = rclcpp::Node::make_shared("fixposition_driver");
    fixposition::FixpositionDriverParams params;

    RCLCPP_INFO(node->get_logger(), "Starting node...");

    if (fixposition::LoadParamsFromRos2(node, params)) {
        RCLCPP_INFO(node->get_logger(), "Params Loaded!");
        fixposition::FixpositionDriverNode driver_node(node, params);
        driver_node.Run();
        RCLCPP_INFO(node->get_logger(), "Exiting.");
    } else {
        RCLCPP_ERROR(node->get_logger(), "Params Loading Failed!");
        rclcpp::shutdown();
        return 1;
    }
}
