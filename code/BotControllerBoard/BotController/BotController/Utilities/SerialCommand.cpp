/**
 * SerialCommand - A Wiring/Arduino library to tokenize and parse commands
 * received over a serial port.
 * 
 * Copyright (C) 2012 Stefan Rado
 * Copyright (C) 2011 Steven Cogswell <steven.cogswell@gmail.com>
 *                    http://husks.wordpress.com
 * 
 * Version 20120522
 * 
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "SerialCommand.h"

int strcicmp(char const *a, char const *b)
{
	for (;; a++, b++) {
		int d = tolower(*a) - tolower(*b);
		if (d != 0 || !*a)
		return d;
	}
}

/**
 * Constructor makes sure some things are set.
 */
SerialCommand::SerialCommand()
  : commandList(NULL),
    commandCount(0),
    defaultHandler(NULL),
    term('\r'),           // default terminator for commands, newline character
    last(NULL),
	savelast(NULL)
{
	withChecksum = false;
	strcpy(delim, " "); // strtok_r needs a null-terminated string
	clearBuffer();
}

void SerialCommand::useChecksum(bool really) {
	withChecksum = really;
}

/**
 * Adds a "command" and a handler function to the list of available commands.
 * This is used for matching a found token in the buffer, and gives the pointer
 * to the handler function to deal with it.
 */
void SerialCommand::addCommand(const char *command, void (*function)()) {
  #ifdef SERIALCOMMAND_DEBUG
    Serial.print("Adding command (");
    Serial.print(commandCount);
    Serial.print("): ");
    Serial.println(command);
  #endif

  commandList = (SerialCommandCallback *) realloc(commandList, (commandCount + 1) * sizeof(SerialCommandCallback));
  strncpy(commandList[commandCount].command, command, SERIALCOMMAND_MAXCOMMANDLENGTH);
  commandList[commandCount].function = function;
  commandCount++;
}

/**
 * This sets up a handler to be called in the event that the receveived command string
 * isn't in the list of commands.
 */
void SerialCommand::setDefaultHandler(void (*function)(const char *)) {
  defaultHandler = function;
}


/**
 * This checks the Serial stream for characters, and assembles them into a buffer.
 * When the terminator character (default '\n') is seen, it starts parsing the
 * buffer for a prefix command, and calls handlers setup by addCommand() member
 */
void SerialCommand::readSerial() {
  while (Serial.available() > 0) {
    char inChar = Serial.read();   // Read single available character, there may be more waiting
    #ifdef SERIALCOMMAND_DEBUG
      Serial.print(inChar);   // Echo back to serial stream
    #endif

    if (inChar == term) {     // Check for the terminator (default '\r') meaning end of command
      #ifdef SERIALCOMMAND_DEBUG
        Serial.print("Received: ");
        Serial.println(buffer);
      #endif

	  savelast = last;
      char *command = strtok_r(buffer, delim, &last);   // Search for command at start of buffer
      if (command != NULL) {
        boolean matched = false;
        for (int i = 0; i < commandCount; i++) {
          #ifdef SERIALCOMMAND_DEBUG
            Serial.print("Comparing [");
            Serial.print(command);
            Serial.print("] to [");
            Serial.print(commandList[i].command);
            Serial.println("]");
          #endif

          // Compare the found command against the list of known commands for a match
          if (strncasecmp(command, commandList[i].command, SERIALCOMMAND_MAXCOMMANDLENGTH) == 0) {
            #ifdef SERIALCOMMAND_DEBUG
              Serial.print("Matched Command: ");
              Serial.println(command);
            #endif

			errorCode = NO_ERROR;

			// compute checksum of command and all params
			computeChecksum(buffer);
			
            // Execute the stored handler function for the command
			// Within the handler, endOfParams has to be called that checks the checksum (if set
            (*commandList[i].function)();

            matched = true;
            break;
          }
        }
        if (!matched && (defaultHandler != NULL)) {
          (*defaultHandler)(command);
        }
      }
      clearBuffer();
    }
    else if (isprint(inChar)) {     // Only printable characters into the buffer
      if (bufPos < SERIALCOMMAND_BUFFER) {
        buffer[bufPos++] = inChar;  // Put character into buffer
        buffer[bufPos] = '\0';      // Null terminate
      } else {
        #ifdef SERIALCOMMAND_DEBUG
          Serial.println("Line buffer is full - increase SERIALCOMMAND_BUFFER");
        #endif
      }
    }
  }
}

