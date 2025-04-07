### [Nano RC12-3](Nano/RC12-3)
- uses a Nano (with ENC28J60 Ethernet shield) & an on-board mounted Cytron for rate control
	- 1 or 2 [MD13S](https://www.cytron.io/p-13amp-6v-30v-dc-motor-driver) for rate control
- self assembled through-hole board, least expensive
- controls two rates
- can be connected to a relay module with a 20 pin cable or dupont connectors
- work switch
- pressure switch
- [OSHPARK]([https://oshpark.com/shared_projects/UU8e90h9](https://oshpark.com/shared_projects/fRBlR1Se)
![RC12-3](https://github.com/user-attachments/assets/63630fa4-6896-430a-83e5-a736d1104b5a)


#
### [Teensy 4.1 RC11-2](Teensy/RC11-2_PCB)
- uses a Teensy & an on-board mounted Cytron for rate control
	- either (1) [MD13S](https://www.cytron.io/p-13amp-6v-30v-dc-motor-driver) for single rate control
 	- or (1) [MDD10A](https://www.cytron.io/p-10amp-5v-30v-dc-motor-driver-2-channels) for dual rate control
- small components are surface mount parts
- Teensy Ethernet 
- can control two rates and eight sections
	- 8 relays (SPDT 12V output)
	- (1) 5V or 12V analog (pressure) input, potentiometer adjustable filtering
	- (2) optically isolated digital (rate/pulse) inputs (5V or 12V)
- 3.3V I2C headers (Qwiic)
	- supports optional external RelayDriver5 I2C board
 - work switch
 - canbus
![RC11-2](https://github.com/user-attachments/assets/0e30f5d2-c3a5-427f-91ff-3a5899effb45)

#
### [ESP32 RC15](ESP32/RC15)
- uses an ESP32 (optional W5500 Ethernet Module)
- mostly surface mount parts
- can control two rates and 7-14 sections
	- (2) DRV8870 motor drivers with reversible outputs for rates
	- (7) DRV8870 motor drivers for either 7 reversible (motorized ball valve) outputs or 14 single ended (high or low) section outputs
- (4) 5V analog inputs (or 2 differential inputs)
- (2) optically isolated digital (rate/pulse) inputs, up to 12V
- RS485 chip & header
- 3.3V I2C header
	- supports optional external RelayDriver5 I2C board
![ESP32-RC15](ESP32/RC15/RC15.jpg)

#
### [RelayDriver5](RelayDriver5)
- PCA9535 based I2C IO expander
- designed to plug into 8 or 16 channel relay board with Qwiic cable
![RelayDriver5 photo](RelayDriver5/RelayDriver.jpg)
#
### Firmware
https://github.com/SK21/AOG_RC
