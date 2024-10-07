use std::{ops::Deref, time::Duration};

use serde::{
    de::{self, Unexpected, Visitor},
    Deserialize, Deserializer,
};

// Exists so that we can deserialize Duration sent from Device Registration Service in C# style d.HH.mm.ss
#[derive(Debug, Clone, Copy)]
pub struct DurationWrapper(Duration);

impl From<DurationWrapper> for Duration {
    fn from(wrapper: DurationWrapper) -> Self {
        wrapper.0
    }
}

impl Deref for DurationWrapper {
    type Target = Duration;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl From<Duration> for DurationWrapper {
    fn from(d: Duration) -> Self {
        DurationWrapper(d)
    }
}

impl<'de> Deserialize<'de> for DurationWrapper {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_any(DurationVisitor)
    }
}

struct DurationVisitor;

impl<'de> Visitor<'de> for DurationVisitor {
    type Value = DurationWrapper;

    fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
        formatter.write_str("a string in the format [-][d.]hh:mm:ss[.fffffff]")
    }

    fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
    where
        E: de::Error,
    {
        if s.starts_with('-') {
            return Ok(Duration::ZERO.into());
        }

        let days: u64;
        let hours: u64;
        // let mut microseconds = 0;

        let parts: [&str; 3] = s.split(':').collect::<Vec<_>>().try_into().map_err(|_| {
            de::Error::invalid_value(
                Unexpected::Str("Duration did not contain two colons."),
                &self,
            )
        })?;

        let day_and_hour = parts[0];
        let minutes: u64 = parts[1].parse().map_err(|_| {
            de::Error::invalid_value(
                Unexpected::Str(&format!("Malformed minutes part: {}", parts[1])),
                &self,
            )
        })?;
        let seconds_and_micros = parts[2];

        if day_and_hour.contains('.') {
            let mut parts = day_and_hour.split('.');
            let days_str = parts.next().unwrap(); // We know the string contains `.` so at least two parts must exist

            days = days_str.parse().map_err(|_| {
                de::Error::invalid_value(
                    Unexpected::Str(&format!("Malformed days part: {days_str}")),
                    &self,
                )
            })?;

            let hours_str = parts.next().unwrap();

            hours = hours_str.parse().map_err(|_| {
                de::Error::invalid_value(
                    Unexpected::Str(&format!("Malformed hours part: {hours_str}")),
                    &self,
                )
            })?;
        } else {
            days = 0;
            hours = day_and_hour.parse().map_err(|_| {
                de::Error::invalid_value(
                    Unexpected::Str(&format!("Malformed hours part: {day_and_hour}")),
                    &self,
                )
            })?;
        }

        let seconds: u64 = if seconds_and_micros.contains('.') {
            let mut parts = seconds_and_micros.split('.');
            let seconds_str = parts.next().unwrap();
            seconds_str.parse().map_err(|_| {
                de::Error::invalid_value(
                    Unexpected::Str(&format!("Malformed seconds part: {seconds_str}")),
                    &self,
                )
            })?
            // Ignore microseconds
        } else {
            seconds_and_micros.parse().map_err(|_| {
                de::Error::invalid_value(
                    Unexpected::Str(&format!("Malformed seconds part: {seconds_and_micros}")),
                    &self,
                )
            })?
        };

        let total_seconds = days * 60 * 60 * 24 + hours * 60 * 60 + minutes * 60 + seconds;

        Ok(Duration::from_secs(total_seconds).into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn deser_duration() {
        let s = "\"8.11:55:36.3296177\"";
        let d: Duration = serde_json::from_str::<DurationWrapper>(s).unwrap().into();
        assert_eq!(d.as_secs(), 734_136);
    }

    #[test]
    fn deser_duration_2() {
        let s = "\"13:00:39\"";
        let d: Duration = serde_json::from_str::<DurationWrapper>(s).unwrap().into();
        assert_eq!(d.as_secs(), 46839);
    }

    #[test]
    fn deser_duration_3() {
        let s = "\"00:00:39\"";
        let d: Duration = serde_json::from_str::<DurationWrapper>(s).unwrap().into();
        assert_eq!(d.as_secs(), 39);
    }

    #[test]
    fn deser_duration_4() {
        let s = "\"0000.00:00:00.00\"";
        let d: Duration = serde_json::from_str::<DurationWrapper>(s).unwrap().into();
        assert_eq!(d.as_secs(), 0);
    }

    #[test]
    #[should_panic(expected = "This duration cannot be deserialized.")]
    fn deser_duration_fail() {
        let s = "\"10:39\"";
        let _d: Duration = serde_json::from_str::<DurationWrapper>(s).unwrap().into();
    }
}
