# open-meteo-arduino
Arduino library that fetch weather data from [Open meteo API](https://open-meteo.com/).

[Open meteo API](https://open-meteo.com/) is free and requires no registration. Only for non-commercial use and less than 10.000 daily API calls. 

## How to use the library
```getHourlyForecast(forecast, <Lat>, <Long>, epochTime);```

### Hardware supported
 ESP32
- ESP8266 ??
- Others ??

### Hourly values:

- Temperature
- Humidity
- Apparent Temperature
- Pressure
- Precipitation
- Cloud Cover
- Visibility
- Wind Speed
- Wind Direction
- Wind Gust
- Weather Code

### Daily:
- Sunrise
- Sunset
