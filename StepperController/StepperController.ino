#include <inttypes.h>

#define M1_STEP_PIN 9
#define M1_DIRECTION_PIN 8
#define M2_STEP_PIN 7
#define M2_DIRECTION_PIN 6

//Microstep Pins
#define MS1_PIN 16
#define MS2_PIN 14
#define MS3_PIN 15

#define NUM_MOTORS 2

#define TIME_OUT 500

#define BASE_COMPARE_REGISTER 64	//Fastest Step Speed

#define STEP_FLAG_BIT _BV(5)		
#define DIRECTION_FLAG_BIT _BV(4)	
#define FEED_FLAG_BITS 0b1111
#define DATA_TYPE_FLAG _BV(7)
#define MOTOR_FLAG _BV(6)
#define START_OF_MESSAGE 1
#define END_OF_MESSAGE _BV(1)

class Stepper {

private:
	class Command {
		Command *_next;
		uint8_t _count;
		uint8_t _feed;
		uint8_t _flags;

	public:
		Command(uint8_t flags, uint8_t count) {
			_count = count;
			_flags = flags;
			_next = NULL;
		}

		Command* next() {
			return _next;
		}

		void add(class Command *command) {
			if (!_next)
				_next = command;
			else
				_next->add(command);
		}

		uint8_t step() {
			_count--;
		}

		uint8_t getCount() {
			return _count;
		}

		uint8_t getFeed() {
			return (_flags & FEED_FLAG_BITS);
		}

		bool getDirection() {
			return (_flags & DIRECTION_FLAG_BIT);
		}

		bool getStep() {
			return (_flags & STEP_FLAG_BIT);
		}
	};

	class Command *_head;
	int8_t _directionPin;
	int8_t _stepPin;
	bool _isStep;

public:

	Stepper(int8_t stepPin, int8_t directionPin) {
		_directionPin = directionPin;
		_stepPin = stepPin;
		_head = NULL;
		_isStep = true;
	}

	void addCommand(uint8_t flags, uint8_t count) {
		class Command *command = new Command(flags, count);
		if (!_head)
			_head = command;
		else
			_head->add(command);
	}

	bool step() {
		if (_head) {
			if (!_isStep) {
				digitalWrite(_stepPin, LOW);
				_isStep = true;
				if (_head->getCount() == 0)
					return false;
			} 
			else {
				digitalWrite(_stepPin, _head->getStep());
				_isStep = false;
				_head->step();
			}			
		}
		return true;
	}

	bool next() {
		Command *head = _head;
		if (head) {

			_head = head->next();
			delete head;

			if (_head) {
				digitalWrite(_directionPin, head->getDirection());
				return true;
			}

			
		}
		return false;
	}

	uint8_t getFeed() {
		return (_head) ? _head->getFeed() : 1;
	}

	uint8_t getCount() {
		return (_head) ? _head->getCount() : 255;
	}
};

uint8_t _timer;
uint16_t _timeOutCount;

uint16_t _feedRate;
uint16_t _OCR1BMax;

bool _isTimer;
bool _isTransmission;
bool _dataRequested;

class Stepper *_middle, *_proximal;
bool _hasMiddle, _hasProximal;

bool (*timer1COMPA)();
bool (*timer1COMPB)();


void setup() 
{
	_timer = 0;
	_isTransmission = false;
	_dataRequested = false;

	_feedRate = BASE_COMPARE_REGISTER;

	pinMode(0, INPUT);           // set pin to input
	digitalWrite(0, HIGH);       // turn on pullup resistors

	pinMode(M1_STEP_PIN, OUTPUT);
	pinMode(M2_STEP_PIN, OUTPUT);
	pinMode(M1_DIRECTION_PIN, OUTPUT);
	pinMode(M2_DIRECTION_PIN, OUTPUT);

	pinMode(MS1_PIN, OUTPUT);
	pinMode(MS2_PIN, OUTPUT);
	pinMode(MS3_PIN, OUTPUT);

	digitalWrite(MS1_PIN, LOW);
	digitalWrite(MS2_PIN, HIGH);
	digitalWrite(MS3_PIN, LOW);

	Serial.begin(115200);
	Serial1.begin(115200);

	_middle = new Stepper(M1_STEP_PIN, M1_DIRECTION_PIN);
	_proximal = new Stepper(M2_STEP_PIN, M2_DIRECTION_PIN);

	timerSetup();
}

/////////////////////////////////////////////////
//												/
//												/
//				STEP FUNCTIONS					/
//												/
//												/
/////////////////////////////////////////////////

