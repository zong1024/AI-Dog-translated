# Dog Bark Translator Backend

This backend receives DTU transparent TCP frames from the ESP32-C3 collar, runs
the open-source Cross-Species-Translation context model, stores events in SQLite,
and exposes simple HTTP APIs for a WeChat mini program.

## Run

```bash
python -m venv .venv
. .venv/bin/activate
pip install -r server/requirements.txt
python -m server.tcp_server --host 0.0.0.0 --port 9000
uvicorn server.app:app --host 0.0.0.0 --port 8000
```

The model weights are downloaded automatically on first inference to
`server/models/weightsForContextPredict.pth`.

## DTU protocol

Frame layout is little-endian:

```text
DBRK magic[4]
version uint8
seq uint32
timestamp uint32
lat_e7 int32
lon_e7 int32
battery_mv uint16
wav_len uint32
wav bytes
crc32 uint32 over header + wav bytes
```

The server returns one JSON line:

```json
{"seq":123,"label":"angry","source_class":"Aggression","confidence":0.73,"alert_ms":5000}
```

`Aggression` with confidence at least `0.55` maps to `angry`; all other classes
and errors map to `non_angry`.

## Mini program APIs

- `GET /api/latest-position`
- `GET /api/events?limit=50`
- `POST /predict` for direct WAV upload testing
