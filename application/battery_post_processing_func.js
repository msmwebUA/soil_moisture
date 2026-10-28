// copy this code to data key configuration (battery_v key) for Battery level widget
let fullBatVoltage = 4.5;
let emptyBatVoltage = 3.7;
if (value >= fullBatVoltage) return 100;
if (value < emptyBatVoltage) return 0;
let newVal = (value - emptyBatVoltage) * 100 / (fullBatVoltage - emptyBatVoltage);
return newVal.toFixed(1);