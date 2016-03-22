#if (PLATFORM_ID == 6) || (PLATFORM_ID == 8) || (PLATFORM_ID == 10)
#define WORDCLOCK_PHOTON
#endif

#ifdef WORDCLOCK_PHOTON
SYSTEM_THREAD(ENABLED);
#endif

#define HW_VERSION      3
#define SW_VERSION      5
#define SETTINGS_URL    "http://spark.wgb.me/github-pages/wordclock-v5"

// http://www.epochconverter.com/
#define YEAR_SECONDS    31556926
#define MONTH_SECONDS   86400*30

// Start up in SEMI_AUTOMATIC mode to be able to display
// rainbows while trying to connect to wifi.
#ifdef USE_SEMIAUTOMATIC
SYSTEM_MODE(SEMI_AUTOMATIC);
#endif

// #define SERIAL_DEBUG

#define	ROWS	11
#define COLS	11


// NeoPixel init stuffs
#include "neopixel.h"
#define PIXEL_COUNT ROWS*COLS
Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, D2, WS2812);


// Timers
#include "elapsedMillis.h"
elapsedMillis elapsedRainbow;


// The physical text of the word clock
static const String text = "\
IT.IS.HALF.\
TEN.QUARTER\
TWENTY.FIVE\
MINUTES.TO.\
.PAST.SEVEN\
.NINE.FIVE.\
.EIGHT.TEN.\
THREE.FOUR.\
.TWELVE.TWO\
ELEVEN.ONE.\
SIX.O'CLOCK\
";


// Get the location of the status pixel (the apostrophe)
static const uint8_t status_pixel = 5; //text.indexOf("'"); // 5
uint8_t status_color[3] = {0, 0, 0};
uint8_t rainbow_cycle = 0;


#include "MDNS.h"
MDNS mdns;


// Settings
struct structSettings {
    // Effect modes
    // 0 = no effect
    // 1 = rainbow
    uint8_t EFFECT_MODE = 1;

    uint16_t RAINBOW_DELAY = 50;

    bool LED_MIRROR = false;

    int8_t timeZone = 0;

    bool observeDST = true;

    uint8_t color[3] = {0, 0, 64};
};

structSettings Settings;


// Other variables
bool resetFlag = false;
elapsedMillis timerReset = 0;
uint8_t currEffect = Settings.EFFECT_MODE;
uint16_t LAST_YEAR = 0;
uint8_t LAST_MONTH = 13;
uint8_t LAST_DAY = 32;
uint8_t LAST_HOUR = 24;
int8_t LAST_MINUTE = 61;
int8_t LAST_MINUTE5 = 61; // Use this for the 5-minute rounding
int8_t LAST_SECOND = 61;
uint32_t dst_start = 0;
uint32_t dst_end = 0;


// LED mirroring
void ledChangeHandler(uint8_t r, uint8_t g, uint8_t b) {
    status_color[0] = r;
    status_color[1] = g;
    status_color[2] = b;
}


// Web server settings
#define WEBDUINO_FAVICON_DATA ""
#define WEBDUINO_FAIL_MESSAGE ""
#include "WebServer.h"
WebServer webserver("", 80);

#define POST_NAME_LENGTH    32
#define POST_VALUE_LENGTH   32


// index.html
void web_index(WebServer &server, WebServer::ConnectionType type, char *, bool) {
    server.httpSuccess();

    server.print("<!DOCTYPE html><html><head><title>Word Clock Settings</title><style type=\"text/css\">html,body,iframe{position:absolute;top:0;right:0;bottom:0;left:0;border:0;width:99%;height:99%;}</style></head><body><iframe src=\""+String(SETTINGS_URL)+"/index.html\"></iframe></body></html>");
}


// demo.html
void web_demo(WebServer &server, WebServer::ConnectionType type, char *, bool) {
    server.httpSuccess();

    server.print("<!DOCTYPE html><html><head><title>Word Clock Demo</title><style type=\"text/css\">html,body,iframe{position:absolute;top:0;right:0;bottom:0;left:0;border:0;width:99%;height:99%;}</style></head><body><iframe src=\""+String(SETTINGS_URL)+"/demo.html\"></iframe></body></html>");
}


// settings.json
void web_settings(WebServer &server, WebServer::ConnectionType type, char *, bool) {
    server.httpSuccess("application/json");

    server.print("\
{\
\"z\":"+String(Settings.timeZone)+",\
\"m\":"+String(Settings.EFFECT_MODE)+",\
\"o\":"+String(Settings.observeDST)+",\
\"l\":"+String(Settings.LED_MIRROR)+",\
\"hw\":"+String(HW_VERSION)+",\
\"sw\":"+String(SW_VERSION)+",\
\"rgbR\":"+String(Settings.color[0])+",\
\"rgbG\":"+String(Settings.color[1])+",\
\"rgbB\":"+String(Settings.color[2])+"\
}");

// \"rows\":"+String(ROWS)+",\
// \"cols\":"+String(COLS)+",\
// \"t\":\""+text+"\"}");

}


