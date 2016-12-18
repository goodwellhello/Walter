/* 
* RotaryEncoder.cpp
*
* Created: 23.04.2016 23:13:20
* Author: JochenAlt
*/

#include "Arduino.h"
#include "RotaryEncoder.h"
#include "BotMemory.h"
#include "utilities.h"


void RotaryEncoder::switchConflictingSensor(bool powerOn) {
	if (powerOn) {
		pinMode(I2C_ADDRESS_ADDON_VDD_PIN,OUTPUT);
		pinMode(I2C_ADDRESS_ADDON_GND_PIN,OUTPUT);

		digitalWrite(I2C_ADDRESS_ADDON_VDD_PIN, HIGH);
		digitalWrite(I2C_ADDRESS_ADDON_GND_PIN, LOW);
	} else {
		pinMode(I2C_ADDRESS_ADDON_VDD_PIN,INPUT);
		digitalWrite(I2C_ADDRESS_ADDON_VDD_PIN, LOW); // disable internal pullup

		pinMode(I2C_ADDRESS_ADDON_GND_PIN,INPUT);
		digitalWrite(I2C_ADDRESS_ADDON_GND_PIN, LOW); // disable internal pullup
	}
}


void RotaryEncoder::setup(ActuatorConfiguration* pActuatorConfig, RotaryEncoderConfig* pConfigData, RotaryEncoderSetupData* pSetupData)
{
	configData = pConfigData;
	setupData = pSetupData;
	actuatorConfig = pActuatorConfig;

	passedCheck= false;	
	
	if (memory.persMem.logSetup) {
		logger->print(F("   setup encoder(0x"));
		logger->print(setupData->I2CAddress,HEX);
		logger->println(")");
		logger->print(F("   "));
		configData->print();
		logger->print(F("   "));
		setupData->print();
	}
	
	bool doProgI2CAddr = doProgI2CAddress();						// true, if this sensor needs reprogrammed i2c address 
	uint8_t i2cAddress = i2CAddress(false);							// i2c address before reprogramming
	uint8_t proggedI2CAddr = i2cAddress + (I2C_ADDRESS_ADDON<<2);	// i2c address after reprogramming

	if (memory.persMem.logSetup) {
		logger->print(F("   connecting to I2C 0x"));
		logger->print(i2cAddress, HEX);
		if (doProgI2CAddr) {
			logger->print(F(", reprogramm to 0x"));
			logger->print(proggedI2CAddr, HEX);
		}
		logger->println();
		logger->print(F("   "));
	}

	if (doProgI2CAddr) {
		// check if new addr is already there
		Wire.beginTransmission(proggedI2CAddr );
		byte error = Wire.endTransmission();
		if (error == 0) {
			if (memory.persMem.logSetup)
				logger->println(F("new I2C works already."));
			// new address already set, dont do anything
			doProgI2CAddr = false;			
			sensor.setI2CAddress(proggedI2CAddr);
			sensor.begin();					// restart sensor with new I2C address
			switchConflictingSensor(true /* = power on */);
		}
	} else {
		sensor.setI2CAddress(i2cAddress);
		sensor.begin();		
	}


	//set clock wise counting
	sensor.setClockWise(isClockwise());

	// check communication		
	currentSensorAngle = sensor.angleR(U_DEG, true);
	
	// do we have to reprogramm the I2C address?
	if (doProgI2CAddr) {
		// address reg contains i2c addr bit 0..4, while bit 4 is inverted. This register gives bit 2..6 of i2c address, 0..1 is in hardware pins
		uint8_t i2cAddressReg = sensor.addressRegR();
		// new i2c address out of old address reg is done setting 1. bit, and xor the inverted 4. bit and shifting by 2 (for i2c part in hardware)
		uint8_t newi2cAddress = ((i2cAddressReg+I2C_ADDRESS_ADDON) ^ (1<<4))<< 2; // see datasheet of AS5048B, computation of I2C address 
		if (newi2cAddress != proggedI2CAddr)
			logFatal(F("new I2C address wrong"));

		if (memory.persMem.logSetup) {
			logger->print(F("reprog: "));
			logger->print(F(" AddrR(old)=0x"));
			logger->print(i2cAddressReg, HEX);
			logger->print(F(" AddrR(new)=0x"));
			logger->print(i2cAddressReg+I2C_ADDRESS_ADDON, HEX);

			logger->print(F(" i2cAddr(new)=0x"));
			logger->println(newi2cAddress, HEX);
			logger->print(F("   "));
		}
		sensor.addressRegW(i2cAddressReg+I2C_ADDRESS_ADDON);		
		sensor.setI2CAddress(newi2cAddress); 
		sensor.begin(); // restart sensor with new I2C address
		
		// check new i2c address
		uint8_t i2cAddressRegCheck = sensor.addressRegR();
		if (i2cAddressRegCheck != (i2cAddressReg+I2C_ADDRESS_ADDON))
			logFatal(F("i2c AddrW failed"));
		else {
			// sensor.doProgCurrI2CAddress();
		}
		
		// now boot the other device with the same i2c address, there is no conflict anymore
		switchConflictingSensor(true /* = power on */);
		delay(20); // after changing the I2C address the sensor needs some time until communication can be initiated
	}

	// check communication
	communicationWorks = false;
	Wire.beginTransmission(i2CAddress(true));
	byte error = Wire.endTransmission();
	communicationWorks = (error == 0);
	logger->print(F("comcheck(0x"));
	logger->print(i2CAddress(true), HEX);
	logger->print(F(") "));
	if (!communicationWorks) 
		logger->println(F("failed!"));
	else {
		logger->print(F("ok"));
	}
	currentSensorAngle = 0.0;
	if (communicationWorks) {
		currentSensorAngle = sensor.angleR(U_DEG, true);	
		logger->print(F("   angle="));
		logger->println(currentSensorAngle);
		logger->print(F("   offset="));
		logger->println(actuatorConfig->angleOffset);
	}
} 


