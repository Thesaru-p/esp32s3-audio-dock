/*
 * ============================================================
 *  ESP32-S3 Audio Input Dock — Double Clap Detection
 * ============================================================
 * 
 * DESCRIPTION:
 *   This sketch listens continuously through an INMP441 I2S
 *   microphone and runs a TinyML model (trained via Edge Impulse)
 *   to detect a "double clap" sound pattern in real time.
 *   When a clap is detected, it displays a message on a 16x2
 *   I2C LCD screen and toggles a state variable (e.g. for
 *   controlling a light or relay).
 * 
 * HARDWARE:
 *   - ESP32-S3 Dev Module
 *   - INMP441 I2S MEMS Microphone
 *   - 16x2 I2C LCD Display
 * 
 * WIRING:
 *   INMP441 Microphone:
 *     SCK  (Bit Clock)   -> GPIO 4
 *     WS   (Word Select) -> GPIO 7
 *     SD   (Data)        -> GPIO 17
 *     VDD                -> 3.3V
 *     GND                -> GND
 *     L/R                -> GND  (selects left channel)
 * 
 *   I2C LCD:
 *     SDA  -> GPIO 41
 *     SCL  -> GPIO 42
 *     VCC  -> 5V
 *     GND  -> GND
 * 
 * LIBRARIES REQUIRED:
 *   - ESP32-S3-Clap-Sensor_inferencing  (exported from Edge Impulse)
 *   - LiquidCrystal_I2C                 (Arduino Library Manager)
 * 
 * AUTHOR: Thesaru Praneeth
 * ============================================================
 */

// --- Library Includes ---
#include <ESP32-S3-Clap-Sensor_inferencing.h>  // Edge Impulse TinyML model for clap detection
#include <driver/i2s.h>                         // ESP32 I2S driver for reading microphone data
#include <Wire.h>                               // I2C communication (used by the LCD)
#include <LiquidCrystal_I2C.h>                  // I2C LCD display control

// ============================================================
//  PIN DEFINITIONS — I2S Microphone (INMP441)
// ============================================================
#define I2S_WS   7          // Word Select (LRCLK) — tells mic which channel to send
#define I2S_SD   17         // Serial Data — audio data line from mic to ESP32
#define I2S_SCK  4          // Bit Clock — synchronises data transfer
#define I2S_PORT I2S_NUM_0  // Use I2S peripheral 0 (ESP32 has two: 0 and 1)
#define SAMPLE_RATE 16000   // 16kHz sample rate — required by the Edge Impulse model

// ============================================================
//  PIN DEFINITIONS — I2C LCD Display
// ============================================================
#define I2C_SDA 41  // I2C data pin
#define I2C_SCL 42  // I2C clock pin

// Initialise LCD: I2C address 0x27, 16 columns, 2 rows
// NOTE: If the screen stays blank, try address 0x3F instead
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================================================
//  GLOBAL VARIABLES
// ============================================================

// Raw audio samples from the microphone (16-bit integers)
int16_t *sampleBuffer;

// Converted audio samples in float format (required by Edge Impulse)
float *inferenceBuffer;

// Set to true to print raw neural network output to Serial Monitor (useful for debugging)
bool debug_nn = false;

// Tracks whether the "light" or output is ON or OFF — toggled on each clap detection
bool lightState = false;


