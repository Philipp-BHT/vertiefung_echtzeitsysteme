#define UART_BAUD 115200

void setup() {
  Serial.begin(UART_BAUD);     // USB monitor
  Serial2.begin(UART_BAUD, SERIAL_8N1, 16, 17); // GPIO16 = RX, GPIO17 = TX

  Serial.println("ESP32 UART bridge ready.");
}

void loop() {
  // Forward Pi -> PC
  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.write(c);
  }

  // Forward PC -> Pi
  while (Serial.available()) {
    char c = Serial.read();
    Serial2.write(c);
  }
}