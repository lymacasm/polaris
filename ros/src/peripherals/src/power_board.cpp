#include <ros/ros.h>
#include <string>
#include <memory>
#include <serial/serial.h>

#include "monitor/GetSerialDevice.h"
#include "peripherals/powerboard.h"
#include "peripherals/power_enable.h"

#define RESPONSE_SIZE_CRA (14)
#define RESPONSE_SIZE_VTA (6)
#define RESPONSE_SIZE_TMP (4)
#define RESPONSE_SIZE_HUM (4)
#define RESPONSE_SIZE_WTR (4)
#define RESPONSE_SIZE_PIN (5)
#define RESPONSE_SIZE_PEX (4)

using rosserv = ros::ServiceServer;
using powerboardInfo = peripherals::powerboard;
using PowerEnableReq = peripherals::power_enable::Request;
using PowerEnableRes = peripherals::power_enable::Response;

class power_board{
public:
    power_board(const std::string & port, int baud_rate = 9600, int timeout = 1000);
    ~power_board();
    void get_powerboard_data(powerboardInfo & msg);
    bool power_enabler(PowerEnableReq &req, PowerEnableRes &res);
private:
    std::unique_ptr<serial::Serial> connection = nullptr;
    std::string write(const std::string & out, bool ignore_response = true, std::string eol = "\n");
    std::size_t write(const std::string & out, uint8_t* in, std::size_t read_len, std::string eol = "\n");
};

power_board::power_board(const std::string & port, int baud_rate, int timeout) {
    ROS_INFO("Connecting to power_board on port: %s", port.c_str());
    connection = std::unique_ptr<serial::Serial>(new serial::Serial(port, (u_int32_t) baud_rate, serial::Timeout::simpleTimeout(timeout)));
}

power_board::~power_board() {
    connection->close();
}

std::string power_board::write(const std::string & out, bool ignore_response, std::string eol)
{
    // Flush the output, then the input (order matters, to flush any unwanted responses)
    connection->flushOutput();
    connection->flushInput();

    // Send command 
    connection->write(out + eol);
    ROS_INFO("%s", out.c_str());

    // Used for commands where response does not matter (enabling, etc)
    if (ignore_response) {
        return "";
    }

    // Used for commands to read data
    return connection->readline(65536ul, eol);
}

std::size_t power_board::write(const std::string & out, uint8_t* in, std::size_t read_len, std::string eol)
{
    // Flush the output, then the input (order matters, to flush any unwanted responses)
    connection->flushOutput();
    connection->flushInput();

    // Send command 
    connection->write(out + eol);
    ROS_INFO("%s", out.c_str());

    // Read specified data length
    return connection->read(in, read_len);
}

