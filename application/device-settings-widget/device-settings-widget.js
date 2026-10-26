self.onInit = function() {
      const ctx = self.ctx;
      const $container = ctx.$container;
      const $injector = ctx.$scope.$injector;
      const deviceService = $injector.get(ctx.servicesMap.get('deviceService'));
      //const telemetry = 
      // DOM elements
      // Card
      const cardTitle = $container.find('.widget-card-title');
      // Last seen
      const lastSeenOutput = $container.find('#last-seen-output');
      // Coordinates
      const showManualCoordinatesBtn = $container.find('#show-manual-coordinates-btn');
      const setCurrentCoordinatesBtn = $container.find('#set-current-coordinates-btn');
      const setManualCoordinatesBtn = $container.find('#set-manual-coordinates-btn');
      const cancelCoordinatesBtn = $container.find('#cancel-coordinates-btn');
      const coordinatesOutput = $container.find('#current-coordinates-output');
      const manualCoordinatesContainer = $container.find('#manual-coordinates-container');
      const coordinatesLatitude = $container.find('#latitude-input');
      const coordinatesLongitude = $container.find('#longitude-input');
      // Interval
      const showIntervalBtn = $container.find('#show-interval-btn');
      const setIntervalBtn = $container.find('#set-interval-btn');
      const cancelIntervalBtn = $container.find('#cancel-interval-btn');
      const intervalOutput = $container.find('#current-interval-output');
      const manualIntervalContainer = $container.find('#manual-interval-container');
      const selectInterval = $container.find('#select-interval');
      // Notifications
      const coordinatesAlert = $container.find('#coordinates-alert');
      const intervalAlert = $container.find('#interval-alert');
      const alertElements = [
            coordinatesAlert,
            intervalAlert
          ]
      
      // Global vars
      let currentDevice = {
        id: "Unknown",
        name: "Unknown"
      };
      let customSubscription; // var for subscription to device telemetry
      let currentInterval; // measurement interval

      showManualCoordinatesBtn.on('click', function() {
        showManualCoordinatesBtn.hide();
        setCurrentCoordinatesBtn.hide();
        manualCoordinatesContainer.show();
        setManualCoordinatesBtn.show();
        cancelCoordinatesBtn.show();
        clearAlerts();
      });

      cancelCoordinatesBtn.on('click', function() {
        manualCoordinatesContainer.hide();
        setManualCoordinatesBtn.hide();
        cancelCoordinatesBtn.hide();
        showManualCoordinatesBtn.show();
        setCurrentCoordinatesBtn.show();
        clearAlerts();
      });

      showIntervalBtn.on('click', function() {
        showIntervalBtn.hide();
        manualIntervalContainer.show();
        setIntervalBtn.show();
        cancelIntervalBtn.show();
        clearAlerts();
      });

      cancelIntervalBtn.on('click', function() {
        manualIntervalContainer.hide();
        cancelIntervalBtn.hide();
        setIntervalBtn.hide();
        showIntervalBtn.show();
        clearAlerts();
      });
      
      setCurrentCoordinatesBtn.on('click', function() {
        const isSecureOrigin = window.location.protocol === 'https:' || window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1';
        if (!isSecureOrigin) {
            console.log("Browser blocks geolocation if not https or localhost");
            showAlert(coordinatesAlert, "⚠️ Geolocation may be blocked by browser if connection is not secured (https or localhost)", "warning", true);
        }
        if (!navigator.geolocation) {
            console.log("Geolocation blocked or unsupported by browser");
            showAlert(coordinatesAlert, "🛑 Error: Geolocation blocked or unsupported by browser", "error", true);
            return;
        }
        navigator.geolocation.getCurrentPosition(function success(position) {
            let lat = position.coords.latitude;
            let lng = position.coords.longitude;
            // to save these, corresponding server-side attributes must be created in device settings in ThingsBoard before this script can be used
            let attributes = [
              { key: "latitude", value: lat },
              { key: "longitude", value: lng }
            ];
            saveAttributes(currentDevice, attributes);
          }, function error(err) {
            showAlert(coordinatesAlert, "🛑 GPS Error: " + err.message, "error", true);
          }, { enableHighAccuracy: true, timeout: 7000 });
      });
      
      setManualCoordinatesBtn.on("click", function() {
          let lat = coordinatesLatitude.val();
          let lng = coordinatesLongitude.val();
          if (isValidCoordinates(lat, lng)) {
            let attributes = [
              { key: "latitude", value: lat },
              { key: "longitude", value: lng }
            ];
            saveAttributes(currentDevice, attributes);
          } else {
            showAlert(coordinatesAlert, "🛑 Invalid coordinates, check your input", "error", true);
          }
      });
      
      setIntervalBtn.on("click", function() {
          let newInterval = parseInt(selectInterval.val()); // interval in minutes
          if (newInterval === parseInt(currentInterval)) {
              showAlert(intervalAlert, "Current interval is same as selected", "warning", true);
              return;
          }
          let intervalHex = newInterval.toString(16).padStart(4, "0"); // 2 bytes, padStart fills small numbers with leading zeroes
          // RPC (Remote Procedure Call) routes to the ThingsBoard's Rule Engine, and custom rule (must be created) uses a REST API call to push the downlink payload directly to ChirpStack's queue endpoint
          ctx.controlService.sendOneWayRpcCommand(
            currentDevice.id,
            {
                method: "downlink",
                params: {
                    fPort: 10,              // LoRaWAN FPort for downlink configured in device firmware
                    confirmed: false,       // Confirmed / Unconfirmed message
                    data: intervalHex       // Hex-encoded string (or plaintext depending on your codec)
                },
                timeout: 5000
            }).subscribe(function success() { 
                    showAlert(intervalAlert, '✅  Downlink command sent to rule engine.', "success", true); 
                }, function error(err) { 
                    showAlert(intervalAlert, '🛑 Error triggering downlink: ' + err, "error", true); 
                }
          );
      });
      
      // search Dashboard alias by name
      async function getCurrentDevice() {
        // search Dashboard alias by name
        let entityAliases = ctx.aliasController.entityAliases;
        for (let aliasId in entityAliases) {
          let aliasObj = entityAliases[aliasId];
          if (aliasObj.alias === "CurrentDevice") { // custom alias name created for current device
            if (aliasObj.filter.singleEntity.entityType === "DEVICE") { // check entity is device
              let deviceId = aliasObj.filter.singleEntity.id;
              console.log("Device ID found");
              try {
                  let device = await deviceService.getDevice(deviceId).toPromise();
                  // ThingsBoard returns the device metadata container
                  if (device) {
                    return {
                      id: deviceId,
                      name: device.name
                    };
                  } else {
                    console.log("Device name not found");
                  }
              } catch (err) {
                  console.error("Cannot find device name: ", err);
              }
            }
          }
        }
        return {
            id: "Unknown",
            name: "Unknown"
        }
      }
        
      function saveAttributes(device, attributes) {
          ctx.attributeService.saveEntityAttributes(device.id, "SERVER_SCOPE", attributes)
            .subscribe(function() {
              console.log("Saved attributes: " + attributes);
              showAlert(coordinatesAlert, `✅ Attributes bound to ${device.name} refresh page to see changes` , "success", true);
            }, function(err) {
                console.log(err);
                showAlert(coordinatesAlert, "🛑 Error: Failed to save attributes for device.", "error", true);
            });
      }
        
      function showAlert(el, msg, type, clear) {
          if (clear) {
              clearAlerts();
          }
          el.show();
          el.text(msg);
          switch (type) {
              case 'success':
                  el.removeClass("alert-warning alert-error");
                  el.addClass("alert-success");
                  break;
              case 'warning':
                  el.removeClass("alert-success alert-error");
                  el.addClass("alert-warning");
                  break;
              case 'error':
                  el.removeClass("alert-warning alert-success");
                  el.addClass("alert-error");
                  break;
              default:
                  console.log("Invalid 'type' argument passed to showAlert function");
          }
      }
      
      function clearAlerts() {
          for (let el of alertElements) {
              el.text("");
              el.hide;
          }
      }
      
      function isValidCoordinates(latInput, lngInput) {
          const lat = parseFloat(latInput);
          const lng = parseFloat(lngInput);
          return (
            !isNaN(lat) && lat >= -90 && lat <= 90 &&
            !isNaN(lng) && lng >= -180 && lng <= 180
          );
      }

    getCurrentDevice().then(device => {
        if (!device || !device.id || device.id === "Unknown") {
          console.log("No valid device found.");
          return;
        }
        currentDevice = device;
        // update card title
        cardTitle.text("Device Setting: <" + device.name + ">");
        // create custom subscription for telemetry
        const subscriptionOptions = {
          type: 'latest',
          datasources: [{
            type: 'entity',
            name: device.name,
            entityFilter: {
              type: 'singleEntity',
              singleEntity: { entityType: 'DEVICE', id: device.id }
            },
            dataKeys: [{
              name: 'interval_min',
              type: 'timeseries',
              label: 'interval_min',
              settings: {}
            }]
          }],
          callbacks: {
            onDataUpdated: function(subscription) {
              const data = subscription.data;
              if (!data || !data.length || !data[0].data.length) return;
              const latestPoint = data[0].data[data[0].data.length - 1];
              const ts = new Date(latestPoint[0]).toLocaleString();
              intervalOutput.text(`Current interval: ${latestPoint[1]} min (updated: ${ts})`);
              lastSeenOutput.text(`Device last seen: ${ts}`);
              currentInterval = latestPoint[1];
            },
            onDataUpdateError: function(subscription, err) {
              console.log("Telemetry update failed:", err);
            }
          }
        };
        customSubscription = ctx.subscriptionApi
          .createSubscription(subscriptionOptions, true)
          .subscribe({
            next: function(subscription) {
              console.log("Custom telemetry subscription created.");
              // Save the subscription if needed for later management.
              ctx.$scope.customTelemetrySubscription = subscription;
            },
            error: function(err) {
              console.error("Cannot create telemetry subscription:", err);
            }
          });
    }).catch(err => {
        console.log("Cannot get current device:", err);
    });
      
      
};

self.onDestroy = function() {
  if (customSubscription) {
    customSubscription.unsubscribe();
    customSubscription = null;
  }
};