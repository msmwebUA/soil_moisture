### ThingsBoard confuguration using UI ###

1. Create device in ThingsBoard
	Log in using your tenant administrator credentials (default local setup is tenant@thingsboardorg / tenant)
	Select Add new device
	Configure:
		Name: Device-Name
		Device Profile: default
	Open your newly created device from the list and copy access token from the main details window

2. Configure ChirpStack to forward data to ThingsBoard
	Use ChirpStack’s internal HTTP Integration to push decoded JSON telemetry directly to ThingsBoard's API
		Open ChirpStack UI
		Ensure you have configured an Uplink Decoder (Codec) inside your ChirpStack Device Profile
		Select Application -> Integrations tab
			Add Integration and select HTTP
				Update uplink payload URL with your values:
					Use this URL format: http://<thingsboard-container-name>:<thingsboard-port>/api/v1/<thingsboard-access-token>/telemetry
				Set the Headers payload if needed
				Click Submit ChirpStack will now instantly POST every decoded packet it receives into ThingsBoard

3. Modify rule chain
  ThingsBoard Rule Chain used to extract the data out of the object field automatically before saving it
	In ThingsBoard UI click on Rule chains
		Click on the Root Rule Chain to open it
		Click on pencil icon to edit
			On the left sidebar menu under Components, look for Transformation nodes and drag a script node onto the canvas
				Name the node Extract ChirpStack Object and replace code (JavaScript tab!) with example from transformation_scriptjs
  		Click Add to save the node
		Now link it into your pipeline:
			Find the existing Message Type Switch node
			Drag a connector line from its Post telemetry output link to your new Extract ChirpStack Object node
			Drag a connector line from your new node (Success link) to the existing Save Telemetry node
			Click the checkmark icon to apply and save changes
  The next time your simulation script sends data, you will instantly see fields like temperature and soil_moisture_pct populate as separate rows in Latest Telemetry

4. Verify data
	Run simulator_crypto_abppy script to fire packets at the ChirpStack Gateway Bridge (1700/udp)
	In ThingsBoard open sensor device asset -> Latest telemetry tab
	If the integration is working, you will see fields like moisture_raw, moisture_pct, temperature etc

5. Create dashboard
	Latest telemetry tab -> check the checkboxes next to moisture_raw, moisture_pct, temperature etc
		Click Show on widget
			Choose a widget style bundle (eg, select Charts -> Timeseries Line Chart)
		Click Add to dashboard
			Select Create a new dashboard and name it Field Monitoring Dashboard
			Check the box labeled Open dashboard and click Add

6 Arrange dashboard
	Click pencil icon (Edit mode)
		Drag the corners of chart widget to size it neatly across your canvas grid
		To add individual digital readouts or gauges (eg, a dial for battery or temperature):
		Click Add new widget at the top
		Search for Analogue Gauge or Digital Card
		Under Datasources, select Device -> Choose device -> Select the specific key (like temperature)
		Click the Checkmark icon (Apply changes) in the bottom right to save your layout
	Your dashboard will now automatically refresh in real-time


### Add custom server-side attributes to device ###



### Add map widget ###


### Add custom widget (Pin Coordinates) ###

1. Create entity alias
	To update a device using js script, it needs a way to find it
	Open Dashboard and click Edit
		Entity aliases ➔ Add alias
			Alias Name: SelectedDevice
			Filter Type: Entity from a dashboard state 
	This allows the custom widget script to change which device the dashboard is currently looking at when a QR code is scanned

2. Create custom widget
	Main menu -> Widget Library -> Widgets bundles -> Add
		Widget bundle name: Custom Widgets
			Add new widget type
				Select Static as the widget type
			Paste the HTML, CSS, Resources, and JavaScript codes to editor tabs
			Give name like "Pin Coordinates Card"
	Dashboard -> Edit mode
		Click Add new widget ➔ select the custom bundle you created ➔ choose your custom widget