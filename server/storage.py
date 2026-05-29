from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any


DEFAULT_DB_PATH = Path("server/data/events.db")


class EventStore:
    def __init__(self, db_path: str | Path = DEFAULT_DB_PATH):
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        return conn

    def _init_db(self) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    created_at TEXT NOT NULL DEFAULT (datetime('now')),
                    seq INTEGER NOT NULL,
                    label TEXT NOT NULL,
                    source_class TEXT NOT NULL,
                    confidence REAL NOT NULL,
                    lat_e7 INTEGER NOT NULL,
                    lon_e7 INTEGER NOT NULL,
                    battery_mv INTEGER NOT NULL,
                    wav_bytes INTEGER NOT NULL,
                    model_error TEXT
                )
                """
            )

    def add_event(
        self,
        *,
        seq: int,
        label: str,
        source_class: str,
        confidence: float,
        lat_e7: int,
        lon_e7: int,
        battery_mv: int,
        wav_bytes: int,
        model_error: str | None,
    ) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                INSERT INTO events
                    (seq, label, source_class, confidence, lat_e7, lon_e7, battery_mv, wav_bytes, model_error)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (seq, label, source_class, confidence, lat_e7, lon_e7, battery_mv, wav_bytes, model_error),
            )

    def latest_position(self) -> dict[str, Any] | None:
        with self._connect() as conn:
            row = conn.execute(
                """
                SELECT created_at, seq, lat_e7, lon_e7, battery_mv
                FROM events
                WHERE lat_e7 != 0 AND lon_e7 != 0
                ORDER BY id DESC
                LIMIT 1
                """
            ).fetchone()
        if row is None:
            return None
        return {
            "created_at": row["created_at"],
            "seq": row["seq"],
            "latitude": row["lat_e7"] / 10_000_000,
            "longitude": row["lon_e7"] / 10_000_000,
            "battery_mv": row["battery_mv"],
        }

    def recent_events(self, limit: int = 50) -> list[dict[str, Any]]:
        limit = min(max(limit, 1), 200)
        with self._connect() as conn:
            rows = conn.execute(
                """
                SELECT created_at, seq, label, source_class, confidence, lat_e7, lon_e7, battery_mv, wav_bytes, model_error
                FROM events
                ORDER BY id DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        return [
            {
                "created_at": row["created_at"],
                "seq": row["seq"],
                "label": row["label"],
                "source_class": row["source_class"],
                "confidence": row["confidence"],
                "latitude": row["lat_e7"] / 10_000_000 if row["lat_e7"] else None,
                "longitude": row["lon_e7"] / 10_000_000 if row["lon_e7"] else None,
                "battery_mv": row["battery_mv"],
                "wav_bytes": row["wav_bytes"],
                "model_error": row["model_error"],
            }
            for row in rows
        ]
