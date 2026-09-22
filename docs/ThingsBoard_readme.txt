Step 1: Create the Device in ThingsBoard
Before ThingsBoard can accept any data, it needs an authenticated device slot.
	1	Open your browser and go to your ThingsBoard Web UI (http://your-server-ip:9090).
	2	Log in using your tenant administrator credentials (default local setup is usually tenant@thingsboard.org / tenant).
	3	In the left navigation rail, go to Device groups -> All.
	4	Click the "+" icon in the top right corner and select Add new device.
	5	Configure the device:
	◦	Name: Wio-E5 Soil Sensor
	◦	Device Profile: default
	6	Click Add.
	7	Open your newly created device from the list, click Copy access token from the main details window. (It will look something like A1_TEST_TOKEN_123).

Step 2: Configure ChirpStack to Forward Data
We will leverage ChirpStack’s internal HTTP Integration to push decoded JSON telemetry directly to ThingsBoard's API endpoint.
	1	Navigate to your ChirpStack UI (http://your-server-ip:9080).
	2	Ensure you have configured an Uplink Decoder (Codec) inside your ChirpStack Device Profile so that the raw 9 bytes are already broken down into JSON keys (soil_raw, temp_c, vbat_mv).
	3	Go to Applications, select your application, and click on the Integrations tab.
	4	Click Add Integration and select HTTP.
	5	Set the Headers payload if needed, but focus entirely on the Uplink payload URL:
	◦	Use this URL format: http://lw-thingsboard:9090/api/v1/YOUR_THINGSBOARD_ACCESS_TOKEN/telemetry
	◦	Replace YOUR_THINGSBOARD_ACCESS_TOKEN with the exact token copied in Step 1.
	6	Click Submit. ChirpStack will now instantly POST every decoded packet it receives right into ThingsBoard.

Step 3: Modify Rule Chain
  You can use a ThingsBoard Rule Chain to extract the data out of the object field automatically before saving it.
	1	In your ThingsBoard Web UI, click on Rule chains in the left navigation sidebar.
	2	Click on the Root Rule Chain to open it.
	3	In the top right corner, click the orange Pencil icon to enter edit mode.
	4	On the left sidebar menu under Components, look for Transformation nodes and drag a script node onto the canvas.
	5	Name the node Extract ChirpStack Object and replace code (JavaScript tab) with example from transformation_script.js:
  6	Click Add to save the node.
	7	Now link it into your pipeline:
	◦	Find the existing Message Type Switch node.
	◦	Drag a connector line from its Post telemetry output link to your new Extract ChirpStack Object node.
	◦	Drag a connector line from your new node (Success link) to the existing Save Telemetry node.
	8	Click the orange Checkmark icon in the bottom right to apply and save changes.
  The next time your simulation script sends data, you will instantly see fields like temperature and soil_moisture_pct populate as separate rows in Latest Telemetry.

Step 4: Verify Data is Arriving
	1	Run your Python simulation script to fire packets at the ChirpStack Gateway Bridge (1700/udp).
	2	Back in ThingsBoard, open your Wio-E5 Soil Sensor device asset.
	3	Click on the Latest telemetry tab.
	4	If the integration is working, you will see fields like soil_raw, temp_c, and vbat_mv dynamically populate with green checkmarks alongside recent timestamps.

Step 5: Create Your First Dashboard
	1	In the Latest telemetry tab where your metrics are streaming, check the checkboxes next to soil_raw, temp_c, and vbat_mv.
	2	Click the orange Show on widget button that appears above the table.
	3	Choose a widget style bundle (e.g., select Charts -> Timeseries Line Chart).
	4	Click Add to dashboard.
	5	A popup window will prompt you. Select Create a new dashboard and name it Soil Monitoring Dashboard.
	6	Check the box labeled Open dashboard and click Add.

Step 6: Arrange and Finalize
	1	You are now inside your new dashboard. Click the orange Pencil icon (Edit mode) in the bottom right corner.
	2	Drag the corners of your chart widget to size it neatly across your canvas grid.
	3	To add individual digital readouts or gauges (e.g., a dial for battery or temperature):
	◦	Click Add new widget at the top.
	◦	Search for Analogue Gauge or Digital Card.
	◦	Under Datasources, select Device -> Choose Wio-E5 Soil Sensor -> Select the specific key (like temp_c).
	4	Click the Checkmark icon (Apply changes) in the bottom right to save your layout.
Your dashboard will now automatically refresh in real-time every single time your Python packet injector fires off a mock transmission.
