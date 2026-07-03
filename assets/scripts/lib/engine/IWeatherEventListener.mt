// IWeatherEventListener - Interface for receiving weather system events
// Implement this interface in @Script classes to receive weather callbacks.
//
// onWeatherChanged/onLightningStrike are GLOBAL: they fire on every listener
// regardless of entity. onWeatherZoneEntered/onWeatherZoneExited fire only
// when THIS script's entity enters/leaves a weather zone.
//
// Weather state arrays are 15 floats:
//   [0] cloudCoverage   [1] cloudDensity     [2] cloudType
//   [3] precipType      [4] precipIntensity  [5] windSpeed
//   [6] windDirectionDeg[7] gustStrength     [8] gustFrequency
//   [9] fogDensity      [10] heightFogDensity [11] ambientLightMult
//   [12..14] atmosphereTint RGB
//
// Usage:
//   @Script
//   public class WeatherReactor implements IWeatherEventListener {
//       @Override
//       public function onWeatherChanged(float[] previousState, float[] newState): void {
//           if (newState[4] > 0.5) { Log::info("heavy precipitation incoming"); }
//       }
//
//       @Override
//       public function onLightningStrike(float[] position, float intensity): void {
//           VFX::spawnAt("lightning_impact", position[0], position[1], position[2]);
//       }
//
//       @Override
//       public function onWeatherZoneEntered(): void {
//       }
//
//       @Override
//       public function onWeatherZoneExited(): void {
//       }
//   }

interface IWeatherEventListener {
    // Called on every weather state transition (both arrays use the 15-float layout above)
    function onWeatherChanged(float[] previousState, float[] newState): void;

    // Called when lightning strikes (position = world position, 3 floats)
    function onLightningStrike(float[] position, float intensity): void;

    // Called when this entity enters a weather zone
    function onWeatherZoneEntered(): void;

    // Called when this entity leaves a weather zone
    function onWeatherZoneExited(): void;
}
