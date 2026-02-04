# **MaraX Evolution HMI \- User Guide**

This guide explains how to use the Nextion Touchscreen Interface and the Rotary Encoder to control your MaraX Evolution machine.

## **1\. First-Time Setup**

### **WiFi Configuration**

When powered on for the first time (or if it cannot connect to WiFi), the HMI creates a Hotspot.

1. Connect your phone or laptop to the WiFi network named: **esp32-arduino-screen-Setup**.  
2. A captive portal should open automatically (or visit 192.168.4.1).  
3. Select your home WiFi network and enter the password.  
4. The device will reboot and connect to your local network.

### **Pairing with Main Controller**

The HMI communicates with the machine via **ESP-NOW** (a fast, direct wireless protocol).

1. Ensure the **Main Controller** (inside the machine) is powered on.  
2. Power on the **HMI**.  
3. They will automatically find each other and pair within 10 seconds. You will see live temperature data appear once paired.

## **2\. Dashboard (Home Screen)**

This is the main view while brewing.

* **Live Chart:** Visualizes the shot in real-time.  
  * **Red Line:** Pressure (0-15 bar).  
  * **Blue Line:** Flow Rate (0-5 g/s) \[Requires Scale\].  
  * **White Line:** Target Profile.  
* **Data Fields:**  
  * **Timer:** Starts automatically when the pump engages.  
  * **Weight:** Live gram reading from the drip tray scale.  
  * **Temperatures:** Boiler (Steam) and Heat Exchanger (Brew) temps.  
* **Tare Button:** Zeros the scale manually \[Requires Scale\].

## **3\. Brewing Settings (Page 1\)**

Access this page to change machine parameters.

* **Brew Temperature:**  
  * Use the **Slider** on the touchscreen OR turn the **Rotary Encoder** to adjust the target brew temperature (e.g., 93.0°C).  
* **Brew Mode:**  
  * **Coffee Priority:** Keeps the heat exchanger at the perfect brew temp. Steam might be weaker.  
  * **Steam Priority:** Keeps the boiler hot for powerful steam. Brew temp may fluctuate more.  
* **Steam Boost:**  
  * When enabled, the machine aggressively heats the boiler immediately after a shot is finished to recover steam pressure quickly.

## **4\. Pressure & Flow Profiling (Page 2\)**

This menu controls how the pump operates during a shot.

### **Modes**

1. **Manual:** The machine behaves like a standard espresso machine (full pump power).  
2. **Flat:** The pump targets a specific constant value (e.g., maintain exactly 9.0 bar or 2.0 g/s).  
   * Select "Flat" and turn the encoder to set the target value.  
3. **Profile:** The machine follows a saved pre-programmed curve.

### **Profile Configuration**

* **Source:** Choose what the pump controls.  
  * **Pressure:** Standard profiling (e.g., pre-infusion at 2 bar, ramp to 9 bar).  
  * **Flow:** \[Requires Scale\] The pump adjusts to maintain a specific flow rate (e.g., 2.5 g/s) regardless of puck resistance.  
* **Target:** Choose when to advance to the next step.  
  * **Time:** Steps change after X seconds.  
  * **Weight:** Steps change after X grams are in the cup.

### **Selecting & Editing Profiles**

* **Select:** Turn the Rotary Encoder to cycle through saved profiles (displayed at the bottom).  
* **Edit (On-Screen):** Tap the table cells to select them, then turn the Encoder to adjust values (Target / Duration).  
* **Edit (Web Interface):** See Section 6 below.

## **5\. Maintenance (Page 3\)**

### **Scale Calibration**

1. Navigate to **System Settings**.  
2. Tap **Calibrate Scale**.  
3. **Step 1:** Ensure the scale is empty. Tap "Next" or the Encoder button to Tare.  
4. **Step 2:** Place a known weight on the scale.  
5. **Step 3:** Use the **Rotary Encoder** to adjust the displayed "Reference Weight" until it matches your known weight (e.g., 100.0g).  
6. Press the Encoder button to save.

### **Cleaning Cycle**

Automated backflush routine.

1. Insert a blind basket and detergent.  
2. Tap **Cleaning Cycle**.  
3. Follow the on-screen prompts:  
   * "Pull Lever" (Starts Pump)  
   * "Lower Lever" (When buzzing/pausing)  
   * Repeat 5x with detergent, 5x with water.

### **MQTT Configuration**

If connected to WiFi, tapping **System Settings** displays an IP address. Enter this IP in a web browser to configure your MQTT Broker settings for Home Assistant integration. This sets the MQTT settings for the main controller.

## **6\. Web Profile Editor**

The HMI hosts a built-in website for easier profile creation.

1. Ensure the HMI is connected to your WiFi.  
2. On a computer or phone connected to the same WiFi, open a browser.  
3. Go to: **http://esp32-arduino-screen.local**  
4. **Features:**  
   * Visual Graph Editor: Drag and drop points to create profiles.  
   * Save/Load: Save profiles to the HMI's memory slots (up to 32).  
   * Import/Export: Share profiles as JSON files.  
   * Live Sync: Changes made on the web update the screen instantly.

## **7\. Troubleshooting**

* **"System Message: Timeout"**: The main controller didn't respond. Ensure the machine is on. If the issue persists, reboot the HMI.  
* **No Chart Data:** Check if the scale is connected properly to the main controller.  
* **WiFi Issues:** If you change your WiFi password, the device will eventually reset to Hotspot mode (esp32-arduino-screen-Setup) so you can re-configure it.