float RotaryEncoder::getAngle() {
	float angle = currentSensorAngle;
	
	angle -= actuatorConfig->angleOffset;
	return angle;
}

void RotaryEncoder::setNullAngle(float rawAngle) {
	configData->nullAngle = rawAngle;
}

float RotaryEncoder::getAngleOffset() {
	return actuatorConfig->angleOffset;
}

float RotaryEncoder::getNullAngle() {
	return configData->nullAngle;
}

float RotaryEncoder::getRawSensorAngle() {
	return currentSensorAngle;
}

bool RotaryEncoder::getNewAngleFromSensor() {
	float rawAngle = sensor.angleR(U_DEG, true); // returns angle between 0..360
	float nulledRawAngle = rawAngle - getNullAngle();
	if (nulledRawAngle> 180.0)
		nulledRawAngle -= 360.0;
	if (nulledRawAngle< -180.0)
		nulledRawAngle += 360.0;

	if (sensor.endTransmissionStatus() != 0) {
		failedReadingCounter = max(failedReadingCounter, failedReadingCounter+1);
		logActuator(setupData->id);
		logger->print(failedReadingCounter);
		logger->print(F(".retry "));
		logError(F("enc comm"));
		return false;
	} else {
		failedReadingCounter = 0;
	}
	
	// apply first order low pass to filter sensor noise
	const float reponseTime = float(ENCODER_FILTER_RESPONSE_TIME)/1000.0;	// signal changes shorter than 2 samples are filtered out
	const float complementaryFilter = reponseTime/(reponseTime + (float(ENCODER_SAMPLE_RATE)/1000.0));
	const float antiComplementaryFilter = 1.0-complementaryFilter;

	currentSensorAngle = antiComplementaryFilter*nulledRawAngle + complementaryFilter*nulledRawAngle;
	return true;
}

bool RotaryEncoder::fetchSample(uint8_t no, float sample[], float& avr, float &variance) {
	avr = 0.;
	for (int check = 0;check<no;check++) {
		if (check > 0) {
			delay(ENCODER_SAMPLE_RATE); // that's not bad, this function is called for calibration only, not during runtime
		}
		getNewAngleFromSensor(); // measure the encoder's angle
		float x = getRawSensorAngle();
		sample[check] = x;
		avr += x;
	}
	
	avr = avr/float(no);
	// compute average and variance, and check if values are reasonable;
	variance = 0;
	for ( int check = 0;check<no;check++) {
		float d = sample[check]-avr;
		variance += d*d;
	}
	variance = variance/no;

	return (variance <= ENCODER_CHECK_MAX_VARIANCE);
}

bool RotaryEncoder::fetchSample(float& avr, float &variance) {
	float sample[ENCODER_CHECK_NO_OF_SAMPLES];
	bool ok = fetchSample(ENCODER_CHECK_NO_OF_SAMPLES,sample,avr, variance);
	return ok;
}

float RotaryEncoder::checkEncoderVariance() {
	
	// collect samples of all encoders
	float value[ENCODER_CHECK_NO_OF_SAMPLES];
	float avr, variance;
	passedCheck = fetchSample(ENCODER_CHECK_NO_OF_SAMPLES,value, avr, variance);

	if (memory.persMem.logEncoder) {
		logger->print(F("encoder("));
		logActuator(setupData->id);
		logger->print(")");

		if (!passedCheck) {
			logger->print(F(" avr="));
			logger->print(avr);
	
			logger->print(F(" var="));
			logger->print(variance);
			logger->print(F(" not"));
		}
		logger->println(F(" stable."));
	}
	return variance;
}