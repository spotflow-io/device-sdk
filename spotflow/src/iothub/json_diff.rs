use anyhow::{Context, Result};
use serde_json::{json, Map};

pub(crate) fn diff(original: &str, desired: &str) -> Result<String> {
    let original = serde_json::from_str::<serde_json::Value>(original)
        .context("Unable to deserialize original object")?;
    let desired = serde_json::from_str::<serde_json::Value>(desired)
        .context("Unable to deserialize desired object")?;
    let patch = diff_objects(&original, &desired)?.unwrap_or_else(|| json!({}));

    serde_json::to_string(&patch).context("Unable to serialize resulting patch")
}

fn diff_objects(
    original: &serde_json::Value,
    desired: &serde_json::Value,
) -> Result<Option<serde_json::Value>> {
    if original == desired {
        return Ok(None);
    }

    let Some(original) = original.as_object() else {
        return Ok(Some(desired.clone()));
    };
    let Some(desired) = desired.as_object() else {
        return Ok(Some(desired.clone()));
    };
    let mut result = Map::new();

    for (name, desired_child) in desired {
        match original.get(name) {
            None => {
                result.insert(name.to_string(), desired_child.clone());
            }
            Some(original_child) => {
                if let Some(diff) = diff_objects(original_child, desired_child)? {
                    result.insert(name.to_string(), diff);
                }
            }
        }
    }

    for removed_child in original
        .iter()
        .filter(|child| !desired.contains_key(child.0))
    {
        result.insert(removed_child.0.to_string(), serde_json::Value::Null);
    }

    Ok(Some(serde_json::Value::Object(result)))
}

#[cfg(test)]
mod test {
    use serde_json::json;

    use super::*;

    #[test]
    fn same() {
        let x = json!({"a": "a", "b": {"c": "c"}});
        let y = json!({"a": "a", "b": {"c": "c"}});
        let diff = diff_objects(&x, &y).unwrap();
        assert!(diff.is_none());
    }

    #[test]
    fn top_level_same() {
        let x = r#"{"a": "a", "b": {"c": "c"}}"#;
        let y = r#"{"a": "a", "b": {"c": "c"}}"#;
        let expected = "{}";
        let diff = diff(x, y).unwrap();
        assert_eq!(expected, diff);
        let mut x = serde_json::from_str::<serde_json::Value>(x).unwrap();
        let y = serde_json::from_str::<serde_json::Value>(y).unwrap();
        let diff = serde_json::from_str::<serde_json::Value>(&diff).unwrap();
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn add_value() {
        let mut x = json!({});
        let y = json!({"a": "a"});
        let expected = json!({"a": "a"});
        let diff = diff_objects(&x, &y).unwrap().unwrap();
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn remove_value() {
        let mut x = json!({"a": "a"});
        let y = json!({});
        let expected = json!({ "a": null });
        let diff = diff_objects(&x, &y).unwrap().unwrap();
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn change_value() {
        let mut x = json!({"a": "a"});
        let y = json!({"a": "b"});
        let expected = json!({"a": "b"});
        let diff = diff_objects(&x, &y).unwrap().unwrap();
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn add_remove_change_leave_value() {
        let mut x = json!({"a": "a", "b": "b", "c": "c"});
        let y = json!({"a": "a", "b": "a", "d": "d"});
        let expected = json!({"b": "a", "c": null, "d": "d"});
        let diff = diff_objects(&x, &y).unwrap().unwrap();
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn change_nested_value() {
        let mut x = json!({"a": {"b": "b"}});
        let y = json!({"a": {"b": "c"}});
        let expected = json!({"a": {"b": "c"}});
        let diff = diff_objects(&x, &y).unwrap().unwrap();
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn add_nested_value() {
        let mut x = json!({});
        let y = json!({"a": {"b": "b"}});
        let expected = json!({"a": {"b": "b"}});
        let diff = diff_objects(&x, &y).unwrap().unwrap();
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn remove_nested_value() {
        let mut x = json!({"a": {"b": "b"}});
        let y = json!({});
        let expected = json!({ "a": null });
        let diff = diff_objects(&x, &y).unwrap().unwrap();
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }

    #[test]
    fn complex_test() {
        let mut x = json!({
            "a": "a",
            "b": "b",
            "c": "c",
            "d": {
                "e": "e",
                "f": {
                    "g": 0
                }
            }
        });
        let y = json!({
            "a": "a",
            "b": 13,
            "d": {
                "e": "e",
                "f": {
                    "h": 0
                }
            },
            "i": "i",
        });

        let diff = diff_objects(&x, &y).unwrap().unwrap();
        let expected = json!({
            "b": 13,
            "c": null,
            "d": {
                "f": {
                    "g": null,
                    "h": 0
                }
            },
            "i": "i"
        });
        assert_eq!(expected, diff);
        json_patch::merge(&mut x, &diff);
        assert_eq!(y, x);
    }
}