// save.html
void web_save(WebServer &server, WebServer::ConnectionType type, char *, bool) {
    URLPARAM_RESULT rc;
    char name[POST_NAME_LENGTH];
    char value[POST_VALUE_LENGTH];


    server.httpSeeOther(String(SETTINGS_URL)+"/index.html");

    bool o = 0;
    bool l = 0;

    while(server.readPOSTparam(name, POST_NAME_LENGTH, value, POST_VALUE_LENGTH)) {
        String _name = String(name).toUpperCase();
        String _value = String(value);

        if(_name.equals("Z"))
            Settings.timeZone = _value.toInt();

        else if(_name.equals("M"))
            Settings.EFFECT_MODE = _value.toInt();

        else if(_name.equals("O"))
            o = 1;

        else if(_name.equals("L"))
            l = 1;

        else if(_name.equals("RGBR"))
            Settings.color[0] = _value.toInt();

        else if(_name.equals("RGBG"))
            Settings.color[1] = _value.toInt();

        else if(_name.equals("RGBB"))
            Settings.color[2] = _value.toInt();
    }

    Settings.observeDST = o;
    Settings.LED_MIRROR = l;


    // Update time zone
    Time.zone(Settings.timeZone+dstOffset());

    // Save the updated settings
    eepromSave();

    // Set this to force the clock to update
    LAST_MINUTE = 1;
    LAST_MINUTE5 = -1;
}


// clearwifi.html
void web_clearwifi(WebServer &server, WebServer::ConnectionType, char *, bool) {
    server.httpSuccess();

    server.print("OK");

    delay(100);
    WiFi.clearCredentials();
    delay(100);
    System.reset();
}


#ifdef WORDCLOCK_PHOTON
STARTUP(RGB.onChange(ledChangeHandler));
#endif


void setup() {
    calculateDST();
    Particle.syncTime();

    // See if this EEPROM has saved data
    if(EEPROM.read(0)==1)
        eepromLoad();
    // If data has not been saved, "initialize" the EEPROM
    else
        eepromSave();


    RGB.onChange(ledChangeHandler);

#ifdef SERIAL_DEBUG
    Serial.begin(9600);
#endif

	// Set all timers
    elapsedRainbow = 0;


	// Initialize NeoPixels
    strip.begin();
    strip.show();


#ifdef WORDCLOCK_PHOTON
    // We have to turn on all the pixels for the rainbow to work
    for(uint8_t i=0; i<ROWS*COLS; i++)
    	strip.setPixelColor(i, strip.Color(0, 0, 1));

	// Wait for the cloud connection to happen
    while(!Particle.connected()) {
        // And do a little rainbow dance while we wait
        elapsedRainbow = Settings.RAINBOW_DELAY; // A little trick to beat the rainbow timer
        applyRainbow();
        strip.setPixelColor(status_pixel, status_color[0], status_color[1], status_color[2]);
        strip.show();
        delay(10);
    }
#endif


    // mDNS / Bonjour
    bool mdns_success = mdns.setHostname("wordclock");

    if(mdns_success) {
        mdns.addService("tcp", "http", 80, "Word Clock");
        mdns.begin();
    }


    // Web server
    webserver.setDefaultCommand(&web_index);
    webserver.addCommand("save.html", &web_save);
    webserver.addCommand("settings.json", &web_settings);
    webserver.addCommand("clearwifi.html", &web_clearwifi);
    webserver.begin();


    // Rainbow all pixels for ~5 seconds
#ifndef WORDCLOCK_PHOTON
    uint16_t stop_rainbowing = millis()+5000;
    while(millis()<stop_rainbowing) {
        rainbow(10);
    }
#endif


    // Do a "wipe" to clear away the rainbow
    for(uint8_t i=0; i<PIXEL_COUNT; i++) {
        strip.setPixelColor(i, strip.Color(i, i, i));

        if(i>0)
            strip.setPixelColor(i-1, strip.Color(0, 0, 0));

        strip.show();
        delay(5);
    }


    // Set the timezone
    Time.zone(Settings.timeZone+dstOffset());

    LAST_YEAR = Time.year();
    LAST_MONTH = Time.month();
    LAST_DAY = Time.day();
    LAST_HOUR = Time.hour();
    LAST_MINUTE = Time.minute();
    LAST_SECOND = Time.second();


    // Blank slate
    blackOut();
    strip.show();
}