void updateFeed() {
	uint8_t mFeed = _middle->getFeed(), pFeed = _proximal->getFeed();

	_hasMiddle = true;
	_hasProximal = true;

	if (mFeed < pFeed) {
		//_OCR1BMax = _feedRate;

		OCR1A = (_feedRate * pFeed) / mFeed;
		//OCR1B = _OCR1BMax;
				
		timer1COMPB = &middleStep;
		timer1COMPA = &proximalStep;
	} else {
		//_OCR1BMax = _feedRate;	
		OCR1A = (_feedRate  * mFeed) / pFeed;
		//OCR1B = _OCR1BMax;
			
		timer1COMPB = &proximalStep;
		timer1COMPA = &middleStep;
	}
}

bool nextCommand() {
	if (_middle->next() & _proximal->next()) {
		updateFeed();
		return true;
	}
	return false;
}

bool middleStep() {
	if (!_hasMiddle || !_middle->step()) {
		if (!_hasProximal) {
			return nextCommand();
		}
		else
			_hasMiddle = false;
	}
	return true;
}

bool proximalStep() {
	
	if (!_hasProximal || !_proximal->step()) {
		if (!_hasMiddle) {
			return nextCommand();
		}
		else
			_hasProximal = false;
	}
	return true;
}

/////////////////////////////////////////////////
//												/
//												/
//				TIMER FUNCTIONS					/
//												/
//												/
/////////////////////////////////////////////////

ISR(TIMER1_COMPB_vect) {
	OCR1B += _OCR1BMax;
	if (!(*timer1COMPB)())
		timerStop();
}

ISR(TIMER1_COMPA_vect) {
	OCR1B -= OCR1A;
	//if (OCR1B > OCR1A)
	//	OCR1B = _OCR1BMax;
	if (!(*timer1COMPA)())
		timerStop();
}

void timerSetup() {
	noInterrupts();						//Disable interrupts
	TCCR1A = 0;
	TCCR1B = 0;
	TCCR1B = _BV(WGM12) | _BV(CS12);	//CTC Mode with a 256 prescaler	

	_OCR1BMax = _feedRate;
	OCR1B = _feedRate;

	//
	interrupts();						//Enable interrupts 	
}

void timerStart() {
	_isTimer = true;
	noInterrupts();						//Disable interrupts
	updateFeed();
	TCNT1 = 0;

	TIFR1 |= _BV(OCF1A) | _BV(OCF1B);	//Clearing interrupt flags

	TIMSK1 |= _BV(OCIE1A) | _BV(OCIE1B);//Output compare register A enabled

	interrupts();						//Enable interrupts
}

void timerStop() {
	_isTimer = false;
	noInterrupts();						//Disable interrupts
	TIMSK1 &= ~_BV(OCIE1A);				//Output compare register A Disable
	TIMSK1 &= ~_BV(OCIE1B);				//Output compare register A Disable
	interrupts();						//Enable interrupts
}

/////////////////////////////////////////////////
//												/
//												/
//			Communication Functions				/
//												/
//												/
/////////////////////////////////////////////////

void recieveCommands(uint8_t flags, uint8_t count) {
	Serial.print("Command Flags:");
	Serial.println(flags,2);
	Serial.print("Command Count:");
	Serial.println(count);
	if ((flags & MOTOR_FLAG) > 0) {
		_middle->addCommand(flags, count);
	} else {
		_proximal->addCommand(flags, count);
	}
}
void recieveFeed(uint8_t flags, uint8_t feedL, uint8_t feedH) {

}

void serialRecieve() {

	bool commandRecieved = false;

	while (Serial1.available() > 0) {
		uint8_t data = Serial1.read();

		Serial.print("Data:");
		Serial.println(data,2);

		if (_isTransmission) {
			if ((data & DATA_TYPE_FLAG) > 0) {
				recieveCommands(data, Serial1.read());
				commandRecieved = true;
			}
			else if (data == END_OF_MESSAGE) {
				Serial.println("END OF MESSAGE");
				if (commandRecieved & !_isTimer) {
					timerStart();
				}
				_isTransmission = false;
			}
		}
		else if (data == START_OF_MESSAGE) {
			Serial.println("START OF MESSAGE");
			_isTransmission = true;
		}

	}
	
	_isTransmission = false;
}

void loop() {
	if (Serial1.available() > 0) {
		serialRecieve();
	}
	/*
	if (_bOCR1A != OCR1A) {
		Serial.print("OCR1A: ");
		Serial.println(OCR1A);
		_bOCR1A = OCR1A;
	}

	if (_bOCR1B != OCR1B) {
		Serial.print("OCR1B: ");
		Serial.println(OCR1B);
		_bOCR1B = OCR1B;
	}
	*/
}
