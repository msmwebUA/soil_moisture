# Soil Moisture Monitoring System

An IoT-based monitoring system for strawberry cultivation. The system was developed as a student project for monitoring soil conditions and temperature in strawberry fields and greenhouses.

The project measures:

- Soil moisture
- Air temperature
- Battery voltage

Sensor nodes send data over LoRaWAN to a gateway, which forwards it to a server for storage and visualization.

## Architecture

Field Sensor Unit (Node) -> LoRaWAN Gateway -> ChirpStack -> ThingsBoard -> PostgreSQL

### Sensor Node

- Wio-E5 Mini (based on STM32WLE5 module)
- DFRobot SEN0193 capacitive soil moisture sensor
- DS18B20 temperature sensor
- Battery-powered, low-power operation

### Gateway

- Raspberry Pi 4
- LR1302 LoRaWAN concentrator
- LTE Internet connection

### Server

- Docker
- ChirpStack
- Mosquitto MQTT
- PostgreSQL
- ThingsBoard CE

## Features

- Long-range LoRaWAN communication
- Low power consumption
- Historical data storage
- Device location tracking
- Remote measurement interval configuration (downlink)
- Dashboard visualization

## Repository Structure

**application/** ThingsBoard widgets  
**chirpstack/** Payload codec and configuration  
**docker/** Docker deployment files  
**docs/** Documentation, readme files and datasheets  
**gateway/** Gateway configuration and tools  
**sensor_unit_code/** Field Sensore Unit firmware (PlatformIO project)  

## Author 

Sergii Mishchaniuk