void loop() {
    ticktock();
    doEffectMode();

    if(Settings.LED_MIRROR)
        strip.setPixelColor(status_pixel, status_color[0], status_color[1], status_color[2]);

	// Handle remote reset
    if(timerReset>=500) {
        if(resetFlag) {
            System.reset();
            resetFlag = false;
        }

        timerReset = 0;
    }

    strip.show();


    mdns.processQueries();

    char web_buff[64];
    int web_len = 64;
    webserver.processConnection(web_buff, &web_len);

    // TODO: These could probably use their own functions
    // // Yearly routines
    // if(LAST_YEAR != Time.year()) {
    //     LAST_YEAR = Time.year();
    // }


    // // Monthly routines
    // if(LAST_MONTH != Time.month()) {
    //     LAST_MONTH = Time.month();
    // }


    // Daily routines
    if(LAST_DAY != Time.day()) {
        calculateDST();
        Particle.syncTime();
        LAST_DAY = Time.day();
    }


    // // Hourly routines
    // if(LAST_HOUR != Time.hour()) {
    //     LAST_HOUR = Time.hour();
    // }


    // // Minutely routines
    // if(LAST_MINUTE != Time.minute()) {
    //     LAST_MINUTE = Time.minute();
    // }


    // // Secondly routines
    // if(LAST_SECOND != Time.second()) {
    //     LAST_SECOND = Time.second();
    // }
}


void calculateDST() {
    uint32_t beforeTime = Time.local();

    uint8_t Sundays = 0;

    // The theoretical beginning of the current year
    uint32_t year_ts=(Time.year()-1970)*YEAR_SECONDS;

    // March-ish
    uint32_t march_ts = year_ts + 2*MONTH_SECONDS;
    march_ts = (march_ts/86400)*86400;

    // November-ish
    uint32_t november_ts = year_ts + 10*MONTH_SECONDS;
    november_ts = (november_ts/86400)*86400;


    // Calculate when DST begins in March
    uint32_t i = march_ts;

    while(1) {
        // Quit after start time + 30 days
        if(i>(march_ts+30*86400))
            break;

        Time.setTime(i);

        if(Time.month()==3 && Time.weekday()==1)
            Sundays++;

        if(Sundays==2) {
            // DST start = The test time + 2 hours
            dst_start = i+7200;
            break;
        }

        // Increment by 1 day
        i+= 86400;
    }


    // Calculate when DST ends in November
    i = november_ts;
    Sundays = 0;

    while(1) {
        // Quit after start time + 30 days
        if(i>(november_ts+30*86400))
            break;

        Time.setTime(i);

        if(Time.month()==11 && Time.weekday()==1)
            Sundays++;

        if(Sundays==1) {
            // DST end = The test time + 2 hours
            dst_end = i+7200;
            break;
        }

        // Increment by 1 day
        i+= 86400;
    }

    Time.setTime(beforeTime);
}


int8_t dstOffset() {
    if(!Settings.observeDST)
        return 0;

    if(Time.local()>=dst_start && Time.local()<=dst_end)
        return 0;
    else
        return -1;
}


void eepromLoad() {
    EEPROM.get(1, Settings);
}


void eepromSave() {
    EEPROM.update(0, 1);
    EEPROM.put(1, Settings);
}


// Handle the different effect modes
void doEffectMode() {
    switch(Settings.EFFECT_MODE) {
        case 1: // Rainbow
            applyRainbow();
            break;

        default: // Solid color
            applySolidColor();
    }
}


// Turn off all pixels
void blackOut() {
    // Black it out
    for(uint8_t x=0; x<PIXEL_COUNT; x++)
        strip.setPixelColor(x, strip.Color(0, 0, 0));
}


// Generate a random color
void randomColor() {
    Settings.color[0] = random(32, 255);
    Settings.color[1] = random(32, 255);
    Settings.color[1] = random(32, 255);
}


// Display the rainbow
void rainbow(uint8_t wait) {
    uint16_t i, j;

    for(j=0; j<256; j++) {
        for(i=0; i<strip.numPixels(); i++) {
            strip.setPixelColor(i, Wheel((i+j) & 255));
        }

        strip.show();
        delay(wait);
    }
}