void power_board::get_powerboard_data(powerboardInfo &msg) {
    // Get data from power board
    uint8_t* currents = new uint8_t[RESPONSE_SIZE_CRA];
    uint8_t* voltages = new uint8_t[RESPONSE_SIZE_VTA];
    uint8_t* temperature = new uint8_t[RESPONSE_SIZE_TMP];
    uint8_t* humidity = new uint8_t[RESPONSE_SIZE_HUM];
    uint8_t* water = new uint8_t[RESPONSE_SIZE_WTR];
    uint8_t* pressure_internal = new uint8_t[RESPONSE_SIZE_PIN];
    uint8_t* pressure_external = new uint8_t[RESPONSE_SIZE_PEX];

    // Populate the message with current data
    if(this->write("CRA", currents, RESPONSE_SIZE_CRA) == RESPONSE_SIZE_CRA) {       
        msg.current_battery_1 = (currents[2] << 16) | (currents[1] << 8) | (currents[0]);
        msg.current_battery_2 = (currents[5] << 16) | (currents[4] << 8) | (currents[3]);
        msg.current_motors = (currents[8] << 16) | (currents[7] << 8) | (currents[6]);
        msg.current_system = (currents[11] << 16) | (currents[10] << 8) | (currents[9]);
    }
    else {      
        ROS_ERROR("Current data is invalid.");
    }

    // Populate message with voltage data
    if(this->write("VTA", voltages, RESPONSE_SIZE_VTA) == RESPONSE_SIZE_VTA) {        
        msg.voltage_battery_1 = (voltages[1] << 8) | (voltages[0]);
        msg.voltage_battery_2 = (voltages[3] << 8) | (voltages[2]);
    }
    else {      
        ROS_ERROR("Voltage data is invalid.");
    }

    // Populate message with temperature data
    if(this->write("TMP", temperature, RESPONSE_SIZE_TMP) == RESPONSE_SIZE_TMP) {     
        // Convert to degrees C
        msg.temperature = (temperature[1] << 8) | (temperature[0]);
        msg.temperature = (msg.temperature / 10.0) - 273.15;
    }
    else {      
        ROS_ERROR("Temperature data is invalid.");
    }

    // Populate message with humidity data
    if(this->write("HUM", humidity, RESPONSE_SIZE_HUM) == RESPONSE_SIZE_HUM) {     
        msg.humidity = (humidity[1] << 8) | (humidity[0]);
    }
    else {      
        ROS_ERROR("Humidity data is invalid.");
    }

    // Populate message with water sensor data
    if(this->write("WTR", water, RESPONSE_SIZE_WTR) == RESPONSE_SIZE_WTR) {     
        msg.water_sensor = (water[1] << 8) | (water[0]);
    }
    else {      
        ROS_ERROR("Water data is invalid.");
    }

    // Populate message with main housing pressure data
    if(this->write("PIN", pressure_internal, RESPONSE_SIZE_PIN) == RESPONSE_SIZE_PIN) {     
        msg.internal_pressure = (pressure_internal[2] << 16) | (pressure_internal[1] << 8) | (pressure_internal[0]);
        // No conversions needed, pressure is in Pa
    }
    else {      
        ROS_ERROR("Internal housing pressure data is invalid."); 
    }

    // Populate message with external water pressure data
    if(this->write("PEX", pressure_external, RESPONSE_SIZE_PEX) == RESPONSE_SIZE_PEX) {     
        msg.external_pressure = (pressure_external[1] << 8) | (pressure_external[0]);
        //msg.external_pressure = (msg.external_pressure - 220) * 1241/2500;
        // Convert from 0.01psi to Pa
        constexpr double factor = 68.94757;
        msg.external_pressure *= factor;
    }
    else {      
        ROS_ERROR("External water pressure data is invalid."); 
    }
}

bool power_board::power_enabler(PowerEnableReq &req, PowerEnableRes &res)
{      
    // Enable/Disable Power to Motors
    std::string out = "PME0";
    out.replace(3, 1, req.motor_pwr_enable ? "1" : "0");
    write(out);

    // Enable/Disable 5V Rail
    out.replace(1, 1, "5");
    out.replace(3, 1, req.rail_5V_pwr_enable ? "1" : "0");
    write(out);

    // Enable/Disable 12V/9V Rails
    out.replace(1, 1, "T");
    out.replace(3, 1, req.rail_12V_9V_pwr_enable ? "1" : "0");
    write(out);

    // Enable/Disable Running Batteries in Parallel
    out = "BP0";
    out.replace(2, 1, req.parallel_batteries_enable ? "1" : "0");
    write(out);
}


int main(int argc, char ** argv)
{
    ros::init(argc, argv, "power_board");
    ros::NodeHandle nh("~");

    monitor::GetSerialDevice srv;
    nh.getParam("device_id", srv.request.device_id);

    int loop_rate;
    nh.getParam("loop_rate", loop_rate);

    ros::ServiceClient client = nh.serviceClient<monitor::GetSerialDevice>("/serial_manager/GetDevicePort");
    if (!client.call(srv)) {
        ROS_INFO("Couldn't get \"%s\" file descripter. Shutting down", srv.request.device_id.c_str());
        return 1;
    }

    ROS_INFO("Using Power Board on fd %s\n", srv.response.device_fd.c_str());
    power_board device(srv.response.device_fd);

    ros::Publisher pub = nh.advertise<peripherals::powerboard>("power_board_data", 10);
    ros::ServiceServer pwr_en = nh.advertiseService("PowerEnable", &power_board::power_enabler, &device); 

    // Main loop
    ros::Rate r(loop_rate);
    while(ros::ok()) {
        // Publish message to topic 
        peripherals::powerboard msg;
        device.get_powerboard_data(msg);
        pub.publish(msg);

        // End of loop maintenance
        ros::spinOnce();
        r.sleep();
    }

    return 0;
}

