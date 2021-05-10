#include "HX711.h"

// Please be aware, that for this code setup to work, the two magnets used for sensing the orientation of the flywheel 
// need to have opposing polarity poles facing the hall effect sensor.

// user constants
#define LEVER_LENGTH_MILLIS 30 // prony break level arm length in millimeters... needs to be changed if distance is different. 
#define DATA_INTERVAL_MILLIS 100ul // how often to query load cell and send a sample of data to serial. 100 milliseconds seems to work fine.
#define HES_SENSITIVITY 3 // flywheel state is only allowed to change if the measured magnetic field strength deviates by more than this amount around the calibrated medium
#define HES_CALIBRATION_ITERATIONS 2500 // defines how many iterations to use to calibrate the hall effect sensor

// user variables
float calibration_factor = -2040; // load cell calibration factor, needs to be figured out manually for each individual load cell used.

// system constants
#define MINUTE_IN_MILLIS 60000.0 // conversion of 1 minute into milliseconds
#define NEWTON_IN_GRAM 0.0098 // constant for calculating torque
#define WATTS_CONSTANT 0.105 // used for calculating power output in watts
#define HES_TYPICAL 503 // a typical midway value for 49E linear hall effect sensor... used for warnings.
#define HES_WARNING_SENSITIVITY 5 // used for checking for potential issues with hall effect sensor calibration

// system variables
uint32_t start_time = 0;
uint32_t delta_time = 0;
uint32_t next_sample_millis = 0; 
uint32_t rpm_sample_start = 0;
uint16_t half_rotations = 0;
uint16_t half_rotation_count = 0;
uint16_t half_rotation_count_remainder = 0;
uint16_t average_half_rotation_count = 0;
float rpm = 0.0;
float total_revolutions = 0.0;
float friction_load = 0.0;
float torque_Nmm = 0.0;
float power = 0.0;
bool is_active = false;
bool flywheel_state = false;
bool flywheel_seen = false;
int16_t HES_current = 0;
int16_t HES_midway = 0;
int16_t HES_low_threshold = 0;
int16_t HES_high_threshold = 0;
HX711 scale;

// set up pins
#define LOADCELL_DOUT_PIN  3
#define LOADCELL_SCK_PIN  2
#define HALL_EFFECT_SENSOR A3


void setup() {
  Serial.begin(57600);
  Serial.println("Booting/calibrating...");
  
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor); 
  scale.tare(); 

  // calibrate hall effect sensor
  int64_t running_count = 0;
  for(int i = 0; i < HES_CALIBRATION_ITERATIONS; i++){
    running_count += analogRead(HALL_EFFECT_SENSOR);
  }
  HES_midway = (int)(running_count / HES_CALIBRATION_ITERATIONS);
  Serial.print("Hall effect sensor calibrated to: ");
  Serial.println(HES_midway);
  HES_low_threshold = HES_midway - HES_SENSITIVITY;
  HES_high_threshold = HES_midway + HES_SENSITIVITY;
  if(HES_midway < (HES_TYPICAL - HES_WARNING_SENSITIVITY)){
    Serial.println("WARNING: Hall effect sensor value calibrated to abnormally low value!");
  }
  else if(HES_midway > (HES_TYPICAL + HES_WARNING_SENSITIVITY)){
    Serial.println("WARNING: Hall effect sensor value calibrated to abnormally high value!");
  }

  scale.tare();

  Serial.println("Ready.");
}

void loop() {
  // toggle whether dynamo is actively measuring and sending data or not
  if(!is_active){
    is_active = !is_active;
    Serial.println("Send any character to start measuring.");
    Serial.println("While measuring, send any character to stop measuring.");
    while(!Serial.available()){
      delay(1);
    }
    scale.tare();
    empty_buffer();
    Serial.println("ms, rpm, g, N.mm, W");
    start_time = millis();
    next_sample_millis = start_time;
  }
  else if(Serial.available()){
    is_active = !is_active;
    empty_buffer();
    return;
  }
  // set up next period of data sampling
  next_sample_millis += DATA_INTERVAL_MILLIS;
  HES_current = HES_midway;
  half_rotation_count = 0;
  half_rotation_count_remainder = 0;
  half_rotations = 0;
  total_revolutions = 0.0;
  average_half_rotation_count = 0;
  flywheel_seen = false;
  rpm_sample_start = millis();

  // sample data
  while(millis() < next_sample_millis){
    HES_current = analogRead(HALL_EFFECT_SENSOR);
    if((HES_current > HES_high_threshold && !flywheel_state) || (HES_current < HES_low_threshold && flywheel_state)){
      flywheel_state = !flywheel_state;
      if(!flywheel_seen){
        flywheel_seen = !flywheel_seen;
        half_rotation_count_remainder = half_rotation_count;
      }
      else{
        average_half_rotation_count += half_rotation_count;
        half_rotations++;
      }
      half_rotation_count = 0;
    }
    else{
      half_rotation_count++;
    }
  }
  delta_time = millis() - rpm_sample_start;
  half_rotation_count_remainder += half_rotation_count;
  
  friction_load = scale.get_units();

  // data calculations
  average_half_rotation_count /= half_rotations;
  if(half_rotations > 0){
    total_revolutions = ((float)((float)half_rotation_count_remainder / (float)average_half_rotation_count) + (float)half_rotations) / 2.0f;
  }
  rpm = MINUTE_IN_MILLIS / (double)delta_time * total_revolutions;

  torque_Nmm = friction_load * NEWTON_IN_GRAM * LEVER_LENGTH_MILLIS;

  power = torque_Nmm * WATTS_CONSTANT * rpm / 1000;

  // transmit data
  Serial.print((millis() - start_time));
  Serial.print(", ");
  Serial.print(rpm);
  Serial.print(", ");
  Serial.print(friction_load, 2);
  Serial.print(", ");
  Serial.print(torque_Nmm, 2);
  Serial.print(", ");
  Serial.println(power, 2);
}

void empty_buffer(){
  while(Serial.available()){
    Serial.read();
  }
  return;
}