// Handle display of the time
void ticktock() {
	if(Time.minute()==LAST_MINUTE5)
		return;

	// Round the minute to the nearest 5-minute interval
	uint8_t minute = 5 * round((Time.minute()+Time.second()/60)/5);
	LAST_MINUTE5 = Time.minute();

	blackOut();

	// Default words
	doWord("IT");
	doWord("IS");
	doWord("O'CLOCK");


	// Determine TO / PAST
	if(minute>30 && minute!=0)
		doWord("TO");
	else if(minute<=30 && minute!=0)
		doWord("PAST");


	// Setting this flag will show "MINUTES"
	bool showMinutes = true;


	// Determine which word to light up for the minutes
	switch(minute) {
		case 0:
			showMinutes = false;
			break;
		case 5:
		case 55:
			doWord("FIVE");
			break;
		case 10:
		case 50:
			doWord("TEN");
			break;
		case 15:
		case 45:
			doWord("QUARTER");
			showMinutes = false;
			break;
		case 20:
		case 40:
			doWord("TWENTY");
			break;
		case 25:
		case 35:
			doWord("TWENTY");
			doWord("FIVE");
			break;
		case 30:
			doWord("HALF");
			showMinutes = false;
			break;
	}

	// Show "MINUTES"
	if(showMinutes)
		doWord("MINUTES");


	// The hour
	uint8_t hour = Time.hour();

	// Convert to 12-hour format
	if(hour>12)
		hour -= 12;

	// Do we need to increment the hour for "TO"
	if(minute>30)
		hour++;

	// Fix for 13 o'clock
	if(hour==13)
		hour = 1;

	// Fix for 0 o'clock
	if(hour==0)
		hour = 12;


    // Display the hour
	switch(hour) {
		case 1:
			doWord("ONE");
			break;
		case 2:
			doWord("TWO");
			break;
		case 3:
			doWord("THREE");
			break;
		case 4:
			doWord("FOUR");
			break;
		case 5:
			doWord("FIVE", true);
			break;
		case 6:
			doWord("SIX");
			break;
		case 7:
			doWord("SEVEN");
			break;
		case 8:
			doWord("EIGHT");
			break;
		case 9:
			doWord("NINE");
			break;
		case 10:
			doWord("TEN", true);
			break;
		case 11:
			doWord("ELEVEN");
			break;
		case 12:
			doWord("TWELVE");
	}


    // Light it up!
	strip.show();
}


// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
    if(WheelPos < 85) {
        return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
    } else if(WheelPos < 170) {
        WheelPos -= 85;
        return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    } else {
        WheelPos -= 170;
        return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
}


// Underloaded(?) version of doWord()
void doWord(String word) {
    doWord(word, false);
}


// Take a word, find it in the string, and turn it on (and optionally "skip" the first word)
void doWord(String word, bool skip) {
	word = word.toUpperCase();

    uint8_t x, y;

	if(!skip) {
		y = text.indexOf(word)/COLS;
		x = text.indexOf(word)-y*COLS;
	} else {
		y = text.lastIndexOf(word)/COLS;
		x = text.lastIndexOf(word)-y*COLS;
	}

	for(uint8_t i=x; i<x+word.length(); i++) {
	    uint8_t this_pixel = xyToPixel(i, y);

	    if(this_pixel!=status_pixel && Settings.LED_MIRROR)
		    strip.setPixelColor(this_pixel, strip.Color(Settings.color[0], Settings.color[1], Settings.color[2]));
		else
		    strip.setPixelColor(this_pixel, strip.Color(Settings.color[0], Settings.color[1], Settings.color[2]));
	}
}


// Convert X, Y coordinates to a pixel on the NeoPixel string
uint8_t xyToPixel(uint8_t x, uint8_t y) {
	return (ROWS*COLS) - (y * ROWS + COLS - x);
}


// All the pretty colors!  Now with more non-blocking!
void applyRainbow() {
	if(elapsedRainbow<Settings.RAINBOW_DELAY)
		return;

	if(rainbow_cycle>255)
		rainbow_cycle = 0;

    for(uint8_t i=0; i<strip.numPixels(); i++) {
        if(strip.getPixelColor(i)>0 && i!=status_pixel && Settings.LED_MIRROR)
            strip.setPixelColor(i, Wheel((i+rainbow_cycle) & 255));
        else if(strip.getPixelColor(i)>0)
            strip.setPixelColor(i, Wheel((i+rainbow_cycle) & 255));
    }

    rainbow_cycle++;
    elapsedRainbow = 0;
}


// Solid colors
void applySolidColor() {
    for(uint8_t i=0; i<strip.numPixels(); i++) {
        if(strip.getPixelColor(i)>0 && i!=status_pixel && Settings.LED_MIRROR)
            strip.setPixelColor(i, strip.Color(Settings.color[0], Settings.color[1], Settings.color[2]));
        else if(strip.getPixelColor(i)>0)
            strip.setPixelColor(i, strip.Color(Settings.color[0], Settings.color[1], Settings.color[2]));
    }
}