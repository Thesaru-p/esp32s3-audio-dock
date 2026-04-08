# ESP32-S3 Audio Input Dock

Welcome to the repository for my first-year hardware project, the **ESP32-S3 Audio Input Dock**. This project features custom PCB design, circuit integration, and on-device TinyML capabilities.

Developed by **Thesaru Praneeth**.

---

## 📋 Project Overview

This dock uses an **ESP32-S3** microcontroller paired with an **INMP441 I2S microphone** and a **16x2 I2C LCD display**. A TinyML model trained via Edge Impulse runs directly on the ESP32-S3 to detect a **double clap** pattern in real time, toggling a response without any cloud dependency.

---

## 🔌 Wiring & Pinout

### INMP441 Microphone → ESP32-S3

| INMP441 Pin | ESP32-S3 GPIO | Description         |
|-------------|---------------|---------------------|
| SCK (BCLK)  | GPIO 4        | I2S Bit Clock       |
| WS (LRCLK)  | GPIO 7        | I2S Word Select     |
| SD (Data)   | GPIO 17       | I2S Serial Data     |
| VDD         | 3.3V          | Power               |
| GND         | GND           | Ground              |
| L/R         | GND           | Select Left Channel |

### I2C LCD (16x2) → ESP32-S3

| LCD Pin | ESP32-S3 GPIO | Description    |
|---------|---------------|----------------|
| SDA     | GPIO 41       | I2C Data       |
| SCL     | GPIO 42       | I2C Clock      |
| VCC     | 5V            | Power          |
| GND     | GND           | Ground         |

> **Note:** The LCD I2C address is typically `0x27` or `0x3F`. If the screen is blank after uploading, try changing `LiquidCrystal_I2C lcd(0x27, 16, 2)` to `0x3F` in the sketch.

---

## 📦 Required Libraries

Install these via the Arduino Library Manager or manually:

| Library | Source |
|---|---|
| `LiquidCrystal_I2C` | Arduino Library Manager |
| `ESP32-S3-Clap-Sensor_inferencing` | Exported from Edge Impulse (see below) |

The Edge Impulse inferencing library is not on the public registry — you must build and export it yourself from your Edge Impulse project. See the [Deployment](#7-deployment-to-esp32-s3) section below.

---

## ⚙️ Board Setup (Arduino IDE)

1. Add the ESP32 board package URL in **File → Preferences → Additional boards manager URLs**:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. Install **esp32 by Espressif Systems** via **Tools → Board → Boards Manager**.
3. Select **ESP32S3 Dev Module** as your board.
4. Set **Upload Speed** to `921600` and **USB Mode** to `Hardware CDC and JTAG`.

---

## 🧠 Double Clap Detection: Edge Impulse Workflow

The dock uses a TinyML model trained to recognize the distinct audio pattern of a double clap while ignoring background noise. Follow these steps to build and deploy the model.

### 1. Data Collection

A balanced dataset of both the target sound and ambient noise is required.

- **Double Claps:** Record at least 10–15 minutes of double claps. Vary the rhythm slightly, change the distance from the microphone, and record in different room environments to prevent overfitting to one acoustic profile.
- **Background Noise:** Record an equal amount of background noise (typing, talking, fans, music, silence).
- **Single Claps (Highly Recommended):** Including single claps in the "noise" or "unknown" category forces the model to specifically learn the *double* clap pattern, preventing false positives from random loud spikes.

### 2. Edge Impulse Setup

- Create an account and a new project on [Edge Impulse](https://studio.edgeimpulse.com/).
- Navigate to **Data acquisition**.
- Upload your raw `.wav` files using the web uploader or the Edge Impulse CLI.
- Ensure data is split into **Training** (~80%) and **Testing** (~20%) sets. Label them appropriately (e.g., `double_clap` and `noise`).

### 3. Designing the Impulse

Navigate to **Impulse design → Create impulse**.

- **Time series data:** Set window size to `1000ms` to capture a full double clap. Set window increase to `200ms`–`300ms`.
- **Processing block:** Choose **Audio (MFE)**. Mel-Filterbank Energy is highly effective for non-voice audio patterns like claps.
- **Learning block:** Choose **Classification (Keras)**.
- Click **Save Impulse**.

### 4. Feature Generation (MFE)

- Go to the **MFE** section under Impulse design.
- Review the parameters (defaults work well for this type of audio).
- Click **Generate features**. In the Feature Explorer 3D graph, you should see clear visual separation between `double_clap` clusters and `noise` clusters.

### 5. Training the Neural Network

- Navigate to the **Classifier** section.
- **Training cycles (epochs):** Start with `100`.
- **Learning rate:** Start with `0.005`.
- Click **Start training**.
- Review the Confusion Matrix. If accuracy is below 85–90%, gather more diverse negative audio data or adjust MFE parameters.

### 6. Model Testing

- Go to **Model testing** and click **Classify all**.
- This evaluates your model on the 20% holdout set, giving a realistic benchmark of real-world performance.

### 7. Deployment to ESP32-S3

- Navigate to **Deployment**.
- Under "Create library", select **Arduino library**.
- Enable the **EON Compiler** — this optimizes the neural network's RAM and ROM footprint for the microcontroller.
- Click **Build** to download the `.zip` library.
- In Arduino IDE: **Sketch → Include Library → Add .ZIP Library**, then select the downloaded file.

---

## 🚀 How to Upload the Sketch

1. Clone this repository:
   ```bash
   git clone https://github.com/YOUR_USERNAME/double_clap.git
   ```
2. Open `double_clap/double_clap.ino` in Arduino IDE.
3. Install `LiquidCrystal_I2C` via **Tools → Manage Libraries**.
4. For the Edge Impulse library, go to **Sketch → Include Library → Add .ZIP Library** and point it to the folder inside `lib/`, or copy it directly into your Arduino `libraries/` directory.
4. Connect your ESP32-S3 via USB.
5. Select the correct **Port** under **Tools → Port**.
6. Click **Upload**.

---

## 🗂️ Repository Structure

```
double_clap/
├── double_clap.ino                          # Main Arduino sketch
├── README.md                                # This file
├── .gitignore                               # Build artifact exclusions
└── lib/
    └── ESP32-S3-Clap-Sensor_inferencing/    # Extracted Edge Impulse library (from .zip export)
```

> **Note:** The `lib/ESP32-S3-Clap-Sensor_inferencing/` folder contains the Arduino inferencing library exported from Edge Impulse. Extract the downloaded `.zip` and place the contents here. Do **not** commit the raw `.zip` file.

---

## 📄 License

This project was developed as part of a first-year hardware module. Feel free to use it for educational purposes.