// ============================================================
//  SETUP — Runs once when the board powers on or resets
// ============================================================
void setup() {

    // Start serial communication at 115200 baud for debugging output
    Serial.begin(115200);

    // ----------------------------------------------------------
    //  1. Initialise the I2C LCD Display
    // ----------------------------------------------------------

    // Start I2C on the custom pins defined above (not default Arduino pins)
    Wire.begin(I2C_SDA, I2C_SCL);

    lcd.init();       // Initialise the LCD hardware
    lcd.backlight();  // Turn on the LCD backlight

    // Show a startup message while the system initialises
    lcd.setCursor(0, 0);
    lcd.print("Voice System");
    lcd.setCursor(0, 1);
    lcd.print("Listening...");
    delay(2000);   // Hold the startup message for 2 seconds
    lcd.clear();   // Clear screen — ready for detection output

    // ----------------------------------------------------------
    //  2. Initialise the I2S Microphone (INMP441)
    // ----------------------------------------------------------

    // Configure the I2S peripheral settings
    i2s_config_t i2s_config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),  // Master mode, receive only (microphone input)
        .sample_rate          = SAMPLE_RATE,                                   // 16000 Hz
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,                    // 16-bit audio depth
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,                    // Mono — left channel only (L/R pin tied to GND)
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,                    // Standard I2S protocol
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,                         // Interrupt priority level
        .dma_buf_count        = 8,                                             // Number of DMA buffers
        .dma_buf_len          = 64,                                            // Samples per DMA buffer
        .use_apll             = false,                                         // Don't use Audio PLL (not needed here)
        .tx_desc_auto_clear   = false,                                         // Not transmitting, so irrelevant
        .fixed_mclk           = 0                                              // No fixed master clock
    };

    // Map the I2S signal lines to physical GPIO pins
    i2s_pin_config_t pin_config = {
        .bck_io_num   = I2S_SCK,   // Bit clock pin
        .ws_io_num    = I2S_WS,    // Word select pin
        .data_out_num = -1,        // Not used — we're only receiving, not transmitting
        .data_in_num  = I2S_SD     // Data input from microphone
    };

    // Install the I2S driver with the config above — halt if it fails
    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) return;

    // Apply the pin mapping to the I2S driver
    i2s_set_pin(I2S_PORT, &pin_config);

    // ----------------------------------------------------------
    //  3. Allocate Memory for Audio Buffers
    // ----------------------------------------------------------

    // EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE is defined by the Edge Impulse library
    // It represents the exact number of samples the model expects per inference window

    // Raw 16-bit sample buffer — filled directly from I2S reads
    sampleBuffer    = (int16_t *)malloc(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE * sizeof(int16_t));

    // Float buffer — Edge Impulse's signal processing functions require float input
    inferenceBuffer = (float *)malloc(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE * sizeof(float));
}


// ============================================================
//  LOOP — Runs repeatedly after setup()
// ============================================================
void loop() {

    size_t bytesIn = 0;  // Will hold the number of bytes actually read from I2S

    // Read a full frame of audio samples from the microphone via I2S
    // portMAX_DELAY means it will wait indefinitely until enough data is available
    esp_err_t result = i2s_read(
        I2S_PORT,
        sampleBuffer,
        EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE * 2,  // Number of bytes to read (2 bytes per int16_t sample)
        &bytesIn,
        portMAX_DELAY
    );

    // Only proceed if the I2S read was successful
    if (result == ESP_OK) {

        // ----------------------------------------------------------
        //  Step 1: Convert raw int16 samples to float
        // ----------------------------------------------------------
        // Edge Impulse's signal functions require float values, not integers
        for (int i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; i++) {
            inferenceBuffer[i] = (float)sampleBuffer[i];
        }

        // ----------------------------------------------------------
        //  Step 2: Wrap the float buffer into an Edge Impulse signal
        // ----------------------------------------------------------
        // The signal_t struct is the standard input format for the EI classifier
        signal_t signal;
        numpy::signal_from_buffer(inferenceBuffer, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);

        // ----------------------------------------------------------
        //  Step 3: Run the TinyML classifier
        // ----------------------------------------------------------
        // This runs the full DSP pipeline and neural network inference
        // Results are stored in the 'predictions' struct
        ei_impulse_result_t predictions;
        run_classifier(&signal, &predictions, debug_nn);

        // ----------------------------------------------------------
        //  Step 4: Extract confidence scores for each label
        // ----------------------------------------------------------
        float single_clap_score = 0;
        float double_clap_score = 0;

        // Loop through all labels the model was trained on
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            String label = predictions.classification[ix].label;
            float  val   = predictions.classification[ix].value;

            // Match label names — Edge Impulse may use spaces or underscores
            // depending on how the dataset was labelled. Both are handled here.
            if (label == "Single clap" || label == "single_clap") single_clap_score = val;
            if (label == "Double clap" || label == "double_clap") double_clap_score = val;
        }

        // ----------------------------------------------------------
        //  Step 5: Trigger action if confidence exceeds threshold
        // ----------------------------------------------------------
        // 0.70 (70%) is the confidence threshold — adjust up to reduce false
        // positives, or down to make detection more sensitive
        if (single_clap_score > 0.70 || double_clap_score > 0.70) {

            Serial.println("!!! CLAP DETECTED !!!");  // Debug output to Serial Monitor

            // Toggle the output state (ON <-> OFF) on every detected clap
            lightState = !lightState;

            // Update the LCD with a response message
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Hey Knurdz");   // Line 1
            lcd.setCursor(0, 1);
            lcd.print("Booting Up");   // Line 2

            // Block for 1 second to prevent the same clap from triggering twice
            // (debounce delay — audio inference can run faster than a clap ends)
            delay(1000);

            // Keep the message on screen for 5 seconds, then clear
            delay(5000);
            lcd.clear();
        }
    }
}
