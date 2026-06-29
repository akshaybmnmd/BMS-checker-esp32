#include <stdarg.h>

void logInfo(String message)
{
    Serial.print("[INFO] ");
    Serial.println(message);
}

void logWarning(String message)
{
    Serial.print("[WARN] ");
    Serial.println(message);
}

void logError(String message)
{
    Serial.print("[ERROR] ");
    Serial.println(message);
}

void blinkLED(int numBlinks, int interval) {
  for (int i = 0; i < numBlinks; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(interval);
    digitalWrite(LED_PIN, LOW);
    delay(interval);
  }
}