"""Train the PM2.5 forecast model on real data and export it as a C header for the ESP32.

Data used is the UCI "Beijing Multi-Site Air Quality" set (hourly, 12 stations, 2013 to 2017).
Model reads the PM2.5 now, its trend over the last 3 hours, the temperature and the humidity.
It predicts the ISPU class of the PM2.5 three hours later.
"""

import io
import json
import os
import tempfile
import urllib.request
import zipfile
from pathlib import Path

import emlearn
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix, f1_score

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "firmware" / "include" / "air_model.h"
RAW = ROOT / "ml" / "data" / "beijing-multisite.zip"
REPORT = ROOT / "ml" / "reports" / "metrics.json"
URL = "https://archive.ics.uci.edu/static/public/501/beijing+multi+site+air+quality+data.zip"

FEATURES = ["pm25_ugm3", "pm25_trend_ph10", "pm25_delta_1h", "temp_c", "humidity_pct"]
CLASSES = ["BAIK", "SEDANG", "TIDAK_SEHAT", "BERBAHAYA"]
SEED = 42
HORIZON_H = 3
TREND_LIMIT = 50.0
TEST_FROM = pd.Timestamp("2016-01-01")

# ISPU PM2.5 breakpoints (Permen LHK P.14/2020) that end the classes BAIK, SEDANG and TIDAK_SEHAT.
CLASS_EDGES = [15.5, 55.4, 150.4]


def fetch() -> None:
    """Download the dataset once. The file is licensed CC BY 4.0."""
    if RAW.exists():
        return
    RAW.parent.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(URL, RAW)


def load_stations() -> list[pd.DataFrame]:
    """Read the 12 station files from the nested zip archive."""
    with zipfile.ZipFile(RAW) as outer:
        inner_name = next(n for n in outer.namelist() if n.endswith(".zip"))
        inner = zipfile.ZipFile(io.BytesIO(outer.read(inner_name)))
        frames = []
        for name in inner.namelist():
            if name.endswith(".csv"):
                frames.append(pd.read_csv(io.BytesIO(inner.read(name))))
    return frames


def relative_humidity(temp: pd.Series, dew: pd.Series) -> pd.Series:
    """Compute the relative humidity in percent from the temperature and the dew point (Magnus formula)."""
    a, b = 17.625, 243.04
    return (100 * np.exp(a * dew / (b + dew)) / np.exp(a * temp / (b + temp))).clip(0, 100)


def class_of(pm: pd.Series) -> pd.Series:
    """Map a PM2.5 concentration to its ISPU class number."""
    return pd.Series(np.digitize(pm, CLASS_EDGES, right=True), index=pm.index)


def build_station(df: pd.DataFrame) -> pd.DataFrame:
    """Build features and the forecast label for one station."""
    stamp = pd.to_datetime(df[["year", "month", "day", "hour"]])
    df = df.assign(stamp=stamp).set_index("stamp").sort_index()
    df = df[["PM2.5", "TEMP", "DEWP"]].asfreq("h").interpolate(limit=3)
    out = pd.DataFrame(index=df.index)
    out["pm25_ugm3"] = df["PM2.5"]
    trend = ((df["PM2.5"] - df["PM2.5"].shift(3)) / 3).clip(-TREND_LIMIT, TREND_LIMIT)
    out["pm25_trend_ph10"] = trend * 10
    out["pm25_delta_1h"] = (df["PM2.5"] - df["PM2.5"].shift(1)).clip(-100, 100)
    out["temp_c"] = df["TEMP"]
    out["humidity_pct"] = relative_humidity(df["TEMP"], df["DEWP"])
    out["now_class"] = class_of(df["PM2.5"])
    out["target"] = class_of(df["PM2.5"].shift(-HORIZON_H))
    valid = df["PM2.5"].shift(-HORIZON_H).notna() & out[FEATURES].notna().all(axis=1)
    return out[valid]


def to_model_input(df: pd.DataFrame) -> np.ndarray:
    """Round the features to the 16-bit integers that the generated C code reads.

    The firmware sends the PM2.5 trend per hour multiplied by ten, with the same limit.
    """
    return df[FEATURES].round().to_numpy(dtype=np.int16)


def main() -> None:
    fetch()
    data = pd.concat([build_station(f) for f in load_stations()])
    train = data[data.index < TEST_FROM]
    test = data[data.index >= TEST_FROM]
    x_train, y_train = to_model_input(train), train.target.to_numpy()
    x_test, y_test = to_model_input(test), test.target.to_numpy()

    model = RandomForestClassifier(
        n_estimators=8, max_depth=8, min_samples_leaf=100, n_jobs=1, random_state=SEED,
    )
    model.fit(x_train, y_train)
    pred = model.predict(x_test)
    persistence = test.now_class.to_numpy()
    changed = persistence != y_test

    cmodel = emlearn.convert(model, method="inline")
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    cmodel.save(file=str(HEADER), name="air")

    os.chdir(tempfile.mkdtemp(prefix="emlearn-"))
    sample = np.random.default_rng(SEED).choice(len(x_test), size=20000, replace=False)
    c_pred = np.asarray(cmodel.predict(x_test[sample]))
    report = {
        "dataset": "UCI Beijing Multi-Site Air Quality, doi 10.24432/C5RK5G, CC BY 4.0",
        "horizon_hours": HORIZON_H,
        "train_rows": len(train),
        "test_rows": len(test),
        "train_period": "2013-03 to 2015-12",
        "test_period": "2016-01 to 2017-02",
        "features": FEATURES,
        "classes": CLASSES,
        "accuracy_model": accuracy_score(y_test, pred),
        "accuracy_persistence": accuracy_score(y_test, persistence),
        "accuracy_model_on_class_changes": accuracy_score(y_test[changed], pred[changed]),
        "share_of_class_changes": float(changed.mean()),
        "macro_f1_model": f1_score(y_test, pred, average="macro"),
        "macro_f1_persistence": f1_score(y_test, persistence, average="macro"),
        "accuracy_deployed_c_sample": accuracy_score(y_test[sample], c_pred),
        "c_code_matches_sklearn_sample": float((c_pred == pred[sample]).mean()),
        "confusion_matrix": confusion_matrix(y_test, pred).tolist(),
        "per_class": classification_report(y_test, pred, target_names=CLASSES, output_dict=True, zero_division=0),
        "feature_importance": dict(zip(FEATURES, model.feature_importances_.round(3).tolist())),
        "header_bytes": HEADER.stat().st_size,
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2))
    keys = (
        "train_rows", "test_rows", "accuracy_model", "accuracy_persistence",
        "accuracy_model_on_class_changes", "share_of_class_changes", "macro_f1_model",
        "macro_f1_persistence", "accuracy_deployed_c_sample", "c_code_matches_sklearn_sample",
        "confusion_matrix", "feature_importance", "header_bytes",
    )
    print(json.dumps({k: report[k] for k in keys}))


if __name__ == "__main__":
    main()
