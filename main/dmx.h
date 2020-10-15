//dmx.h

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RGB_t;

typedef struct {
    uint16_t h;
    uint8_t s;
    uint8_t v;
} HSV_t;

RGB_t HSVtoRGB(HSV_t hsv);
void setDMXData(uint8_t address, uint8_t value);