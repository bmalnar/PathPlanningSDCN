#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "spline.h"
#include "json.hpp"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  // Start in the middle lane, i.e. lane index 1
  int lane = 1;

  // Reference velocity to target is set to 0
  double ref_vel = 0.0; 

  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy,&lane,&ref_vel](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

                json msgJson;

                // Size of the previous path
                int prev_path_size = previous_path_x.size();

                if (prev_path_size > 0) {
                   car_s = end_path_s;
                }

                // Analyze position of other cars
                bool car_ahead = false;
                bool car_left  = false;
                bool car_right = false;
 
                for (int i = 0; i < sensor_fusion.size(); i++) {
                   float d = sensor_fusion[i][6];
                   // Check the lane of the other car, continue if 
                   // d does not satisfy any of the if conditions 
                   // for the 3 lanes

                   int car_lane;
                   if (d >= 0 && d < 4) {
                      car_lane = 0;
                   } else if (d >= 4 && d < 8) {
                      car_lane = 1;
                   } else if (d >= 8 && d <= 12) {
                      car_lane = 2;
                   } else {
                      continue;
                   }

                   // Get the speed of the other car
                   double vx = sensor_fusion[i][3];
                   double vy = sensor_fusion[i][4];
                   double check_speed = sqrt(vx*vx + vy*vy);
                   double check_car_s = sensor_fusion[i][5];

                   // Estimate the position of the car after 
                   // executing the previous trajectory
                   check_car_s += ((double)prev_path_size * 0.02 * check_speed);

                   // Define the safe gap value, in meters
                   double gap = 30.0;

                   // Update the value of the previously defined flags
                   if (car_lane == lane) {
                      // The car is anead of us
                      car_ahead |= (check_car_s > car_s) && ((check_car_s - car_s) < gap);
                   } else if (car_lane - lane == 1) {
                      // The car is in the right lane, lane diff eq +1
                      car_right |= ((car_s - gap) < check_car_s) && ((car_s + gap) > check_car_s);
                   } else if (lane - car_lane == 1) {
                      // The car is in the left lane, lane diff eq. -1
                      car_left |= ((car_s - gap) < check_car_s) && ((car_s + gap) > check_car_s);
                   }
                }

                // Set the max values for the speed and the acceleration
                const double MAX_SPEED = 49.5;
                const double MAX_ACC   = 0.224;

                if (car_ahead) {
                   if (car_left == false && lane > 0) {
                      // There is no car to the left and we are not in 
                      // the leftmost lane, so switch to the left lane
                      lane--;
                   } else if (car_right == false && lane < 2){
                      // There is no car to the right and we are not in 
                      // the rightmost lane, so switch to the right lane
                      lane++;
                   } else {
                      // We have nowhere to go, slow down
                      ref_vel -= MAX_ACC;
                   }
                } else {
                   // If we are not in the center lane, check if it is safe
                   // to move to the center lane
                   if (lane != 1) {
                      if ((lane == 0 && car_right == false) || (lane == 2 && car_left == false)) {
                         lane = 1;
                      }
                   }
                   // Check if there is still room to speed up safely, since
                   // there is no car ahead
                   if (ref_vel < MAX_SPEED) {
                      ref_vel += MAX_ACC;
                   }
                }

                // List of waypoints, x and y
                vector<double> ptsx;
                vector<double> ptsy;

                // Reference x, y, and yaw values
                double ref_x   = car_x;
                double ref_y   = car_y;
                double ref_yaw = deg2rad(car_yaw);

                if ( prev_path_size < 2 ) {

                   // If there are not many points, use the car as the 
                   // starting reference
                   // Use the two points that make the path tang to the car

                   double prev_car_x = car_x - cos(car_yaw);
                   double prev_car_y = car_y - sin(car_yaw);

                   ptsx.push_back(prev_car_x);
                   ptsx.push_back(car_x);

                   ptsy.push_back(prev_car_y);
                   ptsy.push_back(car_y);

                } else {

                   // Use the end point of the previous path as the 
                   // starting reference point
                   // Use the last two points of the previous path
                   // i.e. use the two points that make the path tangent
                   // to the path's previous endpoints

                   ref_x = previous_path_x[prev_path_size - 1];
                   ref_y = previous_path_y[prev_path_size - 1];

                   double ref_x_prev = previous_path_x[prev_path_size - 2];
                   double ref_y_prev = previous_path_y[prev_path_size - 2];

                   ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);

                   ptsx.push_back(ref_x_prev);
                   ptsx.push_back(ref_x);

                   ptsy.push_back(ref_y_prev);
                   ptsy.push_back(ref_y);
                }

                // Setting up target points in the future, evenly spaced 30m 
                vector<double> next_wp0 = getXY(car_s + 30, 2 + 4 * lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
                vector<double> next_wp1 = getXY(car_s + 60, 2 + 4 * lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
                vector<double> next_wp2 = getXY(car_s + 90, 2 + 4 * lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);

                // Add x of the waypoints
                ptsx.push_back(next_wp0[0]);
                ptsx.push_back(next_wp1[0]);
                ptsx.push_back(next_wp2[0]);

                // Add y of the waypoints
                ptsy.push_back(next_wp0[1]);
                ptsy.push_back(next_wp1[1]);
                ptsy.push_back(next_wp2[1]);

                // Change the reference angle of the car to 0 degrees
                for (int i = 0; i < ptsx.size(); i++) {

                   double shift_x = ptsx[i] - ref_x;
                   double shift_y = ptsy[i] - ref_y;

                   ptsx[i] = shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw);
                   ptsy[i] = shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw);
                }
                // Create the spline and set its points to be ptsx, ptsy
                tk::spline s;
                s.set_points(ptsx, ptsy);

                vector<double> next_x_vals;
                vector<double> next_y_vals;

                // Start with the points from the previous path
                for (int i = 0; i < prev_path_size; i++) {
                   next_x_vals.push_back(previous_path_x[i]);
                   next_y_vals.push_back(previous_path_y[i]);
                }

                // Continue with the position 30m ahead
                double target_x = 30.0;
                double target_y = s(target_x);
                double target_dist = sqrt(target_x * target_x + target_y * target_y);
                double x_add_on = 0;

                // Fill the rest of the points with the new values
                // The end path should always have exactly 50 points ahead
                for (int i = 1; i <= 50 - previous_path_x.size(); i++) {
                   double N = (target_dist/(.02*ref_vel/2.24));
                   double x_point = x_add_on + (target_x) / N;
                   double y_point = s(x_point);

                   x_add_on = x_point;

                   double x_ref = x_point;
                   double y_ref = y_point;

                   // Revert rotation from earlier
                   x_point = (x_ref * cos(ref_yaw) - y_ref*sin(ref_yaw));
                   y_point = (x_ref * sin(ref_yaw) + y_ref*cos(ref_yaw));

                   x_point += ref_x;
                   y_point += ref_y;

                   next_x_vals.push_back(x_point);
                   next_y_vals.push_back(y_point);
                }

          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
