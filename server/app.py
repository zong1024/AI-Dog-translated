from __future__ import annotations

import os

from fastapi import FastAPI, File, UploadFile

from .model_service import CNNPredictor
from .storage import EventStore


app = FastAPI(title="Dog Bark Translator Backend")
store = EventStore(os.environ.get("DOG_TRANSLATOR_DB", "server/data/events.db"))
predictor = CNNPredictor(os.environ.get("DOG_TRANSLATOR_WEIGHTS"))


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.post("/predict")
async def predict(file: UploadFile = File(...)) -> dict[str, object]:
    wav_bytes = await file.read()
    prediction = predictor.predict(wav_bytes)
    return {
        "label": prediction.label,
        "source_class": prediction.source_class,
        "confidence": prediction.confidence,
        "alert_ms": 5000 if prediction.label == "angry" else 0,
        "model_error": prediction.model_error,
    }


@app.get("/api/latest-position")
def latest_position() -> dict[str, object]:
    latest = store.latest_position()
    return {"position": latest}


@app.get("/api/events")
def recent_events(limit: int = 50) -> dict[str, object]:
    return {"events": store.recent_events(limit)}