/*
 * Clear the input buffer.
 */
void SerialCommand::clearBuffer() {
  buffer[0] = '\0';
  bufPos = 0;
}

/**
 * Retrieve the next token ("word" or "argument") from the command buffer.
 * Returns NULL if no more tokens exist.
 */
char *SerialCommand::next() {
  savelast = last;
  char* nextParam = strtok_r(NULL, delim, &last);
  return nextParam;
}

void SerialCommand::unnext() {
	last = savelast;
}


void SerialCommand::computeChecksum(char *str) {
	int c;
	checksum = 0;
	while (c = *str++) {
		checksum = ((checksum << 5) + checksum) + c; /* hash * 33 + c */
	}
}


bool SerialCommand::getParamInt(int16_t &param) {
	char* arg = next();
	if (arg != NULL) {
		param = atoi(arg);
		return true;
	}
	return false;
}

bool SerialCommand::getParamFloat(float &param) {
	char* arg = next();
	if (arg != NULL) {
		param = atof(arg);
		return true;
	}
	return false;
}

bool SerialCommand::getNamedParam(char* paramName, char* paramValue) {
	char* arg = next();
	paramValue = NULL;

	if (arg != NULL) {
		// extract name
		char* intstr = NULL;
		char* name = strtok_r(NULL, "=", &intstr);

		// extract param
		if (name != NULL)
			paramValue = strtok_r(NULL, delim, &intstr);
		
		// check if name is the right one
		if ((name != NULL) && (paramValue != NULL) && (strncasecmp(name, paramName, SERIALCOMMAND_MAXCOMMANDLENGTH) == 0)) {
			return true;
		} else {
			// no, param has wrong name or is invalid, push argument back
			unnext();
			paramValue = NULL;
			return false;
		}
	}
	return false;
}


bool SerialCommand::getNamedParamInt(char* paramName,    int16_t &param, bool &paramSet) {
	char* arg = next();
	char* paramValue = NULL;
	paramSet = false;

	if (arg != NULL) {
		if (getNamedParam(paramName, paramValue)) {
			param = atoi(paramValue);
			paramSet = false;
		}
	}
	return true;
}

bool SerialCommand::getNamedParamFloat(char* paramName, float &param,   bool &paramSet) {
	char* arg = next();
	char* paramValue = NULL;
	paramSet = false;

	if (arg != NULL) {
		if (getNamedParam(paramName, paramValue)) {
			param = atof(paramValue);
			paramSet = true;
		}
	}
	return true;
}
bool SerialCommand::getNamedParamString(char* paramName,  char* &param,   bool &paramSet) {
	char* arg = next();
	char* paramValue = NULL;
	paramSet = false;
	if (arg != NULL) {
		if (getNamedParam(paramName, paramValue)) {
			param = paramValue;
		}
	}
	return true;
}

bool SerialCommand::endOfParams() {
	if (withChecksum) {
		errorCode = NO_ERROR;
		int paramCheckSum = 0;
		bool chksumSet = false;
		bool paramOK = getNamedParamInt("chk",paramCheckSum, chksumSet);
		if (paramOK) {
			if (paramCheckSum == checksum) {
				return true;
			}
			else {
				Serial.print(F(".cheksum wrong("));
				Serial.print(paramCheckSum);
				Serial.print("!=");
				Serial.print(checksum);
				Serial.print(")");
				errorCode = CHECKSUM_WRONG;
				return false;
			}
		}
		Serial.print(F("chksum expected"));
		errorCode = CHECKSUM_EXPECTED;
		return false;		
	} else {
		return true;
	}
}

bool SerialCommand::getParamString(char* &param) {
	char* arg = next();
	if (arg != NULL) {
		param = arg;
		return true;
	}
	return false;
}