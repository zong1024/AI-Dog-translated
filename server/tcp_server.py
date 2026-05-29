from __future__ import annotations

import argparse
import asyncio
import json
import logging

try:
    from .model_service import CNNPredictor
    from .protocol import ProtocolError, read_frame
    from .storage import EventStore
except ImportError:
    from model_service import CNNPredictor
    from protocol import ProtocolError, read_frame
    from storage import EventStore


LOG = logging.getLogger("dog_translator.tcp")


class DogBarkTcpServer:
    def __init__(self, host: str, port: int, store: EventStore, predictor: CNNPredictor):
        self.host = host
        self.port = port
        self.store = store
        self.predictor = predictor

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        peer = writer.get_extra_info("peername")
        LOG.info("client connected: %s", peer)
        try:
            while True:
                frame = await read_frame(reader)
                prediction = await asyncio.to_thread(self.predictor.predict, frame.wav_bytes)
                self.store.add_event(
                    seq=frame.seq,
                    label=prediction.label,
                    source_class=prediction.source_class,
                    confidence=prediction.confidence,
                    lat_e7=frame.lat_e7,
                    lon_e7=frame.lon_e7,
                    battery_mv=frame.battery_mv,
                    wav_bytes=len(frame.wav_bytes),
                    model_error=prediction.model_error,
                )
                response = {
                    "seq": frame.seq,
                    "label": prediction.label,
                    "source_class": prediction.source_class,
                    "confidence": round(prediction.confidence, 4),
                    "alert_ms": 5000 if prediction.label == "angry" else 0,
                }
                if prediction.model_error:
                    response["model_error"] = prediction.model_error
                writer.write(json.dumps(response, ensure_ascii=False).encode("utf-8") + b"\n")
                await writer.drain()
        except asyncio.IncompleteReadError:
            LOG.info("client disconnected: %s", peer)
        except ProtocolError as exc:
            LOG.warning("protocol error from %s: %s", peer, exc)
            writer.write(json.dumps({"label": "non_angry", "error": str(exc)}).encode("utf-8") + b"\n")
            await writer.drain()
        finally:
            writer.close()
            await writer.wait_closed()

    async def serve(self) -> None:
        server = await asyncio.start_server(self.handle_client, self.host, self.port)
        sockets = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
        LOG.info("TCP server listening on %s", sockets)
        async with server:
            await server.serve_forever()


def main() -> None:
    parser = argparse.ArgumentParser(description="Dog bark DTU TCP server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--db", default="server/data/events.db")
    parser.add_argument("--weights", default=None)
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    store = EventStore(args.db)
    predictor = CNNPredictor(args.weights)
    asyncio.run(DogBarkTcpServer(args.host, args.port, store, predictor).serve())


if __name__ == "__main__":
    main()
