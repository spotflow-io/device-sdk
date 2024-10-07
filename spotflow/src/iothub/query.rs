use std::collections::HashMap;

use anyhow::{Context, Result};
use urlencoding::decode;

pub(crate) fn parse(query: &str) -> Result<HashMap<String, Option<String>>> {
    let mut map = HashMap::new();

    for prop in query.split('&') {
        match prop.split_once('=') {
            None => {
                let key = decode(prop).context(format!("Unable to URL decode key {prop}"))?;
                map.insert(key.into_owned(), None);
            }
            Some((key, value)) => {
                let key = decode(key).context(format!("Unable to URL decode key {prop}"))?;
                let value = decode(value).context(format!("Unable to URL decode value {prop}"))?;
                map.insert(key.into_owned(), Some(value.into_owned()));
            }
        }
    }

    Ok(map)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_rid() {
        let props = "$rid=0";
        let dict = parse(props).expect("Unable to parse properties");
        assert_eq!(dict.len(), 1);
        assert_eq!(dict.get("$rid").unwrap().as_ref().unwrap(), "0");
    }

    #[test]
    fn parse_multiple() {
        let props = "$rid=1&foo=bar";
        let dict = parse(props).expect("Unable to parse properties");
        assert_eq!(dict.len(), 2);
        assert_eq!(dict.get("$rid").unwrap().as_ref().unwrap(), "1");
        assert_eq!(dict.get("foo").unwrap().as_ref().unwrap(), "bar");
    }
}
